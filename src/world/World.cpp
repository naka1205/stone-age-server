// src/world/world.cpp —— 最小 tick 与一场战斗的生命周期
//
// 01 §3.1 的 tick 顺序被**原样保留**(连未实现的四步也占位),
// 01 §3.2 的节拍层在这里第一次成为真东西:
//   ★★ 战斗推进速度 **不等于** tick 频率。
//      15 §5.2 实测 8.0 的 _BATTLE_TIME 与 _CHAR_LOOP_TIME 均为关
//      ⇒ 原版战斗速度就是 tick 频率,手感取决于当年的硬件与网络。
//      00 §0 又已认下 ④ 层「表现与手感永远无法验证」
//      ⇒ 节拍是**玩法参数**,必须可配、只能靠人试。

#include "WorldImpl.h"
#include "data/Json.h"
#include "world/Api.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "model/EntityIndex.h"
#include "model/EntityPool.h"
#include "model/Player.h"
#include "rules/CaptureItem.h"
#include "rules/ProfessionSkill.h"
#include "rules/Progression.h"
#include "rules/Status.h"

namespace SA::World
{
namespace
{

// ── 批次 W.10: ExChangeMan 道具与宠物交付/奖励解析辅助 ─────────
struct ExchangeItemEntry
{
	std::int32_t item_id = 0;
	std::int32_t count = 1;
};

inline std::vector<ExchangeItemEntry> parseExchangeItems(std::string_view str)
{
	std::vector<ExchangeItemEntry> result;
	std::size_t start = 0;
	while (start < str.size())
	{
		const std::size_t comma = str.find(',', start);
		std::string_view part =
		    (comma == std::string_view::npos) ? str.substr(start) : str.substr(start, comma - start);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
			part.remove_prefix(1);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
			part.remove_suffix(1);
		if (!part.empty() && part != "EVDEL")
		{
			const std::size_t star = part.find('*');
			if (star != std::string_view::npos)
			{
				const int id = std::atoi(std::string(part.substr(0, star)).c_str());
				const int cnt = std::atoi(std::string(part.substr(star + 1)).c_str());
				if (id > 0)
					result.push_back({id, std::max(1, cnt)});
			}
			else
			{
				const int id = std::atoi(std::string(part).c_str());
				if (id > 0)
					result.push_back({id, 1});
			}
		}
		if (comma == std::string_view::npos)
			break;
		start = comma + 1;
	}
	return result;
}

struct ExchangePetEntry
{
	std::int32_t pet_id = 0;
	std::int32_t count = 1;
};

inline std::vector<ExchangePetEntry> parseExchangePets(std::string_view str)
{
	std::vector<ExchangePetEntry> result;
	std::size_t start = 0;
	while (start < str.size())
	{
		const std::size_t comma = str.find(',', start);
		std::string_view part =
		    (comma == std::string_view::npos) ? str.substr(start) : str.substr(start, comma - start);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
			part.remove_prefix(1);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
			part.remove_suffix(1);
		if (!part.empty() && part != "EVDEL")
		{
			const std::size_t star = part.find('*');
			if (star != std::string_view::npos)
			{
				const int id = std::atoi(std::string(part.substr(0, star)).c_str());
				const int cnt = std::atoi(std::string(part.substr(star + 1)).c_str());
				if (id > 0)
					result.push_back({id, std::max(1, cnt)});
			}
			else
			{
				const int id = std::atoi(std::string(part).c_str());
				if (id > 0)
					result.push_back({id, 1});
			}
		}
		if (comma == std::string_view::npos)
			break;
		start = comma + 1;
	}
	return result;
}

inline std::vector<ExchangeItemEntry> resolveDelItems(const ExChangeBlock &blk, int branch_idx)
{
	auto dels = parseExchangeItems(blk.del_item);
	const std::string_view cond{blk.condition};
	if (blk.del_item.find("EVDEL") != std::string_view::npos && !cond.empty())
	{
		std::size_t start = 0;
		int cur_branch = 1;
		std::string_view matched_branch{};
		while (start < cond.size())
		{
			const std::size_t comma = cond.find(',', start);
			const std::string_view branch =
			    (comma == std::string_view::npos) ? cond.substr(start)
			                                      : cond.substr(start, comma - start);
			if (cur_branch == branch_idx)
			{
				matched_branch = branch;
				break;
			}
			++cur_branch;
			if (comma == std::string_view::npos)
				break;
			start = comma + 1;
		}
		if (!matched_branch.empty())
		{
			std::size_t astart = 0;
			while (astart < matched_branch.size())
			{
				const std::size_t amp = matched_branch.find('&', astart);
				const std::string_view atom =
				    (amp == std::string_view::npos) ? matched_branch.substr(astart)
				                                    : matched_branch.substr(astart, amp - astart);
				if (atom.find("ITEM") != std::string_view::npos &&
				    atom.find('=') != std::string_view::npos)
				{
					const std::size_t eq = atom.find('=');
					const std::string_view val = atom.substr(eq + 1);
					const std::size_t star = val.find('*');
					if (star != std::string_view::npos)
					{
						const int id = std::atoi(std::string(val.substr(0, star)).c_str());
						const int cnt = std::atoi(std::string(val.substr(star + 1)).c_str());
						if (id > 0)
							dels.push_back({id, std::max(1, cnt)});
					}
					else
					{
						const int id = std::atoi(std::string(val).c_str());
						if (id > 0)
							dels.push_back({id, 1});
					}
				}
				if (amp == std::string_view::npos)
					break;
				astart = amp + 1;
			}
		}
	}
	return dels;
}

inline std::vector<ExchangePetEntry> resolveDelPets(const ExChangeBlock &blk, int branch_idx)
{
	auto dels = parseExchangePets(blk.del_pet);
	const std::string_view cond{blk.condition};
	if (blk.del_pet.find("EVDEL") != std::string_view::npos && !cond.empty())
	{
		std::size_t start = 0;
		int cur_branch = 1;
		std::string_view matched_branch{};
		while (start < cond.size())
		{
			const std::size_t comma = cond.find(',', start);
			const std::string_view branch =
			    (comma == std::string_view::npos) ? cond.substr(start)
			                                      : cond.substr(start, comma - start);
			if (cur_branch == branch_idx)
			{
				matched_branch = branch;
				break;
			}
			++cur_branch;
			if (comma == std::string_view::npos)
				break;
			start = comma + 1;
		}
		if (!matched_branch.empty())
		{
			std::size_t astart = 0;
			while (astart < matched_branch.size())
			{
				const std::size_t amp = matched_branch.find('&', astart);
				const std::string_view atom =
				    (amp == std::string_view::npos) ? matched_branch.substr(astart)
				                                    : matched_branch.substr(astart, amp - astart);
				if (atom.find("PET") != std::string_view::npos)
				{
					const std::size_t hyphen = atom.find('-');
					if (hyphen != std::string_view::npos)
					{
						const std::string_view right = atom.substr(hyphen + 1);
						const std::size_t star = right.find('*');
						if (star != std::string_view::npos)
						{
							const int id = std::atoi(std::string(right.substr(0, star)).c_str());
							const int cnt = std::atoi(std::string(right.substr(star + 1)).c_str());
							if (id > 0)
								dels.push_back({id, std::max(1, cnt)});
						}
						else
						{
							const int id = std::atoi(std::string(right).c_str());
							if (id > 0)
								dels.push_back({id, 1});
						}
					}
					else if (atom.find('=') != std::string_view::npos)
					{
						const std::size_t eq = atom.find('=');
						const int id = std::atoi(std::string(atom.substr(eq + 1)).c_str());
						if (id > 0)
							dels.push_back({id, 1});
					}
				}
				if (amp == std::string_view::npos)
					break;
				astart = amp + 1;
			}
		}
	}
	return dels;
}

} // namespace

// ★★ config.cpp 把 demo_battle.slot 的上限写死成 9,因为 L0 够不着 L3
//    (platform 不依赖 rules,那是分层的硬约束)。⇒ 两处一致性由这里守。
//    ⚠️ 少了它,某天 kSideOffset 改了、配置校验照旧,表现是玩家被放进敌方半场
//      而没有任何一处报错 —— 00 §10.4 那类静默错误。
static_assert(SA::Rules::kSideOffset == 10,
              "demo_battle.slot 的配置上限(config.cpp 里的 9)是按 "
              "kSideOffset == 10 写死的;kSideOffset 变了就要同步改那里");

World::World(const SA::Platform::ServerConfig &config,
             SA::Platform::Clock &clock, SA::Platform::Logger &logger,
             SA::Platform::RandomSource &random,
             SA::Net::Transport &transport, SA::SessionStorage::Service *storage)
    : _impl(std::make_unique<Impl>(config, clock, logger, random, transport))
{
	_impl->storage = storage;
	transport.setEvents(this);
}

World::~World() = default;

std::int32_t World::Impl::countPlayerItems(const SA::Model::Player &p, std::int32_t item_id) const
{
	std::int32_t cnt = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (p.items[i].valid())
		{
			if (const auto *it = items.resolve(p.items[i]))
			{
				if (it->item_id == item_id)
				{
					cnt += std::max(1, it->current_pile);
				}
			}
		}
	}
	return cnt;
}

std::int32_t World::Impl::countPlayerPets(const SA::Model::Player &p, std::int32_t pet_id,
                                          std::int32_t min_lvl) const
{
	std::int32_t cnt = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
	{
		if (p.pets[i].valid())
		{
			if (const auto *pet = pets.resolve(p.pets[i]))
			{
				if (pet->pet_id == pet_id && pet->level >= min_lvl)
				{
					++cnt;
				}
			}
		}
	}
	return cnt;
}

std::int32_t World::Impl::countFreeItemSlots(const SA::Model::Player &p) const
{
	std::int32_t free_cnt = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (!p.items[i].valid())
			++free_cnt;
	}
	return free_cnt;
}

std::int32_t World::Impl::countFreePetSlots(const SA::Model::Player &p) const
{
	std::int32_t free_cnt = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
	{
		if (!p.pets[i].valid())
			++free_cnt;
	}
	return free_cnt;
}

