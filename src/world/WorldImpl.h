// src/world/WorldImpl.h —— L2/L1 世界状态实现与各子系统共享上下文
//
// 依据 00 §3.1 模块边界约束，本头文件仅供 src/world/ 内部编译单元包含，
// 严禁泄露至 include/world/。

#ifndef __SA_WorldImpl_H__
#define __SA_WorldImpl_H__

#include "world/Api.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data/Json.h"
#include "model/Enemy.h"
#include "model/EntityIndex.h"
#include "model/EntityPool.h"
#include "model/Item.h"
#include "model/Pet.h"
#include "model/Player.h"
#include "rules/Battle.h"
#include "rules/CaptureItem.h"
#include "rules/Combatant.h"
#include "rules/PetSkill.h"
#include "rules/ProfessionSkill.h"
#include "rules/Progression.h"
#include "rules/Status.h"

namespace SA::World
{

inline constexpr std::size_t kMaxPlayers = 100;
inline constexpr std::size_t kMaxPets = 2000;
inline constexpr std::size_t kMaxEnemies = 10000;
inline constexpr std::size_t kMaxItems = 10000;

using PlayerPool = SA::Model::EntityPool<SA::Model::Player, kMaxPlayers>;
using PetPool = SA::Model::EntityPool<SA::Model::Pet, kMaxPets>;
using EnemyPool = SA::Model::EntityPool<SA::Model::Enemy, kMaxEnemies>;
using ItemPool = SA::Model::EntityPool<SA::Model::Item, kMaxItems>;

int giveItemIntoPlayer(SA::Model::Player &owner, const SA::Model::Item &item, ItemPool &items);
SA::Rules::BattleField makeDemoField();

inline int clampEnemyAction(std::uint32_t enemy_action)
{
	if (enemy_action > 100)
		return 100;
	if (enemy_action < 1)
		return 1;
	return static_cast<int>(enemy_action);
}

// 走路间隔:原版 CHAR_walk_check(char.c:4590)判 `time_diff_us >= walksendinterval*100`,
//   csa8.0 setup.cf `walkinterval=2500` ⇒ 2500 × 100us = 250ms 一格。
inline constexpr SA::Platform::Millis kWalkIntervalMs = 250;

// 方向 0-7 → 坐标增量。★ 照抄 CHAR_dxdy[8](char.c:2325):北起顺时针,含四斜向。
struct DirDelta
{
	std::int32_t dx;
	std::int32_t dy;
};
inline constexpr DirDelta kDirDelta[8] = {
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
inline bool decodeDirChar(char moji, std::uint8_t &dir, bool &is_turn)
{
	is_turn = !(moji >= 'a' && moji <= 'h');
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

bool walkStep(SA::Model::Player &p, const GridMap &map, const TileAttrTable &attr,
              std::uint8_t dir, bool is_turn);

struct BattleInstance;

struct WorldWriteContext
{
	BattleInstance *battle = nullptr;
	PlayerPool *players = nullptr;
	PetPool *pets = nullptr;
	const std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> *player_of_slot = nullptr;
	EnemyPool *enemies = nullptr;
	std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> *enemy_of_slot = nullptr;
	ItemPool *items = nullptr;
	SA::Platform::Logger *logger = nullptr;
	int actor = -1;
};

struct ChargeState
{
	std::int32_t beats = -1;
	std::int32_t percent = 0;
	std::int32_t skill_id = 0;
	std::uint32_t target = 0;
};

struct MagicStatusState
{
	std::int32_t status = 0;
	std::int32_t turns = 0;
	std::int32_t nums = 0;
};

constexpr std::int32_t kMagicSuperWall = 2;

struct BattleInstance
{
	BattleId id = 0;
	SA::Rules::BattleField field{};
	SA::Rules::TurnCommands commands{};
	SA::Rules::SeededRandom rng{1};
	SA::Domain::BattleEvents events{};
	std::uint64_t seed = 0;
	SA::Platform::Millis next_turn_at_ms = 0;
	BattleStats stats{};
	std::vector<SA::Net::SessionId> members{};
	std::vector<SA::Net::SessionId> spectators{};
	std::map<SA::Net::SessionId, std::uint8_t> slot_of{};
	std::map<SA::Net::SessionId, std::uint8_t> disconnected_slots{};

	std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> player_of_slot{};
	std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> enemy_of_slot{};
	std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> pet_of_slot{};
	std::array<SA::Model::EntityHandle, SA::Rules::kSlotCount> ride_pet_of_slot{};
	std::array<ChargeState, SA::Rules::kSlotCount> charge_of_slot{};
	std::array<MagicStatusState, SA::Rules::kSlotCount> magic_status_of_slot{};
	std::array<std::optional<int>, SA::Rules::kSlotCount> quick_to_restore{};
	std::array<bool, SA::Rules::kSlotCount> profit_settled{};
	std::array<std::int32_t, SA::Rules::kSlotCount> pending_exp{};
	std::array<std::int32_t, SA::Rules::kSlotCount> gained{};
	std::array<std::array<std::int32_t, 3>, SA::Rules::kBattlePlayerMax> getitem{};
	bool aborted = false;
	bool dp_battle = false;
	bool is_pvp = false;
	bool demo = false;
	SA::Platform::Millis started_sec = 0;
	SA::Platform::Millis command_deadline_sec = 0;

	BattleInstance()
	{
		for (auto &items : getitem)
			items.fill(-1);
	}
};

inline SA::Domain::BattleSnapshot makeBattleSnapshot(const SA::Rules::BattleField &field)
{
	SA::Domain::BattleSnapshot snapshot{};
	snapshot.battle_id = field.battle_id;
	snapshot.field_attribute = static_cast<std::uint32_t>(field.field_attribute);
	constexpr std::uint32_t status_flags[] = {0, 8, 16, 32, 64, 128, 256, 2048, 4096, 8192, 16384, 32768};
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		const auto &unit = field.at(slot);
		if (!unit.occupied)
			continue;
		auto *state = snapshot.combatants.push_back();
		state->slot = static_cast<std::uint32_t>(slot);
		state->level = static_cast<std::uint32_t>(unit.level);
		state->hp = std::max(0, unit.hp);
		state->max_hp = std::max(0, unit.max_hp);
		state->flags = (unit.isPlayer() ? 4u : 0u) | (unit.dead ? 2u : 0u);
		if (unit.status < sizeof(status_flags) / sizeof(status_flags[0]) && unit.status_turns > 0)
			state->flags |= status_flags[unit.status];
		if (unit.has_ride)
		{
			state->ride = SA::Domain::RideState::RIDE_STATE_RIDING;
			state->pet_hp = std::max(0, unit.ride_hp);
			state->pet_max_hp = std::max(0, unit.ride_max_hp);
		}
	}
	return snapshot;
}

inline void syncPetState(BattleInstance &b, PetPool &pets)
{
	for (int slot = 0; slot < SA::Rules::kSlotCount; ++slot)
	{
		if (auto *pet = pets.resolve(b.pet_of_slot[static_cast<std::size_t>(slot)]))
		{
			pet->hp = std::max(0, b.field.at(slot).hp);
			pet->mp = std::max(0, b.field.at(slot).mp);
		}
		if (auto *ride_pet = pets.resolve(b.ride_pet_of_slot[static_cast<std::size_t>(slot)]))
		{
			ride_pet->hp = std::max(0, b.field.at(slot).ride_hp);
		}
	}
}

inline SA::Rules::Combatant makePlayerCombatant(const SA::Model::Player *player = nullptr,
                                                const SA::Rules::EquipModifiers &equip = {},
                                                const SA::Model::Pet *ride_pet = nullptr)
{
	SA::Rules::Combatant c{};
	c.occupied = true;
	c.kind = SA::Rules::CombatantKind::kPlayer;
	c.slot = 0;
	if (player && (player->vital > 0 || player->str > 0 || player->tough > 0 || player->dex > 0))
	{
		c.level = player->level;
		c.charm = player->charm;
		c.luck = player->luck;
		c.vital = player->vital;
		c.str = player->str;
		c.tough = player->tough;
		c.dex = player->dex;
		const auto stats = SA::Rules::deriveEquippedStats(c.vital, c.str, c.tough, c.dex, equip);
		c.max_hp = stats.max_hp;
		c.hp = player->hp > 0 ? std::min(player->hp, stats.max_hp) : stats.max_hp;
		c.mp = player->mp > 0 ? player->mp : 100;
		c.max_mp = player->max_mp > 0 ? player->max_mp : 100;
		c.attack = stats.attack;
		c.defense = stats.defense;
		c.quick = stats.quick;
		c.fix_dex = stats.quick;
		c.dead = c.hp <= 0;
		c.elements[0] = player->earth;
		c.elements[1] = player->water;
		c.elements[2] = player->fire;
		c.elements[3] = player->wind;
	}
	else
	{
		c.level = player ? player->level : 20;
		c.mp = player && player->mp > 0 ? player->mp : 100;
		c.max_mp = player && player->max_mp > 0 ? player->max_mp : 100;
		c.luck = player ? player->luck : 10;
		c.charm = player ? player->charm : 0;
		const SA::Rules::DerivedStats st =
		    SA::Rules::deriveEquippedStats(8000, 30000, 4000, 20000, equip);
		c.vital = 8000;
		c.str = 30000;
		c.tough = 4000;
		c.dex = 20000;
		c.attack = st.attack;
		c.defense = st.defense;
		c.quick = st.quick;
		c.fix_dex = st.quick;
		c.max_hp = st.max_hp;
		c.hp = player && player->hp > 0 ? std::min(player->hp, st.max_hp) : st.max_hp;
		if (player)
		{
			c.elements[0] = player->earth;
			c.elements[1] = player->water;
			c.elements[2] = player->fire;
			c.elements[3] = player->wind;
		}
	}

	if (ride_pet && ride_pet->hp > 0)
	{
		c.has_ride = true;
		const auto rstats = SA::Rules::deriveBaseStats(ride_pet->vital, ride_pet->str, ride_pet->tough, ride_pet->dex);
		c.ride_max_hp = rstats.max_hp;
		c.ride_hp = std::min(ride_pet->hp, rstats.max_hp);
		c.ride_attack = rstats.attack;
		c.ride_defense = rstats.defense;
		c.ride_vital = ride_pet->vital;
		c.ride_str = ride_pet->str;
		c.ride_tough = ride_pet->tough;
		c.ride_dex = ride_pet->dex;
	}

	return c;
}

constexpr inline std::uint64_t encodeHandle(SA::Model::EntityHandle h) noexcept
{
	return (static_cast<std::uint64_t>(h.index) << 32) |
	       static_cast<std::uint64_t>(h.generation);
}

inline SA::Domain::CharAppear makeEnemyAppear(std::uint64_t eid, const SA::Model::Enemy &e)
{
	SA::Domain::CharAppear a{};
	a.entity_id = eid;
	a.floor = e.floor;
	a.x = e.x;
	a.y = e.y;
	a.dir = static_cast<std::uint32_t>(e.dir);
	a.entity_type = static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY);
	a.image = e.base_image;
	return a;
}

struct World::Impl : GoldAuditSink
{
	void onGoldTx(const GoldTx &tx) const override
	{
		logger.log(SA::Platform::LogLevel::kInfo, SA::Platform::LogEvent::kGoldChanged,
		           {{"corr", tx.correlation},
		            {"delta", static_cast<std::int64_t>(tx.delta)},
		            {"before", static_cast<std::int64_t>(tx.before)},
		            {"after", static_cast<std::int64_t>(tx.after)},
		            {"overflow", static_cast<std::int64_t>(tx.overflow)},
		            {"pre_clamped", static_cast<std::int64_t>(tx.pre_clamped)},
		            {"disposition", std::string_view(goldDispositionName(tx.disposition))},
		            {"reason", std::string_view(goldReasonName(tx.reason))}});
	}

