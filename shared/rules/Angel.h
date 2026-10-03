// shared/rules/Angel.h —— S11 精灵/天使系统纯函数规则与数据结构
//
// 对应原版 char/char_angel.c (Robin 天使召唤、使者与勇者使命系统)
// 依据 00 §1.2 / 01 §4: shared/ 纯度规范与零堆分配, 兼容 C++17。

#ifndef __SA_Rules_Angel_H__
#define __SA_Rules_Angel_H__

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SA::Rules
{

// ── 契约角色与信物定义 ──────────────────────────────────────────────
enum class AngelRole : std::uint8_t
{
	kNone = 0,
	kAngel = 1, // 精灵使者
	kHero = 2,  // 勇者
};

// ── 使命阶段流转状态机 (对应原版 MISSION_* 宏) ──────────────────────
enum class AngelMissionStage : std::uint8_t
{
	kNone = 0,
	kWaitAnswer = 1,   // 等待使者答复 (MISSION_WAIT_ANSWER)
	kDoing = 2,        // 使命进行中 (MISSION_DOING)
	kHeroComplete = 3, // 勇者达成使命，可交付领奖 (MISSION_HERO_COMPLETE)
	kTimeOver = 4,     // 超时失败 (MISSION_TIMEOVER)
	kCompleted = 5,    // 领奖结算完成 (MISSION_NONE / 已结案)
};

// ── 信物传送校验结果 ────────────────────────────────────────────────
enum class AngelWarpResult : std::uint8_t
{
	kSuccess = 0,
	kTargetOffline = 1,
	kTargetNotPartner = 2,
	kInPartyBlocked = 3, // 组队中无法传送 (原 char_angel.c:618)
	kUnlawFloor = 4,     // 禁传地图 (原 checkUnlawWarpFloor)
	kHasDropItem = 5,    // 携带登出消失物品 (原 CheckDropatLogout)
	kInvalidToken = 6,   // 非专属信物
	kTimeOver = 7,       // 契约已超时
};

// 道具 ID
inline constexpr std::int32_t kAngelTokenItemId = 2884; // 使者信物 (ANGELITEM)
inline constexpr std::int32_t kHeroTokenItemId = 2885;  // 勇者信物 (HEROITEM)

// 资格门槛 (对齐原版 char_angel.c:181/211)
inline constexpr int kMinAngelCandidateLevel = 30;                // 使者最低等级
inline constexpr int kMinHeroCandidateLevel = 80;                 // 勇者最低等级
inline constexpr std::int64_t kDefaultMissionLimitSeconds = 7200; // 默认2小时

// ── 天使任务元数据 (POD 结构，零堆分配) ──────────────────────────
struct AngelMissionDefinition
{
	std::int32_t id = 0;
	std::int32_t min_hero_level = 80;
	std::int64_t limit_seconds = 7200;
	char name[32]{};
	char detail[128]{};
};

// ── 天使契约记录 (POD 结构，零堆分配) ──────────────────────────
struct AngelContractRecord
{
	std::uint64_t contract_id = 0;
	std::int32_t mission_id = 0;
	AngelMissionStage stage = AngelMissionStage::kNone;
	std::int64_t created_at_sec = 0;
	std::int64_t limit_seconds = 7200;
	char angel_name[32]{};
	char hero_name[32]{};
	char angel_cdkey[32]{};
	char hero_cdkey[32]{};
	bool angel_claimed = false;
	bool hero_claimed = false;
};

// ── 纯函数规则算法 ──────────────────────────────────────────────

// 使者资格校验 (等级 >= 30, 完成前置旗标)
bool isAngelCandidateEligible(int level, bool event_flag_qualified = true) noexcept;

// 勇者资格校验 (等级 >= 80, 完成前置旗标)
bool isHeroCandidateEligible(int level, bool event_flag_qualified = true) noexcept;

// 契约是否已超时
bool isAngelContractExpired(std::int64_t now_sec, const AngelContractRecord &contract) noexcept;

// 获取剩余秒数 (<=0 表示已超时)
std::int64_t angelContractRemainingSeconds(std::int64_t now_sec, const AngelContractRecord &contract) noexcept;

// 判定指定玩家在该契约中的角色身份
AngelRole getPlayerAngelRole(const char *player_name, const AngelContractRecord &contract) noexcept;

// 信物传送门禁校验 (原版 char_angel.c Use_AngelToken / Use_HeroToken 门禁规则)
AngelWarpResult checkAngelTokenWarp(AngelRole user_role, bool is_in_party, bool is_unlaw_floor, bool has_drop_items) noexcept;

// 精灵守护防御加成与伤害减免算法
int calculateSpiritBlessingDefenseBonus(int base_defense, bool has_spirit_blessing) noexcept;
int calculateSpiritBlessingDamageReduction(int incoming_damage, bool has_spirit_blessing) noexcept;

// 暗雷遇敌抑制规则: 装备使者信物或处于天使守护状态时，遇敌完全被抑制 (原 CHAR_WORKANGELMODE)
bool isEncounterSuppressedByAngel(bool is_angel_equipped, bool is_angel_mode) noexcept;

} // namespace SA::Rules

#endif // __SA_Rules_Angel_H__
