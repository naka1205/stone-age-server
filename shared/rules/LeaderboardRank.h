// shared/rules/LeaderboardRank.h —— 阶段 14 排行榜与荣誉殿堂规则层定义
//
// 依据石器时代 8.0 等级/声望/家族/庄园排行榜与荣誉殿堂体系设计。
// 严守 L3 纯函数规则层标准，零宿主依赖，可两端编译。

#ifndef __SA_RULES_LEADERBOARD_RANK_H__
#define __SA_RULES_LEADERBOARD_RANK_H__

#include <cstddef>
#include <cstdint>

namespace SA::Rules
{

// 排行榜类型枚举
enum class LeaderboardKind : std::uint8_t
{
	kNone = 0,
	kLevel = 1,          // 角色等级榜
	kFame = 2,           // 个人声望榜
	kFamilyPrestige = 3, // 家族威望榜
	kManorWins = 4,      // 庄园据点战胜场榜
	kDuelScore = 5,      // 决斗天梯积分榜
};

// 排行榜常量
inline constexpr std::size_t kMaxLeaderboardEntries = 100; // 单榜最大 100 名
inline constexpr int kHallOfFameTopLimit = 3;              // 荣誉殿堂入驻席位: 前 3 名

// 排行榜条目数据结构
struct LeaderboardRecord
{
	std::uint32_t entity_id = 0; // 玩家 ID 或 家族 ID
	char name[32] = {};          // 名称
	int rank = 0;                // 当前名次 (1-based, 0 表示未排)
	std::int64_t score = 0;      // 榜单积分/数值
	char extra_info[32] = {};    // 附加信息 (如所属家族、职业称号等)
};

// 荣誉殿堂专属称号与属性加成
struct HallOfFameBonus
{
	float attack_bonus_percent = 0.0f;  // 攻击力提升百分比
	float defense_bonus_percent = 0.0f; // 防御力提升百分比
	int title_id = 0;                   // 荣誉殿堂专属称号 ID
	const char *title_name = "";        // 称号名称
};

// 每日膜拜/敬仰奖励
struct WorshipReward
{
	std::uint32_t exp_reward = 0;  // 获得经验奖励
	std::uint32_t gold_reward = 0; // 获得石币奖励
	int buff_duration_seconds = 0; // 经验祝福增益时长 (秒)
	float exp_buff_percent = 0.0f; // 经验获取增益加成 (+%)
};

// 对排行榜数据进行确定性排序并赋予 rank 名次 (降序，同分时 entity_id 升序打破僵局)
void sortLeaderboardRecords(LeaderboardRecord *records, std::size_t count) noexcept;

// 计算荣誉殿堂殿堂霸主专属特权加成 (仅限 rank 1..3)
HallOfFameBonus computeHallOfFameBonus(LeaderboardKind kind, int rank) noexcept;

// 计算普通玩家每日膜拜荣誉殿堂第一名所得奖励
WorshipReward calculateWorshipReward(int target_rank, int player_level) noexcept;

// 获取排行榜分类名称
const char *getLeaderboardKindName(LeaderboardKind kind) noexcept;

} // namespace SA::Rules

#endif // __SA_RULES_LEADERBOARD_RANK_H__
