// src/world/WorldMovement.cpp —— 大世界移动、碰撞、多楼层与传送子系统
//
// 负责:
//   ① 玩家移动推进 (advanceMovement / tick 玩家段 CHAR_walkcall)
//   ② 步进碰撞与墙角阻挡 (walkStep)
//   ③ 传送门瞬移与同屏视野广播 (warpSinglePlayer / warpPlayer)
//   ④ 多楼层地图管理与查询 (loadFloorMap / findFloorMap / floorMapCount)
//   ⑤ 传送点加载与测试传送 (loadWarpPoints / warpPointCount / warpPlayerForTest)
//   ⑥ 客户端移动指令接收 (onWalk / 防瞬移 / 门禁预检)
//   ⑦ NPC 传送员瞬移交互 (warpPlayerByNpc)

#include "WorldImpl.h"
#include "world/Api.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace SA::World
{

// 走一步 —— 移植 CHAR_walk_move(char_walk.c:195)的**地图碰撞 + 坐标更新**核心。
bool walkStep(SA::Model::Player &p, const GridMap &map, const TileAttrTable &attr,
              std::uint8_t dir, bool is_turn)
{
	// 转向或移动都先落朝向(原版 :218/:250/:259 一律 CHAR_setInt(CHAR_DIR,dir))。
	p.dir = dir;
	if (is_turn)
		return false; // 大写 ⇒ 只转身,不移动(ctodirmode mode==1)

	const std::int32_t fx = p.x + kDirDelta[dir].dx;
	const std::int32_t fy = p.y + kDirDelta[dir].dy;

	// 直线:看目标格(:258)。斜向:额外看 x/y 两分量,墙角不穿(:263-278)。
	if (!mapWalkable(map, attr, fx, fy))
		return false; // 撞墙,朝向已落(:259)
	if (kDirDelta[dir].dx != 0 && kDirDelta[dir].dy != 0)
	{
		if (!mapWalkable(map, attr, p.x + kDirDelta[dir].dx, p.y) ||
		    !mapWalkable(map, attr, p.x, p.y + kDirDelta[dir].dy))
			return false; // 墙角
	}
	p.x = fx;
	p.y = fy;
	return true;
}

void World::Impl::warpSinglePlayer(SA::Net::SessionId id, std::int32_t dst_floor, std::int32_t dst_x, std::int32_t dst_y)
{
	const auto it = conns.find(id);
	SA::Model::Player *p = players.resolve(player_of_session.find(id));
	if (it == conns.end() || p == nullptr)
		return;

	Conn &c = it->second;
	c.walk_seq.clear(); // 清空剩余未走路径串 (CHAR_WORKWALKARRAY, char.c:4671)

	const std::int32_t ofloor = p->floor;
	const std::int32_t ox = p->x;
	const std::int32_t oy = p->y;

	// 1. 从旧格 olink 移除 (玩家刚从 ofloor 的 ox, oy 走来)
	auto *old_fl = getFloor(ofloor);
	const auto &old_map = old_fl ? old_fl->map : map;
	auto &old_olink = old_fl ? old_fl->olink : olink;
	if (old_map.inBounds(ox, oy))
	{
		const auto idx = old_map.index(ox, oy);
		if (idx < old_olink.size())
		{
			auto &oldcell = old_olink[idx];
			oldcell.erase(std::remove(oldcell.begin(), oldcell.end(), id),
			              oldcell.end());
		}
	}

	// 2. 旧视野广播 Disappear (旧视野内其他玩家看到 id 消失, id 看到旧视野玩家消失)
	const auto old_vis = collectVisible(ofloor, ox, oy, id);
	for (const SA::Net::ConnectionId b : old_vis)
	{
		SA::Domain::CharDisappear dis{};
		dis.entity_id = id;
		dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_PLAYER);
		sendTo(b, dis);
		SA::Domain::CharDisappear dis2{};
		dis2.entity_id = b;
		dis2.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_PLAYER);
		sendTo(id, dis2);
	}

	// 3. 更新玩家坐标
	p->floor = dst_floor;
	p->x = dst_x;
	p->y = dst_y;

	// 4. 新格 olink 挂接
	auto *new_fl = getFloor(dst_floor);
	const auto &new_map = new_fl ? new_fl->map : map;
	auto &new_olink = new_fl ? new_fl->olink : olink;
	if (new_map.inBounds(p->x, p->y))
	{
		const auto idx = new_map.index(p->x, p->y);
		if (idx < new_olink.size())
		{
			new_olink[idx].push_back(id);
		}
	}

	// 5. 新视野广播 Appear + 敌人/NPC 视野刷新 (跨图传送时旧坐标设为 -1000 以全量刷新)
	broadcastSpawn(id, *p);
	const std::int32_t ref_ox = (ofloor == dst_floor) ? ox : -1000;
	const std::int32_t ref_oy = (ofloor == dst_floor) ? oy : -1000;
	refreshEnemyView(id, *p, ref_ox, ref_oy);
	refreshNpcView(id, *p, ref_ox, ref_oy);

	// 6. 给玩家自身下发坐标同步 (CharMove)
	SA::Domain::CharMove self_mv{};
	self_mv.entity_id = id;
	self_mv.x = p->x;
	self_mv.y = p->y;
	self_mv.dir = static_cast<std::uint32_t>(p->dir);
	self_mv.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_PLAYER);
	sendTo(id, self_mv);

	// 7. 跨图时同步目标楼层当前天气环境 (EF 指令)
	if (ofloor != dst_floor)
	{
		syncWeatherToSession(id, static_cast<std::uint32_t>(dst_floor));
	}
}

