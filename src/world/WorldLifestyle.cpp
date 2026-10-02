// src/world/WorldLifestyle.cpp —— 家园、称号、声望荣誉商城与料理/合成手艺系统实现
//
// 对应原版 char.c, npc_fameshop.c, item.c (合成/料理)

#include "WorldImpl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SA::World
{

bool World::isValidPlayerImage(std::int32_t image) noexcept
{
	if (image >= 100000 && image <= 100235)
	{
		const std::int32_t offset = image - 100000;
		return (offset % 5 == 0);
	}
	return false;
}

std::int32_t World::computeFaceImage(std::int32_t image) noexcept
{
	if (image >= 100000 && image <= 100235 && ((image - 100000) % 5 == 0))
	{
		const std::int32_t k = (image - 100000) / 5;
		const std::int32_t archetype = k / 4; // 0..11
		const std::int32_t color = k % 4;     // 0..3
		return 30000 + archetype * 100 + color * 25;
	}
	return 30000;
}

void World::setHometownSpawn(int hometown, std::int32_t floor, std::int32_t x, std::int32_t y)
{
	if (hometown >= 0 && hometown < static_cast<int>(_impl->hometown_spawns.size()))
	{
		_impl->hometown_spawns[static_cast<std::size_t>(hometown)] = {floor, x, y};
	}
}

World::HometownSpawn World::hometownSpawn(int hometown) const noexcept
{
	if (hometown >= 0 && hometown < static_cast<int>(_impl->hometown_spawns.size()))
	{
		return _impl->hometown_spawns[static_cast<std::size_t>(hometown)];
	}
	return {0, 0, 0};
}

void World::setSessionHometown(SA::Net::SessionId session, int hometown)
{
	if (hometown >= 0 && hometown < static_cast<int>(_impl->hometown_spawns.size()))
	{
		_impl->session_hometowns[session] = hometown;
	}
	else
	{
		_impl->session_hometowns.erase(session);
	}
}

int World::sessionHometown(SA::Net::SessionId session) const noexcept
{
	const auto it = _impl->session_hometowns.find(session);
	return (it != _impl->session_hometowns.end()) ? it->second : -1;
}

int World::playerTransmigration(SA::Net::SessionId session) const
{
	const auto it = _impl->player_extra_stats.find(session);
	return (it != _impl->player_extra_stats.end()) ? it->second.transmigration : 0;
}

bool World::setPlayerTransmigration(SA::Net::SessionId session, int trans)
{
	if (trans < 0 || trans > 10)
		return false;
	_impl->player_extra_stats[session].transmigration = trans;
	return true;
}

int World::playerFame(SA::Net::SessionId session) const
{
	const auto it = _impl->player_extra_stats.find(session);
	return (it != _impl->player_extra_stats.end()) ? it->second.fame : 0;
}

bool World::setPlayerFame(SA::Net::SessionId session, int fame)
{
	_impl->player_extra_stats[session].fame = fame;
	return true;
}

bool World::registerTitle(const TitleDefinition &title)
{
	if (title.title_id <= 0 || title.name.empty())
		return false;
	_impl->registered_titles[title.title_id] = title;
	return true;
}

std::optional<TitleDefinition> World::findTitle(int title_id) const
{
	const auto it = _impl->registered_titles.find(title_id);
	if (it == _impl->registered_titles.end())
		return std::nullopt;
	return it->second;
}

bool World::grantTitle(SA::Net::SessionId session, int title_id)
{
	SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return false;
	if (_impl->registered_titles.find(title_id) == _impl->registered_titles.end())
		return false;

	auto &data = _impl->player_titles[session];
	for (int tid : data.owned_titles)
	{
		if (tid == title_id)
			return true;
	}
	if (data.owned_titles.size() >= 30)
		return false;
	data.owned_titles.push_back(title_id);
	return true;
}

bool World::revokeTitle(SA::Net::SessionId session, int title_id)
{
	auto it = _impl->player_titles.find(session);
	if (it == _impl->player_titles.end())
		return false;
	auto &vec = it->second.owned_titles;
	const auto pos = std::find(vec.begin(), vec.end(), title_id);
	if (pos == vec.end())
		return false;
	vec.erase(pos);
	if (it->second.active_title_id == title_id)
		it->second.active_title_id = 0;
	return true;
}

bool World::hasTitle(SA::Net::SessionId session, int title_id) const
{
	const auto it = _impl->player_titles.find(session);
	if (it == _impl->player_titles.end())
		return false;
	for (int tid : it->second.owned_titles)
	{
		if (tid == title_id)
			return true;
	}
	return false;
}

std::vector<int> World::playerOwnedTitles(SA::Net::SessionId session) const
{
	const auto it = _impl->player_titles.find(session);
	if (it == _impl->player_titles.end())
		return {};
	return it->second.owned_titles;
}

bool World::equipTitle(SA::Net::SessionId session, int title_id)
{
	SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return false;
	const auto tit = _impl->registered_titles.find(title_id);
	if (tit == _impl->registered_titles.end())
		return false;
	if (!hasTitle(session, title_id))
		return false;
	if (playerFame(session) < tit->second.req_fame)
		return false;

	_impl->player_titles[session].active_title_id = title_id;
	return true;
}

bool World::unequipTitle(SA::Net::SessionId session)
{
	auto it = _impl->player_titles.find(session);
	if (it == _impl->player_titles.end())
		return false;
	it->second.active_title_id = 0;
	return true;
}

int World::playerActiveTitle(SA::Net::SessionId session) const
{
	const auto it = _impl->player_titles.find(session);
	if (it == _impl->player_titles.end())
		return 0;
	return it->second.active_title_id;
}

std::string World::playerActiveTitleName(SA::Net::SessionId session) const
{
	const int tid = playerActiveTitle(session);
	if (tid <= 0)
		return "";
	const auto it = _impl->registered_titles.find(tid);
	if (it == _impl->registered_titles.end())
		return "";
	return it->second.name;
}

TitleStatsBonus World::playerTitleBonus(SA::Net::SessionId session) const
{
	const int tid = playerActiveTitle(session);
	if (tid <= 0)
		return {};
	const auto it = _impl->registered_titles.find(tid);
	if (it == _impl->registered_titles.end())
		return {};
	return it->second.bonus;
}

World::FameShopResultCode World::buyFromFameShop(SA::Net::SessionId session, std::uint64_t npc_id, int entry_id)
{
	Impl &s = *_impl;
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return FameShopResultCode::kSessionInvalid;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kFameShop)
		return FameShopResultCode::kShopNpcNotFound;

	// 距离检查: 同地图且曼哈顿距离 <= 3
	if (p->floor != npc->floor || std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return FameShopResultCode::kDistanceTooFar;

	const FameShopItem *chosen = nullptr;
	for (const auto &item : npc->fame_shop_items)
	{
		if (item.entry_id == entry_id)
		{
			chosen = &item;
			break;
		}
	}
	if (chosen == nullptr)
		return FameShopResultCode::kEntryNotFound;

	// 1. 声望充足性检查
	const int cur_fame = playerFame(session);
	if (cur_fame < chosen->fame_cost)
	{
		std::string msg = npc->fame_less_msg.empty() ? "声望不足，无法兑换！" : npc->fame_less_msg;
		s.sendExChangeWindow(session, npc->id, msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return FameShopResultCode::kInsufficientFame;
	}

	// 2. 类型专属门禁检查
	if (chosen->type == FameShopItemType::kTitle)
	{
		if (hasTitle(session, chosen->target_id))
		{
			s.sendExChangeWindow(session, npc->id, "你已经拥有该荣誉称号！",
			                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
			return FameShopResultCode::kAlreadyHaveTitle;
		}
		if (playerOwnedTitles(session).size() >= 30)
		{
			s.sendExChangeWindow(session, npc->id, "称号栏已满！",
			                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
			return FameShopResultCode::kTitleSlotsFull;
		}
	}
	else if (chosen->type == FameShopItemType::kItem)
	{
		if (s.countFreeItemSlots(*p) < 1)
		{
			std::string msg = npc->item_full_msg.empty() ? "你的背包空间不足！" : npc->item_full_msg;
			s.sendExChangeWindow(session, npc->id, msg,
			                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
			return FameShopResultCode::kInventoryFull;
		}
	}
	else if (chosen->type == FameShopItemType::kPet)
	{
		if (s.countFreePetSlots(*p) < 1)
		{
			std::string msg = npc->pet_full_msg.empty() ? "你的宠物栏已满！" : npc->pet_full_msg;
			s.sendExChangeWindow(session, npc->id, msg,
			                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
			return FameShopResultCode::kPetSlotsFull;
		}
	}

	// 3. 原子执行与扣费
	setPlayerFame(session, cur_fame - chosen->fame_cost);

	if (chosen->type == FameShopItemType::kTitle)
	{
		grantTitle(session, chosen->target_id);
	}
	else if (chosen->type == FameShopItemType::kItem)
	{
		SA::Model::Item item{};
		item.uid = ++s.next_window_id;
		item.item_id = chosen->target_id;
		item.current_pile = chosen->count > 0 ? chosen->count : 1;
		item.use_pile_nums = 1;
		item.name.assign(chosen->name.c_str());
		(void)giveItemToPlayer(session, item);
	}
	else if (chosen->type == FameShopItemType::kPet)
	{
		SA::Model::Pet pet{};
		pet.pet_id = chosen->target_id;
		pet.name.assign(chosen->name.c_str());
		pet.level = chosen->count > 0 ? chosen->count : 1;
		const auto base_stats = SA::Rules::deriveBaseStats(20, 20, 20, 20);
		pet.vital = 20;
		pet.str = 20;
		pet.tough = 20;
		pet.dex = 20;
		pet.hp = base_stats.max_hp;
		pet.mp = 50;
		pet.max_mp = 50;
		(void)givePetToPlayer(session, pet);
	}

	s.sendExChangeWindow(session, npc->id, "荣誉兑换成功！",
	                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
	return FameShopResultCode::kSuccess;
}

bool World::registerCraftingRecipe(const CraftingRecipe &recipe)
{
	if (recipe.recipe_id <= 0 || recipe.name.empty() || recipe.result_item_id <= 0)
		return false;
	_impl->registered_recipes[recipe.recipe_id] = recipe;
	return true;
}

const CraftingRecipe *World::findCraftingRecipe(int recipe_id) const
{
	const auto it = _impl->registered_recipes.find(recipe_id);
	if (it == _impl->registered_recipes.end())
		return nullptr;
	return &it->second;
}

CraftingResultCode World::craftItem(SA::Net::SessionId session, std::int32_t recipe_id,
                                    const std::vector<int> &input_slots, int pet_slot)
{
	Impl &s = *_impl;
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return CraftingResultCode::kSessionInvalid;

	// 1. 状态门禁: 濒死、战斗中、摆摊中
	if (p->hp <= 0)
		return CraftingResultCode::kPlayerDead;
	if (s.inBattle(session))
		return CraftingResultCode::kInBattle;
	if (isPlayerVending(session))
		return CraftingResultCode::kInVending;

	// 2. 配方查找与门槛检查
	const CraftingRecipe *recipe = findCraftingRecipe(recipe_id);
	if (recipe == nullptr)
		return CraftingResultCode::kRecipeNotFound;

	if (p->level < recipe->min_player_level)
		return CraftingResultCode::kLevelTooLow;

	if (recipe->cost_gold > 0 && p->gold < recipe->cost_gold)
		return CraftingResultCode::kInsufficientGold;

	// 3. 输入槽位合法性前置检查
	if (input_slots.empty())
		return CraftingResultCode::kInvalidSlots;

	std::vector<int> seen_slots;
	for (int slot : input_slots)
	{
		if (slot < static_cast<int>(SA::Model::kStartItemArray) || slot >= static_cast<int>(SA::Model::kMaxItemHave))
			return CraftingResultCode::kInvalidSlots;
		if (std::find(seen_slots.begin(), seen_slots.end(), slot) != seen_slots.end())
			return CraftingResultCode::kInvalidSlots; // 重复槽位防御
		seen_slots.push_back(slot);

		if (!p->items[static_cast<std::size_t>(slot)].valid())
			return CraftingResultCode::kInvalidSlots;
		const auto *item = s.items.resolve(p->items[static_cast<std::size_t>(slot)]);
		if (item == nullptr)
			return CraftingResultCode::kInvalidSlots;
	}

	// 检查是否处于摆摊货架
	auto stall_opt = getPlayerStall(session);
	if (stall_opt.has_value())
	{
		for (const auto &stall_item : stall_opt->items)
		{
			if (std::find(seen_slots.begin(), seen_slots.end(), stall_item.item_slot) != seen_slots.end())
				return CraftingResultCode::kSlotLocked;
		}
	}

	// 4. 原材料类型互斥检查 (原版 item_type == ITEM_DISH 互斥)
	for (int slot : input_slots)
	{
		const auto *item = s.items.resolve(p->items[static_cast<std::size_t>(slot)]);
		if (recipe->type == CraftingType::kCooking)
		{
			// 料理只能使用食材 (type 20 为料理/食材) 或配方明确指定的食材
			bool is_recipe_ingredient = false;
			for (const auto &ing : recipe->ingredients)
			{
				if (ing.item_id == item->item_id)
				{
					is_recipe_ingredient = true;
					break;
				}
			}
			if (item->type != 20 && !is_recipe_ingredient)
				return CraftingResultCode::kTypeMismatch;
		}
		else if (recipe->type == CraftingType::kSynthesis)
		{
			// 装备与道具合成严禁混入食材 (type 20)
			if (item->type == 20)
				return CraftingResultCode::kTypeMismatch;
		}
	}

	// 5. 原材料充足性校验
	std::unordered_map<int, int> provided_counts;
	for (int slot : input_slots)
	{
		const auto *item = s.items.resolve(p->items[static_cast<std::size_t>(slot)]);
		int count = item->current_pile > 0 ? item->current_pile : 1;
		provided_counts[item->item_id] += count;
	}

	for (const auto &ing : recipe->ingredients)
	{
		if (provided_counts[ing.item_id] < ing.count)
			return CraftingResultCode::kMissingIngredient;
	}

	// 6. 背包空间容量预检: 模拟扣除后释放的空槽 + 现有空槽 >= 1 (产物槽)
	int freed_slots = 0;
	auto needed_map = recipe->ingredients;
	for (int slot : input_slots)
	{
		const auto *item = s.items.resolve(p->items[static_cast<std::size_t>(slot)]);
		int slot_count = item->current_pile > 0 ? item->current_pile : 1;
		for (auto &ing : needed_map)
		{
			if (ing.item_id == item->item_id && ing.count > 0)
			{
				int to_deduct = std::min(slot_count, ing.count);
				slot_count -= to_deduct;
				ing.count -= to_deduct;
			}
		}
		if (slot_count <= 0)
		{
			freed_slots++;
		}
	}

	int cur_free_slots = s.countFreeItemSlots(*p);
	if (cur_free_slots + freed_slots < 1)
		return CraftingResultCode::kInventoryFull;

	// 7. 辅助宠物加成计算
	int pet_bonus = 0;
	if (pet_slot >= 0 && pet_slot < static_cast<int>(SA::Model::kMaxPetHave))
	{
		if (p->pets[static_cast<std::size_t>(pet_slot)].valid())
		{
			const auto *pet = s.pets.resolve(p->pets[static_cast<std::size_t>(pet_slot)]);
			if (pet != nullptr && pet->hp > 0)
			{
				pet_bonus = 10; // 存活宠物协助 +10% 成功率
			}
		}
	}

	// 8. 成功率骰子判定
	int effective_rate = std::clamp(recipe->success_rate + pet_bonus, 5, 100);
	bool success = (static_cast<int>(s.world_rng.randMod(100)) < effective_rate);

	// 9. 原子执行事务: 扣钱 -> 扣材料 -> 发放产出/碎料 -> 加声望
	if (recipe->cost_gold > 0)
	{
		(void)delGold(*p, GoldReason::kCraftingFee, recipe->cost_gold, 0, 0, s);
	}

	// 扣除材料
	auto remain_needed = recipe->ingredients;
	for (int slot : input_slots)
	{
		auto *item = s.items.resolve(p->items[static_cast<std::size_t>(slot)]);
		int slot_count = item->current_pile > 0 ? item->current_pile : 1;
		for (auto &ing : remain_needed)
		{
			if (ing.item_id == item->item_id && ing.count > 0)
			{
				int to_deduct = std::min(slot_count, ing.count);
				slot_count -= to_deduct;
				ing.count -= to_deduct;
			}
		}
		if (slot_count <= 0)
		{
			(void)s.items.release(p->items[static_cast<std::size_t>(slot)]);
			p->items[static_cast<std::size_t>(slot)] = SA::Model::kNullHandle;
		}
		else
		{
			item->current_pile = slot_count;
		}
	}

	// 产物发放
	if (success)
	{
		SA::Model::Item out_item{};
		out_item.uid = ++s.next_window_id;
		out_item.item_id = recipe->result_item_id;
		out_item.name.assign(recipe->result_name.empty() ? recipe->name.c_str() : recipe->result_name.c_str());
		out_item.type = (recipe->type == CraftingType::kCooking) ? 20 : 1;
		out_item.current_pile = recipe->result_count > 0 ? recipe->result_count : 1;
		out_item.use_pile_nums = recipe->result_count > 1 ? recipe->result_count : 1;
		(void)giveItemToPlayer(session, out_item);

		if (recipe->fame_reward > 0)
		{
			setPlayerFame(session, playerFame(session) + recipe->fame_reward);
		}
		return CraftingResultCode::kSuccess;
	}
	else
	{
		if (recipe->failure_item_id > 0)
		{
			SA::Model::Item fail_item{};
			fail_item.uid = ++s.next_window_id;
			fail_item.item_id = recipe->failure_item_id;
			fail_item.name.assign(recipe->failure_name.empty() ? "失败的碎料" : recipe->failure_name.c_str());
			fail_item.type = (recipe->type == CraftingType::kCooking) ? 20 : 1;
			fail_item.current_pile = 1;
			fail_item.use_pile_nums = 1;
			(void)giveItemToPlayer(session, fail_item);
		}
		return CraftingResultCode::kFailedGarbage;
	}
}

} // namespace SA::World
