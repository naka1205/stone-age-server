// src/world/WorldPetFeatures.cpp —— 宠物忠诚度、进阶技能、融合与转生系统实现
//
// 对应原版 char.c, pet.c, pet_skill.c, include/pet.h

#include "WorldImpl.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace SA::World
{

int World::petLoyalty(std::uint64_t pet_uid) const
{
	const auto it = _impl->pet_loyalty.find(pet_uid);
	if (it != _impl->pet_loyalty.end())
		return it->second;
	return 100; // 默认满忠诚 100
}

void World::setPetLoyalty(std::uint64_t pet_uid, int loyalty)
{
	_impl->pet_loyalty[pet_uid] = std::clamp(loyalty, 0, 100);
}

// ── 宠物进阶技能与忠诚度交互体系 (批次 §9.0.101) ──────────────────────

PetFeedResult World::feedPet(SA::Net::SessionId session, int pet_slot, int item_slot)
{
	PetFeedResult result{};
	Impl &s = *_impl;
	const auto pit = s.player_of_session.find(session);
	if (!pit.valid())
	{
		result.code = PetFeedResultCode::kInvalidSession;
		return result;
	}
	SA::Model::Player *p = s.players.resolve(pit);
	if (p == nullptr || p->hp <= 0)
	{
		result.code = PetFeedResultCode::kPlayerDead;
		return result;
	}

	if (s.inBattle(session))
	{
		result.code = PetFeedResultCode::kPlayerInBattle;
		return result;
	}
	if (isPlayerVending(session))
	{
		result.code = PetFeedResultCode::kPlayerVending;
		return result;
	}

	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
	{
		result.code = PetFeedResultCode::kPetNotFound;
		return result;
	}
	const auto ph = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!ph.valid())
	{
		result.code = PetFeedResultCode::kPetNotFound;
		return result;
	}
	auto *pet = s.pets.resolve(ph);
	if (pet == nullptr)
	{
		result.code = PetFeedResultCode::kPetNotFound;
		return result;
	}
	if (pet->hp <= 0)
	{
		result.code = PetFeedResultCode::kPetDead;
		return result;
	}

	if (item_slot < static_cast<int>(SA::Model::kStartItemArray) ||
	    static_cast<std::size_t>(item_slot) >= SA::Model::kMaxItemHave)
	{
		result.code = PetFeedResultCode::kItemNotFound;
		return result;
	}
	const auto ih = p->items[static_cast<std::size_t>(item_slot)];
	if (!ih.valid())
	{
		result.code = PetFeedResultCode::kItemNotFound;
		return result;
	}
	auto *item = s.items.resolve(ih);
	if (item == nullptr)
	{
		result.code = PetFeedResultCode::kItemNotFound;
		return result;
	}

	// 摆摊锁定检查
	const auto stall_it = s.stalls.find(session);
	if (stall_it != s.stalls.end())
	{
		for (const auto &it_entry : stall_it->second.items)
		{
			if (it_entry.item_slot == item_slot)
			{
				result.code = PetFeedResultCode::kItemLocked;
				return result;
			}
		}
	}

	// 食材/食物类型门禁 (原版 item_type == 20 为料理/肉类)
	if (item->type != 20)
	{
		result.code = PetFeedResultCode::kNotFoodItem;
		return result;
	}

	const int cur_loyalty = petLoyalty(pet->uid);
	// 等级压制与忠诚度上限计算 (原版 char.c / pet.c 规则)
	int max_loyalty_cap = 100;
	if (pet->level > p->level)
	{
		const int diff = pet->level - p->level;
		max_loyalty_cap = std::max(20, 100 - diff * 3);
	}

	if (cur_loyalty >= max_loyalty_cap)
	{
		result.code = PetFeedResultCode::kLoyaltyCapped;
		result.final_hp = pet->hp;
		result.final_loyalty = cur_loyalty;
		return result;
	}

	// 计算生命恢复与忠诚度提升
	const auto base_stats = SA::Rules::deriveBaseStats(pet->vital, pet->str, pet->tough, pet->dex);
	const std::int32_t max_hp = std::max(base_stats.max_hp, pet->hp);
	const std::int32_t hp_heal = 50; // 食物基准恢复 50 点生命
	const std::int32_t old_hp = pet->hp;
	pet->hp = std::min(max_hp, pet->hp + hp_heal);
	result.hp_recovered = pet->hp - old_hp;

	int gain_loyalty = 5; // 基准提升 5 点忠诚度
	if (pet->level > p->level)
	{
		gain_loyalty = std::max(1, gain_loyalty / 2);
	}
	const int new_loyalty = std::min(max_loyalty_cap, cur_loyalty + gain_loyalty);
	result.loyalty_gained = new_loyalty - cur_loyalty;
	setPetLoyalty(pet->uid, new_loyalty);

	// 原子扣减食物道具
	if (item->current_pile > 1)
	{
		item->current_pile -= 1;
	}
	else
	{
		p->clearItemSlot(item_slot);
		s.items.release(ih);
	}

	result.code = PetFeedResultCode::kSuccess;
	result.final_hp = pet->hp;
	result.final_loyalty = new_loyalty;
	return result;
}