void World::Impl::warpPlayer(SA::Net::SessionId id, std::int32_t dst_floor, std::int32_t dst_x, std::int32_t dst_y)
{
	std::vector<SA::Net::SessionId> followers{};
	auto pit = party_of_session.find(id);
	if (pit != party_of_session.end())
	{
		auto party_it = parties.find(pit->second);
		if (party_it != parties.end() && party_it->second.leader == id)
		{
			for (std::size_t idx = 1; idx < party_it->second.members.size(); ++idx)
			{
				followers.push_back(party_it->second.members[idx]);
			}
		}
	}

	warpSinglePlayer(id, dst_floor, dst_x, dst_y);

	// 队长传送时全队队员一同同步传送 (移植 map_warppoint.c:270-278 / npc_warpman.c:705-715)
	for (auto fid : followers)
	{
		warpSinglePlayer(fid, dst_floor, dst_x, dst_y);
	}
}

void World::advanceMovement()
{
	Impl &s = *_impl;

	// ── 5. 角色循环 —— 玩家段(批次 W.1。原 CHAR_Loop:4667 玩家 for + CHAR_walk_check:4583)──
	//   ★ 全扫在线玩家:走路串非空 且距上次走够 walksendinterval ⇒ 走一步(CHAR_walkcall)。
	for (auto &kv : s.conns)
	{
		Impl::Conn &c = kv.second;
		if (s.inBattle(kv.first))
		{
			c.walk_seq.clear();
			continue;
		}
		if (s.partyModeOf(kv.first) == PartyMode::kMember)
		{
			c.walk_seq.clear();
			continue;
		}
		if (c.session == nullptr || c.walk_seq.empty() || (s.storage && (c.pending || c.detached || c.save_failed)))
			continue;
		// 间隔门(CHAR_walk_check:4590):到点才走一步,走完把下次时刻推后 kWalkIntervalMs。
		if (s.now_ms < c.next_walk_at_ms)
			continue;
		SA::Model::Player *p = s.players.resolve(s.player_of_session.find(kv.first));
		if (p == nullptr)
		{
			c.walk_seq.clear(); // 无实体 ⇒ 丢弃走路串(不会再有落点)
			continue;
		}
		// 消费首字符(CHAR_walkcall:730 ctodirmode + :849 &tmp[1])。
		std::uint8_t dir = 0;
		bool is_turn = false;
		const std::int32_t ox = p->x;
		const std::int32_t oy = p->y;
		bool moved = false;
		auto *fl = s.getFloor(p->floor);
		const auto &cur_map = fl ? fl->map : s.map;
		auto &cur_olink = fl ? fl->olink : s.olink;
		if (decodeDirChar(c.walk_seq.front(), dir, is_turn))
			moved = walkStep(*p, cur_map, s.map_attr, dir, is_turn);
		// ⚠️ 非法字符也消费掉,不卡住整串(原版 ctodirmode 不校验,越界由 VALIDATEDIR 兜)。
		c.walk_seq.erase(c.walk_seq.begin());
		c.next_walk_at_ms = s.now_ms + kWalkIntervalMs;

		// ── W.5:撞明雷退回(移植 char_walk.c:585-593)────────────────────────
		//   走到的新格若有世界敌人(明雷)⇒ 弹回原格,不占敌人格(朝向已落,保留)。
		//   ★ 与开战解耦:开战靠玩家主动发 EV(onEvent);走路撞上只退回 —— 原版两条独立机制。
		//   ⚠️ 客户端坐标纠正(原版 XYD_send:588)划出:W.1 未建 XYD 下行,同其走路同步残缺。
		if (moved && s.worldEnemyAt(p->floor, p->x, p->y) != s.world_enemies.size())
		{
			p->x = ox;
			p->y = oy;
			moved = false;
		}

		// ── W.7:撞 NPC 实体退回(CHAR_ISOVERED=0 阻挡不可穿透)────────────
		if (moved && s.npcAt(p->floor, p->x, p->y) != s.npc_entities.size())
		{
			p->x = ox;
			p->y = oy;
			moved = false;
		}

		// ── W.6: WARP 传送点触发(移植 npc_warp.c / char.c:4594-4675)────────────
		//   玩家走入新格(moved)若命中 WarpPoint,且目标格合法可通行,则触发瞬移:
		//   清空剩余路径串 + 旧视野 Disappear + 瞬移新坐标 + 新视野 Appear + 自身 CharMove 同步。
		bool warped = false;
		if (moved)
		{
			const WarpPoint *wp = s.findWarpPoint(p->floor, p->x, p->y);
			if (wp != nullptr)
			{
				const bool same_floor = (wp->dst_floor == p->floor);
				const auto *dst_fl = s.getFloor(wp->dst_floor);
				const auto &dst_map = dst_fl ? dst_fl->map : s.map;
				if (dst_fl != nullptr)
				{
					if (dst_map.inBounds(wp->dst_x, wp->dst_y) &&
					    mapWalkable(dst_map, s.map_attr, wp->dst_x, wp->dst_y))
					{
						warped = true;
						s.warpPlayer(kv.first, wp->dst_floor, wp->dst_x, wp->dst_y);
					}
				}
				else if (!same_floor || (s.map.inBounds(wp->dst_x, wp->dst_y) &&
				                         mapWalkable(s.map, s.map_attr, wp->dst_x, wp->dst_y)))
				{
					warped = true;
					s.warpPlayer(kv.first, wp->dst_floor, wp->dst_x, wp->dst_y);
				}
			}
		}

		// 里程碑②:位置变了且未传送 ⇒ 更新 olink(旧格摘、新格挂)+ 视野广播(扫格 diff)。
		if (moved && !warped)
		{
			if (cur_map.inBounds(ox, oy))
			{
				const auto idx = cur_map.index(ox, oy);
				if (idx < cur_olink.size())
				{
					auto &oldcell = cur_olink[idx];
					oldcell.erase(std::remove(oldcell.begin(), oldcell.end(), kv.first),
					              oldcell.end());
				}
			}
			if (cur_map.inBounds(p->x, p->y))
			{
				const auto idx = cur_map.index(p->x, p->y);
				if (idx < cur_olink.size())
					cur_olink[idx].push_back(kv.first);
			}
			s.broadcastMove(kv.first, ox, oy, *p);
			// W.3:玩家移动后补发视野内**世界敌人**的 appear / disappear(玩家看敌人那一半)。
			s.refreshEnemyView(kv.first, *p, ox, oy);
			// W.7:玩家移动后补发视野内**世界 NPC** 的 appear / disappear。
			s.refreshNpcView(kv.first, *p, ox, oy);

			// ── 队伍跟随 (贪吃蛇跟随): 队员沿前一人足迹前进一步 (移植 char_walk.c:689-705) ──
			if (s.partyModeOf(kv.first) == PartyMode::kLeader)
			{
				auto pit = s.party_of_session.find(kv.first);
				if (pit != s.party_of_session.end())
				{
					auto party_it = s.parties.find(pit->second);
					if (party_it != s.parties.end())
					{
						std::int32_t end_x = ox;
						std::int32_t end_y = oy;
						for (std::size_t idx = 1; idx < party_it->second.members.size(); ++idx)
						{
							const auto mid = party_it->second.members[idx];
							SA::Model::Player *mp = s.players.resolve(s.player_of_session.find(mid));
							if (mp == nullptr || mp->floor != p->floor)
								continue;
							const std::int32_t start_x = mp->x;
							const std::int32_t start_y = mp->y;
							const int fdir = getDirFromTwoPoints(start_x, start_y, end_x, end_y);
							end_x = start_x;
							end_y = start_y;
							if (fdir >= 0)
							{
								mp->dir = static_cast<std::uint8_t>(fdir);
								const std::int32_t target_x = start_x + kDirDelta[fdir].dx;
								const std::int32_t target_y = start_y + kDirDelta[fdir].dy;
								mp->x = target_x;
								mp->y = target_y;
								if (cur_map.inBounds(start_x, start_y))
								{
									const auto idx_old = cur_map.index(start_x, start_y);
									if (idx_old < cur_olink.size())
									{
										auto &oldcell = cur_olink[idx_old];
										oldcell.erase(std::remove(oldcell.begin(), oldcell.end(), mid),
										              oldcell.end());
									}
								}
								if (cur_map.inBounds(mp->x, mp->y))
								{
									const auto idx_new = cur_map.index(mp->x, mp->y);
									if (idx_new < cur_olink.size())
										cur_olink[idx_new].push_back(mid);
								}
								s.broadcastMove(mid, start_x, start_y, *mp);
								s.refreshEnemyView(mid, *mp, start_x, start_y);
								s.refreshNpcView(mid, *mp, start_x, start_y);
							}
						}
					}
				}
			}

			// ── 遇敌判定(批次 W.4。原 char_walk.c:585,展开视图基准)────────────
			//   ★ 只在真移动(moved)后判:转身 / 撞墙不触发(原版遇敌在 walk_move 成功后)。
			//   ⚠️ 数据表空(未 loadEncounterTables)⇒ findEncountArea 恒 -1 ⇒ 不遇敌
			//      (现有走路用例不注入即不受影响)。
			const std::int32_t arow =
			    findEncountArea(s.encount_areas, p->floor, p->x, p->y);
			if (arow >= 0)
			{
				const EncountArea &area =
				    s.encount_areas[static_cast<std::size_t>(arow)];
				// cep 夹在 [prob_min, prob_max](char_walk.c:553-554),temp = cep
				//   (无技能 ⇒ p_cep=0 ⇒ temp=cep)。min/max 写反自动纠正(encount.c:245-253,
				//   与敌人表 lv_min/max 同族)——载入期做,这里防御性纠一次不改行为。
				std::int32_t lo = area.prob_min;
				std::int32_t hi = area.prob_max;
				if (c.cep < lo)
					c.cep = lo;
				if (c.cep > hi)
					c.cep = hi;
				// 检查猎人遇敌率修正 (原 CHAR_ENCOUNT_FIX / CHAR_ENCOUNT_NUM, char_walk.c:558-575)
				std::int32_t eff_cep = c.cep;
				if (p->encounter_rate_fix != 0)
				{
					if (s.now_ms > p->encounter_rate_expire_ms)
					{
						p->encounter_rate_fix = 0;
						p->encounter_rate_expire_ms = 0;
					}
					else
					{
						eff_cep = c.cep * (100 + p->encounter_rate_fix) / 100;
						if (eff_cep < 0)
							eff_cep = 0;
					}
				}

				// ★ S11 精灵/天使系统: 装备使者信物或处于天使神佑模式时遇敌率完全抑制 (原 CHAR_WORKANGELMODE)
				if (s.isAngelModeActive(kv.first))
				{
					eff_cep = 0;
				}

				// 掷骰(encount.c:267 `RAND(0, max_prob) < temp`,csa8.0 max_prob 写死 120):
				//   ★ 随机源经 world_rng(00 §4.2 统一管线)。
				//   enemy_action >100 ⇒ 乘法放大分母 ⇒ 遇敌概率成反比下降。
				const int denom = 120 * clampEnemyAction(s.config.enemy_action);
				if (s.world_rng.randMod(denom) < eff_cep)
				{
					// 命中 ⇒ 清走路串(EN_recv:WALKARRAY="")+ cep 重置 prob_min(:594)+ 开战。
					//   ⚠️ 清串后本 conn 剩余方向作废(原版遇敌即中断走路);triggerEncounter
					//      只动 s.battles / 池,不增删 s.conns ⇒ 本遍历的引用 c 仍有效。
					c.walk_seq.clear();
					c.cep = lo;
					(void)triggerEncounter(kv.first, arow);
				}
			}
		}
		if (s.storage && !s.inBattle(kv.first))
			saveCharacter(kv.first, false, 0);
	}
}

