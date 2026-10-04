// src/world/WorldLeaderboard.cpp —— 阶段 14 排行榜与荣誉殿堂大世界服务实现
//
// 依据石器时代 8.0 排行榜与荣誉殿堂规则设计。
// 严守零宿主依赖、可测试性契约，提供各榜单聚合、排名查询、每日膜拜领赏与荣誉加成。

#include "WorldImpl.h"
#include "rules/LeaderboardRank.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace SA::World
{

void World::updateLeaderboardEntry(
    SA::Rules::LeaderboardKind kind,
    std::uint32_t entity_id,
    const std::string &name,
    std::int64_t score,
    const std::string &extra)
{
	if (kind == SA::Rules::LeaderboardKind::kNone || entity_id == 0)
	{
		return;
	}

	auto &list = _impl->leaderboards[kind];
	auto it = std::find_if(list.begin(), list.end(), [entity_id](const SA::Rules::LeaderboardRecord &rec)
	                       { return rec.entity_id == entity_id; });

	if (it != list.end())
	{
		it->score = score;
		std::snprintf(it->name, sizeof(it->name), "%s", name.c_str());
		std::snprintf(it->extra_info, sizeof(it->extra_info), "%s", extra.c_str());
	}
	else
	{
		SA::Rules::LeaderboardRecord record{};
		record.entity_id = entity_id;
		record.score = score;
		std::snprintf(record.name, sizeof(record.name), "%s", name.c_str());
		std::snprintf(record.extra_info, sizeof(record.extra_info), "%s", extra.c_str());
		list.push_back(record);
	}

	SA::Rules::sortLeaderboardRecords(list.data(), list.size());

	if (list.size() > SA::Rules::kMaxLeaderboardEntries)
	{
		list.resize(SA::Rules::kMaxLeaderboardEntries);
	}
}

std::vector<SA::Rules::LeaderboardRecord> World::getLeaderboard(
    SA::Rules::LeaderboardKind kind,
    std::size_t limit) const
{
	std::vector<SA::Rules::LeaderboardRecord> result;
	auto it = _impl->leaderboards.find(kind);
	if (it == _impl->leaderboards.end() || it->second.empty())
	{
		return result;
	}

	const std::size_t actual_count = std::min(it->second.size(), limit);
	result.assign(it->second.begin(), it->second.begin() + static_cast<std::ptrdiff_t>(actual_count));
	return result;
}

bool World::worshipHallOfFame(
    SA::Net::SessionId session,
    SA::Rules::LeaderboardKind kind,
    int target_rank,
    std::uint32_t current_day)
{
	if (target_rank != 1)
	{
		return false;
	}

	const auto pit = _impl->player_of_session.find(session);
	if (!pit.valid())
	{
		return false;
	}
	auto *player = _impl->players.resolve(pit);
	if (player == nullptr)
	{
		return false;
	}

	auto lb_it = _impl->leaderboards.find(kind);
	if (lb_it == _impl->leaderboards.end() || lb_it->second.empty())
	{
		return false;
	}

	// 必须存在 rank 1 的殿堂领袖
	if (lb_it->second.front().rank != 1)
	{
		return false;
	}

	// 每日限膜拜一次 (以角色名作为唯一业务凭据)
	const std::string char_key = player->name.c_str();
	auto worship_it = _impl->player_worship_days.find(char_key);
	if (worship_it != _impl->player_worship_days.end() && worship_it->second == current_day)
	{
		return false;
	}

	auto reward = SA::Rules::calculateWorshipReward(target_rank, player->level);
	if (reward.gold_reward > 0)
	{
		addGold(*player, GoldReason::kQuestReward, static_cast<std::int32_t>(reward.gold_reward), 0, 0, *_impl);
	}

	_impl->player_worship_days[char_key] = current_day;
	return true;
}

SA::Rules::HallOfFameBonus World::playerHallOfFameBonus(SA::Net::SessionId session) const
{
	const auto pit = _impl->player_of_session.find(session);
	if (!pit.valid())
	{
		return {};
	}
	auto *player = _impl->players.resolve(pit);
	if (player == nullptr)
	{
		return {};
	}

	const std::string char_name = player->name.c_str();

	for (const auto &[kind, list] : _impl->leaderboards)
	{
		for (const auto &rec : list)
		{
			if ((rec.entity_id == static_cast<std::uint32_t>(session) || std::strcmp(rec.name, char_name.c_str()) == 0) && rec.rank >= 1 && rec.rank <= SA::Rules::kHallOfFameTopLimit)
			{
				return SA::Rules::computeHallOfFameBonus(kind, rec.rank);
			}
		}
	}

	return {};
}

} // namespace SA::World