	struct Conn
	{
		SA::Net::ConnectionId conn_id = 0;
		SA::Net::FrameReader reader{};
		std::unique_ptr<SA::Net::Session> session;
		std::vector<std::uint8_t> outbound{};

		std::string walk_seq{};
		SA::Platform::Millis next_walk_at_ms = 0;

		std::int32_t cep = 0;
		bool logged_in = false;
		bool pending = false;
		bool detached = false;
		bool save_failed = false;
		bool deferred_logout = false;
		std::uint64_t deferred_correlation = 0;
		std::uint64_t char_id = 0;
		std::uint64_t revision = 0;
		SA::Platform::Millis retry_at = 0;
		SA::Platform::Millis login_after = 0;

		std::uint32_t active_window_id = 0;
		std::uint64_t active_window_npc_id = 0;
		std::string last_window_text{};

		struct PendingExChange
		{
			std::uint64_t npc_id = 0;
			int block_index = -1;
			int branch_idx = 0;
		};
		PendingExChange pending_exchange{};

		struct PendingShop
		{
			std::uint64_t npc_id = 0;
		};
		PendingShop pending_shop{};

		struct PendingPetShop
		{
			std::uint64_t npc_id = 0;
		};
		PendingPetShop pending_pet_shop{};

		struct PendingPetSkillShop
		{
			std::uint64_t npc_id = 0;
		};
		PendingPetSkillShop pending_pet_skill_shop{};