void World::onWalk(SA::Net::SessionId id, const SA::Domain::WalkRequest &req)
{
	// 移植 lssproto_W_recv(callfromcli.c:503)的净核:防瞬移 + 碰撞预检 + 排走路串。
	//   ⚠️ 划外(各有归属):nuke 反作弊(:517,自由服魔改)· 交易模式门(:513,交易系统)·
	//      组队分支(walk_init:947,组队系统)。
	Impl &s = *_impl;
	if (s.partyModeOf(id) == PartyMode::kMember)
	{
		// 队员自主移动被禁止 (移植 char_walk.c:924)
		bool has_move = false;
		for (char ch : std::string_view(req.direction.c_str()))
		{
			if (ch >= 'a' && ch <= 'z')
			{
				has_move = true;
				break;
			}
		}
		if (has_move)
			return;
	}
	const auto it = s.conns.find(id); // 1.5:SessionId == ConnectionId
	if (it == s.conns.end())
		return;
	if (s.inBattle(id) || isPlayerVending(id))
	{
		it->second.walk_seq.clear();
		return;
	}
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (p == nullptr)
		return;

	// (0,0) 门(lssproto_W_recv:532):照抄 —— 原版历史调试门,坐标(0,0)直接忽略。
	//   ⚠️ 纪律⓪「照抄不声称要紧」:fixture 出生点在地图中心、用例避开(0,0),
	//      真实地图出生点也不在(0,0)。
	if (req.x == 0 && req.y == 0)
		return;

	// 防瞬移(:543):客户端声明坐标离服务端当前 >1 格 ⇒ 不信,按当前坐标处理。
	std::int32_t cx = req.x;
	std::int32_t cy = req.y;
	const std::int32_t ddx = p->x - cx;
	const std::int32_t ddy = p->y - cy;
	if (ddx > 1 || ddx < -1 || ddy > 1 || ddy < -1)
	{
		cx = p->x;
		cy = p->y;
	}
	// 碰撞预检(:552):声明的目标格不可走 ⇒ 忽略本次请求。
	//   ⚠️ 原版拉回当前后仍排串(direction 串会走回合法处);我们更严:目标非法直接不排,
	//      理由是 fixture 期无预测回滚需求,严格拒绝更好定位问题。真实客户端预测接入时再放宽。
	const auto *fl = s.getFloor(p->floor);
	const auto &cur_map = fl ? fl->map : s.map;
	if (!mapWalkable(cur_map, s.map_attr, cx, cy))
		return;

	// 排走路串(walk_init:948 → walk_start:891 setWorkChar WALKARRAY)。
	//   ⚠️ FixedStr<32> 已保证 ≤32(原版 walk_init:939 的长度门);实际逐步移动由
	//      kCharLoop 玩家段按 walksendinterval 消费(CHAR_walkcall)。
	it->second.walk_seq = std::string(req.direction.c_str());
	// Preserve the last step's deadline across requests; one-character packets
	// must obey the same walk interval as a multi-character route.
}