bool World::forgetPetSkill(SA::Net::SessionId session, int pet_slot, int skill_slot)
{
	Impl &s = *_impl;
	if (s.inBattle(session) || isPlayerVending(session))
		return false;

	const auto pit = s.player_of_session.find(session);
	if (!pit.valid())
		return false;
	auto *p = s.players.resolve(pit);
	if (p == nullptr || p->hp <= 0)
		return false;

	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return false;
	const auto ph = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!ph.valid())
		return false;
	auto *pet = s.pets.resolve(ph);
	if (pet == nullptr)
		return false;

	if (skill_slot < 0 || static_cast<std::size_t>(skill_slot) >= SA::Model::Pet::kPetSkillSlots)
		return false;

	if (pet->pet_skills[static_cast<std::size_t>(skill_slot)] <= 0)
		return false;

	pet->pet_skills[static_cast<std::size_t>(skill_slot)] = 0;
	return true;
}

PetObedienceState World::checkPetObedience(SA::Net::SessionId session, int pet_slot) const
{
	const auto pit = _impl->player_of_session.find(session);
	if (!pit.valid())
		return PetObedienceState::kBetray;
	const auto *p = _impl->players.resolve(pit);
	if (p == nullptr)
		return PetObedienceState::kBetray;
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return PetObedienceState::kBetray;
	const auto ph = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!ph.valid())
		return PetObedienceState::kBetray;
	const auto *pet = _impl->pets.resolve(ph);
	if (pet == nullptr)
		return PetObedienceState::kBetray;

	const int loyalty = petLoyalty(pet->uid);
	if (loyalty >= 60)
		return PetObedienceState::kObedient;
	if (loyalty >= 20)
		return PetObedienceState::kConfused;
	return PetObedienceState::kBetray;
}

// ── 宠物融合与转生系统 (批次 §9.0.96) ───────────────────────────────────

bool World::isPetFusion(std::uint64_t pet_uid) const noexcept
{
	const auto it = _impl->pet_progression.find(pet_uid);
	return (it != _impl->pet_progression.end()) && it->second.is_fusion;
}

void World::setPetFusion(std::uint64_t pet_uid, bool is_fusion) noexcept
{
	_impl->pet_progression[pet_uid].is_fusion = is_fusion;
}

std::int32_t World::petTransCount(std::uint64_t pet_uid) const noexcept
{
	const auto it = _impl->pet_progression.find(pet_uid);
	return (it != _impl->pet_progression.end()) ? it->second.trans_count : 0;
}

void World::setPetTransCount(std::uint64_t pet_uid, std::int32_t count) noexcept
{
	_impl->pet_progression[pet_uid].trans_count = count;
}