		struct PendingWarpMan
		{
			std::uint64_t npc_id = 0;
			int dest_idx = -1;
		};
		PendingWarpMan pending_warpman{};
	};

	Impl(const SA::Platform::ServerConfig &cfg, SA::Platform::Clock &clk,
	     SA::Platform::Logger &log, SA::Platform::RandomSource &rnd,
	     SA::Net::Transport &tp)
	    : config(cfg), clock(clk), logger(log), random(rnd), transport(tp),
	      world_rng(rnd.masterSeed() ^ 0x9E3779B97F4A7C15ull)
	{
		olink.assign(static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height),
		             {});
	}

	SA::Platform::ServerConfig config;
	SA::Platform::Clock &clock;
	SA::Platform::Logger &logger;
	SA::Platform::RandomSource &random;
	SA::Net::Transport &transport;
	SA::SessionStorage::Service *storage = nullptr;
	std::string content_version;
	SA::Domain::CharacterRecord character_defaults{};
	std::array<HometownSpawn, 4> hometown_spawns = {{
	    {1000, 98, 93}, // 0: 玛丽娜斯村 (Marina)
	    {2000, 85, 80}, // 1: 萨姆吉尔村 (Shamgir)
	    {3000, 85, 75}, // 2: 加加村 (Jaja)
	    {4000, 60, 60}, // 3: 卡鲁它那村 (Karutana)
	}};
	std::unordered_map<SA::Net::SessionId, int> session_hometowns;
	SA::Domain::CharacterRecord snapshot(SA::Net::SessionId id) const;
	bool install(SA::Net::SessionId id, const SA::Domain::CharacterRecord &record);