void World::Impl::sendExChangeWindow(SA::Net::SessionId id, std::uint64_t npc_id,
                                     const std::string &raw_text, std::uint32_t buttons)
{
	auto it = conns.find(id);
	if (it == conns.end())
		return;

	SA::Domain::WindowOpen win{};
	win.window_id = ++next_window_id;
	win.kind = SA::Domain::WindowKind::WINDOW_KIND_MESSAGE;
	win.buttons = buttons;
	win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
	win.source.entity_id = static_cast<std::uint32_t>(npc_id);
	win.body_kind = SA::Domain::WindowOpen::BodyKind::MESSAGE;
	win.body.message.wide = false;

	std::size_t lstart = 0;
	while (lstart < raw_text.size() && win.body.message.lines.size() < 16)
	{
		const std::size_t nl = raw_text.find('\n', lstart);
		std::string line = (nl == std::string::npos) ? raw_text.substr(lstart)
		                                             : raw_text.substr(lstart, nl - lstart);
		if (line.size() > 255)
			line.resize(255);
		if (auto *slot = win.body.message.lines.push_back())
			slot->assign(line.data(), line.size());
		if (nl == std::string::npos)
			break;
		lstart = nl + 1;
	}
	if (win.body.message.lines.empty())
	{
		std::string line = raw_text;
		if (line.size() > 255)
			line.resize(255);
		if (auto *slot = win.body.message.lines.push_back())
			slot->assign(line.data(), line.size());
	}

	it->second.active_window_id = win.window_id;
	it->second.active_window_npc_id = static_cast<std::uint64_t>(npc_id);
	it->second.last_window_text = raw_text;
	sendTo(id, win);
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

bool World::Impl::checkExChangePreconditions(const SA::Model::Player &p,
                                             const ExChangeBlock &blk, int branch_idx,
                                             std::string &msg_out,
                                             SA::Net::SessionId session_id)
{
	// 1. 石币不足门 (DelStone vs p.gold)
	if (blk.del_stone > 0 && p.gold < blk.del_stone)
	{
		msg_out = !blk.stone_less_msg.empty() ? blk.stone_less_msg : "石币不足。";
		return false;
	}

	// 2. 石币超限门 (GetStone vs maxHaveGold)
	if (blk.get_stone > 0 && (p.gold + blk.get_stone > maxHaveGold(0)))
	{
		if (!blk.stone_full_msg.empty())
		{
			msg_out = blk.stone_full_msg;
			return false;
		}
	}

	// 3. 声望不足门 (DelFame)
	if (blk.del_fame > 0)
	{
		int fame = 0;
		const auto fit = player_extra_stats.find(session_id);
		if (fit != player_extra_stats.end())
			fame = fit->second.fame;
		if (fame < blk.del_fame)
		{
			msg_out = "声望不足。";
			return false;
		}
	}

	// 4. 交付道具持有校验 (必须在背包中拥有足够的道具)
	const auto req_dels = resolveDelItems(blk, branch_idx);
	for (const auto &d : req_dels)
	{
		if (countPlayerItems(p, d.item_id) < d.count)
		{
			msg_out = "缺少所需道具。";
			return false;
		}
	}

	// 5. 交付宠物持有校验 (必须在随行宠物中拥有足够的宠物)
	const auto req_del_pets = resolveDelPets(blk, branch_idx);
	for (const auto &d : req_del_pets)
	{
		if (countPlayerPets(p, d.pet_id, 0) < d.count)
		{
			msg_out = "缺少所需宠物。";
			return false;
		}
	}

	// 6. 背包容量门 (ItemFullCheck, 09 §4)
	if (!blk.get_item.empty())
	{
		const auto gets = parseExchangeItems(blk.get_item);
		std::int32_t get_slots = 0;
		for (const auto &g : gets)
			get_slots += g.count;

		const auto dels = req_dels;
		std::int32_t del_slots = 0;
		for (const auto &d : dels)
		{
			std::int32_t rem = d.count;
			for (std::size_t i = SA::Model::kStartItemArray;
			     i < SA::Model::kMaxItemHave && rem > 0; ++i)
			{
				if (p.items[i].valid())
				{
					if (const auto *item = items.resolve(p.items[i]))
					{
						if (item->item_id == d.item_id)
						{
							++del_slots;
							--rem;
						}
					}
				}
			}
		}

		const std::int32_t free_slots = countFreeItemSlots(p);
		if (free_slots + del_slots < get_slots)
		{
			msg_out = !blk.item_full_msg.empty() ? blk.item_full_msg : "道具栏已满。";
			return false;
		}
	}

	// 7. 宠物槽容量门 (PetFullCheck, 09 §4)
	if (!blk.get_pet.empty())
	{
		const auto gets = parseExchangePets(blk.get_pet);
		std::int32_t get_pet_slots = 0;
		for (const auto &g : gets)
			get_pet_slots += g.count;

		const auto dels = req_del_pets;
		std::int32_t del_pet_slots = 0;
		for (const auto &d : dels)
		{
			std::int32_t rem = d.count;
			for (std::size_t i = 0; i < SA::Model::kMaxPetHave && rem > 0; ++i)
			{
				if (p.pets[i].valid())
				{
					if (const auto *pet = pets.resolve(p.pets[i]))
					{
						if (pet->pet_id == d.pet_id)
						{
							++del_pet_slots;
							--rem;
						}
					}
				}
			}
		}

		const std::int32_t free_pet_slots = countFreePetSlots(p);
		if (free_pet_slots + del_pet_slots < get_pet_slots)
		{
			msg_out = !blk.pet_full_msg.empty() ? blk.pet_full_msg : "宠物栏已满。";
			return false;
		}
	}

	return true;
}

void World::Impl::applyExChangeEffects(SA::Net::SessionId id, SA::Model::Player &p,
                                       const ExChangeBlock &blk, int branch_idx)
{
	// ① 扣除石币 (必须走 delGold, 守卫 check_gold_writes)
	if (blk.del_stone > 0)
	{
		(void)delGold(p, GoldReason::kQuestFee, blk.del_stone, /*trans=*/0, /*corr=*/0, *this);
	}

	// ② 给予石币 (必须走 addGold, 守卫 check_gold_writes)
	if (blk.get_stone > 0)
	{
		(void)addGold(p, GoldReason::kQuestReward, blk.get_stone, /*trans=*/0, /*corr=*/0, *this);
	}

	// ③ 声望结算
	if (blk.del_fame > 0)
	{
		player_extra_stats[id].fame = std::max(0, player_extra_stats[id].fame - blk.del_fame);
	}
	if (blk.add_fame > 0)
	{
		player_extra_stats[id].fame += blk.add_fame;
	}

	// ④ 经验与属性点奖励
	if (blk.add_exp > 0)
	{
		p.exp += blk.add_exp;
	}
	if (blk.add_skill_points > 0)
	{
		p.skillup_points += blk.add_skill_points;
	}

	// ⑤ 生命与法力恢复
	if (blk.heal_hp > 0)
	{
		const auto max_hp = SA::Rules::deriveBaseStats(p.vital, p.str, p.tough, p.dex).max_hp;
		if (max_hp > 0)
			p.hp = std::min(max_hp, p.hp + blk.heal_hp);
		else
			p.hp += blk.heal_hp;
	}
	if (blk.heal_mp > 0)
	{
		if (p.max_mp > 0)
			p.mp = std::min(p.max_mp, p.mp + blk.heal_mp);
		else
			p.mp += blk.heal_mp;
	}

	// ⑥ 扣除道具
	const auto dels = resolveDelItems(blk, branch_idx);
	for (const auto &d : dels)
	{
		std::int32_t remaining = d.count;
		for (std::size_t i = SA::Model::kStartItemArray;
		     i < SA::Model::kMaxItemHave && remaining > 0; ++i)
		{
			if (p.items[i].valid())
			{
				auto *item = items.resolve(p.items[i]);
				if (item != nullptr && item->item_id == d.item_id)
				{
					if (item->current_pile > remaining)
					{
						item->current_pile -= remaining;
						remaining = 0;
					}
					else
					{
						remaining -= std::max(1, item->current_pile);
						const auto h = p.items[i];
						p.clearItemSlot(static_cast<int>(i));
						items.release(h);
					}
				}
			}
		}
	}

	// ⑦ 给予道具
	if (!blk.get_item.empty())
	{
		const auto gets = parseExchangeItems(blk.get_item);
		for (const auto &g : gets)
		{
			for (int c = 0; c < g.count; ++c)
			{
				SA::Model::Item new_item{};
				new_item.uid = ++next_window_id;
				new_item.item_id = g.item_id;
				new_item.current_pile = 1;
				new_item.use_pile_nums = 1;
				(void)giveItemIntoPlayer(p, new_item, items);
			}
		}
	}

	// ⑧ 扣除宠物
	const auto del_pets = resolveDelPets(blk, branch_idx);
	for (const auto &d : del_pets)
	{
		std::int32_t remaining = d.count;
		for (std::size_t i = 0; i < SA::Model::kMaxPetHave && remaining > 0; ++i)
		{
			if (p.pets[i].valid())
			{
				const auto *pet = pets.resolve(p.pets[i]);
				if (pet != nullptr && pet->pet_id == d.pet_id)
				{
					const auto h = p.pets[i];
					p.clearPetSlot(static_cast<int>(i));
					pets.release(h);
					--remaining;
				}
			}
		}
	}

	// ⑨ 给予宠物
	if (!blk.get_pet.empty())
	{
		const auto gets = parseExchangePets(blk.get_pet);
		for (const auto &g : gets)
		{
			for (int c = 0; c < g.count; ++c)
			{
				const int slot = p.findFreePetSlot();
				if (slot >= 0)
				{
					const SA::Model::EntityHandle ph = pets.allocate();
					if (ph.valid())
					{
						if (auto *pet_dst = pets.resolve(ph))
						{
							pet_dst->uid = ++next_window_id;
							pet_dst->pet_id = g.pet_id;
							pet_dst->level = 1;
							pet_dst->hp = 100;
							pet_dst->mp = 100;
							pet_dst->max_mp = 100;
							pet_dst->owner = player_of_session.find(id);
							p.pets[static_cast<std::size_t>(slot)] = ph;
						}
					}
				}
			}
		}
	}

	// ⑩ 旗标操作
	auto parse_and_apply_flags = [](std::string_view flg_str, auto &&func)
	{
		if (flg_str.empty())
			return;
		std::size_t start = 0;
		while (start < flg_str.size())
		{
			const std::size_t comma = flg_str.find(',', start);
			const std::string s_flag =
			    (comma == std::string_view::npos) ? std::string(flg_str.substr(start))
			                                      : std::string(flg_str.substr(start, comma - start));
			const int f = std::atoi(s_flag.c_str());
			if (f >= 0 && f < 256)
				func(f);
			if (comma == std::string_view::npos)
				break;
			start = comma + 1;
		}
	};

	parse_and_apply_flags(blk.end_set_flg, [&](int f)
	                      { p.setEndEvent(f); });
	parse_and_apply_flags(blk.set_now_flg, [&](int f)
	                      { p.setNowEvent(f); });
	parse_and_apply_flags(blk.clean_flg, [&](int f)
	                      { p.clearNowEvent(f); p.clearEndEvent(f); });
	parse_and_apply_flags(blk.clean_now_flg, [&](int f)
	                      { p.clearNowEvent(f); });
	parse_and_apply_flags(blk.clean_end_flg, [&](int f)
	                      { p.clearEndEvent(f); });

	if (blk.event_no != -1)
	{
		if (!blk.end_set_flg.empty())
			p.clearNowEvent(blk.event_no);
		else
			p.setNowEvent(blk.event_no);
	}

	// ⑪ 传送效果 (NpcWarp: floor,x,y)
	if (!blk.npc_warp.empty())
	{
		const std::size_t c1 = blk.npc_warp.find(',');
		if (c1 != std::string::npos)
		{
			const std::size_t c2 = blk.npc_warp.find(',', c1 + 1);
			if (c2 != std::string::npos)
			{
				const int wf = std::atoi(blk.npc_warp.substr(0, c1).c_str());
				const int wx = std::atoi(blk.npc_warp.substr(c1 + 1, c2 - c1 - 1).c_str());
				const int wy = std::atoi(blk.npc_warp.substr(c2 + 1).c_str());
				warpSinglePlayer(id, wf, wx, wy);
			}
		}
	}
}

namespace
{

// ── 走路辅助(批次 W.1)────────────────────────────────────────────────
//
// 走路间隔:原版 CHAR_walk_check(char.c:4590)判 `time_diff_us >= walksendinterval*100`,
//   csa8.0 setup.cf `walkinterval=2500` ⇒ 2500 × 100us = 250ms 一格。
constexpr SA::Platform::Millis kWalkIntervalMs = 250;

// 方向 0-7 → 坐标增量。★ 照抄 CHAR_dxdy[8](char.c:2325):北起顺时针,含四斜向。
struct DirDelta
{
	std::int32_t dx;
	std::int32_t dy;
};
constexpr DirDelta kDirDelta[8] = {
    {0, -1},
    {1, -1},
    {1, 0},
    {1, 1},
    {0, 1},
    {-1, 1},
    {-1, 0},
    {-1, -1},
};

// 方向字符解码 —— 移植 CHAR_ctodirmode(char_walk.c:1398):
//   小写 'a'-'h' ⇒ 移动(is_turn=false);其余(大写 'A'-'H')⇒ 转身;dir = tolower-'a'。
// 返回 false = 非法字符(dir 越界),调用方跳过该字符。
bool decodeDirChar(char moji, std::uint8_t &dir, bool &is_turn)
{
	is_turn = !(moji >= 'a' && moji <= 'h'); // 小写 a-h 才是移动(:1401)
	const char lower =
	    (moji >= 'A' && moji <= 'Z') ? static_cast<char>(moji - 'A' + 'a') : moji;
	const int d = lower - 'a';
	if (d < 0 || d > 7)
		return false;
	dir = static_cast<std::uint8_t>(d);
	return true;
}

// 两点相对方向计算 —— 移植 NPC_Util_getDirFromTwoPoint(npcutil.c:280):
//   返回 sx, sy 到 ex, ey 的 8 方向 dir (0-7); 若坐标重合返回 -1。
inline int getDirFromTwoPoints(std::int32_t sx, std::int32_t sy, std::int32_t ex, std::int32_t ey) noexcept
{
	static constexpr int dirtable[3][3] = {
	    {7, 0, 1},
	    {6, -1, 2},
	    {5, 4, 3},
	};
	int difx = ex - sx;
	int dify = ey - sy;
	if (difx < 0)
		difx = -1;
	else if (difx > 0)
		difx = 1;
	if (dify < 0)
		dify = -1;
	else if (dify > 0)
		dify = 1;
	return dirtable[dify + 1][difx + 1];
}

// 走一步 —— 移植 CHAR_walk_move(char_walk.c:195)的**地图碰撞 + 坐标更新**核心。
// ⚠️ 本批不做(各有归属):目标格对象碰撞(notover,:350,需 olink)· 进出格 on/off 事件
//    (RunCharOverlapEvent,:281-449,依赖 NPC/Lua)· 视野广播(:469,视野批次)· 遇敌(:585,遇敌批次)。
// 返回 true = 位置真的变了(供视野批次决定是否广播)。
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

// ── 视野广播辅助(里程碑②)────────────────────────────────────────────────
//
// 视野常量。★ 决策取 23(10 §9 决策1 / 05 §5.1),⚠️ 而展开视图 unifdef_80 的
//   CHAR_DEFAULTSEESIZ 是 **20**(char_base.h:58,8.5 血统 —— unifdef 不改 #define 字面值);
//   8.0 血统源码取 23、与数据基线一致,但 **#define 不进符号表 ⇒ 无二进制证据**
//   (00 §10.2 六项不可判定之一)⇒ 取 23 是裁定不是观测。
// 扫格公式 (2*(c/2)+1)² = 23² = 529(10 §5.1;c/2 是整数除,c 为奇数时 ≠ (c+1)²)。
constexpr std::int32_t kSeeSize = 23;
constexpr std::int32_t kSeeRadius = kSeeSize / 2; // 11

bool visContains(const std::vector<SA::Net::ConnectionId> &v, SA::Net::ConnectionId c)
{
	return std::find(v.begin(), v.end(), c) != v.end();
}

// (tx,ty) 是否落在以 (cx,cy) 为心的 529 格视野框内(与 collectVisible 的方框一致)。批次 W.3。
bool inSee(std::int32_t cx, std::int32_t cy, std::int32_t tx, std::int32_t ty) noexcept
{
	return tx >= cx - kSeeRadius && tx <= cx + kSeeRadius && ty >= cy - kSeeRadius &&
	       ty <= cy + kSeeRadius;
}

SA::Domain::CharAppear makeAppear(SA::Net::ConnectionId who, const SA::Model::Player &p)
{
	SA::Domain::CharAppear a{};
	a.entity_id = who;
	a.floor = p.floor;
	a.x = p.x;
	a.y = p.y;
	a.dir = static_cast<std::uint32_t>(p.dir);
	a.image = p.image;
	return a;
}

} // namespace

// ══ World::Impl 的视野广播方法(里程碑②)══════════════════════════════════

// 扫 (cx,cy) 周围 529 格的 olink,收集其中的玩家会话(除 self)。
//   ★ 10 §5.3 决策5:先扫格 + 聚合,不做订阅(视野连续变化,订阅维护成本可能更高,
//     留到有实测数据之后)。
std::vector<SA::Net::ConnectionId> World::Impl::collectVisible(std::int32_t floor, std::int32_t cx, std::int32_t cy,
                                                               SA::Net::ConnectionId self) const
{
	std::vector<SA::Net::ConnectionId> out;
	const auto *fl = getFloor(floor);
	const auto &m = fl ? fl->map : map;
	const auto &ol = fl ? fl->olink : olink;
	for (std::int32_t j = cy - kSeeRadius; j <= cy + kSeeRadius; ++j)
		for (std::int32_t i = cx - kSeeRadius; i <= cx + kSeeRadius; ++i)
		{
			if (!m.inBounds(i, j))
				continue;
			const auto idx = m.index(i, j);
			if (idx >= ol.size())
				continue;
			for (const SA::Net::ConnectionId c : ol[idx])
				if (c != self)
					out.push_back(c);
		}
	return out;
}

// 某会话进入视野 ⇒ 双向 CharAppear(视野对称:我看到你出现,你也看到我出现,char.c:4100)。
void World::Impl::appearBetween(SA::Net::ConnectionId a, const SA::Model::Player &pa,
                                SA::Net::ConnectionId b)
{
	const SA::Model::Player *pb = players.resolve(player_of_session.find(b));
	if (pb == nullptr)
		return;
	sendTo(b, makeAppear(a, pa));  // b 看到 a 出现
	sendTo(a, makeAppear(b, *pb)); // a 看到 b 出现
}

// A 移动 (ox,oy)→(p.x,p.y) 后的视野广播(扫格 diff,10 §5.2)。⚠️ olink 须**已更新到新位置**。
void World::Impl::broadcastMove(SA::Net::ConnectionId mover, std::int32_t ox, std::int32_t oy,
                                const SA::Model::Player &p)
{
	const auto old_vis = collectVisible(p.floor, ox, oy, mover);
	const auto new_vis = collectVisible(p.floor, p.x, p.y, mover);

	SA::Domain::CharMove mv{};
	mv.entity_id = mover;
	mv.x = p.x;
	mv.y = p.y;
	mv.dir = static_cast<std::uint32_t>(p.dir);

	for (const SA::Net::ConnectionId b : new_vis)
	{
		if (visContains(old_vis, b))
			sendTo(b, mv); // 一直可见 ⇒ b 看到 a 移动
		else
			appearBetween(mover, p, b); // 新进入 ⇒ 双向出现
	}
	for (const SA::Net::ConnectionId b : old_vis)
	{
		if (visContains(new_vis, b))
			continue;
		SA::Domain::CharDisappear dis{}; // 离开 ⇒ 双向消失
		dis.entity_id = mover;
		sendTo(b, dis); // b 看到 a 消失
		SA::Domain::CharDisappear dis2{};
		dis2.entity_id = b;
		sendTo(mover, dis2); // a 看到 b 消失
	}
}

// 出生 / 进图:与视野内每个玩家双向 CharAppear(原版进图 CHAR_sendCToArroundCharacter)。
void World::Impl::broadcastSpawn(SA::Net::ConnectionId who, const SA::Model::Player &p)
{
	for (const SA::Net::ConnectionId b : collectVisible(p.floor, p.x, p.y, who))
		appearBetween(who, p, b);
}

// 离场 / 断线:给视野内每个玩家发 CharDisappear(who)。⚠️ 须在 olink 移除**之前**调(要 who 的位置)。
void World::Impl::broadcastDespawn(SA::Net::ConnectionId who, std::int32_t floor, std::int32_t x, std::int32_t y)
{
	SA::Domain::CharDisappear dis{};
	dis.entity_id = who;
	for (const SA::Net::ConnectionId b : collectVisible(floor, x, y, who))
		sendTo(b, dis);
}

// ══ 世界敌人:视野 / 生成 / 游荡(批次 W.2 / W.3)══════════════════════════════

// 收视野内玩家会话(★ 不排除 self)。敌人无会话,没有"自己"要排 —— 与 collectVisible 的唯一区别。
std::vector<SA::Net::ConnectionId> World::Impl::collectVisiblePlayers(std::int32_t floor, std::int32_t cx,
                                                                      std::int32_t cy) const
{
	std::vector<SA::Net::ConnectionId> out;
	const auto *fl = getFloor(floor);
	const auto &m = fl ? fl->map : map;
	const auto &ol = fl ? fl->olink : olink;
	for (std::int32_t j = cy - kSeeRadius; j <= cy + kSeeRadius; ++j)
		for (std::int32_t i = cx - kSeeRadius; i <= cx + kSeeRadius; ++i)
		{
			if (!m.inBounds(i, j))
				continue;
			const auto idx = m.index(i, j);
			if (idx >= ol.size())
				continue;
			for (const SA::Net::ConnectionId c : ol[idx])
				out.push_back(c);
		}
	return out;
}

// 敌人进入世界 ⇒ 给视野内每个玩家发 CharAppear(★ 单向)。
void World::Impl::broadcastEnemySpawn(const SA::Model::Enemy &e, std::uint64_t eid)
{
	const SA::Domain::CharAppear a = makeEnemyAppear(eid, e);
	for (const SA::Net::ConnectionId b : collectVisiblePlayers(e.floor, e.x, e.y))
		sendTo(b, a);
}

// 敌人移动一步 ⇒ 扫格 diff(同 broadcastMove 但单向:一直可见→CharMove / 新进→CharAppear / 离开→CharDisappear)。
void World::Impl::broadcastEnemyMove(const SA::Model::Enemy &e, std::uint64_t eid,
                                     std::int32_t ox, std::int32_t oy)
{
	const auto old_vis = collectVisiblePlayers(e.floor, ox, oy);
	const auto new_vis = collectVisiblePlayers(e.floor, e.x, e.y);

	SA::Domain::CharMove mv{};
	mv.entity_id = eid;
	mv.x = e.x;
	mv.y = e.y;
	mv.dir = static_cast<std::uint32_t>(e.dir);
	mv.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
	const SA::Domain::CharAppear ap = makeEnemyAppear(eid, e);

	for (const SA::Net::ConnectionId b : new_vis)
	{
		if (visContains(old_vis, b))
			sendTo(b, mv); // 一直可见 ⇒ 移动
		else
			sendTo(b, ap); // 新进入视野 ⇒ 出现
	}
	SA::Domain::CharDisappear dis{};
	dis.entity_id = eid;
	dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
	for (const SA::Net::ConnectionId b : old_vis)
		if (!visContains(new_vis, b))
			sendTo(b, dis); // 离开视野 ⇒ 消失
}

// 敌人离开世界(被拉进战斗 / 死亡)⇒ 给视野内每个玩家发 CharDisappear。⚠️ 须在改位置**之前**调。
void World::Impl::broadcastEnemyDespawn(std::int32_t floor, std::int32_t x, std::int32_t y, std::uint64_t eid)
{
	SA::Domain::CharDisappear dis{};
	dis.entity_id = eid;
	dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
	for (const SA::Net::ConnectionId b : collectVisiblePlayers(floor, x, y))
		sendTo(b, dis);
}

// 玩家从 (ox,oy) 走到 (p.x,p.y) 后,补发**世界敌人**的 appear / disappear(★ 玩家看敌人那一半)。
//   对每只世界敌人:旧位置可见→新不可见 ⇒ CharDisappear;旧不可见→新可见 ⇒ CharAppear;
//   两者都可见 ⇒ 不发(敌人自身移动由 broadcastEnemyMove 覆盖)。
void World::Impl::refreshEnemyView(SA::Net::ConnectionId viewer, const SA::Model::Player &p,
                                   std::int32_t ox, std::int32_t oy)
{
	for (const WorldEnemy &we : world_enemies)
	{
		const SA::Model::Enemy *e = enemies.resolve(we.handle);
		if (e == nullptr || e->floor != p.floor)
			continue;
		const bool saw = inSee(ox, oy, e->x, e->y);
		const bool sees = inSee(p.x, p.y, e->x, e->y);
		if (sees == saw)
			continue;
		const std::uint64_t eid = encodeHandle(we.handle);
		if (sees)
			sendTo(viewer, makeEnemyAppear(eid, *e));
		else
		{
			SA::Domain::CharDisappear dis{};
			dis.entity_id = eid;
			dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
			sendTo(viewer, dis);
		}
	}
}

// 玩家从 (ox,oy) 走到 (p.x,p.y) 后,补发世界 NPC 的 appear / disappear(批次 W.7)。
void World::Impl::refreshNpcView(SA::Net::ConnectionId viewer, const SA::Model::Player &p,
                                 std::int32_t ox, std::int32_t oy)
{
	for (const NpcEntity &npc : npc_entities)
	{
		if (npc.floor != p.floor)
			continue;
		const bool saw = inSee(ox, oy, npc.x, npc.y);
		const bool sees = inSee(p.x, p.y, npc.x, npc.y);
		if (sees == saw)
			continue;
		if (sees)
		{
			SA::Domain::CharAppear a{};
			a.entity_id = npc.id;
			a.floor = npc.floor;
			a.x = npc.x;
			a.y = npc.y;
			a.dir = static_cast<std::uint32_t>(npc.dir);
			a.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
			a.image = npc.image;
			sendTo(viewer, a);
		}
		else
		{
			SA::Domain::CharDisappear dis{};
			dis.entity_id = npc.id;
			dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
			sendTo(viewer, dis);
		}
	}
}

// 检查是否有在线玩家当前正在与该 NPC 打开窗口对话 (批次 W.11)
bool World::Impl::isNpcEngagedInDialog(std::uint64_t npc_id) const
{
	for (const auto &kv : conns)
	{
		if (kv.second.active_window_id > 0 && kv.second.active_window_npc_id == npc_id)
			return true;
	}
	return false;
}

// NPC 移动一步广播 (批次 W.11, 视野扫格 diff: 一直可见→CharMove / 新进→CharAppear / 离开→CharDisappear)
void World::Impl::broadcastNpcMove(const NpcEntity &npc, std::int32_t ox, std::int32_t oy)
{
	const auto old_vis = collectVisiblePlayers(npc.floor, ox, oy);
	const auto new_vis = collectVisiblePlayers(npc.floor, npc.x, npc.y);

	SA::Domain::CharMove mv{};
	mv.entity_id = npc.id;
	mv.x = npc.x;
	mv.y = npc.y;
	mv.dir = static_cast<std::uint32_t>(npc.dir);
	mv.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);

	SA::Domain::CharAppear ap{};
	ap.entity_id = npc.id;
	ap.floor = npc.floor;
	ap.x = npc.x;
	ap.y = npc.y;
	ap.dir = static_cast<std::uint32_t>(npc.dir);
	ap.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	ap.image = npc.image;

	for (const SA::Net::ConnectionId b : new_vis)
	{
		if (visContains(old_vis, b))
			sendTo(b, mv); // 一直可见 ⇒ 移动或转身
		else
			sendTo(b, ap); // 新进入视野 ⇒ 出现
	}
	SA::Domain::CharDisappear dis{};
	dis.entity_id = npc.id;
	dis.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC);
	for (const SA::Net::ConnectionId b : old_vis)
	{
		if (!visContains(new_vis, b))
			sendTo(b, dis); // 离开视野 ⇒ 消失
	}
}

