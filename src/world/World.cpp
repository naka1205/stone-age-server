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

namespace
{

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

	// ── 5. 角色循环 —— 玩家移动与状态推进(批次 W.1)──
	advanceMovement();

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