	SA::Rules::SeededRandom world_rng;
	std::uint32_t next_window_id = 0;

	std::map<SA::Net::ConnectionId, Conn> conns;
	std::map<BattleId, BattleInstance> battles;
	struct FinishedBattle
	{
		BattleStats stats;
		SA::Rules::BattleField field;
		bool is_pvp = false;
	};
	static constexpr std::size_t kFinishedBattleLimit = 128;
	std::map<BattleId, FinishedBattle> finished_battles;

	bool isSpectating(SA::Net::SessionId sid) const
	{
		for (const auto &entry : battles)
		{
			const auto &battle = entry.second;
			if (!battle.stats.finished &&
			    std::find(battle.spectators.begin(), battle.spectators.end(), sid) != battle.spectators.end())
				return true;
		}
		return false;
	}

	bool inBattle(SA::Net::SessionId sid) const
	{
		for (const auto &entry : battles)
		{
			const auto &battle = entry.second;
			const auto slot = battle.slot_of.find(sid);
			if (!battle.stats.finished && slot != battle.slot_of.end() &&
			    battle.field.at(slot->second).occupied)
				return true;
			if (!battle.stats.finished &&
			    std::find(battle.spectators.begin(), battle.spectators.end(), sid) != battle.spectators.end())
				return true;
		}
		return false;
	}