// kCharLoop 非玩家段: 世界 NPC 巡逻与漫游游荡 (条数制摊还, 批次 W.11)
void World::Impl::wanderNpcs(std::size_t max_this_tick)
{
	const std::size_t n = npc_entities.size();
	if (n == 0 || max_this_tick == 0)
		return;

	std::size_t moved = 0;
	std::size_t scanned = 0;

	while (scanned < n && moved < max_this_tick)
	{
		if (npc_charloop_cursor >= n)
			npc_charloop_cursor = 0;

		const std::size_t idx = npc_charloop_cursor++;
		++scanned;

		NpcEntity &npc = npc_entities[idx];
		if (npc.wander_interval_ms <= 0)
			continue;
		if (now_ms < npc.next_wander_at_ms)
			continue;

		// 对话锁定: 若有玩家正在与该 NPC 打开窗口对话, 本节拍不移动
		if (isNpcEngagedInDialog(npc.id))
		{
			npc.next_wander_at_ms = now_ms + npc.wander_interval_ms;
			continue;
		}

		std::int32_t dir = -1;
		std::int32_t nx = npc.x;
		std::int32_t ny = npc.y;

		if (!npc.route.empty())
		{
			// ── 模式 1: 巡逻路线模式 (沿着 route 路点逐步行进) ─────────
			if (npc.route_index >= npc.route.size())
				npc.route_index = 0;

			const NpcPoint &target = npc.route[npc.route_index];
			if (npc.x == target.x && npc.y == target.y)
			{
				npc.route_index = (npc.route_index + 1) % npc.route.size();
			}

			const NpcPoint &next_target = npc.route[npc.route_index];
			std::int32_t difx = next_target.x - npc.x;
			std::int32_t dify = next_target.y - npc.y;
			if (difx < 0)
				difx = -1;
			else if (difx > 0)
				difx = 1;
			if (dify < 0)
				dify = -1;
			else if (dify > 0)
				dify = 1;

			// 移植原版 NPC_Util_getDirFromTwoPoint dirtable[dify+1][difx+1]
			static constexpr int dirtable[3][3] = {
			    {7, 0, 1},
			    {6, -1, 2},
			    {5, 4, 3},
			};
			dir = dirtable[dify + 1][difx + 1];
			if (dir >= 0)
			{
				nx = npc.x + kDirDelta[dir].dx;
				ny = npc.y + kDirDelta[dir].dy;
			}
		}
		else if (npc.wander_radius > 0)
		{
			// ── 模式 2: 自由漫游模式 (以 born_x, born_y 为中心随机游荡) ───
			dir = static_cast<std::int32_t>(world_rng.randMod(8));
			nx = npc.x + kDirDelta[dir].dx;
			ny = npc.y + kDirDelta[dir].dy;

			std::int32_t adx = nx - npc.born_x;
			adx = adx < 0 ? -adx : adx;
			std::int32_t ady = ny - npc.born_y;
			ady = ady < 0 ? -ady : ady;

			if (adx > npc.wander_radius || ady > npc.wander_radius)
			{
				// 超出游荡半径，不位移但可转向
				dir = -1;
			}
		}

		if (dir >= 0)
		{
			npc.dir = static_cast<std::uint8_t>(dir);

			// ── 通行与碰撞守卫 ───────────────────────────────────────
			bool blocked = false;

			// ① 地图通行门 (含斜向墙角保护)
			const auto *npc_fl = getFloor(npc.floor);
			const auto &npc_map = npc_fl ? npc_fl->map : map;
			if (!mapWalkable(npc_map, map_attr, nx, ny))
			{
				blocked = true;
			}
			else if (kDirDelta[dir].dx != 0 && kDirDelta[dir].dy != 0)
			{
				if (!mapWalkable(npc_map, map_attr, npc.x + kDirDelta[dir].dx, npc.y) ||
				    !mapWalkable(npc_map, map_attr, npc.x, npc.y + kDirDelta[dir].dy))
				{
					blocked = true;
				}
			}

			// ② 实体碰撞门 1: 撞其他 NPC (CHAR_ISOVERED=0)
			if (!blocked)
			{
				const std::size_t other_npc = npcAt(npc.floor, nx, ny);
				if (other_npc != npc_entities.size() && other_npc != idx)
				{
					blocked = true;
				}
			}

			// ③ 实体碰撞门 2: 撞玩家实体 (CHAR_ISOVERED=0)
			if (!blocked)
			{
				for (const auto &kv : conns)
				{
					const auto *p = players.resolve(player_of_session.find(kv.first));
					if (p != nullptr && p->floor == npc.floor && p->x == nx && p->y == ny)
					{
						blocked = true;
						break;
					}
				}
			}

			// ④ 实体碰撞门 3: 撞世界敌人 (明雷)
			if (!blocked && worldEnemyAt(npc.floor, nx, ny) != world_enemies.size())
			{
				blocked = true;
			}

			if (!blocked)
			{
				const std::int32_t ox = npc.x;
				const std::int32_t oy = npc.y;
				npc.x = nx;
				npc.y = ny;
				broadcastNpcMove(npc, ox, oy);
			}
			else
			{
				// 阻挡未位移: 原地更新朝向并向视野内广播转身
				broadcastNpcMove(npc, npc.x, npc.y);
			}
		}

		npc.next_wander_at_ms = now_ms + npc.wander_interval_ms;
		++moved;
	}
}

// (floor,x,y) 上的世界敌人在 world_enemies 的下标;无则返回 world_enemies.size()(批次 W.5)。
//   ★ 撞明雷退回(kCharLoop)与明雷开战(onEvent 面前格)共用这一个「这格有没有明雷」查询。
std::size_t World::Impl::worldEnemyAt(std::int32_t floor, std::int32_t x, std::int32_t y)
{
	for (std::size_t i = 0; i < world_enemies.size(); ++i)
	{
		const SA::Model::Enemy *e = enemies.resolve(world_enemies[i].handle);
		if (e != nullptr && e->floor == floor && e->x == x && e->y == y)
			return i;
	}
	return world_enemies.size();
}

// kNpcSpawn:据刷怪点把世界态敌人补齐到各点 count。★ 不阻塞 D6 的注入式刷怪(见 Api.h SpawnPoint)。
void World::Impl::spawnWorldEnemies()
{
	for (std::size_t pi = 0; pi < spawn_points.size(); ++pi)
	{
		const SpawnPoint &sp = spawn_points[pi];
		const std::size_t target = sp.count < 0 ? 0 : static_cast<std::size_t>(sp.count);
		std::size_t alive = 0;
		for (const WorldEnemy &we : world_enemies)
			if (we.spawn_point == pi)
				++alive;
		while (alive < target)
		{
			// 敌人来源:enemy_id → 敌人表行 → 模板行(单一真源,复用 M.7 find + M.4b spawnEnemy)。
			const std::int32_t erow = findEnemyEncounter(encounters, sp.enemy_id);
			if (erow < 0)
			{
				logger.log(SA::Platform::LogLevel::kWarn,
				           SA::Platform::LogEvent::kWorldEnemySpawnFailed,
				           {{"enemy_id", static_cast<std::uint64_t>(sp.enemy_id)},
				            {"reason", std::string_view("no_encounter")}});
				break; // 敌人表查不到(多半没 loadEncounterTables)⇒ 该点整个刷不出
			}
			const EnemyEncounter &enc = encounters[static_cast<std::size_t>(erow)];
			const std::int32_t trow = findEnemyTemplate(enemy_templates, enc.temp_no);
			if (trow < 0)
			{
				logger.log(SA::Platform::LogLevel::kWarn,
				           SA::Platform::LogEvent::kWorldEnemySpawnFailed,
				           {{"enemy_id", static_cast<std::uint64_t>(sp.enemy_id)},
				            {"temp_no", static_cast<std::uint64_t>(enc.temp_no)},
				            {"reason", std::string_view("no_template")}});
				break;
			}
			const EnemyTemplate &tmpl = enemy_templates[static_cast<std::size_t>(trow)];
			const SA::Model::EntityHandle eh = enemies.allocate();
			if (!eh.valid())
			{
				logger.log(SA::Platform::LogLevel::kError,
				           SA::Platform::LogEvent::kEntityPoolExhausted,
				           {{"pool", std::string_view("enemy")},
				            {"capacity", static_cast<std::uint64_t>(kMaxEnemies)}});
				break;
			}
			SA::Model::Enemy *e = enemies.resolve(eh);
			if (e == nullptr)
			{
				(void)enemies.release(eh);
				break;
			}
			// ★ 生成用**世界 rng**(世界态敌人不在战斗内;同遇敌链)。⚠️ 消耗 world_rng ⇒
			//   刷怪时机影响遇敌骰子 / 选怪序列(原版刷怪也在全局 rand;可回放前提是刷怪调用序重现)。
			*e = spawnEnemy(tmpl, enc, sp.level, world_rng, rules_config);
			e->floor = sp.floor;
			e->x = sp.x;
			e->y = sp.y;
			e->dir = 0;
			e->next_wander_at_ms = now_ms + sp.wander_interval_ms;
			world_enemies.push_back({eh, pi});
			broadcastEnemySpawn(*e, encodeHandle(eh));
			logger.log(SA::Platform::LogLevel::kDebug,
			           SA::Platform::LogEvent::kWorldEnemySpawned,
			           {{"enemy_id", static_cast<std::uint64_t>(sp.enemy_id)},
			            {"floor", static_cast<std::uint64_t>(sp.floor)},
			            {"x", static_cast<std::uint64_t>(sp.x)},
			            {"y", static_cast<std::uint64_t>(sp.y)}});
			++alive;
		}
	}
}

// kCharLoop 非玩家段:**条数制**摊还(原 CHAR_Loop 的 #else 分支,`_CHAR_LOOP_TIME` 8.0 关)。
//   从 charloop_cursor 起,本 tick 最多**游荡** max_this_tick 只(对应原版 movecnt >= EnemyMoveNum),
//   最多**检查** world_enemies.size() 只(对应原版 for 的迭代上限,防没一个到期时空转),游标记位下 tick 续。
void World::Impl::wanderWorldEnemies(std::size_t max_this_tick)
{
	const std::size_t n = world_enemies.size();
	if (n == 0 || max_this_tick == 0)
		return;
	std::size_t moved = 0;   // 真跑了 AI 的只数(对应原版 movecnt:节拍到期即计,不论走没走成)
	std::size_t scanned = 0; // 检查的只数(对应原版 for 迭代上限,防空转)
	while (scanned < n && moved < max_this_tick)
	{
		if (charloop_cursor >= n)
			charloop_cursor = 0; // 绕回(原版 charcnt >= charnum ⇒ playernum)
		const WorldEnemy we = world_enemies[charloop_cursor];
		++charloop_cursor;
		++scanned;
		SA::Model::Enemy *e = enemies.resolve(we.handle);
		if (e == nullptr)
			continue; // 悬空(世界态理论上不会;守零成本)
		if (now_ms < e->next_wander_at_ms)
			continue; // 未到游荡节拍(对应 CHAR_callLoop 返回 FALSE ⇒ movecnt 不增)
		// 到期 ⇒ 游荡一步(随机方向 + 通行门 + 刷怪点半径门)。
		const SpawnPoint &sp = spawn_points[we.spawn_point];
		const std::uint8_t dir = static_cast<std::uint8_t>(world_rng.randMod(8));
		const std::int32_t nx = e->x + kDirDelta[dir].dx;
		const std::int32_t ny = e->y + kDirDelta[dir].dy;
		e->dir = dir; // 面向选中方向(即使没走成 = 转身,同原版 ctodirmode 大写转身)
		std::int32_t adx = nx - sp.x;
		adx = adx < 0 ? -adx : adx;
		std::int32_t ady = ny - sp.y;
		ady = ady < 0 ? -ady : ady;
		const auto *e_fl = getFloor(e->floor);
		const auto &e_map = e_fl ? e_fl->map : map;
		const bool blocked_or_far =
		    !mapWalkable(e_map, map_attr, nx, ny) ||
		    (sp.wander_radius >= 0 && (adx > sp.wander_radius || ady > sp.wander_radius));
		if (!blocked_or_far)
		{
			const std::int32_t ox = e->x;
			const std::int32_t oy = e->y;
			e->x = nx;
			e->y = ny;
			broadcastEnemyMove(*e, encodeHandle(we.handle), ox, oy);
		}
		// ★ 无论走没走成,节拍到了就顺延(对应原版 loopfunc 执行后记 now)⇒ moved 计数(movecnt)。
		e->next_wander_at_ms = now_ms + sp.wander_interval_ms;
		++moved;
	}
}

// ══ tick(01 §3.1)═══════════════════════════════════════════════
void World::tick()
{
	Impl &s = *_impl;
	if (s.stopped)
		return;
	++s.ticks;

	// ── 1. 时钟推进 ──
	// ★ 统一时钟源、单调时钟。整个 tick 内**只取一次** ——
	//   同一 tick 里两处取到不同的"现在"会让节拍判断出现自相矛盾的结果。
	s.now_ms = s.clock.nowMs();

	// ── 2. 网络入站 ──
	// 从传输层取已到达的字节,派发到会话。★ 不阻塞(01 §2)。
	s.transport.poll();
	processStorage();

	// ── 3. NPC 生成(批次 W.2)──
	//   据刷怪点把世界态敌人补齐到各点 count(默认无刷怪点 ⇒ 空操作,现有用例不受影响)。
	//   ★ 最小切法(不阻塞 D6 脚本层);真玩法接脚本层后由脚本产出刷怪参数,见 Api.h SpawnPoint。
	s.spawnWorldEnemies();

	// ── 4. 战斗推进 ──  ★ 受节拍层控制,不等于 tick 频率(01 §3.2)
	advanceBattles();

	// ── 5. 角色循环 —— 玩家段(批次 W.1。原 CHAR_Loop:4667 玩家 for + CHAR_walk_check:4583)──
	//   ★ 全扫在线玩家:走路串非空 且距上次走够 walksendinterval ⇒ 走一步(CHAR_walkcall)。
	//   ⚠️ 非玩家段(世界敌人 AI 摊还)见下方 5b —— **条数制**(EnemyMoveNum 上限 + 游标),
	//      **不是** CHAR_Loop:4712 那个时间预算 while(那是 _CHAR_LOOP_TIME,8.0 三证关,见 §9.0.45);
	//      组队跟随各留其批。
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
				if (lo > hi)
				{
					const std::int32_t t = lo;
					lo = hi;
					hi = t;
				}
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
				// 遇敌骰子 rand()%(120*getEnemyAction()) < temp(char_walk.c:585)。
				//   ★ 用**世界 rng**(遇敌是世界事件,不是战斗内可回放序列)。
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

	// ── 5b. 角色循环 —— 非玩家段:世界敌人 AI(批次 W.3)──────────────────────
	//   ★ 条数制摊还:每 tick 最多游荡 tempo.enemy_move_num 只世界敌人,游标续跑(wanderWorldEnemies)。
	//   ⚠️★ **不是时间预算制** —— 8.0 的 _CHAR_LOOP_TIME 三证实测关(15 §5.2 C18),走 #else 条数制。
	s.wanderWorldEnemies(s.config.tempo.enemy_move_num);

	// ── 5c. 角色循环 —— 非玩家段:世界 NPC 巡逻与漫游 AI(批次 W.11) ──────────
	//   ★ 条数制摊还:每 tick 最多游荡 tempo.enemy_move_num 只世界 NPC,游标续跑(wanderNpcs)。
	s.wanderNpcs(s.config.tempo.enemy_move_num);

	// ── 6. 定时业务 ──   ⬜ 阶段 2
	// ── 7. 出站聚合 ──   ⬜ 阶段 2(CA/CD 视野聚合;1.5 无视野)
	//
	// ⚠️ 但**出站字节仍要发出去** —— 上面第 4 步往 outbound 里写了东西。
	//    这不是 §7 说的那种聚合(那是视野 Appear/Disappear 攒批),
	//    只是"把已经生成的字节交给传输层"。别把这里读成 §7 已经做了。
	// send/close 可能同步触发断线回调，不能持有 conns 迭代器或其缓冲再继续使用。
	std::vector<SA::Net::ConnectionId> flushing;
	for (const auto &entry : s.conns)
		flushing.push_back(entry.first);
	for (auto id : flushing)
	{
		auto entry = s.conns.find(id);
		if (entry == s.conns.end())
			continue;
		bool closing = entry->second.session != nullptr && entry->second.session->closed();
		std::vector<std::uint8_t> bytes;
		bytes.swap(entry->second.outbound);
		if (!bytes.empty() && !s.transport.send(id, bytes.data(), bytes.size()))
			closing = true;
		bytes.clear();
		entry = s.conns.find(id);
		if (entry != s.conns.end() && entry->second.outbound.empty())
			bytes.swap(entry->second.outbound);
		if (closing)
			s.transport.close(id);
	}

	// ── 8. 关闭检查 ──
	if (s.shutdown_requested && s.storage)
	{
		std::vector<SA::Net::SessionId> sessions;
		for (const auto &entry : s.conns)
			if (!entry.second.detached)
				sessions.push_back(entry.first);
		for (auto id : sessions)
		{
			onDisconnected(id);
			s.transport.close(id);
		}
		s.stopped = s.conns.empty() && s.storage->idle();
		return;
	}
	if (s.shutdown_requested)
	{
		// ⚠️ 01 §11.2 的完整停服流程(拒绝新连接 → 广播倒计时 → 逐会话保存
		//    → 等在途请求收敛 → 落盘确认)在 1.5 **做不了也不该做**:
		//    没有 storage、没有跨模块请求。这里只做能做的那部分。
		s.logger.log(SA::Platform::LogLevel::kInfo,
		             SA::Platform::LogEvent::kServerStopping,
		             {{"connections", static_cast<std::uint64_t>(s.conns.size())},
		              {"battles", static_cast<std::uint64_t>(s.battles.size())}});
		// ⚠️★ **不能边遍历 s.conns 边关**:transport.Close() 会**同步**回调
		//    OnDisconnected,而它做的第一件事就是 s.conns.erase(it)
		//    ⇒ 迭代器当场失效。首次运行本用例即 SIGSEGV(2026-09-04)。
		//    ⇒ 先取快照,再逐个关。
		std::vector<SA::Net::ConnectionId> closing;
		closing.reserve(s.conns.size());
		for (const auto &kv : s.conns)
			closing.push_back(kv.first);
		for (const SA::Net::ConnectionId cid : closing)
		{
			const auto it = s.conns.find(cid);
			if (it != s.conns.end() && it->second.session != nullptr)
			{
				it->second.session->close();
			}
			s.transport.close(cid);
		}
		s.stopped = true;
	}
}

void World::loadSpawnPoints(std::vector<SpawnPoint> points)
{
	_impl->spawn_points = std::move(points);
}