void World::loadFloorMap(std::int32_t floor_id, GridMap map)
{
	auto &s = *_impl;
	Impl::FloorState state;
	state.floor_id = floor_id;
	state.olink.assign(map.tile.size(), {});
	state.map = std::move(map);
	s.floors[floor_id] = std::move(state);
}

const GridMap *World::findFloorMap(std::int32_t floor_id) const noexcept
{
	const auto *fl = _impl->getFloor(floor_id);
	return fl ? &fl->map : nullptr;
}

std::size_t World::floorMapCount() const noexcept
{
	return _impl->floors.size();
}

void World::loadWarpPoints(std::vector<WarpPoint> points)
{
	_impl->warp_points = std::move(points);
}

std::size_t World::warpPointCount() const noexcept
{
	return _impl->warp_points.size();
}

void World::warpPlayerForTest(SA::Net::SessionId session, std::int32_t floor, std::int32_t x, std::int32_t y)
{
	_impl->warpPlayer(session, floor, x, y);
}

World::PlayerPos World::playerPos(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return PlayerPos{}; // valid == false
	return PlayerPos{true, p->floor, p->x, p->y, p->dir};
}

bool World::warpPlayerByNpc(SA::Net::SessionId id, std::uint64_t npc_id, std::size_t dest_idx)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kWarpMan)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 目的地有效性检查
	if (dest_idx >= npc->warp_destinations.size())
		return false;

	const auto &dest = npc->warp_destinations[dest_idx];

	// 等级门禁检查 (移植 npc_warpman.c)
	if (p->level < dest.level)
	{
		std::string low_msg = npc->level_low_msg.empty() ? "你的等级不足，无法前往该区域！" : npc->level_low_msg;
		s.sendExChangeWindow(id, npc->id, low_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 石币门禁检查 (路费, 移植 npc_warpman.c)
	if (p->gold < dest.cost)
	{
		std::string stone_msg = npc->stone_less_msg.empty() ? "你的石币不足以支付路费！" : npc->stone_less_msg;
		s.sendExChangeWindow(id, npc->id, stone_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 目标坐标可通行门禁检查 (同层时校验目标格通行性, 跨层时由目标地图管辖, 移植 npc_warpman.c)
	const bool same_floor = (dest.floor == p->floor);
	const auto *dst_fl = s.getFloor(dest.floor);
	const auto &dst_map = dst_fl ? dst_fl->map : s.map;
	if (dst_fl != nullptr)
	{
		if (!dst_map.inBounds(dest.x, dest.y) || !mapWalkable(dst_map, s.map_attr, dest.x, dest.y))
		{
			s.sendExChangeWindow(id, npc->id, "目标地点暂时无法通行！",
			                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
			return false;
		}
	}
	else if (same_floor && (!s.map.inBounds(dest.x, dest.y) || !mapWalkable(s.map, s.map_attr, dest.x, dest.y)))
	{
		s.sendExChangeWindow(id, npc->id, "目标地点暂时无法通行！",
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 扣除路费 (走 GoldLedger, 汇 kWarpFee)
	if (dest.cost > 0)
	{
		const GoldTx tx = delGold(*p, GoldReason::kWarpFee, dest.cost, /*trans=*/0,
		                          static_cast<std::uint64_t>(id), s);
		if (tx.disposition != GoldDisposition::kApplied)
		{
			return false;
		}
	}

	// 执行瞬移与视野同步
	s.warpPlayer(id, dest.floor, dest.x, dest.y);

	it->second.active_window_id = 0;
	it->second.active_window_npc_id = 0;
	it->second.pending_warpman = {};

	return true;
}

} // namespace SA::World