	void retireBattle(BattleId id)
	{
		const auto it = battles.find(id);
		if (it == battles.end())
			return;
		auto &battle = it->second;
		syncPetState(battle, pets);
		for (auto handle : battle.enemy_of_slot)
			(void)enemies.release(handle);
		battle.stats.finished = true;
		finished_battles.emplace(id, FinishedBattle{battle.stats, battle.field, battle.is_pvp});
		while (finished_battles.size() > kFinishedBattleLimit)
			finished_battles.erase(finished_battles.begin());
		battles.erase(it);
	}
	void pushBattleSnapshot(const BattleInstance &battle)
	{
		auto snapshot = makeBattleSnapshot(battle.field);
		for (auto &unit : snapshot.combatants)
		{
			if (const auto *player = players.resolve(battle.player_of_slot[unit.slot]); player && storage)
			{
				unit.name = player->name;
				unit.image_id = static_cast<std::uint32_t>(player->image);
			}
			else if (const auto *pet = pets.resolve(battle.pet_of_slot[unit.slot]))
			{
				unit.name = pet->name;
				unit.image_id = static_cast<std::uint32_t>(pet->base_image);
			}
			else if (const auto *enemy = enemies.resolve(battle.enemy_of_slot[unit.slot]))
			{
				unit.name = enemy->name;
				unit.image_id = static_cast<std::uint32_t>(enemy->base_image);
			}
		}
		for (auto sid : battle.members)
			if (auto conn = conns.find(sid); conn != conns.end() && conn->second.session != nullptr)
				if (!conn->second.session->push(snapshot, conn->second.outbound))
					conn->second.session->close();
		for (auto sid : battle.spectators)
			if (auto conn = conns.find(sid); conn != conns.end() && conn->second.session != nullptr)
				if (!conn->second.session->push(snapshot, conn->second.outbound))
					conn->second.session->close();
	}

	SA::Rules::RulesConfig rules_config{};
	BattleId next_battle_id = 1;
	std::uint64_t ticks = 0;
	SA::Platform::Millis now_ms = 0;
	bool shutdown_requested = false;
	bool stopped = false;

	PlayerPool players{};
	PetPool pets{};
	EnemyPool enemies{};
	ItemPool items{};

	SA::Model::ConnIndex player_of_session{};

	GridMap map = makeFixtureMap(64, 64);
	TileAttrTable map_attr = makeFixtureAttr();

	struct FloorState
	{
		std::int32_t floor_id = 0;
		GridMap map{};
		std::vector<std::vector<SA::Net::ConnectionId>> olink{};
	};
	std::unordered_map<std::int32_t, FloorState> floors{};

	FloorState *getFloor(std::int32_t floor) noexcept
	{
		auto it = floors.find(floor);
		if (it != floors.end())
			return &it->second;
		return nullptr;
	}
	const FloorState *getFloor(std::int32_t floor) const noexcept
	{
		auto it = floors.find(floor);
		if (it != floors.end())
			return &it->second;
		return nullptr;
	}

	std::vector<EncountArea> encount_areas{};
	std::vector<EnemyGroup> enemy_groups{};
	std::vector<EnemyEncounter> encounters{};
	std::vector<EnemyTemplate> enemy_templates{};

