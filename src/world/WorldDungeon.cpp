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

bool World::loadPresetDungeons()
{
	// 1. 装配并注册经典的五兄弟的山寨 (kDungeonFiveBrothers = 31700, Floor 31701..31706)
	DungeonDefinition five_brothers{};
	five_brothers.dungeon_id = kDungeonFiveBrothers;
	five_brothers.name = "五兄弟的山寨";
	five_brothers.min_entry_level = 25;
	five_brothers.entry_floor = 31701;
	five_brothers.entry_x = 29;
	five_brothers.entry_y = 49;
	five_brothers.exit_floor = 300; // 重返吉鲁岛大世界地面
	five_brothers.exit_x = 343;
	five_brothers.exit_y = 191;

	// Floor 31701 (地下 1 楼)
	DungeonFloorConfig f1{};
	f1.floor_id = 31701;
	f1.depth = 1;
	f1.floor_name = "五兄弟的山寨地下１楼";
	f1.is_dark = true;
	f1.default_vision_radius = 2;
	f1.min_level = 25;
	// 楼梯 0: 退出洞窟返回吉鲁大世界地面
	DungeonStair s1_exit{};
	s1_exit.type = DungeonStairType::kExit;
	s1_exit.trigger_x = 29;
	s1_exit.trigger_y = 49;
	s1_exit.target_floor = 300;
	s1_exit.target_x = 343;
	s1_exit.target_y = 191;
	f1.stairs.push_back(s1_exit);
	// 楼梯 1: 前往下层 2 楼
	DungeonStair s1_down{};
	s1_down.type = DungeonStairType::kDown;
	s1_down.trigger_x = 11;
	s1_down.trigger_y = 4;
	s1_down.target_floor = 31702;
	s1_down.target_x = 20;
	s1_down.target_y = 2;
	f1.stairs.push_back(s1_down);
	five_brothers.floors.push_back(f1);

	// Floor 31702 (地下 2 楼)
	DungeonFloorConfig f2{};
	f2.floor_id = 31702;
	f2.depth = 2;
	f2.floor_name = "五兄弟的山寨地下２楼";
	f2.is_dark = true;
	f2.default_vision_radius = 2;
	f2.min_level = 25;
	DungeonStair s2_up{};
	s2_up.type = DungeonStairType::kUp;
	s2_up.trigger_x = 20;
	s2_up.trigger_y = 2;
	s2_up.target_floor = 31701;
	s2_up.target_x = 11;
	s2_up.target_y = 4;
	f2.stairs.push_back(s2_up);
	DungeonStair s2_down{};
	s2_down.type = DungeonStairType::kDown;
	s2_down.trigger_x = 37;
	s2_down.trigger_y = 41;
	s2_down.target_floor = 31703;
	s2_down.target_x = 45;
	s2_down.target_y = 41;
	f2.stairs.push_back(s2_down);
	five_brothers.floors.push_back(f2);

	// Floor 31703 (地下 3 楼)
	DungeonFloorConfig f3{};
	f3.floor_id = 31703;
	f3.depth = 3;
	f3.floor_name = "五兄弟的山寨地下３楼";
	f3.is_dark = true;
	f3.default_vision_radius = 2;
	f3.min_level = 25;
	DungeonStair s3_up{};
	s3_up.type = DungeonStairType::kUp;
	s3_up.trigger_x = 45;
	s3_up.trigger_y = 41;
	s3_up.target_floor = 31702;
	s3_up.target_x = 37;
	s3_up.target_y = 41;
	f3.stairs.push_back(s3_up);
	DungeonStair s3_down{};
	s3_down.type = DungeonStairType::kDown;
	s3_down.trigger_x = 10;
	s3_down.trigger_y = 4;
	s3_down.target_floor = 31704;
	s3_down.target_x = 5;
	s3_down.target_y = 5;
	f3.stairs.push_back(s3_down);
	five_brothers.floors.push_back(f3);

	// Floor 31704 (地下 4 楼)
	DungeonFloorConfig f4{};
	f4.floor_id = 31704;
	f4.depth = 4;
	f4.floor_name = "五兄弟的山寨地下４楼";
	f4.is_dark = true;
	f4.default_vision_radius = 2;
	f4.min_level = 25;
	DungeonStair s4_up{};
	s4_up.type = DungeonStairType::kUp;
	s4_up.trigger_x = 5;
	s4_up.trigger_y = 5;
	s4_up.target_floor = 31703;
	s4_up.target_x = 10;
	s4_up.target_y = 4;
	f4.stairs.push_back(s4_up);
	DungeonStair s4_down{};
	s4_down.type = DungeonStairType::kDown;
	s4_down.trigger_x = 25;
	s4_down.trigger_y = 25;
	s4_down.target_floor = 31705;
	s4_down.target_x = 10;
	s4_down.target_y = 10;
	s4_down.min_level = 30; // 4楼往下进入深层需 30 级门槛
	f4.stairs.push_back(s4_down);
	five_brothers.floors.push_back(f4);

	// Floor 31705 (地下 5 楼)
	DungeonFloorConfig f5{};
	f5.floor_id = 31705;
	f5.depth = 5;
	f5.floor_name = "五兄弟的山寨地下５楼";
	f5.is_dark = true;
	f5.default_vision_radius = 2;
	f5.min_level = 30;
	DungeonStair s5_up{};
	s5_up.type = DungeonStairType::kUp;
	s5_up.trigger_x = 10;
	s5_up.trigger_y = 10;
	s5_up.target_floor = 31704;
	s5_up.target_x = 25;
	s5_up.target_y = 25;
	f5.stairs.push_back(s5_up);
	DungeonStair s5_down{};
	s5_down.type = DungeonStairType::kDown;
	s5_down.trigger_x = 45;
	s5_down.trigger_y = 65;
	s5_down.target_floor = 31706;
	s5_down.target_x = 25;
	s5_down.target_y = 75;
	s5_down.min_level = 30;
	s5_down.required_event_flag = 28; // 智者谜题任务旗标门禁
	s5_down.gate_block_message = "必须先通过前面楼层的智者考验！";
	f5.stairs.push_back(s5_down);
	five_brothers.floors.push_back(f5);

	// Floor 31706 (地下 6 楼圣殿殿堂)
	DungeonFloorConfig f6{};
	f6.floor_id = 31706;
	f6.depth = 6;
	f6.floor_name = "五兄弟的山寨圣殿６楼";
	f6.is_dark = false; // 终极圣殿为明亮大殿
	f6.default_vision_radius = 10;
	f6.min_level = 30;
	DungeonStair s6_up{};
	s6_up.type = DungeonStairType::kUp;
	s6_up.trigger_x = 25;
	s6_up.trigger_y = 75;
	s6_up.target_floor = 31705;
	s6_up.target_x = 45;
	s6_up.target_y = 65;
	f6.stairs.push_back(s6_up);
	DungeonStair s6_exit{};
	s6_exit.type = DungeonStairType::kExit;
	s6_exit.trigger_x = 39;
	s6_exit.trigger_y = 6;
	s6_exit.target_floor = 300; // 踏上神圣石板直返地面吉鲁岛
	s6_exit.target_x = 343;
	s6_exit.target_y = 191;
	f6.stairs.push_back(s6_exit);
	five_brothers.floors.push_back(f6);

	// 为地下城各楼层初始化基础网格地图 (若未注册)
	for (const auto &floor : five_brothers.floors)
	{
		if (findFloorMap(floor.floor_id) == nullptr)
		{
			loadFloorMap(floor.floor_id, makeFixtureMap(80, 80));
		}
	}
	if (findFloorMap(300) == nullptr)
	{
		loadFloorMap(300, makeFixtureMap(400, 400));
	}

	return registerDungeon(five_brothers);
}