std::int32_t World::petFusionCode(std::uint64_t pet_uid) const noexcept
{
	const auto it = _impl->pet_progression.find(pet_uid);
	if (it != _impl->pet_progression.end())
	{
		if (it->second.fusion_code != 0)
			return it->second.fusion_code;
		const auto tit = _impl->pet_template_fusion_codes.find(it->second.pet_id);
		if (tit != _impl->pet_template_fusion_codes.end())
			return tit->second;
	}
	return 0;
}

void World::setPetFusionCode(std::uint64_t pet_uid, std::int32_t code) noexcept
{
	_impl->pet_progression[pet_uid].fusion_code = code;
}

void World::registerPetTemplateFusionCode(std::int32_t pet_id, std::int32_t code) noexcept
{
	_impl->pet_template_fusion_codes[pet_id] = code;
}

PetFusionResultCode World::fusePets(SA::Net::SessionId session, int main_slot, int sub1_slot, int sub2_slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(session);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (it == s.conns.end() || p == nullptr)
		return PetFusionResultCode::kInvalidSession;

	if (main_slot < 0 || static_cast<std::size_t>(main_slot) >= SA::Model::kMaxPetHave ||
	    sub1_slot < 0 || static_cast<std::size_t>(sub1_slot) >= SA::Model::kMaxPetHave ||
	    main_slot == sub1_slot)
	{
		return PetFusionResultCode::kInvalidSlot;
	}

	if (sub2_slot >= 0)
	{
		if (static_cast<std::size_t>(sub2_slot) >= SA::Model::kMaxPetHave ||
		    sub2_slot == main_slot || sub2_slot == sub1_slot)
		{
			return PetFusionResultCode::kInvalidSlot;
		}
	}

	const auto main_h = p->pets[static_cast<std::size_t>(main_slot)];
	const auto sub1_h = p->pets[static_cast<std::size_t>(sub1_slot)];
	auto *main_pet = s.pets.resolve(main_h);
	auto *sub1_pet = s.pets.resolve(sub1_h);
	if (main_pet == nullptr || sub1_pet == nullptr)
		return PetFusionResultCode::kPetNotFound;

	SA::Model::EntityHandle sub2_h{};
	SA::Model::Pet *sub2_pet = nullptr;
	if (sub2_slot >= 0)
	{
		sub2_h = p->pets[static_cast<std::size_t>(sub2_slot)];
		sub2_pet = s.pets.resolve(sub2_h);
		if (sub2_pet == nullptr)
			return PetFusionResultCode::kPetNotFound;
	}

	// 1. 已是融合宠拦截 (原版 CHAR_FUSIONBEIT == 1)
	if (isPetFusion(main_pet->uid) || isPetFusion(sub1_pet->uid) ||
	    (sub2_pet != nullptr && isPetFusion(sub2_pet->uid)))
	{
		return PetFusionResultCode::kAlreadyFused;
	}

	// 2. 融合码校验 (原版 EVOLUTION_getPetFusionCode < 0 阻断)
	auto getCode = [&](const SA::Model::Pet &pt) -> int
	{
		auto pit = s.pet_progression.find(pt.uid);
		if (pit != s.pet_progression.end() && pit->second.fusion_code != 0)
			return pit->second.fusion_code;
		auto tit = s.pet_template_fusion_codes.find(pt.pet_id);
		if (tit != s.pet_template_fusion_codes.end())
			return tit->second;
		return (pt.pet_id >= 0) ? (pt.pet_id % 29) : -1;
	};

	const int main_code = getCode(*main_pet);
	const int sub1_code = getCode(*sub1_pet);
	if (main_code < 0 || sub1_code < 0)
		return PetFusionResultCode::kIneligibleFusionCode;

	int sub2_code = -1;
	if (sub2_pet != nullptr)
	{
		sub2_code = getCode(*sub2_pet);
		if (sub2_code < 0)
			return PetFusionResultCode::kIneligibleFusionCode;
	}

	// 3. 状态互斥拦截: 出战中或骑乘中阻断
	if (p->default_pet == main_slot || p->default_pet == sub1_slot ||
	    (sub2_slot >= 0 && p->default_pet == sub2_slot))
	{
		return PetFusionResultCode::kPetInBattleOrRide;
	}
	if (isPlayerRiding(session))
	{
		const int rslot = playerRidePetSlot(session);
		if (rslot == main_slot || rslot == sub1_slot || (sub2_slot >= 0 && rslot == sub2_slot))
			return PetFusionResultCode::kPetInBattleOrRide;
	}

	// 4. 摆摊货架互斥拦截
	const auto stall_it = s.stalls.find(session);
	if (stall_it != s.stalls.end())
	{
		for (const auto &sp : stall_it->second.pets)
		{
			if (sp.pet_slot == main_slot || sp.pet_slot == sub1_slot ||
			    (sub2_slot >= 0 && sp.pet_slot == sub2_slot))
			{
				return PetFusionResultCode::kPetInTradeOrStall;
			}
		}
	}

	// 5. 目标宠物模板计算 (三表投影)
	auto getElem = [](const SA::Model::Pet &pt) -> int
	{
		if (pt.earth > 0)
			return 0;
		if (pt.water > 0)
			return 1;
		if (pt.fire > 0)
			return 2;
		if (pt.wind > 0)
			return 3;
		return 0;
	};
	const int main_elem = getElem(*main_pet);
	const int sub1_elem = getElem(*sub1_pet);
	int target_id = resolvePetFusionResultId(sub1_code, main_code, sub1_elem, main_elem);
	if (target_id < 0)
		target_id = 989; // 兜底融合宠

	// 6. 四维成长与初始能力计算
	PetGrowth mg{
	    main_pet->growth_vital ? static_cast<int>(main_pet->growth_vital) : main_pet->vital,
	    main_pet->growth_str ? static_cast<int>(main_pet->growth_str) : main_pet->str,
	    main_pet->growth_tough ? static_cast<int>(main_pet->growth_tough) : main_pet->tough,
	    main_pet->growth_dex ? static_cast<int>(main_pet->growth_dex) : main_pet->dex};
	PetGrowth s1g{
	    sub1_pet->growth_vital ? static_cast<int>(sub1_pet->growth_vital) : sub1_pet->vital,
	    sub1_pet->growth_str ? static_cast<int>(sub1_pet->growth_str) : sub1_pet->str,
	    sub1_pet->growth_tough ? static_cast<int>(sub1_pet->growth_tough) : sub1_pet->tough,
	    sub1_pet->growth_dex ? static_cast<int>(sub1_pet->growth_dex) : sub1_pet->dex};
	std::optional<PetGrowth> s2g_opt;
	if (sub2_pet != nullptr)
	{
		s2g_opt = PetGrowth{
		    sub2_pet->growth_vital ? static_cast<int>(sub2_pet->growth_vital) : sub2_pet->vital,
		    sub2_pet->growth_str ? static_cast<int>(sub2_pet->growth_str) : sub2_pet->str,
		    sub2_pet->growth_tough ? static_cast<int>(sub2_pet->growth_tough) : sub2_pet->tough,
		    sub2_pet->growth_dex ? static_cast<int>(sub2_pet->growth_dex) : sub2_pet->dex};
	}

	const PetGrowth fused_growth = calculateFusionGrowth(
	    mg, main_pet->level, s1g, sub1_pet->level,
	    s2g_opt ? &*s2g_opt : nullptr, sub2_pet ? sub2_pet->level : 0);

	// 7. 技能继承
	const auto fused_skills = calculateFusionSkills(
	    main_pet->pet_skills, sub1_pet->pet_skills,
	    sub2_pet ? sub2_pet->pet_skills : nullptr);

	// 8. 属性继承 (继承主宠属性倾向)
	const int f_earth = main_pet->earth;
	const int f_water = main_pet->water;
	const int f_fire = main_pet->fire;
	const int f_wind = main_pet->wind;

	// 9. 原子扣除主副宠
	s.pets.release(main_h);
	p->pets[static_cast<std::size_t>(main_slot)] = {};
	s.pets.release(sub1_h);
	p->pets[static_cast<std::size_t>(sub1_slot)] = {};
	if (sub2_pet != nullptr)
	{
		s.pets.release(sub2_h);
		p->pets[static_cast<std::size_t>(sub2_slot)] = {};
	}

	// 10. 分配新融合宠物
	const auto new_h = s.pets.allocate();
	if (!new_h.valid())
		return PetFusionResultCode::kPoolFull;

	auto *new_pet = s.pets.resolve(new_h);
	if (new_pet == nullptr)
	{
		s.pets.release(new_h);
		return PetFusionResultCode::kPoolFull;
	}

	new_pet->uid = ++s.next_window_id;
	new_pet->pet_id = target_id;
	new_pet->name.assign("融合宠");
	new_pet->level = 1;
	new_pet->exp = 0;
	new_pet->growth_vital = static_cast<std::uint8_t>(fused_growth.vital);
	new_pet->growth_str = static_cast<std::uint8_t>(fused_growth.str);
	new_pet->growth_tough = static_cast<std::uint8_t>(fused_growth.tough);
	new_pet->growth_dex = static_cast<std::uint8_t>(fused_growth.dex);
	new_pet->vital = fused_growth.vital;
	new_pet->str = fused_growth.str;
	new_pet->tough = fused_growth.tough;
	new_pet->dex = fused_growth.dex;

	const auto stats = SA::Rules::deriveBaseStats(new_pet->vital, new_pet->str, new_pet->tough, new_pet->dex);
	new_pet->hp = stats.max_hp;
	new_pet->mp = 50;
	new_pet->max_mp = 50;
	new_pet->earth = f_earth;
	new_pet->water = f_water;
	new_pet->fire = f_fire;
	new_pet->wind = f_wind;
	new_pet->origin_image = 100000 + target_id;
	new_pet->base_image = 100000 + target_id;
	new_pet->owner = s.player_of_session.find(session);
	new_pet->owner_char_name = p->name;
	for (std::size_t i = 0; i < 7; ++i)
	{
		new_pet->pet_skills[i] = fused_skills[i];
	}

	p->pets[static_cast<std::size_t>(main_slot)] = new_h;
	setPetFusion(new_pet->uid, true);
	_impl->pet_progression[new_pet->uid].pet_id = target_id;
	return PetFusionResultCode::kSuccess;
}