	std::vector<ItemEffect> item_effects{};
	std::vector<PetSkillEffect> pet_skill_effects{};

	std::vector<WarpPoint> warp_points{};
	const WarpPoint *findWarpPoint(std::int32_t floor, std::int32_t x,
	                               std::int32_t y) const noexcept
	{
		for (const auto &wp : warp_points)
		{
			if (wp.src_floor == floor && wp.src_x == x && wp.src_y == y)
				return &wp;
		}
		return nullptr;
	}

	std::vector<NpcEntity> npc_entities{};
	std::size_t npcAt(std::int32_t floor, std::int32_t x,
	                  std::int32_t y) const noexcept
	{
		for (std::size_t i = 0; i < npc_entities.size(); ++i)
		{
			if (npc_entities[i].floor == floor && npc_entities[i].x == x &&
			    npc_entities[i].y == y)
				return i;
		}
		return npc_entities.size();
	}
	void refreshNpcView(SA::Net::ConnectionId viewer, const SA::Model::Player &p,
	                    std::int32_t ox, std::int32_t oy);

	std::size_t npc_charloop_cursor = 0;
	void wanderNpcs(std::size_t max_this_tick);
	void broadcastNpcMove(const NpcEntity &npc, std::int32_t ox, std::int32_t oy);
	bool isNpcEngagedInDialog(std::uint64_t npc_id) const;

	std::vector<SpawnPoint> spawn_points{};
	struct WorldEnemy
	{
		SA::Model::EntityHandle handle;
		std::size_t spawn_point;
	};
	std::vector<WorldEnemy> world_enemies{};

	std::size_t charloop_cursor = 0;
	std::vector<std::vector<SA::Net::ConnectionId>> olink;

	std::vector<SA::Net::ConnectionId> collectVisible(std::int32_t floor, std::int32_t cx, std::int32_t cy,
	                                                  SA::Net::ConnectionId self) const;
	template <typename M>
	void sendTo(SA::Net::ConnectionId to, const M &msg)
	{
		const auto it = conns.find(to);
		if (it == conns.end() || it->second.session == nullptr)
			return;
		(void)it->second.session->push(msg, it->second.outbound);
	}
	void appearBetween(SA::Net::ConnectionId a, const SA::Model::Player &pa,
	                   SA::Net::ConnectionId b);
	void broadcastMove(SA::Net::ConnectionId mover, std::int32_t ox, std::int32_t oy,
	                   const SA::Model::Player &p);
	void broadcastSpawn(SA::Net::ConnectionId who, const SA::Model::Player &p);
	void broadcastDespawn(SA::Net::ConnectionId who, std::int32_t floor, std::int32_t x, std::int32_t y);

	std::size_t worldEnemyAt(std::int32_t floor, std::int32_t x, std::int32_t y);
	void spawnWorldEnemies();
	void wanderWorldEnemies(std::size_t max_this_tick);
	std::vector<SA::Net::ConnectionId> collectVisiblePlayers(std::int32_t floor, std::int32_t cx,
	                                                         std::int32_t cy) const;
	void broadcastEnemySpawn(const SA::Model::Enemy &e, std::uint64_t eid);
	void broadcastEnemyMove(const SA::Model::Enemy &e, std::uint64_t eid, std::int32_t ox,
	                        std::int32_t oy);
	void broadcastEnemyDespawn(std::int32_t floor, std::int32_t x, std::int32_t y, std::uint64_t eid);
	void refreshEnemyView(SA::Net::ConnectionId viewer, const SA::Model::Player &p,
	                      std::int32_t ox, std::int32_t oy);