DungeonQuestReward World::concludeFiveBrothersQuest(SA::Net::SessionId session, std::uint64_t guardian_npc_id, int brother_index)
{
	(void)guardian_npc_id;
	DungeonQuestReward reward{};
	if (brother_index < 0 || brother_index > 4)
	{
		reward.message = "未知的守护者。";
		return reward;
	}

	Impl &s = *_impl;
	auto *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr || p->hp <= 0 || s.inBattle(session) || isPlayerVending(session))
	{
		reward.message = "当前状态无法进行誓约。";
		return reward;
	}

	// 1. 寻找智慧水晶 (Item ID: 2517)
	int crystal_slot = -1;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (const auto *item = s.items.resolve(p->items[i]))
		{
			if (item->item_id == 2517)
			{
				crystal_slot = static_cast<int>(i);
				break;
			}
		}
	}
	if (crystal_slot < 0)
	{
		reward.message = "请给我‘智慧的挡晶’，我再给你我的信物以作为认定你是一名勇者。";
		return reward;
	}

	// 2. 检查背包空槽 (预留至少 1 个空位)
	int free_item_slot = -1;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (static_cast<int>(i) != crystal_slot && !p->items[i].valid())
		{
			free_item_slot = static_cast<int>(i);
			break;
		}
	}
	if (free_item_slot < 0)
	{
		reward.message = "要持有我所给的信物，必须先预留一个道具栏空位。";
		return reward;
	}

	// 3. 检查随身宠物栏空槽 (至少预留 1 个空位)
	if (p->findFreePetSlot() < 0)
	{
		reward.message = "要得到我的分身，必须先将宠物栏空下来。";
		return reward;
	}

	// 4. 对应五兄弟奖励映射 (对齐原版 giiru/event/ev28.create)
	// index 0: 利诺恩 (地), Item 2518 (地之首饰), Pet 54 (利诺恩), Image 100287
	// index 1: 奇洛斯 (水火), Item 2520 (水之戒指), Pet 55 (奇洛斯), Image 100288
	// index 2: 浦洛斯 (火), Item 2526 (火之笛), Pet 56 (浦洛斯), Image 100289
	// index 3: 吉诺 (火风), Item 2519 (风之手环), Pet 57 (吉诺), Image 100290
	// index 4: 浦鲁德 (风火), Item 2525 (风火竖琴), Pet 481 (浦鲁德), Image 100394
	struct BrotherConfig
	{
		std::int32_t item_id;
		const char *item_name;
		std::int32_t pet_id;
		const char *pet_name;
		std::int32_t pet_image;
		const char *speech;
	};
	static constexpr BrotherConfig kBrothers[5] = {
	    {2518, "地之首饰", 54, "利诺恩", 100287, "那我依照约定收下“智之挡晶”了。我把地之首饰与我的分身赐给你，这就是我跟你的信赖证明。"},
	    {2520, "水之戒指", 55, "奇洛斯", 100288, "那我依照约定收下“智之挡晶”了。我把水之戒指与我的分身赐给你，这就是我跟你的信赖证明。"},
	    {2526, "火之笛", 56, "浦洛斯", 100289, "那我依照约定收下“智之挡晶”了。我把火之笛与我的分身赐给你，这就是我跟你的信赖证明。"},
	    {2519, "风之手环", 57, "吉诺", 100290, "那我依照约定收下“智之挡晶”了。我把风之手环与我的分身赐给你，这就是我跟你的信赖证明。"},
	    {2525, "风火竖琴", 481, "浦鲁德", 100394, "那我依照约定收下“智之挡晶”了。我把风火竖琴与我的分身赐给你，这就是我跟你的信赖证明。"},
	};

	const auto &b = kBrothers[brother_index];

	// 5. 原子消耗智慧水晶
	(void)s.items.release(p->items[static_cast<std::size_t>(crystal_slot)]);
	p->items[static_cast<std::size_t>(crystal_slot)] = SA::Model::kNullHandle;

	// 6. 原子生成发放首饰道具
	SA::Model::Item reward_item{};
	reward_item.item_id = b.item_id;
	reward_item.type = 2; // 饰品类型
	(void)reward_item.name.assign(b.item_name);
	giveItemIntoPlayer(*p, reward_item, s.items);

	// 7. 原子生成发放守护神兽宠物
	SA::Model::Pet reward_pet{};
	reward_pet.pet_id = b.pet_id;
	(void)reward_pet.name.assign(b.pet_name);
	reward_pet.base_image = b.pet_image;
	reward_pet.origin_image = b.pet_image;
	reward_pet.level = 1;
	reward_pet.hp = 120;
	reward_pet.mp = 50;
	reward_pet.max_mp = 50;
	reward_pet.vital = 25;
	reward_pet.str = 20;
	reward_pet.tough = 20;
	reward_pet.dex = 20;
	(void)givePetToPlayer(session, reward_pet);

	// 8. 登记终身完成旗标 (EndSetFlg 31)
	p->setEndEvent(31);

	reward.success = true;
	reward.item_id = b.item_id;
	reward.pet_id = b.pet_id;
	reward.message = b.speech;
	return reward;
}

} // namespace SA::World
