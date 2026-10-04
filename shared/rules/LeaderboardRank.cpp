// shared/rules/LeaderboardRank.cpp —— 排行榜与荣誉殿堂纯函数规则层实现
//
// 依据石器时代 8.0 排行榜与荣誉殿堂规范设计。
// 严守零引擎依赖、高可测性与严格的跨平台类型安全。

#include "rules/LeaderboardRank.h"

#include <algorithm>

namespace SA::Rules
{

void sortLeaderboardRecords(LeaderboardRecord *records, std::size_t count) noexcept
{
	if (records == nullptr || count == 0)
	{
		return;
	}

	std::sort(records, records + count, [](const LeaderboardRecord &a, const LeaderboardRecord &b)
	          {
		          if (a.score != b.score)
		          {
			          return a.score > b.score; // 积分降序
		          }
		          return a.entity_id < b.entity_id; // 同分按 ID 升序打破僵局
	          });

	for (std::size_t i = 0; i < count; ++i)
	{
		records[i].rank = static_cast<int>(i + 1);
	}
}

HallOfFameBonus computeHallOfFameBonus(LeaderboardKind kind, int rank) noexcept
{
	HallOfFameBonus bonus{};
	if (rank < 1 || rank > kHallOfFameTopLimit)
	{
		return bonus;
	}

	switch (kind)
	{
	case LeaderboardKind::kLevel:
		if (rank == 1)
		{
			bonus.attack_bonus_percent = 5.0f;
			bonus.defense_bonus_percent = 5.0f;
			bonus.title_id = 1001;
			bonus.title_name = "尼斯传奇怪兽猎人";
		}
		else if (rank == 2)
		{
			bonus.attack_bonus_percent = 3.0f;
			bonus.defense_bonus_percent = 3.0f;
			bonus.title_id = 1002;
			bonus.title_name = "远古冒险名宿";
		}
		else
		{
			bonus.attack_bonus_percent = 2.0f;
			bonus.defense_bonus_percent = 2.0f;
			bonus.title_id = 1003;
			bonus.title_name = "荒原先锋勇士";
		}
		break;

	case LeaderboardKind::kFame:
		if (rank == 1)
		{
			bonus.attack_bonus_percent = 5.0f;
			bonus.defense_bonus_percent = 5.0f;
			bonus.title_id = 1011;
			bonus.title_name = "天下名扬四海之主";
		}
		else if (rank == 2)
		{
			bonus.attack_bonus_percent = 3.0f;
			bonus.defense_bonus_percent = 3.0f;
			bonus.title_id = 1012;
			bonus.title_name = "尼斯万民敬仰者";
		}
		else
		{
			bonus.attack_bonus_percent = 2.0f;
			bonus.defense_bonus_percent = 2.0f;
			bonus.title_id = 1013;
			bonus.title_name = "声威赫赫的大陆领袖";
		}
		break;

	case LeaderboardKind::kFamilyPrestige:
		if (rank == 1)
		{
			bonus.attack_bonus_percent = 5.0f;
			bonus.defense_bonus_percent = 5.0f;
			bonus.title_id = 1021;
			bonus.title_name = "天下第一霸主氏族";
		}
		else if (rank == 2)
		{
			bonus.attack_bonus_percent = 3.0f;
			bonus.defense_bonus_percent = 3.0f;
			bonus.title_id = 1022;
			bonus.title_name = "威震尼斯宗族";
		}
		else
		{
			bonus.attack_bonus_percent = 2.0f;
			bonus.defense_bonus_percent = 2.0f;
			bonus.title_id = 1023;
			bonus.title_name = "盛名显赫同盟";
		}
		break;

	case LeaderboardKind::kManorWins:
		if (rank == 1)
		{
			bonus.attack_bonus_percent = 5.0f;
			bonus.defense_bonus_percent = 5.0f;
			bonus.title_id = 1031;
			bonus.title_name = "四大庄园永恒守护者";
		}
		else if (rank == 2)
		{
			bonus.attack_bonus_percent = 3.0f;
			bonus.defense_bonus_percent = 3.0f;
			bonus.title_id = 1032;
			bonus.title_name = "据点百战不败将领";
		}
		else
		{
			bonus.attack_bonus_percent = 2.0f;
			bonus.defense_bonus_percent = 2.0f;
			bonus.title_id = 1033;
			bonus.title_name = "庄园铁血卫士";
		}
		break;

	case LeaderboardKind::kDuelScore:
		if (rank == 1)
		{
			bonus.attack_bonus_percent = 5.0f;
			bonus.defense_bonus_percent = 5.0f;
			bonus.title_id = 1041;
			bonus.title_name = "天下第一武斗魁首";
		}
		else if (rank == 2)
		{
			bonus.attack_bonus_percent = 3.0f;
			bonus.defense_bonus_percent = 3.0f;
			bonus.title_id = 1042;
			bonus.title_name = "巅峰竞技傲视群雄";
		}
		else
		{
			bonus.attack_bonus_percent = 2.0f;
			bonus.defense_bonus_percent = 2.0f;
			bonus.title_id = 1043;
			bonus.title_name = "斗技场登峰宗师";
		}
		break;

	default:
		break;
	}

	return bonus;
}

WorshipReward calculateWorshipReward(int target_rank, int player_level) noexcept
{
	WorshipReward reward{};
	if (target_rank != 1)
	{
		return reward;
	}

	const int lvl = std::clamp(player_level, 1, 140);
	reward.exp_reward = static_cast<std::uint32_t>(lvl * 500);
	reward.gold_reward = static_cast<std::uint32_t>(lvl * 100);
	reward.buff_duration_seconds = 3600;
	reward.exp_buff_percent = 5.0f;

	return reward;
}

const char *getLeaderboardKindName(LeaderboardKind kind) noexcept
{
	switch (kind)
	{
	case LeaderboardKind::kLevel:
		return "角色等级榜";
	case LeaderboardKind::kFame:
		return "个人声望榜";
	case LeaderboardKind::kFamilyPrestige:
		return "家族威望榜";
	case LeaderboardKind::kManorWins:
		return "庄园据点战胜场榜";
	case LeaderboardKind::kDuelScore:
		return "决斗天梯积分榜";
	default:
		return "未知排行榜";
	}
}

} // namespace SA::Rules