	std::int32_t countPlayerItems(const SA::Model::Player &p, std::int32_t item_id) const;
	std::int32_t countPlayerPets(const SA::Model::Player &p, std::int32_t pet_id, std::int32_t min_lvl) const;
	std::int32_t countFreeItemSlots(const SA::Model::Player &p) const;
	std::int32_t countFreePetSlots(const SA::Model::Player &p) const;
	void sendExChangeWindow(SA::Net::SessionId id, std::uint64_t npc_id,
	                        const std::string &raw_text, std::uint32_t buttons);
	bool checkExChangePreconditions(const SA::Model::Player &p, const ExChangeBlock &blk, int branch_idx, std::string &msg_out, SA::Net::SessionId session_id = 0);
	void applyExChangeEffects(SA::Net::SessionId id, SA::Model::Player &p, const ExChangeBlock &blk, int branch_idx);

	struct Party
	{
		std::uint64_t party_id = 0;
		SA::Net::SessionId leader = 0;
		std::vector<SA::Net::SessionId> members{};
	};
	std::unordered_map<std::uint64_t, Party> parties{};
	std::unordered_map<SA::Net::SessionId, std::uint64_t> party_of_session{};
	std::uint64_t next_party_id = 1;

	PartyMode partyModeOf(SA::Net::SessionId session) const noexcept
	{
		auto it = party_of_session.find(session);
		if (it == party_of_session.end())
			return PartyMode::kNone;
		auto pit = parties.find(it->second);
		if (pit == parties.end())
			return PartyMode::kNone;
		return (pit->second.leader == session) ? PartyMode::kLeader : PartyMode::kMember;
	}

	SA::Net::SessionId partyLeaderOf(SA::Net::SessionId session) const noexcept
	{
		auto it = party_of_session.find(session);
		if (it == party_of_session.end())
			return 0;
		auto pit = parties.find(it->second);
		if (pit == parties.end())
			return 0;
		return pit->second.leader;
	}

	std::vector<SA::Net::SessionId> partyMembersOf(SA::Net::SessionId session) const
	{
		auto it = party_of_session.find(session);
		if (it == party_of_session.end())
			return {};
		auto pit = parties.find(it->second);
		if (pit == parties.end())
			return {};
		return pit->second.members;
	}

	struct TradeSession
	{
		std::uint64_t trade_id = 0;
		SA::Net::SessionId player_a = 0;
		SA::Net::SessionId player_b = 0;
		bool a_locked = false;
		bool b_locked = false;
		bool a_confirmed = false;
		bool b_confirmed = false;
		std::vector<int> a_items{};
		std::vector<int> b_items{};
		std::vector<int> a_pets{};
		std::vector<int> b_pets{};
		std::uint32_t a_gold = 0;
		std::uint32_t b_gold = 0;
	};
	std::unordered_map<std::uint64_t, TradeSession> trades{};
	std::unordered_map<SA::Net::SessionId, std::uint64_t> trade_of_session{};
	std::unordered_map<SA::Net::SessionId, SA::Net::SessionId> pending_trade_requests{};
	std::uint64_t next_trade_id = 1;

	std::unordered_map<SA::Net::SessionId, std::vector<AddressBookEntry>> address_books{};
	std::unordered_map<SA::Net::SessionId, SA::Net::SessionId> pending_card_requests{};

	std::unordered_map<std::string, std::vector<MailEntry>> mailboxes{};
	std::uint64_t next_mail_id = 1;

	std::unordered_map<SA::Net::SessionId, std::vector<ChatMessage>> incoming_chats{};

	std::unordered_map<std::uint32_t, FamilyInfo> families{};
	std::unordered_map<SA::Net::SessionId, std::uint32_t> session_to_family{};
	std::unordered_map<std::string, std::uint32_t> player_to_family_name{};
	std::map<FamilyManor, std::uint32_t> manor_owners{};
	std::map<FamilyManor, ManorWarInfo> manor_wars{};
	std::uint32_t next_family_id = 1;

	std::unordered_map<SA::Net::SessionId, StallInfo> stalls{};

	std::unordered_map<std::uint64_t, MarketListing> market_listings{};
	std::uint64_t next_market_listing_id = 1;