void World::loadWarpPoints(std::vector<WarpPoint> points)
{
	_impl->warp_points = std::move(points);
}

void World::loadNpcEntities(std::vector<NpcEntity> npcs)
{
	for (auto &npc : npcs)
	{
		if (npc.born_x == 0 && npc.born_y == 0)
		{
			npc.born_x = npc.x;
			npc.born_y = npc.y;
		}
		if (npc.wander_interval_ms > 0 && npc.next_wander_at_ms == 0)
		{
			npc.next_wander_at_ms = _impl->now_ms + npc.wander_interval_ms;
		}
	}
	_impl->npc_entities = std::move(npcs);
}

void World::loadItemEffects(std::vector<ItemEffect> effects)
{
	_impl->item_effects = std::move(effects);
}

void World::loadPetSkillEffects(std::vector<PetSkillEffect> effects)
{
	_impl->pet_skill_effects = std::move(effects);
}

int World::giveItemToPlayer(SA::Net::SessionId session, const SA::Model::Item &item)
{
	Impl &s = *_impl;
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return -1; // 门 ①:无 L2 玩家实体
	// 门 ②③(背包空槽 / 池满)抽到 `giveItemIntoPlayer`,与掉落灌包共用(道具域第三批 I.3)。
	return giveItemIntoPlayer(*p, item, s.items);
}

// 批次 B2 的注入 seam(声明见 Api.h):三门全过才写 ⇒ 失败不留孤儿。
// ⚠️ 刻意**不写 `default_pet`** —— 它的唯一写者是换宠指令 PET_OUT(DR-BT21),
//   本 seam 在它旁边开第二扇门就会把"换宠语义在世界侧只有一处"打破。
int World::givePetToPlayer(SA::Net::SessionId session, const SA::Model::Pet &pet)
{
	Impl &s = *_impl;
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return -1; // 门 ①:无 L2 玩家实体
	const int pet_slot = p->findFreePetSlot();
	if (pet_slot < 0)
		return -1; // 门 ②:宠物槽满(悬空句柄算占用,两步分工见 Player.h)
	const SA::Model::EntityHandle handle = s.pets.allocate();
	if (!handle.valid())
		return -1; // 门 ③:宠物池满
	SA::Model::Pet *dst = s.pets.resolve(handle);
	if (dst == nullptr)
	{
		(void)s.pets.release(handle); // 走不到(刚 allocate 成功);守它零成本
		return -1;
	}
	*dst = pet; // 整只落池(含 pet_skills 七槽 —— 模拟"这只宠从模板带技")
	// 主人反向引用(捕获路径同款;句柄带 generation ⇒ 主人换人后旧引用作废,M10)。
	dst->owner = s.player_of_session.find(session);
	// ── 提交:挂进主人的槽(捕获路径 `:391 CHAR_setCharPet` 同位)──
	p->pets[static_cast<std::size_t>(pet_slot)] = handle;
	return pet_slot;
}

bool World::setPlayerStatsForTest(SA::Net::SessionId session, std::int32_t hp,
                                  std::int32_t mp, std::int32_t vital,
                                  std::int32_t str, std::int32_t tough,
                                  std::int32_t dex)
{
	SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return false;
	p->hp = hp;
	p->mp = mp;
	p->vital = vital;
	p->str = str;
	p->tough = tough;
	p->dex = dex;
	return true;
}

bool World::giveGoldToPlayerForTest(SA::Net::SessionId session, std::int32_t amount)
{
	SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr || amount <= 0)
		return false;
	const auto tx = addGold(*p, GoldReason::kBattleReward, amount, /*trans=*/0, 0, *_impl);
	return tx.disposition != GoldDisposition::kRejected;
}

SA::Model::Player *World::playerForTest(SA::Net::SessionId session) noexcept
{
	return _impl->players.resolve(_impl->player_of_session.find(session));
}

SA::Model::Pet *World::playerPetForTest(SA::Net::SessionId session, int pet_slot) noexcept
{
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return nullptr;
	auto *player = _impl->players.resolve(_impl->player_of_session.find(session));
	if (player == nullptr)
		return nullptr;
	return _impl->pets.resolve(player->pets[static_cast<std::size_t>(pet_slot)]);
}

FamilyInfo *World::familyForTest(std::uint32_t family_id) noexcept
{
	auto it = _impl->families.find(family_id);
	if (it == _impl->families.end())
		return nullptr;
	return &it->second;
}

void World::warpPlayerForTest(SA::Net::SessionId session, std::int32_t floor, std::int32_t x, std::int32_t y)
{
	_impl->warpPlayer(session, floor, x, y);
}

std::size_t World::worldEnemyCount() const noexcept { return _impl->world_enemies.size(); }

std::vector<WorldEnemyPos> World::worldEnemies() const
{
	Impl &s = *_impl;
	std::vector<WorldEnemyPos> out;
	out.reserve(s.world_enemies.size());
	for (const auto &we : s.world_enemies)
	{
		const SA::Model::Enemy *e = s.enemies.resolve(we.handle);
		if (e == nullptr)
			continue; // 悬空(世界态理论上不会;守零成本)
		WorldEnemyPos p{};
		p.entity_id = encodeHandle(we.handle);
		p.floor = e->floor;
		p.x = e->x;
		p.y = e->y;
		p.dir = e->dir;
		p.image = e->base_image;
		out.push_back(p);
	}
	return out;
}

std::size_t World::warpPointCount() const noexcept
{
	return _impl->warp_points.size();
}

std::size_t World::npcCount() const noexcept
{
	return _impl->npc_entities.size();
}

const NpcEntity *World::findNpc(std::uint64_t id) const noexcept
{
	for (const auto &npc : _impl->npc_entities)
	{
		if (npc.id == id)
			return &npc;
	}
	return nullptr;
}

bool World::playerHasActiveWindow(SA::Net::SessionId id) const noexcept
{
	const auto it = _impl->conns.find(id);
	return it != _impl->conns.end() && it->second.active_window_id != 0;
}

std::uint32_t World::playerActiveWindowId(SA::Net::SessionId id) const noexcept
{
	const auto it = _impl->conns.find(id);
	if (it != _impl->conns.end())
		return it->second.active_window_id;
	return 0;
}

std::string World::playerLastWindowText(SA::Net::SessionId id) const
{
	const auto it = _impl->conns.find(id);
	if (it != _impl->conns.end())
		return it->second.last_window_text;
	return {};
}

bool World::playerHasNowEvent(SA::Net::SessionId id, int flag) const noexcept
{
	const SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(id));
	if (p == nullptr)
		return false;
	return p->hasNowEvent(flag);
}

bool World::playerHasEndEvent(SA::Net::SessionId id, int flag) const noexcept
{
	const SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(id));
	if (p == nullptr)
		return false;
	return p->hasEndEvent(flag);
}

// ══ TransportEvents ═════════════════════════════════════════════
void World::onConnected(SA::Net::ConnectionId id)
{
	Impl &s = *_impl;
	Impl::Conn c;
	c.conn_id = id;
	// 1.5:SessionId == ConnectionId。⚠️ 阶段 2 加重连窗口时这条要断开 ——
	//    那正是 01 §5.2 把两者分开的理由。
	c.session = std::make_unique<SA::Net::Session>(
	    id, s.config.protocol_version, s.config.heartbeat_interval_ms, this);
	s.conns.emplace(id, std::move(c));

	s.logger.log(SA::Platform::LogLevel::kDebug,
	             SA::Platform::LogEvent::kConnectionAccepted, {{"conn_id", id}});
}

void World::onBytes(SA::Net::ConnectionId id, const std::uint8_t *data,
                    std::size_t n)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	if (it == s.conns.end())
		return;
	Impl::Conn &c = it->second;

	if (!c.reader.push(data, n))
	{
		s.logger.log(SA::Platform::LogLevel::kWarn,
		             SA::Platform::LogEvent::kFrameRejected,
		             {{"conn_id", id}, {"reason", std::string_view("buffer_limit")}});
		s.transport.close(id);
		return;
	}

	for (;;)
	{
		const std::uint8_t *payload = nullptr;
		std::uint32_t len = 0;
		const SA::Net::FrameStatus st = c.reader.next(&payload, &len);
		if (st == SA::Net::FrameStatus::kNeedMore)
			break;
		if (st != SA::Net::FrameStatus::kOk)
		{
			// ⚠️ kTooLarge / kEmpty 不可恢复:字节流已无法对齐(见 net/api.h)。
			s.logger.log(SA::Platform::LogLevel::kWarn,
			             SA::Platform::LogEvent::kFrameRejected,
			             {{"conn_id", id},
			              {"reason", std::string_view(
			                             st == SA::Net::FrameStatus::kTooLarge
			                                 ? "frame_too_large"
			                                 : "frame_empty")}});
			s.transport.close(id);
			return;
		}

		const bool ok = c.session->handleFrame(payload, len, c.outbound);
		c.reader.pop();
		if (!ok)
		{
			s.logger.log(SA::Platform::LogLevel::kWarn,
			             SA::Platform::LogEvent::kHandshakeRejected,
			             {{"conn_id", id},
			              {"msg_id", static_cast<std::uint64_t>(
			                             c.session->lastRejectMsgId())},
			              {"state", std::string_view(SA::Net::sessionStateName(
			                            c.session->state()))}});
			// ★ 先把已生成的出站字节发出去(可能含 HandshakeRejected),再关。
			if (!c.outbound.empty())
			{
				std::vector<std::uint8_t> bytes;
				bytes.swap(c.outbound);
				// send 失败可能同步释放 c；缓冲须由本次调用持有。
				(void)s.transport.send(id, bytes.data(), bytes.size());
			}
			s.transport.close(id);
			return;
		}
	}
}

void World::removeSession(SA::Net::ConnectionId id)
{
	_impl->notifyAddressBookStatus(id, false);
	_impl->pending_card_requests.erase(id);
	for (auto pit = _impl->pending_card_requests.begin(); pit != _impl->pending_card_requests.end();)
	{
		if (pit->second == id)
			pit = _impl->pending_card_requests.erase(pit);
		else
			++pit;
	}
	_impl->incoming_chats.erase(id);
	{
		auto fit = _impl->session_to_family.find(id);
		if (fit != _impl->session_to_family.end())
		{
			auto fam_it = _impl->families.find(fit->second);
			if (fam_it != _impl->families.end())
			{
				for (auto &m : fam_it->second.members)
				{
					if (m.session == id)
					{
						m.online = false;
						m.session = 0;
						break;
					}
				}
			}
			_impl->session_to_family.erase(fit);
		}
	}
	cancelTrade(id);
	leaveParty(id);
	closeStall(id);
	dismountPet(id);
	_impl->player_ride_permits.erase(id);
	_impl->player_ride_certs.erase(id);
	_impl->session_hometowns.erase(id);
	for (auto &kv : _impl->market_listings)
	{
		if (kv.second.seller_session == id)
		{
			kv.second.seller_session = 0;
		}
	}
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	if (it == s.conns.end())
		return;
	if (it->second.session != nullptr)
		it->second.session->close();

	// ── L2:释放该会话的 Player 实体及其宠物(批次 M.1)────────────────────
	//
	// ⚠️★★ **宠物必须一起释放**,否则 Pet 池只增不减:主人走了,它的宠物槽再没人看,
	//    而那些槽在池里仍然占用。★ 这不会有任何一处报错 —— 只会在跑够久之后表现为
	//    「捕获突然开始失败」(池满),而那时离真正的原因(这里没释放)已经很远。
	//    ⇒ 与 Player.h 里 `pets` 的注释是同一条的两半:那边说"释放 Pet 时要清槽",
	//      这边是唯一真正执行它的地方。
	const SA::Model::EntityHandle ph = s.player_of_session.find(id);
	detachBattles(id);

	if (SA::Model::Player *p = s.players.resolve(ph); p != nullptr)
	{
		// 里程碑②:先给视野内玩家发 CharDisappear + 从 olink 摘除(都要 p 的位置,须在释放前)。
		s.broadcastDespawn(id, p->floor, p->x, p->y);
		auto *fl = s.getFloor(p->floor);
		const auto &cur_map = fl ? fl->map : s.map;
		auto &cur_olink = fl ? fl->olink : s.olink;
		if (cur_map.inBounds(p->x, p->y))
		{
			const auto idx = cur_map.index(p->x, p->y);
			if (idx < cur_olink.size())
			{
				auto &cell = cur_olink[idx];
				cell.erase(std::remove(cell.begin(), cell.end(), id), cell.end());
			}
		}
		for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
		{
			if (!p->pets[i].valid())
				continue;
			// ★ release 对悬空句柄返回 false 且不做事(generation 校验)⇒ 无需先 resolve。
			(void)s.pets.release(p->pets[i]);
			(void)p->clearPetSlot(static_cast<int>(i));
		}
		for (std::size_t slot = 0; slot < p->items.size(); ++slot)
		{
			(void)s.items.release(p->items[slot]);
			(void)p->clearItemSlot(static_cast<int>(slot));
		}
		(void)s.players.release(ph);
	}
	s.player_of_session.erase(id);

	s.conns.erase(it);

	s.logger.log(SA::Platform::LogLevel::kDebug,
	             SA::Platform::LogEvent::kConnectionClosed, {{"conn_id", id}});
}

