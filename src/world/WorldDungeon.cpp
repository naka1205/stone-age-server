// src/world/WorldDungeon.cpp —— 地下城多层迷宫与漆黑洞窟视野探险系统 (阶段 18 §9.0.134)

#include "WorldImpl.h"
#include "world/Api.h"

namespace SA::World
{

bool World::registerDungeon(const DungeonDefinition &dungeon)
{
	if (dungeon.dungeon_id == 0 || dungeon.floors.empty())
		return false;

	Impl &s = *_impl;
	s.dungeons[dungeon.dungeon_id] = dungeon;
	for (const auto &floor : dungeon.floors)
	{
		s.floor_to_dungeon[floor.floor_id] = dungeon.dungeon_id;
	}
	return true;
}

std::optional<DungeonDefinition> World::findDungeon(std::uint32_t dungeon_id) const
{
	const Impl &s = *_impl;
	auto it = s.dungeons.find(dungeon_id);
	if (it != s.dungeons.end())
		return it->second;
	return std::nullopt;
}

bool World::enterDungeon(SA::Net::SessionId session, std::uint32_t dungeon_id)
{
	Impl &s = *_impl;
	auto *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr || s.inBattle(session) || isPlayerVending(session))
		return false;

	auto it = s.dungeons.find(dungeon_id);
	if (it == s.dungeons.end())
		return false;

	const auto &dungeon = it->second;
	if (p->level < dungeon.min_entry_level)
		return false;

	s.player_active_dungeon[session] = dungeon_id;
	s.warpPlayer(session, dungeon.entry_floor, dungeon.entry_x, dungeon.entry_y);
	return true;
}

bool World::leaveDungeon(SA::Net::SessionId session)
{
	Impl &s = *_impl;
	auto *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr || s.inBattle(session))
		return false;

	auto it = s.player_active_dungeon.find(session);
	if (it == s.player_active_dungeon.end())
	{
		// 容错：检查是否处于某个地下城楼层中
		auto dit = s.floor_to_dungeon.find(p->floor);
		if (dit == s.floor_to_dungeon.end())
			return false;
		it = s.player_active_dungeon.emplace(session, dit->second).first;
	}

	auto dit = s.dungeons.find(it->second);
	if (dit == s.dungeons.end())
		return false;

	const auto &dungeon = dit->second;
	s.warpPlayer(session, dungeon.exit_floor, dungeon.exit_x, dungeon.exit_y);
	s.player_active_dungeon.erase(it);
	return true;
}

bool World::isPlayerInDungeon(SA::Net::SessionId session) const
{
	const Impl &s = *_impl;
	const auto *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	if (s.player_active_dungeon.find(session) != s.player_active_dungeon.end())
		return true;

	return s.floor_to_dungeon.find(p->floor) != s.floor_to_dungeon.end();
}

std::optional<std::uint32_t> World::playerCurrentDungeonId(SA::Net::SessionId session) const
{
	const Impl &s = *_impl;
	const auto *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return std::nullopt;

	auto it = s.player_active_dungeon.find(session);
	if (it != s.player_active_dungeon.end())
		return it->second;

	auto fit = s.floor_to_dungeon.find(p->floor);
	if (fit != s.floor_to_dungeon.end())
		return fit->second;

	return std::nullopt;
}

bool World::isPlayerInDarkCave(SA::Net::SessionId session) const
{
	return _impl->isPlayerInDarkCave(session);
}

std::int32_t World::playerEffectiveVisionRadius(SA::Net::SessionId session) const
{
	return _impl->playerEffectiveVisionRadius(session);
}

bool World::useTorch(SA::Net::SessionId session, std::uint32_t duration_ms, std::int32_t radius)
{
	if (radius <= 0)
		return false;

	Impl &s = *_impl;
	auto *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	const auto current_ms = std::max(s.now_ms, s.clock.nowMs());
	PlayerTorchState state{};
	state.has_torch = true;
	state.light_radius = radius;
	state.expire_time_ms = current_ms + static_cast<std::int64_t>(duration_ms);
	s.player_torches[session] = state;
	return true;
}

bool World::triggerDungeonStair(SA::Net::SessionId session)
{
	return _impl->triggerDungeonStair(session);
}

double World::playerEncounterRateMultiplier(SA::Net::SessionId session) const
{
	return _impl->playerEncounterRateMultiplier(session);
}

} // namespace SA::World