	struct PlayerRideState
	{
		int pet_slot = -1;
		std::int32_t original_image = 0;
	};
	std::unordered_map<SA::Net::SessionId, PlayerRideState> player_rides{};
	std::unordered_map<SA::Net::SessionId, std::set<std::string>> player_ride_permits{};
	std::unordered_map<SA::Net::SessionId, std::set<RideCertType>> player_ride_certs{};

	struct PetProgressionState
	{
		bool is_fusion = false;
		std::int32_t fusion_raise = 0;
		std::int32_t trans_count = 0;
		std::int32_t fusion_code = 0;
		std::int32_t pet_id = 0;
	};
	std::unordered_map<std::uint64_t, PetProgressionState> pet_progression{};
	std::unordered_map<std::int32_t, std::int32_t> pet_template_fusion_codes{};
	std::unordered_map<std::uint64_t, int> pet_loyalty{};

	struct PlayerExtraStats
	{
		std::int32_t transmigration = 0;
		std::int32_t fame = 0;
	};
	std::unordered_map<SA::Net::SessionId, PlayerExtraStats> player_extra_stats{};

	struct PlayerTitleData
	{
		std::vector<int> owned_titles{};
		int active_title_id = 0;
	};
	std::unordered_map<int, TitleDefinition> registered_titles{};
	std::unordered_map<SA::Net::SessionId, PlayerTitleData> player_titles{};
	std::unordered_map<int, CraftingRecipe> registered_recipes{};

	const SA::Model::Pet *getRidingPet(SA::Net::SessionId sid) const
	{
		const auto rit = player_rides.find(sid);
		if (rit == player_rides.end())
			return nullptr;
		const auto *player = players.resolve(player_of_session.find(sid));
		if (player == nullptr)
			return nullptr;
		const int rslot = rit->second.pet_slot;
		if (rslot < 0 || static_cast<std::size_t>(rslot) >= SA::Model::kMaxPetHave)
			return nullptr;
		return pets.resolve(player->pets[static_cast<std::size_t>(rslot)]);
	}

	bool deliverSystemMail(const std::string &receiver_name,
	                       const std::string &title,
	                       const std::string &message,
	                       std::optional<SA::Model::Item> item = std::nullopt,
	                       std::optional<SA::Model::Pet> pet = std::nullopt,
	                       std::uint32_t gold = 0)
	{
		auto &mbox = mailboxes[receiver_name];
		if (mbox.size() >= kMaxMailBoxSize)
		{
			for (auto it = mbox.begin(); it != mbox.end(); ++it)
			{
				if (it->is_read && !it->has_attachment)
				{
					mbox.erase(it);
					break;
				}
			}
		}
		MailEntry mail{};
		mail.mail_id = next_mail_id++;
		mail.sender_name = "拍卖市场";
		mail.receiver_name = receiver_name;
		mail.title = title;
		mail.message = message;
		mail.sent_time_ms = now_ms;
		mail.is_read = false;
		mail.has_attachment = (item.has_value() || pet.has_value() || gold > 0);
		mail.attached_item = std::move(item);
		mail.attached_pet = std::move(pet);
		mail.attached_gold = gold;
		mbox.push_back(std::move(mail));
		return true;
	}

	void notifyAddressBookStatus(SA::Net::SessionId id, bool online)
	{
		SA::Model::Player *p = players.resolve(player_of_session.find(id));
		if (p == nullptr)
			return;
		const std::string name = p->name.c_str();
		for (auto &kv : address_books)
		{
			for (auto &card : kv.second)
			{
				if (card.charname == name)
				{
					card.online = online;
					card.session = online ? id : 0;
					if (online)
					{
						card.level = p->level;
						card.graphicsno = p->image;
					}
				}
			}
		}
	}

	void warpSinglePlayer(SA::Net::SessionId id, std::int32_t dst_floor, std::int32_t dst_x, std::int32_t dst_y);
	void warpPlayer(SA::Net::SessionId id, std::int32_t dst_floor, std::int32_t dst_x, std::int32_t dst_y);
};

} // namespace SA::World

#endif // __SA_WorldImpl_H__