// ══ SessionHost ═════════════════════════════════════════════════
void World::onSessionReady(SA::Net::SessionId id)
{
	Impl &s = *_impl;
	s.logger.log(SA::Platform::LogLevel::kInfo,
	             SA::Platform::LogEvent::kHandshakeAccepted,
	             {{"session_id", id}});

	if (s.storage)
		return;

	// ── L2:会话就绪 ⇒ 该会话有了一个 Player 实体(批次 M.1)───────────────
	//
	// ⚠️★ **这是一处有意的临时形态,与 demo_battle 同族**:真玩法里 Player 实体是
	//    **选角**的产物(阶段 2,要 storage),握手只做认证。此处握手后就建,是为了让
	//    捕获的世界写有一个主人可挂 —— 而**不是**因为「握手 == 有角色」这句话是对的。
	//    ⇒ 阶段 2 接上选角时,这一段移到选角完成的回调里(与欠债 17 删 demo_battle 同期)。
	//
	// ★ 但它**不放在下面的 demo 分支里**:`demo_battle` 关掉时会话照样该有实体 ——
	//   「这条会话背后有个玩家」是会话事实,与要不要进 demo 战斗无关。
	if (!s.player_of_session.find(id).valid())
	{
		const SA::Model::EntityHandle ph = s.players.allocate();
		if (!ph.valid())
		{
			// ⚠️ 池满必须报出来,理由见 LogEvent::kEntityPoolExhausted。
			//   ★ 不 return:没有 L2 实体不该挡住会话本身(战斗事件流仍然能跑,
			//     只是捕获会在提交阶段失败并落 capture_commit_failed)。
			s.logger.log(SA::Platform::LogLevel::kError,
			             SA::Platform::LogEvent::kEntityPoolExhausted,
			             {{"session_id", id},
			              {"pool", std::string_view("player")},
			              {"capacity", static_cast<std::uint64_t>(kMaxPlayers)}});
		}
		else
		{
			s.player_of_session.insert(id, ph);
			// ── 出生点(批次 W.1)──────────────────────────────────────
			//   ⚠️★ 与名字同族的临时形态:真出生点来自存档 / 登录点(阶段 2)。
			//     1.5 没有 ⇒ 给 fixture 地图中心,让玩家有个能走的合法落点;
			//     ⇒ 阶段 2 接选角时由登录点坐标取代(与下面名字留空同期删/换)。
			if (SA::Model::Player *np = s.players.resolve(ph); np != nullptr)
			{
				if (!s.content_version.empty())
				{
					np->floor = s.character_defaults.player.floor;
					np->x = s.character_defaults.player.x;
					np->y = s.character_defaults.player.y;
					np->image = s.character_defaults.player.image;
					np->level = s.character_defaults.player.level;
					np->hp = s.character_defaults.player.hp;
					np->mp = s.character_defaults.player.mp;
					np->max_mp = s.character_defaults.player.max_mp;
				}
				else
				{
					np->floor = 0;
					np->x = s.map.width / 2;
					np->y = s.map.height / 2;
					if (np->hp <= 0)
						np->hp = 100;
					if (np->max_mp <= 0)
						np->max_mp = 100;
					if (np->mp <= 0)
						np->mp = 100;
				}
				if (np->name.empty())
				{
					np->name.assign(("Player_" + std::to_string(id)).c_str());
				}
				s.notifyAddressBookStatus(id, true);
				{
					auto fit = s.player_to_family_name.find(np->name.c_str());
					if (fit != s.player_to_family_name.end())
					{
						s.session_to_family[id] = fit->second;
						auto fam_it = s.families.find(fit->second);
						if (fam_it != s.families.end())
						{
							for (auto &m : fam_it->second.members)
							{
								if (m.charname == np->name.c_str())
								{
									m.session = id;
									m.online = true;
									m.level = np->level;
									m.graphicsno = np->image;
									break;
								}
							}
						}
					}
				}
				// 里程碑②:入 olink + 与视野内玩家双向 CharAppear(原版进图 sendCToArround)。
				auto *fl = s.getFloor(np->floor);
				const auto &cur_map = fl ? fl->map : s.map;
				auto &cur_olink = fl ? fl->olink : s.olink;
				if (cur_map.inBounds(np->x, np->y))
				{
					const auto idx = cur_map.index(np->x, np->y);
					if (idx < cur_olink.size())
						cur_olink[idx].push_back(id);
				}
				s.broadcastSpawn(id, *np);
				s.refreshEnemyView(id, *np, -1000, -1000);
				s.refreshNpcView(id, *np, -1000, -1000);
			}
			// ⚠️★ 名字**留空**:1.5 没有选角 ⇒ 没有名字的来源。
			//    ★ 不编一个 "player_1" 之类的占位 —— 那会让「名字是哪来的」看起来
			//      已经有答案了。11 §14 记的 DR-TS5 正是这么被撞出来的:定长 POD 强制
			//      回答「名字能多长」,而「名字从哪来」是同一族问题,同样该由裁定回答。
		}
	}

	// ── 1.4 demo 的入场装配(默认关,见 platform/api.h 的 DemoBattleConfig)──
	//
	// ⚠️★ 这是**脚手架**:真玩法里「握手完进哪里」是选角与登录点的结果(阶段 2,
	//    要 storage)。此处走捷径是为了让 1.4 有一条能被客户端走通的路径,
	//    而不是因为这条捷径是对的。⇒ 阶段 2 接上选角时整块删掉。
	if (!s.config.demo_battle.enabled)
		return;

	// ★ 每条会话开**自己的**一场,不共用:多会话共用一场就要回答
	//   "第二个人落在哪个槽""先来的打到一半后来的怎么进",那是组队/观战的玩法口径
	//   (阶段 2),不该由一段 demo 脚手架顺手定下来。
	const BattleId battle = startBattle(makeDemoField());
	s.battles.at(battle).demo = true;
	const std::uint8_t slot = s.config.demo_battle.slot;
	if (!joinBattle(battle, id, slot))
	{
		// ⚠️ 进不去要**报出来**。这条路径上 JoinBattle 的每一个 false 都意味着
		//    上面刚建的战斗成了没人看的孤儿,而客户端会停在"连上了但什么都没发生"
		//    —— 那正是 00 §10.4 说的静默错误。
		s.logger.log(SA::Platform::LogLevel::kError,
		             SA::Platform::LogEvent::kBattleJoinFailed,
		             {{"battle_id", battle},
		              {"session_id", id},
		              {"reason", std::string_view("demo_join_failed")}});
		return;
	}
	s.logger.log(SA::Platform::LogLevel::kDebug,
	             SA::Platform::LogEvent::kSessionStateChanged,
	             {{"session_id", id},
	              {"state", std::string_view("online")},
	              {"demo", true}});
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

void World::onEvent(SA::Net::SessionId id, const SA::Domain::EventRequest &req)
{
	// 移植 lssproto_EV_recv(callfromcli.c:1405)→ EVENT_main(event.c:37)净核:
	//   算面前格 → 扫该格事件对象 → 命中明雷则开战。★ 本批只接 ENTITY_ENEMY 一路
	//   (原版 functbl[event] 是通用派发,传送点 warppoint 等其他事件族为后续预留)。
	Impl &s = *_impl;
	if (s.partyModeOf(id) == PartyMode::kMember)
		return; // 队员不能自主触发事件与明雷开战 (移植 npc_npcenemy.c:358)
	bool ok = false;

	// 只处理明雷(ENTITY_ENEMY);其他 event_type ⇒ ok=false(未接的事件族,不报错、不断连)。
	if (req.event_type ==
	    static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY))
	{
		const auto it = s.conns.find(id); // 1.5:SessionId == ConnectionId
		SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
		if (it != s.conns.end() && p != nullptr && req.dir < 8)
		{
			// 面前格 = 玩家**权威**坐标朝 dir 前一格(不信 req.x/y,同 onWalk 防瞬移;原版
			//   callfromcli.c:1402 CHAR_getCoordinationDir(dir, CHAR_X, CHAR_Y, 1, &fx, &fy))。
			const std::int32_t fx = p->x + kDirDelta[req.dir].dx;
			const std::int32_t fy = p->y + kDirDelta[req.dir].dy;
			const std::size_t we = s.worldEnemyAt(p->floor, fx, fy);
			if (we != s.world_enemies.size())
				ok = triggerNpcEnemyBattle(id, we);
		}
	}
	else if (req.event_type ==
	         static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC))
	{
		const auto it = s.conns.find(id);
		SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
		if (it != s.conns.end() && p != nullptr && req.dir < 8)
		{
			const std::int32_t fx = p->x + kDirDelta[req.dir].dx;
			const std::int32_t fy = p->y + kDirDelta[req.dir].dy;
			const std::size_t ni = s.npcAt(p->floor, fx, fy);
			if (ni != s.npc_entities.size())
			{
				const NpcEntity &npc = s.npc_entities[ni];
				if (npc.type == NpcType::kHealer)
				{
					// 1. 检查并扣除费用 (唯一写入口 GoldLedger, DR-EC3 余额不足拒绝)
					bool can_pay = true;
					if (npc.cost > 0)
					{
						if (p->gold < npc.cost)
						{
							can_pay = false;
						}
						else
						{
							const GoldTx tx = delGold(*p, GoldReason::kHealerFee, npc.cost,
							                          /*trans=*/0, static_cast<std::uint64_t>(id), s);
							if (tx.disposition != GoldDisposition::kApplied)
								can_pay = false;
						}
					}

					// 2. 满状态恢复 (原版 NPC_HealerAllHeal, npc_healer.c:109-141)
					if (can_pay)
					{
						// 玩家自身满血满蓝
						const auto p_stats = SA::Rules::deriveBaseStats(p->vital, p->str,
						                                                p->tough, p->dex);
						p->hp = p_stats.max_hp > 0 ? p_stats.max_hp : std::max(p->hp, 1);
						p->mp = p->max_mp;

						// 随行宠物满血满蓝
						for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
						{
							if (p->pets[i].valid())
							{
								SA::Model::Pet *pet = s.pets.resolve(p->pets[i]);
								if (pet != nullptr)
								{
									const auto pet_stats = SA::Rules::deriveBaseStats(
									    pet->vital, pet->str, pet->tough, pet->dex);
									pet->hp = pet_stats.max_hp > 0 ? pet_stats.max_hp : std::max(pet->hp, 1);
									pet->mp = pet->max_mp;
								}
							}
						}
						ok = true;
					}
				}
				else if (npc.type == NpcType::kTownPeople)
				{
					// 城镇居民对话 (原版 npc_townpeople.c:28-52)
					// 1. 切分逗号分隔的文案候选
					std::vector<std::string> candidates;
					std::size_t start = 0;
					while (start < npc.message.size())
					{
						const std::size_t comma = npc.message.find(',', start);
						if (comma == std::string::npos)
						{
							candidates.push_back(npc.message.substr(start));
							break;
						}
						candidates.push_back(npc.message.substr(start, comma - start));
						start = comma + 1;
					}
					if (candidates.empty() && !npc.message.empty())
						candidates.push_back(npc.message);

					// 2. 选择文案(多条文案按 world_rng 随机摇选，对应原版 rand() % tokennum + 1)
					std::string chosen;
					if (!candidates.empty())
					{
						if (candidates.size() == 1)
						{
							chosen = candidates[0];
						}
						else
						{
							const std::size_t idx = static_cast<std::size_t>(
							    s.world_rng.randMod(static_cast<int>(candidates.size())));
							chosen = candidates[idx];
						}
					}

					// 3. 组装并下发 WindowOpen 消息 (kind = MESSAGE, buttons = OK)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_MESSAGE;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::MESSAGE;
					win.body.message.wide = false;

					// 按换行符切分为多行 (MessageBody.lines 最多 16 行，每行最大 255 字符)
					std::size_t lstart = 0;
					while (lstart < chosen.size() && win.body.message.lines.size() < 16)
					{
						const std::size_t nl = chosen.find('\n', lstart);
						std::string line = (nl == std::string::npos)
						                       ? chosen.substr(lstart)
						                       : chosen.substr(lstart, nl - lstart);
						if (line.size() > 255)
							line.resize(255);
						if (auto *slot = win.body.message.lines.push_back())
							slot->assign(line.data(), line.size());
						if (nl == std::string::npos)
							break;
						lstart = nl + 1;
					}
					if (win.body.message.lines.empty())
					{
						std::string line = chosen;
						if (line.size() > 255)
							line.resize(255);
						if (auto *slot = win.body.message.lines.push_back())
							slot->assign(line.data(), line.size());
					}

					// 记录窗口会话状态 (DR-PR3)
					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = chosen;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kExChangeMan)
				{
					// ExChangeMan 任务事件 NPC (原版 npc_exchangeman.c, 09 §4)
					int matched_block_idx = -1;
					int matched_branch_idx = 0;

					struct ExChangeEvalUserData
					{
						const World::Impl *impl = nullptr;
						const World *world = nullptr;
						SA::Net::SessionId session = 0;
					};
					ExChangeEvalUserData eval_ud{&s, this, id};

					auto count_item_cb = [](const SA::Model::Player &pl, std::int32_t item_id,
					                        void *userdata) -> std::int32_t
					{
						return static_cast<const ExChangeEvalUserData *>(userdata)->impl->countPlayerItems(
						    pl, item_id);
					};
					auto count_pet_cb = [](const SA::Model::Player &pl, std::int32_t pet_id,
					                       std::int32_t min_lvl, void *userdata) -> std::int32_t
					{
						return static_cast<const ExChangeEvalUserData *>(userdata)->impl->countPlayerPets(
						    pl, pet_id, min_lvl);
					};
					auto count_free_items_cb = [](const SA::Model::Player &pl,
					                              void *userdata) -> std::int32_t
					{
						return static_cast<const ExChangeEvalUserData *>(userdata)->impl->countFreeItemSlots(pl);
					};
					auto count_free_pets_cb = [](const SA::Model::Player &pl,
					                             void *userdata) -> std::int32_t
					{
						return static_cast<const ExChangeEvalUserData *>(userdata)->impl->countFreePetSlots(pl);
					};
					auto get_trans_cb = [](const SA::Model::Player &, void *userdata) -> std::int32_t
					{
						const auto *ud = static_cast<const ExChangeEvalUserData *>(userdata);
						const auto it = ud->impl->player_extra_stats.find(ud->session);
						return (it != ud->impl->player_extra_stats.end()) ? it->second.transmigration : 0;
					};
					auto get_fame_cb = [](const SA::Model::Player &, void *userdata) -> std::int32_t
					{
						const auto *ud = static_cast<const ExChangeEvalUserData *>(userdata);
						const auto it = ud->impl->player_extra_stats.find(ud->session);
						return (it != ud->impl->player_extra_stats.end()) ? it->second.fame : 0;
					};
					auto get_fm_cb = [](const SA::Model::Player &, void *userdata) -> std::uint32_t
					{
						const auto *ud = static_cast<const ExChangeEvalUserData *>(userdata);
						return ud->world->playerFamilyId(ud->session);
					};

					const EventCheckContext check_ctx{*p, count_item_cb, count_pet_cb,
					                                  count_free_items_cb, count_free_pets_cb,
					                                  get_trans_cb, get_fame_cb, get_fm_cb, &eval_ud};

					for (std::size_t bi = 0; bi < npc.exchange_blocks.size(); ++bi)
					{
						const auto &blk = npc.exchange_blocks[bi];
						// 前置门: 若 event_no != -1 且已完成, 跳过该块 (C21)
						if (blk.event_no != -1 && p->hasEndEvent(blk.event_no))
							continue;

						const int branch = evaluateEventCondition(blk.condition, check_ctx);
						if (branch > 0)
						{
							matched_block_idx = static_cast<int>(bi);
							matched_branch_idx = branch;
							break;
						}
					}

					if (matched_block_idx >= 0)
					{
						const auto &blk = npc.exchange_blocks[static_cast<std::size_t>(matched_block_idx)];
						std::string door_msg;
						if (!s.checkExChangePreconditions(*p, blk, matched_branch_idx, door_msg, id))
						{
							it->second.pending_exchange = {};
							s.sendExChangeWindow(id, npc.id, door_msg,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							ok = true;
						}
						else if (blk.type == ExChangeType::kMessage)
						{
							// 立即结算全部副作用 (石币/道具/宠物/旗标/经验/点数/血蓝/传送)
							s.applyExChangeEffects(id, *p, blk, matched_branch_idx);

							std::string msg = blk.nomal_window_msg;
							if (msg.empty())
								msg = blk.nomal_msg;
							if (msg.empty())
								msg = blk.thanks_msg;

							if (blk.next_block_index >= 0)
							{
								it->second.pending_exchange = {npc.id, matched_block_idx, matched_branch_idx};
							}
							else
							{
								it->second.pending_exchange = {};
							}
							s.sendExChangeWindow(id, npc.id, msg,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							ok = true;
						}
						else if (blk.type == ExChangeType::kAccept)
						{
							// 弹出接取/交付确认窗
							std::string msg = blk.accept_msg;
							if (msg.empty())
								msg = blk.nomal_window_msg;
							if (msg.empty())
								msg = blk.nomal_msg;

							const std::uint32_t buttons =
							    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
							    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
							s.sendExChangeWindow(id, npc.id, msg, buttons);
							it->second.pending_exchange = {npc.id, matched_block_idx, matched_branch_idx};
							ok = true;
						}
						else if (blk.type == ExChangeType::kRequest)
						{
							// 09 §4: 委托型。已在进行中走进度文案(OK)，未接取走接取文案(YES/NO)
							if (blk.event_no != -1 && p->hasNowEvent(blk.event_no))
							{
								std::string msg = blk.nomal_window_msg;
								if (msg.empty())
									msg = blk.nomal_msg;
								if (msg.empty())
									msg = "任务正在进行中，请加油！";

								it->second.pending_exchange = {};
								s.sendExChangeWindow(id, npc.id, msg,
								                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
								ok = true;
							}
							else
							{
								std::string msg = blk.request_msg;
								if (msg.empty())
									msg = blk.accept_msg;
								if (msg.empty())
									msg = blk.nomal_window_msg;
								if (msg.empty())
									msg = blk.nomal_msg;

								const std::uint32_t buttons =
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
								s.sendExChangeWindow(id, npc.id, msg, buttons);
								it->second.pending_exchange = {npc.id, matched_block_idx, matched_branch_idx};
								ok = true;
							}
						}
						else if (blk.type == ExChangeType::kClean)
						{
							// 09 §4: 清除旗标型
							std::string msg = blk.nomal_window_msg;
							if (msg.empty())
								msg = blk.nomal_msg;
							if (msg.empty())
								msg = "是否确定放弃任务并清除任务记录？";

							const std::uint32_t buttons =
							    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
							    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
							s.sendExChangeWindow(id, npc.id, msg, buttons);
							it->second.pending_exchange = {npc.id, matched_block_idx, matched_branch_idx};
							ok = true;
						}
					}
					else if (!npc.nomal_main_msg.empty())
					{
						// 全部块不满足 ⇒ 随机选择兜底对白
						std::vector<std::string> candidates;
						std::size_t start = 0;
						while (start < npc.nomal_main_msg.size())
						{
							const std::size_t comma = npc.nomal_main_msg.find(',', start);
							if (comma == std::string::npos)
							{
								candidates.push_back(npc.nomal_main_msg.substr(start));
								break;
							}
							candidates.push_back(npc.nomal_main_msg.substr(start, comma - start));
							start = comma + 1;
						}
						std::string chosen;
						if (candidates.size() == 1)
							chosen = candidates[0];
						else if (!candidates.empty())
						{
							const std::size_t idx = static_cast<std::size_t>(
							    s.world_rng.randMod(static_cast<int>(candidates.size())));
							chosen = candidates[idx];
						}
						else
							chosen = npc.nomal_main_msg;

						it->second.pending_exchange = {};
						s.sendExChangeWindow(id, npc.id, chosen,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						ok = true;
					}
				}
				else if (npc.type == NpcType::kShop)
				{
					// 商店 NPC 交互 (批次 W.12, 移植 npc_itemshop.c)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_ITEM_SHOP;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::SHOP;

					auto &shop = win.body.shop;
					shop.header.can_buy = true;
					shop.header.reuse_previous = false;

					std::string shop_name = npc.shop_name.empty() ? "道具商店" : npc.shop_name;
					if (shop_name.size() > 63)
						shop_name.resize(63);
					shop.header.shop_name.assign(shop_name.data(), shop_name.size());

					std::string msg = npc.main_msg.empty() ? "欢迎光临！请选择你要购买的道具。" : npc.main_msg;
					if (msg.size() > 255)
						msg.resize(255);
					shop.header.message.assign(msg.data(), msg.size());

					std::string full_msg = npc.item_full_msg.empty() ? "道具栏已满！" : npc.item_full_msg;
					if (full_msg.size() > 255)
						full_msg.resize(255);
					shop.header.item_full_message.assign(full_msg.data(), full_msg.size());

					// 填充在售道具列表 (最多 32 个)
					const std::size_t limit = std::min<std::size_t>(npc.shop_products.size(), 32);
					for (std::size_t i = 0; i < limit; ++i)
					{
						const auto &prod = npc.shop_products[i];
						if (auto *entry = shop.entries.push_back())
						{
							entry->entry_id = static_cast<std::uint32_t>(i + 1); // 1-based ID
							entry->item_id = static_cast<std::uint32_t>(prod.item_id);
							entry->image_id = prod.image_id;
							entry->level = prod.level;
							entry->price = std::max(1, static_cast<std::int32_t>(prod.cost * npc.buy_rate));
							entry->purchasable = (p->gold >= entry->price);
						}
					}

					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = msg;
					it->second.pending_shop.npc_id = npc.id;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kPetShop)
				{
					// 宠物商店 NPC 交互 (批次 W.13, 移植 npc_petshop.c)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_ITEM_SHOP;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::SHOP;

					auto &shop = win.body.shop;
					shop.header.can_buy = true;
					shop.header.reuse_previous = false;

					std::string shop_name = npc.shop_name.empty() ? "宠物商店" : npc.shop_name;
					if (shop_name.size() > 63)
						shop_name.resize(63);
					shop.header.shop_name.assign(shop_name.data(), shop_name.size());

					std::string msg = npc.main_msg.empty() ? "欢迎光临宠物商店！请挑选您心仪的宠物。" : npc.main_msg;
					if (msg.size() > 255)
						msg.resize(255);
					shop.header.message.assign(msg.data(), msg.size());

					std::string full_msg = npc.pet_full_msg.empty() ? "宠物栏已满！" : npc.pet_full_msg;
					if (full_msg.size() > 255)
						full_msg.resize(255);
					shop.header.item_full_message.assign(full_msg.data(), full_msg.size());

					// 填充在售宠物列表 (最多 32 个)
					const std::size_t limit = std::min<std::size_t>(npc.pet_products.size(), 32);
					for (std::size_t i = 0; i < limit; ++i)
					{
						const auto &prod = npc.pet_products[i];
						if (auto *entry = shop.entries.push_back())
						{
							entry->entry_id = static_cast<std::uint32_t>(i + 1); // 1-based ID
							entry->item_id = static_cast<std::uint32_t>(prod.pet_id);
							entry->image_id = static_cast<std::uint32_t>(prod.image);
							entry->level = static_cast<std::uint32_t>(prod.level);
							entry->price = std::max(1, static_cast<std::int32_t>(prod.cost * npc.buy_rate));
							entry->purchasable = (p->gold >= entry->price);
						}
					}

					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = msg;
					it->second.pending_pet_shop.npc_id = npc.id;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kPetSkillShop)
				{
					// 宠物技能导师 NPC 交互 (批次 W.13, 移植 npc_petskillshop.c)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_PET_SKILL_SHOP;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::SHOP;

					auto &shop = win.body.shop;
					shop.header.can_buy = true;
					shop.header.reuse_previous = false;

					std::string shop_name = npc.shop_name.empty() ? "宠物技能导师" : npc.shop_name;
					if (shop_name.size() > 63)
						shop_name.resize(63);
					shop.header.shop_name.assign(shop_name.data(), shop_name.size());

					std::string msg = npc.main_msg.empty() ? "你好！我可以传授你的宠物强大的技能。" : npc.main_msg;
					if (msg.size() > 255)
						msg.resize(255);
					shop.header.message.assign(msg.data(), msg.size());

					std::string full_msg = npc.skill_full_msg.empty() ? "宠物技能栏已满！" : npc.skill_full_msg;
					if (full_msg.size() > 255)
						full_msg.resize(255);
					shop.header.item_full_message.assign(full_msg.data(), full_msg.size());

					// 填充教授技能列表 (最多 32 个)
					const std::size_t limit = std::min<std::size_t>(npc.pet_skill_products.size(), 32);
					for (std::size_t i = 0; i < limit; ++i)
					{
						const auto &prod = npc.pet_skill_products[i];
						if (auto *entry = shop.entries.push_back())
						{
							entry->entry_id = static_cast<std::uint32_t>(i + 1); // 1-based ID
							entry->item_id = static_cast<std::uint32_t>(prod.skill_id);
							entry->image_id = 0;
							entry->level = static_cast<std::uint32_t>(prod.level);
							entry->price = std::max(1, static_cast<std::int32_t>(prod.cost * npc.buy_rate));
							entry->purchasable = (p->gold >= entry->price);
						}
					}

					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = msg;
					it->second.pending_pet_skill_shop.npc_id = npc.id;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kSignBoard)
				{
					// 告示牌 NPC 交互 (批次 W.14, 移植 npc_signboard.c)
					std::string title = npc.sign_title.empty() ? "＜　看板　＞\n" : (npc.sign_title + "\n");
					std::string body = npc.message.empty() ? npc.name : npc.message;
					std::string sign_text = title + body;
					s.sendExChangeWindow(id, npc.id, sign_text,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kWarpMan)
				{
					// 传送员 NPC 交互 (批次 W.14, 移植 npc_warpman.c)
					if (npc.warp_destinations.empty())
					{
						std::string msg = npc.warp_msg.empty() ? "暂无可以前往的目的地。" : npc.warp_msg;
						s.sendExChangeWindow(id, npc.id, msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						ok = true;
					}
					else if (npc.warp_destinations.size() == 1)
					{
						// 单目的地: 弹出确认窗口 (YES / NO)
						const auto &dest = npc.warp_destinations[0];
						std::string msg = npc.warp_msg;
						if (msg.empty())
						{
							msg = "确定要前往 " + dest.name + " 吗？需要花费 " + std::to_string(dest.cost) + " 石币。";
						}
						it->second.pending_warpman.npc_id = npc.id;
						it->second.pending_warpman.dest_idx = 0;
						s.sendExChangeWindow(id, npc.id, msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
						                         static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO));
						ok = true;
					}
					else
					{
						// 多目的地: 弹出 SELECT 窗口
						SA::Domain::WindowOpen win{};
						win.window_id = ++s.next_window_id;
						win.kind = SA::Domain::WindowKind::WINDOW_KIND_SELECT;
						win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
						win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
						win.source.entity_id = static_cast<std::uint32_t>(npc.id);
						win.body_kind = SA::Domain::WindowOpen::BodyKind::SELECT;

						std::string msg = npc.warp_msg.empty() ? "请选择你想前往的目的地：" : npc.warp_msg;
						if (msg.size() > 255)
							msg.resize(255);
						if (auto *slot = win.body.select.lines.push_back())
							slot->assign(msg.data(), msg.size());

						const std::size_t limit = std::min<std::size_t>(npc.warp_destinations.size(), 32);
						for (std::size_t i = 0; i < limit; ++i)
						{
							const auto &dest = npc.warp_destinations[i];
							if (auto *choice = win.body.select.choices.push_back())
							{
								choice->choice_id = static_cast<std::uint32_t>(i + 1); // 1-based choice_id
								std::string item_text = dest.name;
								if (dest.cost > 0)
								{
									item_text += " (" + std::to_string(dest.cost) + "石币)";
								}
								if (item_text.size() > 255)
									item_text.resize(255);
								choice->text.assign(item_text.data(), item_text.size());
								choice->enabled = (p->gold >= dest.cost && p->level >= dest.level);
							}
						}

						it->second.active_window_id = win.window_id;
						it->second.active_window_npc_id = npc.id;
						it->second.last_window_text = msg;
						it->second.pending_warpman.npc_id = npc.id;
						it->second.pending_warpman.dest_idx = -1;

						s.sendTo(id, win);
						ok = true;
					}
				}
				else if (npc.type == NpcType::kPetFusionMan)
				{
					// 宠物融合师 NPC 交互 (批次 §9.0.96)
					std::string msg = npc.message.empty() ? "欢迎来到宠物融合所！请选择要融合的主宠与副宠。" : npc.message;
					s.sendExChangeWindow(id, npc.id, msg,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kPetTransMan)
				{
					// 宠物转生师 NPC 交互 (批次 §9.0.96)
					std::string msg = npc.message.empty() ? "我是宠物转生师。只有达到 100 级以上的忠诚宠物才能进行转生仪式。" : npc.message;
					s.sendExChangeWindow(id, npc.id, msg,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kFameShop)
				{
					// 声望商城 / 荣誉兑换使者 NPC 交互 (批次 §9.0.98)
					std::string msg = npc.message.empty() ? "欢迎来到荣誉殿堂！可以使用声望兑换珍稀称号与宝物。" : npc.message;
					s.sendExChangeWindow(id, npc.id, msg,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kCraftsman)
				{
					// 工匠 / 料理大师 NPC 交互 (批次 §9.0.99)
					std::string msg = npc.message.empty() ? "欢迎来到工坊！在这里可以烹饪美味料理与合成精炼装备。" : npc.message;
					s.sendExChangeWindow(id, npc.id, msg,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kRideMaster)
				{
					// 骑乘导师 / 骑乘考官 NPC 交互 (批次 §9.0.100)
					std::string msg = npc.message.empty() ? "欢迎来到骑乘训练所！通过严苛的考核，你将获得驾驭凶猛骑宠的专属认证资格。" : npc.message;
					s.sendExChangeWindow(id, npc.id, msg,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
			}
		}
	}

	// 回执(原版 lssproto_EV_send(fd, seqno, rc)):seqno 原样带回,ok = 是否命中并开战。
	//   ★ 靠 seqno 关联(不依赖传输层 corr_id),同原版 EV 的 seqno 机制。
	SA::Domain::EventResult res{};
	res.seqno = req.seqno;
	res.ok = ok;
	s.sendTo(id, res);
}

void World::onWindowReply(SA::Net::SessionId id, const SA::Domain::WindowReply &reply)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return;

	// 校验 window_id 是否匹配当前会话开启的活动窗口 (DR-PR3 / DR-PR8)
	if (it->second.active_window_id != 0 && it->second.active_window_id == reply.window_id)
	{
		// 检查是否存在待决 ExChange 上下文 (批次 W.9, 阶段 2 扩展 §9.0.97)
		if (it->second.pending_exchange.npc_id != 0 &&
		    (reply.source.entity_id == 0 || it->second.pending_exchange.npc_id == reply.source.entity_id))
		{
			const auto pending = it->second.pending_exchange;
			it->second.pending_exchange = {};

			const NpcEntity *npc = findNpc(pending.npc_id);
			if (npc != nullptr && pending.block_index >= 0 &&
			    static_cast<std::size_t>(pending.block_index) < npc->exchange_blocks.size())
			{
				const auto &blk = npc->exchange_blocks[static_cast<std::size_t>(pending.block_index)];
				const bool is_yes = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES)) != 0 ||
				                    reply.button == static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);

				if (is_yes)
				{
					if (blk.type == ExChangeType::kAccept)
					{
						std::string door_msg;
						if (!s.checkExChangePreconditions(*p, blk, pending.branch_idx, door_msg, id))
						{
							s.sendExChangeWindow(id, npc->id, door_msg,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							return;
						}

						// 执行全部副作用 (石币/道具/宠物/旗标/经验/点数/血蓝/传送)
						s.applyExChangeEffects(id, *p, blk, pending.branch_idx);

						// 多步对话树跳转 (next_block_index)
						if (blk.next_block_index >= 0 &&
						    static_cast<std::size_t>(blk.next_block_index) < npc->exchange_blocks.size())
						{
							const auto &next_blk = npc->exchange_blocks[static_cast<std::size_t>(blk.next_block_index)];
							std::string next_msg = next_blk.nomal_window_msg;
							if (next_msg.empty())
								next_msg = next_blk.accept_msg;
							if (next_msg.empty())
								next_msg = next_blk.nomal_msg;

							if (next_blk.type == ExChangeType::kAccept)
							{
								const std::uint32_t buttons =
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
								s.sendExChangeWindow(id, npc->id, next_msg, buttons);
								it->second.pending_exchange = {npc->id, blk.next_block_index, 1};
							}
							else
							{
								s.applyExChangeEffects(id, *p, next_blk, 1);
								if (next_blk.next_block_index >= 0)
									it->second.pending_exchange = {npc->id, blk.next_block_index, 1};
								s.sendExChangeWindow(id, npc->id, next_msg,
								                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							}
							return;
						}

						std::string thanks = blk.thanks_msg;
						if (thanks.empty())
							thanks = blk.nomal_window_msg;

						if (!thanks.empty())
						{
							s.sendExChangeWindow(id, npc->id, thanks,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							return; // 保持活动新窗口
						}
					}
					else if (blk.type == ExChangeType::kRequest)
					{
						// 接取委托任务
						if (blk.event_no != -1)
							p->setNowEvent(blk.event_no);

						s.applyExChangeEffects(id, *p, blk, pending.branch_idx);

						if (blk.next_block_index >= 0 &&
						    static_cast<std::size_t>(blk.next_block_index) < npc->exchange_blocks.size())
						{
							const auto &next_blk = npc->exchange_blocks[static_cast<std::size_t>(blk.next_block_index)];
							std::string next_msg = next_blk.nomal_window_msg;
							if (next_msg.empty())
								next_msg = next_blk.nomal_msg;

							if (next_blk.next_block_index >= 0)
								it->second.pending_exchange = {npc->id, blk.next_block_index, 1};
							s.sendExChangeWindow(id, npc->id, next_msg,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							return;
						}

						std::string thanks = blk.thanks_msg;
						if (thanks.empty())
							thanks = "委托任务已接受，请前往完成！";
						s.sendExChangeWindow(id, npc->id, thanks,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}
					else if (blk.type == ExChangeType::kClean)
					{
						// 清除任务旗标
						if (blk.event_no != -1)
						{
							p->clearNowEvent(blk.event_no);
							p->clearEndEvent(blk.event_no);
						}
						s.applyExChangeEffects(id, *p, blk, pending.branch_idx);

						std::string thanks = blk.thanks_msg;
						if (thanks.empty())
							thanks = "任务记录已清除。";
						s.sendExChangeWindow(id, npc->id, thanks,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}
					else if (blk.type == ExChangeType::kMessage)
					{
						// 多步推进中的 kMessage
						if (blk.next_block_index >= 0 &&
						    static_cast<std::size_t>(blk.next_block_index) < npc->exchange_blocks.size())
						{
							const auto &next_blk = npc->exchange_blocks[static_cast<std::size_t>(blk.next_block_index)];
							std::string next_msg = next_blk.nomal_window_msg;
							if (next_msg.empty())
								next_msg = next_blk.accept_msg;
							if (next_msg.empty())
								next_msg = next_blk.nomal_msg;

							if (next_blk.type == ExChangeType::kAccept)
							{
								const std::uint32_t buttons =
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
								s.sendExChangeWindow(id, npc->id, next_msg, buttons);
								it->second.pending_exchange = {npc->id, blk.next_block_index, 1};
							}
							else
							{
								s.applyExChangeEffects(id, *p, next_blk, 1);
								if (next_blk.next_block_index >= 0)
									it->second.pending_exchange = {npc->id, blk.next_block_index, 1};
								s.sendExChangeWindow(id, npc->id, next_msg,
								                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							}
							return;
						}
					}
				}
			}
		}

		// 检查是否存在待决 Shop 商店交互 (批次 W.12)
		if (it->second.pending_shop.npc_id != 0 &&
		    it->second.pending_shop.npc_id == reply.source.entity_id)
		{
			const std::uint64_t shop_npc_id = it->second.pending_shop.npc_id;
			it->second.pending_shop = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel && reply.result_kind == SA::Domain::WindowReply::ResultKind::ENTRY_ID)
			{
				const NpcEntity *npc = findNpc(shop_npc_id);
				const std::uint32_t entry_id = reply.result.entry_id;
				if (npc != nullptr && entry_id >= 1 && entry_id <= npc->shop_products.size())
				{
					const auto &prod = npc->shop_products[entry_id - 1];
					const std::int32_t price = std::max(1, static_cast<std::int32_t>(prod.cost * npc->buy_rate));

					// 门 ①: 石币是否充足 (DR-EC3 余额不足拒绝)
					if (p->gold < price)
					{
						std::string less_msg = npc->stone_less_msg.empty() ? "石币不足！" : npc->stone_less_msg;
						s.sendExChangeWindow(id, npc->id, less_msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}

					// 门 ②: 背包是否有空槽
					if (p->findFreeItemSlot() < 0)
					{
						std::string full_msg = npc->item_full_msg.empty() ? "道具栏已满！" : npc->item_full_msg;
						s.sendExChangeWindow(id, npc->id, full_msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}

					// 门 ③: 道具池分配
					const auto h = s.items.allocate();
					if (h.valid())
					{
						auto *new_item = s.items.resolve(h);
						if (new_item != nullptr)
						{
							// 扣除石币 (走 GoldLedger, 汇 kShopBuy)
							(void)delGold(*p, GoldReason::kShopBuy, price,
							              /*trans=*/0, static_cast<std::uint64_t>(id), s);

							// 填充道具信息并入包
							new_item->uid = ++s.next_window_id;
							new_item->item_id = prod.item_id;
							new_item->name.assign(prod.name.c_str());
							new_item->cost = prod.cost;
							new_item->level = static_cast<std::int32_t>(prod.level);
							new_item->current_pile = 1;
							new_item->use_pile_nums = 1;

							const int slot = p->findFreeItemSlot();
							if (slot >= 0)
							{
								p->items[static_cast<std::size_t>(slot)] = h;
								s.sendExChangeWindow(id, npc->id, "购买成功！欢迎下次光临。",
								                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
								return;
							}
							else
							{
								s.items.release(h);
							}
						}
						else
						{
							s.items.release(h);
						}
					}
				}
			}
		}

		// 检查是否存在待决 PetShop 宠物商店交互 (批次 W.13)
		if (it->second.pending_pet_shop.npc_id != 0 &&
		    it->second.pending_pet_shop.npc_id == reply.source.entity_id)
		{
			const std::uint64_t shop_npc_id = it->second.pending_pet_shop.npc_id;
			it->second.pending_pet_shop = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel && reply.result_kind == SA::Domain::WindowReply::ResultKind::ENTRY_ID)
			{
				(void)buyPetFromShop(id, shop_npc_id, reply.result.entry_id);
				return;
			}
		}

		// 检查是否存在待决 PetSkillShop 技能导师交互 (批次 W.13)
		if (it->second.pending_pet_skill_shop.npc_id != 0 &&
		    it->second.pending_pet_skill_shop.npc_id == reply.source.entity_id)
		{
			const std::uint64_t shop_npc_id = it->second.pending_pet_skill_shop.npc_id;
			it->second.pending_pet_skill_shop = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel && reply.result_kind == SA::Domain::WindowReply::ResultKind::ENTRY_ID)
			{
				const NpcEntity *npc = findNpc(shop_npc_id);
				const std::uint32_t entry_id = reply.result.entry_id;
				if (npc != nullptr && entry_id >= 1 && entry_id <= npc->pet_skill_products.size())
				{
					const auto &prod = npc->pet_skill_products[entry_id - 1];
					int chosen_pet_slot = -1;
					for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
					{
						if (p->pets[i].valid())
						{
							chosen_pet_slot = static_cast<int>(i);
							break;
						}
					}
					if (chosen_pet_slot >= 0)
					{
						(void)learnPetSkill(id, shop_npc_id, chosen_pet_slot, prod.skill_id, /*skill_slot=*/-1);
						return;
					}
				}
			}
		}

		// 检查是否存在待决 WarpMan 传送员交互 (批次 W.14)
		if (it->second.pending_warpman.npc_id != 0 &&
		    it->second.pending_warpman.npc_id == reply.source.entity_id)
		{
			const auto pending = it->second.pending_warpman;
			it->second.pending_warpman = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel)
			{
				int target_idx = -1;
				if (pending.dest_idx >= 0)
				{
					// 单目的地 (Yes/No 确认弹窗)
					const bool is_yes = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES)) != 0 ||
					                    reply.button == static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
					if (is_yes)
					{
						target_idx = pending.dest_idx;
					}
				}
				else if (reply.result_kind == SA::Domain::WindowReply::ResultKind::CHOICE_ID)
				{
					// 多目的地 (SELECT 选项列表)
					if (reply.result.choice_id >= 1)
					{
						target_idx = static_cast<int>(reply.result.choice_id - 1);
					}
				}

				if (target_idx >= 0)
				{
					(void)warpPlayerByNpc(id, pending.npc_id, static_cast<std::size_t>(target_idx));
					return;
				}
			}
		}

		it->second.active_window_id = 0;
		it->second.active_window_npc_id = 0;
	}
}

bool World::sellItemToShop(SA::Net::SessionId id, std::uint64_t npc_id, int slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 槽位有效性与道具存在性检查
	if (slot < static_cast<int>(SA::Model::kStartItemArray) ||
	    slot >= static_cast<int>(SA::Model::kMaxItemHave))
		return false;

	const auto h = p->items[static_cast<std::size_t>(slot)];
	if (!h.valid())
		return false;

	auto *item = s.items.resolve(h);
	if (item == nullptr)
		return false;

	// 计算回购价格: 道具原价 * sell_rate (保底 1 石币)
	std::int32_t base_cost = item->cost;
	if (base_cost <= 0)
	{
		for (const auto &prod : npc->shop_products)
		{
			if (prod.item_id == item->item_id && prod.cost > 0)
			{
				base_cost = prod.cost;
				break;
			}
		}
	}
	if (base_cost <= 0)
		base_cost = 1;

	const std::int32_t unit_price = std::max(1, static_cast<std::int32_t>(base_cost * npc->sell_rate));
	const std::int32_t total_price = unit_price * std::max(1, item->current_pile);

	// 门: 随身石币上限检查 (DR-EC3 拒绝, 零改动)
	const std::int32_t cap = maxHaveGold(0);
	if (static_cast<std::int64_t>(p->gold) + total_price > cap)
	{
		std::string full_msg = npc->stone_full_msg.empty() ? "钱包装不下这么多石币！" : npc->stone_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 执行出售原子操作: 扣除并释放道具 + 增加石币 (走 GoldLedger, 源 kShopSell)
	p->clearItemSlot(slot);
	s.items.release(h);

	(void)addGold(*p, GoldReason::kShopSell, total_price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);
	return true;
}

bool World::buyPetFromShop(SA::Net::SessionId id, std::uint64_t npc_id, std::uint32_t entry_id)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kPetShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	if (entry_id < 1 || entry_id > npc->pet_products.size())
		return false;

	const auto &prod = npc->pet_products[entry_id - 1];
	const std::int32_t price = std::max(1, static_cast<std::int32_t>(prod.cost * npc->buy_rate));

	// 门 ①: 石币是否充足 (DR-EC3 余额不足拒绝)
	if (p->gold < price)
	{
		std::string less_msg = npc->stone_less_msg.empty() ? "石币不足！" : npc->stone_less_msg;
		s.sendExChangeWindow(id, npc->id, less_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ②: 宠物栏是否有空槽
	const int pet_slot = p->findFreePetSlot();
	if (pet_slot < 0)
	{
		std::string full_msg = npc->pet_full_msg.empty() ? "宠物栏已满！" : npc->pet_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ③: 宠物池分配
	const auto h = s.pets.allocate();
	if (!h.valid())
		return false;

	auto *dst = s.pets.resolve(h);
	if (dst == nullptr)
	{
		s.pets.release(h);
		return false;
	}

	// 扣除石币 (走 GoldLedger, 汇 kPetShopBuy)
	(void)delGold(*p, GoldReason::kPetShopBuy, price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);

	// 填充新宠物数据并落池
	dst->uid = ++s.next_window_id;
	dst->pet_id = prod.pet_id;
	dst->name.assign(prod.name.c_str());
	dst->level = prod.level;
	dst->hp = prod.hp;
	dst->mp = prod.mp;
	dst->max_mp = prod.mp;
	dst->vital = prod.vital;
	dst->str = prod.str;
	dst->tough = prod.tough;
	dst->dex = prod.dex;
	dst->origin_image = prod.image;
	dst->base_image = prod.image;
	dst->owner = s.player_of_session.find(id);
	dst->owner_char_name = p->name;

	p->pets[static_cast<std::size_t>(pet_slot)] = h;

	s.sendExChangeWindow(id, npc->id, "购买宠物成功！好好照顾它哦。",
	                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
	return true;
}

bool World::sellPetToShop(SA::Net::SessionId id, std::uint64_t npc_id, int pet_slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kPetShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 槽位有效性与宠物存在性检查
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return false;

	const auto h = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!h.valid())
		return false;

	auto *pet = s.pets.resolve(h);
	if (pet == nullptr)
		return false;

	// 计算回购价格: 宠物原价 * sell_rate (保底 1 石币)
	std::int32_t base_cost = 0;
	for (const auto &prod : npc->pet_products)
	{
		if (prod.pet_id == pet->pet_id && prod.cost > 0)
		{
			base_cost = prod.cost;
			break;
		}
	}
	if (base_cost <= 0)
	{
		base_cost = std::max(1, pet->level * 100);
	}

	const std::int32_t unit_price = std::max(1, static_cast<std::int32_t>(base_cost * npc->sell_rate));

	// 门: 随身石币上限检查 (DR-EC3 拒绝, 零改动)
	const std::int32_t cap = maxHaveGold(0);
	if (static_cast<std::int64_t>(p->gold) + unit_price > cap)
	{
		std::string full_msg = npc->stone_full_msg.empty() ? "钱包装不下这么多石币！" : npc->stone_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 执行出售原子操作: 扣除并释放宠物 + 增加石币 (走 GoldLedger, 源 kPetShopSell)
	if (playerRidePetSlot(id) == pet_slot)
	{
		dismountPet(id);
	}
	p->clearPetSlot(pet_slot);
	s.pets.release(h);

	(void)addGold(*p, GoldReason::kPetShopSell, unit_price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);
	return true;
}

bool World::learnPetSkill(SA::Net::SessionId id, std::uint64_t npc_id, int pet_slot,
                          std::int32_t skill_id, int skill_slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kPetSkillShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 槽位有效性检查
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return false;

	const auto h = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!h.valid())
		return false;

	auto *pet = s.pets.resolve(h);
	if (pet == nullptr)
		return false;

	// 查找导师技能条目
	const PetSkillProduct *target_prod = nullptr;
	for (const auto &prod : npc->pet_skill_products)
	{
		if (prod.skill_id == skill_id)
		{
			target_prod = &prod;
			break;
		}
	}
	if (target_prod == nullptr)
		return false;

	// 门 ①: 宠物等级是否达标 (移植 npc_petskillshop.c 门槛)
	if (pet->level < target_prod->level)
	{
		std::string low_msg = npc->level_low_msg.empty() ? "宠物等级不足以学习此技能！" : npc->level_low_msg;
		s.sendExChangeWindow(id, npc->id, low_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ②: 是否已习得该技能 (不可重复学同一技能)
	for (std::size_t i = 0; i < SA::Model::Pet::kPetSkillSlots; ++i)
	{
		if (pet->pet_skills[i] == skill_id)
			return false;
	}

	// 门 ③: 目标技能槽位解析
	int target_slot = -1;
	if (skill_slot >= 0 && static_cast<std::size_t>(skill_slot) < SA::Model::Pet::kPetSkillSlots)
	{
		target_slot = skill_slot;
	}
	else
	{
		for (std::size_t i = 0; i < SA::Model::Pet::kPetSkillSlots; ++i)
		{
			if (pet->pet_skills[i] <= 0) // 0 或 -1 表示空槽
			{
				target_slot = static_cast<int>(i);
				break;
			}
		}
	}
	if (target_slot < 0)
	{
		std::string full_msg = npc->skill_full_msg.empty() ? "宠物技能栏已满！" : npc->skill_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ④: 石币是否充足
	const std::int32_t price = std::max(1, static_cast<std::int32_t>(target_prod->cost * npc->buy_rate));
	if (p->gold < price)
	{
		std::string less_msg = npc->stone_less_msg.empty() ? "石币不足！" : npc->stone_less_msg;
		s.sendExChangeWindow(id, npc->id, less_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 执行扣除石币 (走 GoldLedger, 汇 kPetSkillFee)
	(void)delGold(*p, GoldReason::kPetSkillFee, price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);

	// 写入宠物技能槽
	pet->pet_skills[static_cast<std::size_t>(target_slot)] = skill_id;

	s.sendExChangeWindow(id, npc->id, "宠物成功学会了新技能！",
	                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
	return true;
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

void World::onSessionClosed(SA::Net::SessionId id)
{
	_impl->notifyAddressBookStatus(id, false);
	{
		auto fit = _impl->session_to_family.find(id);
		if (fit != _impl->session_to_family.end())
		{
			auto fam_it = _impl->families.find(fit->second);
			if (fam_it != _impl->families.end())
			{
				for (auto &m : fam_it->second.members)
				{
					if (m.session == id)
					{
						m.online = false;
						m.session = 0;
						break;
					}
				}
			}
			_impl->session_to_family.erase(fit);
		}
	}
	cancelTrade(id);
	leaveParty(id);
	closeStall(id);
	dismountPet(id);
	_impl->player_ride_permits.erase(id);
	_impl->player_ride_certs.erase(id);
	for (auto &kv : _impl->market_listings)
	{
		if (kv.second.seller_session == id)
		{
			kv.second.seller_session = 0;
		}
	}
	_impl->logger.log(SA::Platform::LogLevel::kDebug,
	                  SA::Platform::LogEvent::kSessionStateChanged,
	                  {{"session_id", id},
	                   {"state", std::string_view("closed")}});
}

// ══ 观察面 ═══════════════════════════════════════════════════════
void World::requestShutdown() noexcept { _impl->shutdown_requested = true; }

bool World::stopped() const noexcept { return _impl->stopped; }

std::uint64_t World::ticks() const noexcept { return _impl->ticks; }

std::size_t World::sessionCount() const noexcept
{
	return _impl->conns.size();
}

SA::Net::SessionState World::sessionState(SA::Net::SessionId id) const
{
	const auto it = _impl->conns.find(id);
	if (it == _impl->conns.end() || it->second.session == nullptr)
	{
		return SA::Net::SessionState::kClosed;
	}
	return it->second.session->state();
}

// ── L2 实体池的观察面(批次 M.1)─────────────────────────────────
std::size_t World::playerCount() const noexcept { return _impl->players.size(); }

std::size_t World::petCount() const noexcept { return _impl->pets.size(); }

// ── 敌人池的观察面(批次 M.4b)────────────────────────────────────
std::size_t World::enemyCount() const noexcept { return _impl->enemies.size(); }

// ── 道具池的观察面(批次 I.1)。⚠️ 本批恒 0(无写入者),见 Api.h 声明处。──
std::size_t World::itemCount() const noexcept { return _impl->items.size(); }

const SA::Model::Pet *World::playerPetAt(SA::Net::SessionId session,
                                         int pet_slot) const
{
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return nullptr;
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return nullptr;
	// ⚠️ 槽里的句柄可能悬空(宠物已回池)⇒ resolve 返 nullptr,与空槽同一个答案。
	return _impl->pets.resolve(p->pets[static_cast<std::size_t>(pet_slot)]);
}

int World::playerCaptureCount(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->capture_count);
}

int World::playerDefaultPet(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : p->default_pet;
}

int World::playerExp(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->exp);
}

int World::playerGold(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->gold);
}

int World::playerHp(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->hp);
}

int World::playerMp(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->mp);
}

World::PlayerPos World::playerPos(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return PlayerPos{}; // valid == false
	return PlayerPos{true, p->floor, p->x, p->y, p->dir};
}

int World::playerPetSlotsUsed(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return -1;
	int used = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
	{
		if (p->pets[i].valid())
			++used;
	}
	return used;
}

int World::playerItemSlotsUsed(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return -1;
	// ★ 只数背包段(装备位段不是"拿到的道具",两步分工同 findFreeItemSlot)。
	int used = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (p->items[i].valid())
			++used;
	}
	return used;
}

const SA::Model::Item *World::playerItemAt(SA::Net::SessionId session, int slot) const
{
	if (slot < 0 || static_cast<std::size_t>(slot) >= SA::Model::kMaxItemHave)
		return nullptr;
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return nullptr;
	// ⚠️ 槽里的句柄可能悬空(道具已回池)⇒ resolve 返 nullptr,与空槽同一个答案。
	return _impl->items.resolve(p->items[static_cast<std::size_t>(slot)]);
}

std::int32_t World::playerItemPile(SA::Net::SessionId session, int slot) const
{
	if (slot < 0 || static_cast<std::size_t>(slot) >= SA::Model::kMaxItemHave)
		return -1;
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return -1;
	const SA::Model::Item *it =
	    _impl->items.resolve(p->items[static_cast<std::size_t>(slot)]);
	return it == nullptr ? -1 : it->current_pile; // 空槽 / 悬空句柄 ⇒ -1
}

int World::playerLevel(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->level);
}

int World::playerSkillupPoints(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->skillup_points);
}

int World::playerVital(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->vital);
}

int World::playerStr(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->str);
}

int World::playerTough(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->tough);
}

int World::playerDex(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->dex);
}

SA::Rules::ProfessionClass World::playerProfessionClass(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? SA::Rules::ProfessionClass::kNone : p->profession_class;
}

int World::playerProfessionLevel(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	return p == nullptr ? -1 : static_cast<int>(p->profession_level);
}

bool World::setPlayerProfession(SA::Net::SessionId session, SA::Rules::ProfessionClass profession, int level)
{
	SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return false;
	p->profession_class = profession;
	p->profession_level = std::max(0, level);
	return true;
}

int World::playerEncounterRateFix(SA::Net::SessionId session) const
{
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return 0;
	if (_impl->now_ms > p->encounter_rate_expire_ms)
		return 0;
	return static_cast<int>(p->encounter_rate_fix);
}

bool World::castHunterEncounterSkill(SA::Net::SessionId session, bool is_track, int skill_level,
                                     int rate, std::int64_t duration_ms)
{
	Impl &s = *_impl;
	SA::Model::Player *player = s.players.resolve(s.player_of_session.find(session));
	if (player == nullptr || player->hp <= 0)
		return false;

	// 仅猎人允许施放猎人非战斗职技 (profession_skill.c:1332)
	if (player->profession_class != SA::Rules::ProfessionClass::kHunter)
		return false;

	const int fix = SA::Rules::computeHunterEncounterFix(skill_level, rate, is_track);
	player->encounter_rate_fix = fix;
	player->encounter_rate_expire_ms = s.now_ms + duration_ms;
	return true;
}

bool World::allocateStatPoint(SA::Net::SessionId session, int stat_index, int points)
{
	if (stat_index < 0 || stat_index > 3)
		return false;
	return allocateStatPoint(session, static_cast<SA::Rules::StatCategory>(stat_index), points);
}

bool World::allocateStatPoint(SA::Net::SessionId session, SA::Rules::StatCategory category, int points)
{
	if (points <= 0)
		return false;

	Impl &s = *_impl;
	if (s.inBattle(session))
		return false; // 战斗中禁止加点 (callfromcli.c:714)

	SA::Model::Player *player = s.players.resolve(s.player_of_session.find(session));
	if (player == nullptr)
		return false;

	if (player->hp <= 0)
		return false; // 阵亡状态禁止加点 (char.c:2553)

	if (player->skillup_points < points)
		return false; // 点数不足

	const auto old_equip = playerEquipModifiers(session);
	const auto old_stats = SA::Rules::deriveEquippedStats(player->vital, player->str, player->tough, player->dex, old_equip);

	const auto res = SA::Rules::applyStatAllocation(
	    player->vital, player->str, player->tough, player->dex,
	    player->skillup_points, category, points, player->profession_class);
	if (!res.success)
		return false;

	player->skillup_points = res.remaining_skillup_points;
	player->vital = res.new_vital;
	player->str = res.new_str;
	player->tough = res.new_tough;
	player->dex = res.new_dex;

	// 重算战斗三围并调整 HP (char.c:2648 CHAR_complianceParameter)
	const auto new_stats = SA::Rules::deriveEquippedStats(player->vital, player->str, player->tough, player->dex, old_equip);
	if (category == SA::Rules::StatCategory::kVital)
	{
		if (player->hp >= old_stats.max_hp)
			player->hp = new_stats.max_hp; // 原先满血时保持满血
		else
			player->hp = std::min(player->hp, new_stats.max_hp);
	}
	else
	{
		player->hp = std::min(player->hp, new_stats.max_hp);
	}
	player->hp = std::max(1, player->hp);

	return true;
}

int World::petLevel(SA::Net::SessionId session, int pet_slot) const
{
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return -1;
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return -1;
	const SA::Model::Pet *pet = _impl->pets.resolve(p->pets[static_cast<std::size_t>(pet_slot)]);
	return pet == nullptr ? -1 : static_cast<int>(pet->level);
}

bool World::equipItem(SA::Net::SessionId session, int inventory_slot, int target_slot)
{
	Impl &s = *_impl;
	if (s.inBattle(session))
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	if (inventory_slot < static_cast<int>(SA::Model::kStartItemArray) ||
	    static_cast<std::size_t>(inventory_slot) >= SA::Model::kMaxItemHave)
	{
		return false;
	}

	const auto inv_handle = p->items[static_cast<std::size_t>(inventory_slot)];
	const SA::Model::Item *it = s.items.resolve(inv_handle);
	if (it == nullptr)
		return false;

	if (target_slot < 0)
	{
		target_slot = SA::Rules::getEquipSlotForCategory(it->type);
		if (target_slot < 0)
			return false;
		// 若为首饰位 (4: Deco1)，且 Deco1 已穿戴而 Deco2 (5) 为空，则自动穿到 Deco2
		if (target_slot == 4 && p->items[4].valid() && !p->items[5].valid())
		{
			target_slot = 5;
		}
	}
	else
	{
		if (target_slot < 0 || target_slot >= static_cast<int>(SA::Model::kStartItemArray))
			return false;
		// 校验目标槽位是否与该道具类别相符
		const int expected_slot = SA::Rules::getEquipSlotForCategory(it->type);
		if (expected_slot < 0)
			return false;
		if (target_slot != expected_slot)
		{
			// 允许首饰在 4 (Deco1) 与 5 (Deco2) 互换
			if (!(expected_slot == 4 && target_slot == 5))
				return false;
		}
	}

	// 互换装备槽与背包槽
	std::swap(p->items[static_cast<std::size_t>(inventory_slot)],
	          p->items[static_cast<std::size_t>(target_slot)]);

	// 重新校验生命值与法力值上限 (char.c:3892 CHAR_complianceParameter)
	const auto eq_stats = SA::Rules::deriveEquippedStats(p->vital, p->str, p->tough, p->dex, playerEquipModifiers(session));
	p->hp = std::max(1, std::min(p->hp, eq_stats.max_hp));
	p->mp = std::max(0, std::min(p->mp, p->max_mp));
	return true;
}

bool World::unequipItem(SA::Net::SessionId session, int equip_slot, int target_inventory_slot)
{
	Impl &s = *_impl;
	if (s.inBattle(session))
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	if (equip_slot < 0 || static_cast<std::size_t>(equip_slot) >= SA::Model::kStartItemArray)
		return false;

	const auto eq_handle = p->items[static_cast<std::size_t>(equip_slot)];
	if (!eq_handle.valid() || s.items.resolve(eq_handle) == nullptr)
		return false;

	if (target_inventory_slot < 0)
	{
		// 自动寻找背包中的第一个空槽 [kStartItemArray, kMaxItemHave)
		for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
		{
			if (!p->items[i].valid() || s.items.resolve(p->items[i]) == nullptr)
			{
				target_inventory_slot = static_cast<int>(i);
				break;
			}
		}
		if (target_inventory_slot < 0)
			return false; // 背包已满
	}
	else
	{
		if (target_inventory_slot < static_cast<int>(SA::Model::kStartItemArray) ||
		    static_cast<std::size_t>(target_inventory_slot) >= SA::Model::kMaxItemHave)
		{
			return false;
		}
		// 若指定槽位非空，返回 false (脱装备必须脱到空格)
		if (p->items[static_cast<std::size_t>(target_inventory_slot)].valid() &&
		    s.items.resolve(p->items[static_cast<std::size_t>(target_inventory_slot)]) != nullptr)
		{
			return false;
		}
	}

	p->items[static_cast<std::size_t>(target_inventory_slot)] = eq_handle;
	p->items[static_cast<std::size_t>(equip_slot)] = SA::Model::kNullHandle;

	// 重新校验生命值与法力值上限 (char.c:3892 CHAR_complianceParameter)
	const auto eq_stats = SA::Rules::deriveEquippedStats(p->vital, p->str, p->tough, p->dex, playerEquipModifiers(session));
	p->hp = std::max(1, std::min(p->hp, eq_stats.max_hp));
	p->mp = std::max(0, std::min(p->mp, p->max_mp));
	return true;
}

SA::Rules::EquipModifiers World::playerEquipModifiers(SA::Net::SessionId session) const
{
	SA::Rules::EquipModifiers mods{};
	const SA::Model::Player *p =
	    _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return mods;

	for (std::size_t i = 0; i < SA::Model::kStartItemArray; ++i)
	{
		const SA::Model::Item *it = _impl->items.resolve(p->items[i]);
		if (it != nullptr)
		{
			mods.modify_attack += it->modify_attack;
			mods.modify_defense += it->modify_defense;
			mods.modify_quick += it->modify_quick;
			mods.modify_hp += it->modify_hp;
			mods.modify_mp += it->modify_mp;
		}
	}
	return mods;
}

// ── 遇敌:坐标 → 区域 → 编组(批次 M.6)──────────────────────────────────
SA::Domain::CharacterRecord World::Impl::snapshot(SA::Net::SessionId id) const
{
	SA::Domain::CharacterRecord record{};
	const auto found = conns.find(id);
	const auto *player = players.resolve(player_of_session.find(id));
	if (found == conns.end() || !player)
		return record;
	record.schema_ver = 1;
	record.char_id = found->second.char_id;
	record.revision = found->second.revision;
	SA::Domain::copyPlayerData(*player, record.player);
	for (std::size_t slot = 0; slot < player->pets.size(); ++slot)
		if (const auto *pet = pets.resolve(player->pets[slot]))
		{
			SA::Domain::PetSlot value{};
			value.uid = pet->uid;
			value.slot = static_cast<std::uint32_t>(slot);
			SA::Domain::copyPetData(*pet, value.value);
			(void)record.pets.push_back(value);
		}
	for (std::size_t slot = 0; slot < player->items.size(); ++slot)
		if (const auto *item = items.resolve(player->items[slot]))
		{
			SA::Domain::ItemSlot value{};
			value.uid = item->uid;
			value.slot = static_cast<std::uint32_t>(slot);
			SA::Domain::copyItemData(*item, value.value);
			(void)record.items.push_back(value);
		}
	return record;
}

bool World::Impl::install(SA::Net::SessionId id, const SA::Domain::CharacterRecord &record)
{
	const bool on_default_floor = (record.player.floor == character_defaults.player.floor);
	auto *fl = getFloor(record.player.floor);
	const auto &target_map = (on_default_floor || !fl) ? map : fl->map;
	const bool floor_valid = on_default_floor || (fl != nullptr);

	if (!SA::SessionStorage::validRecord(record) || !floor_valid ||
	    !mapWalkable(target_map, map_attr, record.player.x, record.player.y) || player_of_session.find(id).valid())
		return false;
	const auto handle = players.allocate();
	auto *player = players.resolve(handle);
	if (!player)
		return false;
	SA::Domain::copyPlayerData(record.player, *player);
	const auto rollback = [&]
	{
		for (auto value : player->pets)
			(void)pets.release(value);
		for (auto value : player->items)
			(void)items.release(value);
		(void)players.release(handle);
	};
	for (const auto &value : record.pets)
	{
		const auto allocated = pets.allocate();
		auto *pet = pets.resolve(allocated);
		if (!pet)
		{
			rollback();
			return false;
		}
		SA::Domain::copyPetData(value.value, *pet);
		pet->uid = value.uid;
		pet->owner = handle;
		player->pets[value.slot] = allocated;
	}
	for (const auto &value : record.items)
	{
		const auto allocated = items.allocate();
		auto *item = items.resolve(allocated);
		if (!item)
		{
			rollback();
			return false;
		}
		SA::Domain::copyItemData(value.value, *item);
		item->uid = value.uid;
		item->owner = handle;
		player->items[value.slot] = allocated;
	}
	player_of_session.insert(id, handle);
	auto &conn = conns.at(id);
	conn.char_id = record.char_id;
	conn.revision = record.revision;
	conn.session->markOnline();
	fl = getFloor(player->floor);
	const auto &cur_map = fl ? fl->map : map;
	auto &cur_olink = fl ? fl->olink : olink;
	if (cur_map.inBounds(player->x, player->y))
	{
		const auto idx = cur_map.index(player->x, player->y);
		if (idx < cur_olink.size())
			cur_olink[idx].push_back(id);
	}
	broadcastSpawn(id, *player);
	refreshEnemyView(id, *player, -1000, -1000);
	refreshNpcView(id, *player, -1000, -1000);
	return true;
}

void World::configurePlayable(GridMap map, TileAttrTable attributes, std::string version,
                              SA::Domain::CharacterRecord defaults)
{
	auto &s = *_impl;
	if (!s.conns.empty() || !s.world_enemies.empty() || map.width <= 0 || map.height <= 0 ||
	    map.width > 2048 || map.height > 2048 ||
	    map.tile.size() != static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height) ||
	    map.obj.size() != map.tile.size() || !mapWalkable(map, attributes, defaults.player.x, defaults.player.y) ||
	    version.empty() || version.size() > 63)
		throw std::invalid_argument("invalid playable content");
	s.map = std::move(map);
	s.map_attr = std::move(attributes);
	s.olink.assign(s.map.tile.size(), {});
	s.content_version = std::move(version);
	s.character_defaults = defaults;
}

void World::onLogin(SA::Net::SessionId id, const SA::Transport::LoginRequest &login, std::uint64_t corr)
{
	auto &s = *_impl;
	auto &conn = s.conns.at(id);
	SA::SessionStorage::Request request{};
	request.operation = SA::SessionStorage::Operation::kLogin;
	request.session = id;
	request.correlation = corr;
	request.login = login;
	if (s.storage && !s.shutdown_requested && s.now_ms >= conn.login_after && !conn.pending && s.storage->submit(std::move(request)))
	{
		conn.pending = true;
		conn.login_after = s.now_ms + 1000;
		return;
	}
	SA::Transport::LoginResult result{};
	result.code = SA::Transport::AccountCode::ACCOUNT_UNAVAILABLE;
	(void)SA::Net::encodeFramed(corr, result, conn.outbound);
	conn.session->awaitLogin();
}

void World::onCreateCharacter(SA::Net::SessionId id, const SA::Transport::CreateCharacterRequest &req, std::uint64_t corr)
{
	auto &s = *_impl;
	auto &conn = s.conns.at(id);

	if (conn.char_id != 0 || s.player_of_session.find(id).valid())
	{
		SA::Transport::CharacterResult result{};
		result.code = SA::Transport::AccountCode::ACCOUNT_CONFLICT;
		(void)SA::Net::encodeFramed(corr, result, conn.outbound);
		return;
	}

	const std::int64_t points = static_cast<std::int64_t>(req.vital) + req.str + req.tough + req.dex;
	const std::int64_t elements = static_cast<std::int64_t>(req.earth) + req.water + req.fire + req.wind;
	const bool valid_image = (req.image == s.character_defaults.player.image) || isValidPlayerImage(req.image);
	bool valid = !req.name.empty() && req.name.size() <= 31 && valid_image &&
	             SA::Data::Json::validUtf8(std::string_view(req.name.data, req.name.size()));
	for (std::size_t i = 0; i < req.name.size(); ++i)
		if (static_cast<unsigned char>(req.name.data[i]) < 0x20 || req.name.data[i] == 0x7f)
			valid = false;
	for (auto point : {req.vital, req.str, req.tough, req.dex})
		if (point < 0 || point > 20)
			valid = false;
	for (auto element : {req.earth, req.water, req.fire, req.wind})
		if (element < 0 || element > 10)
			valid = false;
	const int elem_cnt = (req.earth > 0) + (req.water > 0) + (req.fire > 0) + (req.wind > 0);
	valid = valid && points == 20 && elements == 10 && !(req.earth && req.fire) && !(req.water && req.wind) && elem_cnt <= 2;
	SA::SessionStorage::Request request{};
	request.operation = SA::SessionStorage::Operation::kCreate;
	request.session = id;
	request.correlation = corr;
	request.character = s.character_defaults;
	auto &player = request.character.player;
	player.name = req.name;
	if (valid)
	{
		player.image = req.image;
		player.face_image = computeFaceImage(req.image);
		player.vital = req.vital * 100;
		player.str = req.str * 100;
		player.tough = req.tough * 100;
		player.dex = req.dex * 100;
		player.earth = req.earth * 10;
		player.water = req.water * 10;
		player.fire = req.fire * 10;
		player.wind = req.wind * 10;
		const auto base_stats = SA::Rules::deriveBaseStats(player.vital, player.str, player.tough, player.dex);
		player.hp = base_stats.max_hp;
		player.mp = player.max_mp = 100;

		const auto it_ht = s.session_hometowns.find(id);
		if (it_ht != s.session_hometowns.end() && it_ht->second >= 0 &&
		    it_ht->second < static_cast<int>(s.hometown_spawns.size()))
		{
			const auto &sp = s.hometown_spawns[static_cast<std::size_t>(it_ht->second)];
			const bool is_def = (sp.floor == s.character_defaults.player.floor);
			auto *fl = s.getFloor(sp.floor);
			const auto &target_map = (is_def || !fl) ? s.map : fl->map;
			if ((is_def || fl != nullptr) && mapWalkable(target_map, s.map_attr, sp.x, sp.y))
			{
				player.floor = sp.floor;
				player.x = sp.x;
				player.y = sp.y;
			}
		}

		for (auto &pet : request.character.pets)
			pet.value.owner_char_name = player.name;
	}
	if (s.storage && conn.logged_in && !conn.pending && valid && s.storage->submit(std::move(request)))
	{
		conn.pending = true;
		return;
	}
	SA::Transport::CharacterResult result{};
	result.code = valid ? SA::Transport::AccountCode::ACCOUNT_UNAVAILABLE : SA::Transport::AccountCode::ACCOUNT_INVALID;
	(void)SA::Net::encodeFramed(corr, result, conn.outbound);
	conn.session->selectCharacter();
}

void World::onSelectCharacter(SA::Net::SessionId id, const SA::Transport::SelectCharacterRequest &req, std::uint64_t corr)
{
	auto &s = *_impl;
	auto &conn = s.conns.at(id);

	if (conn.char_id != 0 || s.player_of_session.find(id).valid())
	{
		SA::Transport::CharacterResult result{};
		result.code = SA::Transport::AccountCode::ACCOUNT_CONFLICT;
		(void)SA::Net::encodeFramed(corr, result, conn.outbound);
		return;
	}

	SA::SessionStorage::Request request{};
	request.operation = SA::SessionStorage::Operation::kSelect;
	request.session = id;
	request.correlation = corr;
	request.character.char_id = req.char_id;
	if (s.storage && conn.logged_in && !conn.pending && req.char_id && s.storage->submit(std::move(request)))
	{
		conn.pending = true;
		return;
	}
	SA::Transport::CharacterResult result{};
	result.code = SA::Transport::AccountCode::ACCOUNT_UNAVAILABLE;
	(void)SA::Net::encodeFramed(corr, result, conn.outbound);
	conn.session->selectCharacter();
}

void World::onSave(SA::Net::SessionId id, const SA::Transport::SaveRequest &req, std::uint64_t corr)
{
	auto &s = *_impl;
	auto &conn = s.conns.at(id);
	if (s.inBattle(id))
	{
		SA::Transport::SaveResult result{};
		result.code = SA::Transport::AccountCode::ACCOUNT_IN_BATTLE;
		result.logout = req.logout;
		(void)SA::Net::encodeFramed(corr, result, conn.outbound);
		return;
	}
	saveCharacter(id, req.logout, corr);
}

void World::saveCharacter(SA::Net::SessionId id, bool logout, std::uint64_t correlation)
{
	auto &s = *_impl;
	auto found = s.conns.find(id);
	if (!s.storage || found == s.conns.end() || !found->second.logged_in)
		return;
	auto &conn = found->second;
	if (conn.pending)
	{
		if (correlation || logout)
		{
			conn.deferred_correlation = correlation;
			conn.deferred_logout = logout;
		}
		return;
	}
	SA::SessionStorage::Request request{};
	request.operation = conn.char_id ? SA::SessionStorage::Operation::kSave : SA::SessionStorage::Operation::kRelease;
	request.session = id;
	request.correlation = correlation;
	request.logout = logout;
	if (conn.char_id)
	{
		request.character = s.snapshot(id);
		auto tit = s.player_titles.find(id);
		if (tit != s.player_titles.end())
			request.titles = tit->second.owned_titles;
		auto ait = s.address_books.find(id);
		if (ait != s.address_books.end())
		{
			for (std::size_t i = 0; i < ait->second.size(); ++i)
			{
				const auto &card = ait->second[i];
				if (!card.use && card.charname.empty())
					continue;
				SA::SessionStorage::AddressBookRecord rec{};
				rec.seq = static_cast<std::uint8_t>(i);
				rec.friend_char_id = 0;
				rec.friend_name = card.charname;
				rec.image = card.graphicsno;
				rec.level = card.level;
				request.address_book.push_back(std::move(rec));
			}
		}
	}
	conn.retry_at = s.now_ms + 1000;
	if (s.storage->submit(std::move(request)))
	{
		conn.pending = true;
		conn.save_failed = false;
		if (correlation || logout)
			conn.session->saving();
	}
	else
	{
		conn.save_failed = true;
		conn.session->saving();
		SA::Transport::SaveResult result{};
		result.code = SA::Transport::AccountCode::ACCOUNT_UNAVAILABLE;
		result.logout = logout;
		(void)SA::Net::encodeFramed(correlation, result, conn.outbound);
	}
}

void World::onDisconnected(SA::Net::ConnectionId id)
{
	auto &s = *_impl;
	auto found = s.conns.find(id);
	if (found == s.conns.end())
		return;
	if (!s.storage)
	{
		removeSession(id);
		return;
	}
	auto &conn = found->second;
	if (conn.detached)
		return;
	conn.detached = true;
	conn.walk_seq.clear();
	conn.session->close();
	detachBattles(id);
	if (conn.pending)
	{
		conn.deferred_logout = true;
		return;
	}
	if (conn.logged_in)
		saveCharacter(id, true, 0);
	else
		removeSession(id);
}

void World::processStorage()
{
	auto &s = *_impl;
	if (!s.storage)
		return;
	using Op = SA::SessionStorage::Operation;
	using Code = SA::Transport::AccountCode;
	for (auto &completion : s.storage->poll())
	{
		auto found = s.conns.find(completion.session);
		if (found == s.conns.end())
			continue;
		auto &conn = found->second;
		conn.pending = false;
		if (completion.operation == Op::kLeaseLost || completion.code == Code::ACCOUNT_LEASE_LOST)
		{
			SA::Transport::SaveResult result{};
			result.code = Code::ACCOUNT_LEASE_LOST;
			(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
			conn.logged_in = false;
			conn.session->close();
			if (conn.detached)
				removeSession(completion.session);
			continue;
		}
		if (completion.operation == Op::kLogin)
		{
			conn.logged_in = completion.code == Code::ACCOUNT_OK;
			if (conn.detached)
			{
				if (conn.logged_in)
					saveCharacter(completion.session, true, 0);
				else
					removeSession(completion.session);
				continue;
			}
			SA::Transport::LoginResult result{};
			result.code = completion.code;
			result.characters = completion.characters;
			(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
			if (conn.logged_in)
				conn.session->selectCharacter();
			else
				conn.session->awaitLogin();
		}
		else if (completion.operation == Op::kCreate || completion.operation == Op::kSelect)
		{
			if (conn.detached)
			{
				saveCharacter(completion.session, true, 0);
				continue;
			}
			SA::Transport::CharacterResult result{};
			result.code = completion.code;
			if (result.code == Code::ACCOUNT_OK)
			{
				if (s.install(completion.session, completion.character))
				{
					result.character = completion.character;
					if (!completion.titles.empty())
					{
						auto &pt = s.player_titles[completion.session];
						pt.owned_titles = completion.titles;
					}
					if (!completion.address_book.empty())
					{
						auto &ab = s.address_books[completion.session];
						ab.clear();
						for (const auto &rec : completion.address_book)
						{
							AddressBookEntry entry{};
							entry.use = true;
							entry.charname = rec.friend_name;
							entry.graphicsno = rec.image;
							entry.level = rec.level;
							ab.push_back(std::move(entry));
						}
					}
				}
				else
					result.code = Code::ACCOUNT_INVALID;
			}
			(void)result.content_version.assign(s.content_version.c_str());
			(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
			if (result.code != Code::ACCOUNT_OK)
			{
				conn.session->selectCharacter();
				if (completion.code == Code::ACCOUNT_OK)
					saveCharacter(completion.session, true, 0);
			}
		}
		else if (completion.operation == Op::kSave || completion.operation == Op::kRelease)
		{
			SA::Transport::SaveResult result{};
			result.code = completion.code;
			result.revision = completion.character.revision;
			result.logout = completion.logout;
			if (completion.code == Code::ACCOUNT_OK)
			{
				conn.revision = completion.character.revision;
				if (auto *player = s.players.resolve(s.player_of_session.find(completion.session)))
				{
					for (const auto &value : completion.character.pets)
						if (auto *pet = s.pets.resolve(player->pets[value.slot]))
							pet->uid = value.uid;
					for (const auto &value : completion.character.items)
						if (auto *item = s.items.resolve(player->items[value.slot]))
							item->uid = value.uid;
				}
				if (completion.logout || completion.operation == Op::kRelease)
				{
					conn.logged_in = false;
					if (conn.detached)
					{
						removeSession(completion.session);
						continue;
					}
					(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
					conn.session->close();
				}
				else
				{
					conn.session->markOnline();
					SA::Transport::CharacterState state{};
					state.character = s.snapshot(completion.session);
					(void)conn.session->push(state, conn.outbound);
					if (completion.correlation)
						(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
				}
			}
			else
			{
				conn.save_failed = true;
				conn.session->saving();
				(void)SA::Net::encodeFramed(completion.correlation, result, conn.outbound);
			}
			if (completion.code == Code::ACCOUNT_OK && conn.logged_in && (conn.detached || conn.deferred_logout || conn.deferred_correlation))
			{
				const auto corr = conn.deferred_correlation;
				const bool logout = conn.detached || conn.deferred_logout;
				conn.deferred_correlation = 0;
				conn.deferred_logout = false;
				saveCharacter(completion.session, logout, corr);
			}
		}
	}
	std::vector<SA::Net::SessionId> retry;
	for (const auto &entry : s.conns)
		if (entry.second.detached && entry.second.logged_in && !entry.second.pending && s.now_ms >= entry.second.retry_at)
			retry.push_back(entry.first);
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
	const auto &s = *_impl;
	const auto *fl = s.getFloor(floor_id);
	return fl ? &fl->map : nullptr;
}

std::size_t World::floorMapCount() const noexcept
{
	const auto &s = *_impl;
	return s.floors.size();
}

// ══ 角色名称与查询 ═════════════════════════════════════════════════════
std::string World::playerName(SA::Net::SessionId session) const
{
	const SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return {};
	return p->name.c_str();
}

bool World::setPlayerName(SA::Net::SessionId session, const std::string &name)
{
	if (name.empty() || name.size() >= SA::Model::kNameMaxBytes)
		return false;
	SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return false;
	const std::string old_name = p->name.c_str();
	p->name.assign(name.c_str());
	_impl->notifyAddressBookStatus(session, true);
	auto fit = _impl->player_to_family_name.find(old_name);
	if (fit != _impl->player_to_family_name.end())
	{
		const auto fid = fit->second;
		_impl->player_to_family_name.erase(fit);
		_impl->player_to_family_name[name] = fid;
		auto fam_it = _impl->families.find(fid);
		if (fam_it != _impl->families.end())
		{
			if (fam_it->second.leader_name == old_name)
				fam_it->second.leader_name = name;
			for (auto &m : fam_it->second.members)
			{
				if (m.charname == old_name)
				{
					m.charname = name;
					break;
				}
			}
		}
	}
	else
	{
		auto fit_new = _impl->player_to_family_name.find(name);
		if (fit_new != _impl->player_to_family_name.end())
		{
			_impl->session_to_family[session] = fit_new->second;
			auto fam_it = _impl->families.find(fit_new->second);
			if (fam_it != _impl->families.end())
			{
				for (auto &m : fam_it->second.members)
				{
					if (m.charname == name)
					{
						m.session = session;
						m.online = true;
						break;
					}
				}
			}
		}
	}
	return true;
}

} // namespace SA::World