PetTransResultCode World::reincarnatePet(SA::Net::SessionId session, int target_slot, int sacrifice_slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(session);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (it == s.conns.end() || p == nullptr)
		return PetTransResultCode::kInvalidSession;

	if (target_slot < 0 || static_cast<std::size_t>(target_slot) >= SA::Model::kMaxPetHave)
		return PetTransResultCode::kInvalidSlot;

	const auto target_h = p->pets[static_cast<std::size_t>(target_slot)];
	auto *pet = s.pets.resolve(target_h);
	if (pet == nullptr)
		return PetTransResultCode::kPetNotFound;

	// 1. 等级门限: 必须 >= 100 级 (原版 TransLV = 100)
	if (pet->level < 100)
		return PetTransResultCode::kInsufficientLevel;

	// 2. 转生上限: 最多 2 转 (0 -> 1, 1 -> 2)
	const int cur_trans = petTransCount(pet->uid);
	if (cur_trans >= 2)
		return PetTransResultCode::kMaxTransReached;

	// 3. 状态互斥: 出战中或骑乘中阻断
	if (p->default_pet == target_slot)
		return PetTransResultCode::kPetInBattleOrRide;
	if (isPlayerRiding(session) && playerRidePetSlot(session) == target_slot)
		return PetTransResultCode::kPetInBattleOrRide;

	// 4. 摆摊货架互斥拦截
	const auto stall_it = s.stalls.find(session);
	if (stall_it != s.stalls.end())
	{
		for (const auto &sp : stall_it->second.pets)
		{
			if (sp.pet_slot == target_slot)
				return PetTransResultCode::kPetInTradeOrStall;
		}
	}

	// 5. 辅助宠校验
	SA::Model::EntityHandle sac_h{};
	SA::Model::Pet *sac_pet = nullptr;
	if (sacrifice_slot >= 0)
	{
		if (sacrifice_slot == target_slot || static_cast<std::size_t>(sacrifice_slot) >= SA::Model::kMaxPetHave)
			return PetTransResultCode::kInvalidSacrifice;
		sac_h = p->pets[static_cast<std::size_t>(sacrifice_slot)];
		sac_pet = s.pets.resolve(sac_h);
		if (sac_pet == nullptr)
			return PetTransResultCode::kInvalidSacrifice;
		if (p->default_pet == sacrifice_slot || (isPlayerRiding(session) && playerRidePetSlot(session) == sacrifice_slot))
			return PetTransResultCode::kPetInBattleOrRide;
		if (stall_it != s.stalls.end())
		{
			for (const auto &sp : stall_it->second.pets)
			{
				if (sp.pet_slot == sacrifice_slot)
					return PetTransResultCode::kPetInTradeOrStall;
			}
		}
	}

	// 6. 计算转生成长与四维增量
	PetGrowth base{
	    pet->growth_vital ? static_cast<int>(pet->growth_vital) : pet->vital,
	    pet->growth_str ? static_cast<int>(pet->growth_str) : pet->str,
	    pet->growth_tough ? static_cast<int>(pet->growth_tough) : pet->tough,
	    pet->growth_dex ? static_cast<int>(pet->growth_dex) : pet->dex};
	PetGrowth work{30, 30, 30, 30}; // 默认转生基准
	if (sac_pet != nullptr)
	{
		work = PetGrowth{
		    sac_pet->growth_vital ? static_cast<int>(sac_pet->growth_vital) : sac_pet->vital,
		    sac_pet->growth_str ? static_cast<int>(sac_pet->growth_str) : sac_pet->str,
		    sac_pet->growth_tough ? static_cast<int>(sac_pet->growth_tough) : sac_pet->tough,
		    sac_pet->growth_dex ? static_cast<int>(sac_pet->growth_dex) : sac_pet->dex};
	}

	const PetGrowth new_growth = calculatePetTransStats(base, work, pet->level, pet->pet_rank, cur_trans);

	// 7. 消耗辅助宠 (若有)
	if (sac_pet != nullptr)
	{
		s.pets.release(sac_h);
		p->pets[static_cast<std::size_t>(sacrifice_slot)] = {};
	}

	// 8. 重置等级为 1，回写新转生成长资质与基础数值
	pet->level = 1;
	pet->exp = 0;
	pet->growth_vital = static_cast<std::uint8_t>(new_growth.vital);
	pet->growth_str = static_cast<std::uint8_t>(new_growth.str);
	pet->growth_tough = static_cast<std::uint8_t>(new_growth.tough);
	pet->growth_dex = static_cast<std::uint8_t>(new_growth.dex);
	pet->vital = new_growth.vital;
	pet->str = new_growth.str;
	pet->tough = new_growth.tough;
	pet->dex = new_growth.dex;

	const auto stats = SA::Rules::deriveBaseStats(pet->vital, pet->str, pet->tough, pet->dex);
	pet->hp = stats.max_hp;

	setPetTransCount(pet->uid, cur_trans + 1);
	_impl->pet_progression[pet->uid].pet_id = pet->pet_id;
	return PetTransResultCode::kSuccess;
}

} // namespace SA::World
