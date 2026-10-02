// src/world/WorldRide.cpp —— 骑乘系统、庄园骑乘认证与骑宠契合度体系实现
//
// 对应原版 char/char.c:3990-4075, char/char_base.c:50, include/char_base.h:1567 tagRidePetTable, npc_riderman.c

#include "WorldImpl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace SA::World
{

namespace
{

inline std::int32_t computeRideImage(std::int32_t player_image, std::int32_t pet_image) noexcept
{
	const std::int32_t p_base = player_image >= 100000 ? (player_image - 100000) : player_image;
	const std::int32_t p_rem = p_base >= 0 ? (p_base % 500) : 0;
	const std::int32_t pet_rem = pet_image >= 0 ? (pet_image % 100) : 0;
	return 100700 + p_rem + pet_rem;
}

inline bool matchesManorPet(FamilyManor manor, const std::string &pet_name) noexcept
{
	if (pet_name.empty())
		return false;

	switch (manor)
	{
	case FamilyManor::kSamo:
		// 萨姆吉尔庄园: 暴龙系
		return pet_name.find("暴龙") != std::string::npos ||
		       pet_name.find("巴朵兰恩") != std::string::npos ||
		       pet_name.find("左迪洛斯") != std::string::npos ||
		       pet_name.find("奥卡洛斯") != std::string::npos ||
		       pet_name.find("帖拉所伊朵") != std::string::npos ||
		       pet_name.find("红暴") != std::string::npos ||
		       pet_name.find("机暴") != std::string::npos ||
		       pet_name.find("绿暴") != std::string::npos ||
		       pet_name.find("蓝暴") != std::string::npos ||
		       pet_name.find("暴") != std::string::npos;
	case FamilyManor::kMarina:
		// 玛丽娜斯庄园: 虎系
		return pet_name.find("虎") != std::string::npos ||
		       pet_name.find("佩露夏") != std::string::npos ||
		       pet_name.find("贝鲁卡") != std::string::npos ||
		       pet_name.find("格鲁西斯") != std::string::npos ||
		       pet_name.find("贝鲁伊卡") != std::string::npos;
	case FamilyManor::kJaja:
		// 加加庄园: 飞龙 / 加美系
		return pet_name.find("飞龙") != std::string::npos ||
		       pet_name.find("加美") != std::string::npos ||
		       pet_name.find("朵拉比斯") != std::string::npos ||
		       pet_name.find("飞飞") != std::string::npos ||
		       pet_name.find("布伊") != std::string::npos ||
		       pet_name.find("加宝格") != std::string::npos ||
		       pet_name.find("扑扑") != std::string::npos;
	case FamilyManor::kKarutana:
		// 卡鲁它那庄园: 雷龙系
		return pet_name.find("雷龙") != std::string::npos ||
		       pet_name.find("布拉奇多斯") != std::string::npos ||
		       pet_name.find("布鲁顿") != std::string::npos ||
		       pet_name.find("斯天多斯") != std::string::npos ||
		       pet_name.find("邦恩多斯") != std::string::npos;
	case FamilyManor::kNone:
		return false;
	}
	return false;
}

} // namespace

bool World::canPlayerRide(SA::Net::SessionId session, int pet_slot) const
{
	const auto pit = _impl->player_of_session.find(session);
	if (!pit.valid())
		return false;
	const auto *p = _impl->players.resolve(pit);
	if (p == nullptr)
		return false;
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return false;
	const auto ph = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!ph.valid())
		return false;
	const auto *pet = _impl->pets.resolve(ph);
	if (pet == nullptr)
		return false;

	// [RV-1] 濒死门禁: 骑宠生命值 hp <= 0 严格阻断上马
	if (pet->hp <= 0)
		return false;

	// 1. 资质检查: 通用骑乘学习证或特约骑宠证
	const auto permit_it = _impl->player_ride_permits.find(session);
	if (permit_it != _impl->player_ride_permits.end())
	{
		const auto &permits = permit_it->second;
		if (permits.find("骑乘学习证") != permits.end() ||
		    permits.find("骑乘许可") != permits.end())
		{
			return true;
		}
		const std::string pet_name = pet->name.c_str();
		for (const auto &permit : permits)
		{
			if (permit == pet_name ||
			    (!pet_name.empty() && permit.find(pet_name) != std::string::npos) ||
			    (!permit.empty() && pet_name.find(permit) != std::string::npos))
			{
				return true;
			}
		}
	}

	// 2. 庄园特权检查: 占领庄园的家族成员享有对应骑宠特权
	const std::uint32_t fam_id = playerFamilyId(session);
	if (fam_id != 0)
	{
		const FamilyManor manor = familyManor(fam_id);
		if (manor != FamilyManor::kNone)
		{
			const std::string pname = pet->name.c_str();
			if (matchesManorPet(manor, pname))
			{
				return true;
			}
		}
	}

	// 3. 庄园骑乘认证体系检查 (批次 §9.0.100)
	const auto cert_it = _impl->player_ride_certs.find(session);
	if (cert_it != _impl->player_ride_certs.end() && !cert_it->second.empty())
	{
		const auto &certs = cert_it->second;
		// 宗师全能认证持有者：所有合法骑宠均特许骑乘
		if (certs.find(RideCertType::kMaster) != certs.end())
		{
			return true;
		}

		const std::string pname = pet->name.c_str();
		// 萨姆吉尔庄园暴龙系认证
		if (matchesManorPet(FamilyManor::kSamo, pname) && certs.find(RideCertType::kManorSamo) != certs.end())
		{
			return true;
		}
		// 玛丽娜斯庄园绿暴/虎系认证
		if (matchesManorPet(FamilyManor::kMarina, pname) &&
		    (certs.find(RideCertType::kManorMarina) != certs.end() || certs.find(RideCertType::kBasic) != certs.end()))
		{
			return true;
		}
		// 加加庄园飞龙/加美系认证
		if (matchesManorPet(FamilyManor::kJaja, pname) && certs.find(RideCertType::kJaja) != certs.end())
		{
			return true;
		}
		// 卡鲁它那庄园雷龙系认证
		if (matchesManorPet(FamilyManor::kKarutana, pname) && certs.find(RideCertType::kKarutana) != certs.end())
		{
			return true;
		}
		// 基础认证：允许骑乘基础虎系、穿山甲系等基础坐骑
		if (certs.find(RideCertType::kBasic) != certs.end())
		{
			if (pname.find("虎") != std::string::npos ||
			    pname.find("佩露夏") != std::string::npos ||
			    pname.find("贝鲁卡") != std::string::npos ||
			    pname.find("格鲁西斯") != std::string::npos ||
			    pname.find("贝鲁伊卡") != std::string::npos ||
			    pname.find("穿山甲") != std::string::npos ||
			    pname.find("龟") != std::string::npos)
			{
				return true;
			}
		}
	}

	return false;
}

bool World::mountPet(SA::Net::SessionId session, int pet_slot)
{
	Impl &s = *_impl;
	if (s.inBattle(session))
		return false;
	if (isPlayerVending(session))
		return false;

	if (!canPlayerRide(session, pet_slot))
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	const auto ph = p->pets[static_cast<std::size_t>(pet_slot)];
	const auto *pet = s.pets.resolve(ph);
	if (pet == nullptr)
		return false;

	// 若已处于骑乘状态，先执行下马还原
	if (isPlayerRiding(session))
	{
		dismountPet(session);
	}

	// 出战宠互斥防护: 上马的宠物若是当前出战宠，自动重置 default_pet = -1
	if (p->default_pet == pet_slot)
	{
		p->default_pet = -1;
	}

	const std::int32_t orig_img = p->image;
	const std::int32_t ride_img = computeRideImage(orig_img, pet->base_image);
	p->image = ride_img;

	s.player_rides[session] = {pet_slot, orig_img};
	return true;
}

bool World::dismountPet(SA::Net::SessionId session)
{
	Impl &s = *_impl;
	auto it = s.player_rides.find(session);
	if (it == s.player_rides.end())
		return false;

	if (SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session)))
	{
		p->image = it->second.original_image;
	}
	s.player_rides.erase(it);
	return true;
}

bool World::isPlayerRiding(SA::Net::SessionId session) const
{
	return _impl->player_rides.find(session) != _impl->player_rides.end();
}

int World::playerRidePetSlot(SA::Net::SessionId session) const
{
	const auto it = _impl->player_rides.find(session);
	if (it == _impl->player_rides.end())
		return -1;
	return it->second.pet_slot;
}

std::optional<RideInfo> World::getPlayerRideInfo(SA::Net::SessionId session) const
{
	const auto it = _impl->player_rides.find(session);
	if (it == _impl->player_rides.end())
		return std::nullopt;

	const SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return std::nullopt;

	const int slot = it->second.pet_slot;
	if (slot < 0 || static_cast<std::size_t>(slot) >= SA::Model::kMaxPetHave)
		return std::nullopt;

	const auto *pet = _impl->pets.resolve(p->pets[static_cast<std::size_t>(slot)]);
	if (pet == nullptr)
		return std::nullopt;

	RideInfo info{};
	info.pet_slot = slot;
	info.original_image = it->second.original_image;
	info.ride_image = p->image;
	info.pet_name = pet->name.c_str();
	info.pet_level = pet->level;
	info.pet_hp = pet->hp;
	info.pet_max_hp = SA::Rules::deriveBaseStats(pet->vital, pet->str, pet->tough, pet->dex).max_hp;
	return info;
}

bool World::grantRidePermit(SA::Net::SessionId session, const std::string &permit_name)
{
	if (permit_name.empty())
		return false;
	auto &permits = _impl->player_ride_permits[session];
	const auto res = permits.insert(permit_name);
	return res.second;
}

bool World::revokeRidePermit(SA::Net::SessionId session, const std::string &permit_name)
{
	auto it = _impl->player_ride_permits.find(session);
	if (it == _impl->player_ride_permits.end())
		return false;
	const auto count = it->second.erase(permit_name);
	if (count == 0)
		return false;

	if (isPlayerRiding(session))
	{
		const int rslot = playerRidePetSlot(session);
		if (!canPlayerRide(session, rslot))
		{
			dismountPet(session);
		}
	}
	return true;
}

bool World::hasRidePermit(SA::Net::SessionId session, const std::string &permit_name) const
{
	const auto it = _impl->player_ride_permits.find(session);
	if (it == _impl->player_ride_permits.end())
		return false;
	return it->second.find(permit_name) != it->second.end();
}

std::vector<std::string> World::playerRidePermits(SA::Net::SessionId session) const
{
	std::vector<std::string> res;
	const auto it = _impl->player_ride_permits.find(session);
	if (it != _impl->player_ride_permits.end())
	{
		res.assign(it->second.begin(), it->second.end());
	}
	return res;
}

// ── 庄园骑乘认证体系 (批次 §9.0.100) ──────────────────────────────────

RideExamRequirement World::getRideExamRequirement(RideCertType cert_type) const
{
	RideExamRequirement req{};
	req.cert_type = cert_type;
	switch (cert_type)
	{
	case RideCertType::kBasic:
		req.cert_name = "基础骑乘学习认证";
		req.associated_manor = FamilyManor::kNone;
		req.required_level = 40;
		req.required_fame = 50;
		req.cost_gold = 5000;
		break;
	case RideCertType::kManorSamo:
		req.cert_name = "萨姆吉尔庄园暴龙骑乘认证";
		req.associated_manor = FamilyManor::kSamo;
		req.required_level = 80;
		req.required_fame = 200;
		req.cost_gold = 20000;
		break;
	case RideCertType::kManorMarina:
		req.cert_name = "玛丽娜斯庄园绿暴骑乘认证";
		req.associated_manor = FamilyManor::kMarina;
		req.required_level = 80;
		req.required_fame = 200;
		req.cost_gold = 20000;
		break;
	case RideCertType::kJaja:
		req.cert_name = "加加庄园飞龙骑乘认证";
		req.associated_manor = FamilyManor::kJaja;
		req.required_level = 80;
		req.required_fame = 200;
		req.cost_gold = 20000;
		break;
	case RideCertType::kKarutana:
		req.cert_name = "卡鲁它那庄园雷龙骑乘认证";
		req.associated_manor = FamilyManor::kKarutana;
		req.required_level = 80;
		req.required_fame = 200;
		req.cost_gold = 20000;
		break;
	case RideCertType::kMaster:
		req.cert_name = "宗师全能骑乘认证";
		req.associated_manor = FamilyManor::kNone;
		req.required_level = 120;
		req.required_fame = 1000;
		req.cost_gold = 100000;
		break;
	}
	return req;
}

RideExamResultCode World::takeRideExam(SA::Net::SessionId session, RideCertType cert_type)
{
	Impl &s = *_impl;
	const auto pit = s.player_of_session.find(session);
	if (!pit.valid())
		return RideExamResultCode::kInvalidSession;
	SA::Model::Player *p = s.players.resolve(pit);
	if (p == nullptr || p->hp <= 0)
		return RideExamResultCode::kPlayerDead;

	if (s.inBattle(session))
		return RideExamResultCode::kPlayerInBattle;
	if (isPlayerVending(session))
		return RideExamResultCode::kPlayerVending;

	if (hasRideCert(session, cert_type))
		return RideExamResultCode::kAlreadyCertified;

	const auto req = getRideExamRequirement(cert_type);
	if (p->level < req.required_level)
		return RideExamResultCode::kInsufficientLevel;
	if (playerFame(session) < req.required_fame)
		return RideExamResultCode::kInsufficientFame;
	if (p->gold < req.cost_gold)
		return RideExamResultCode::kInsufficientGold;

	// 严格通过 GoldLedger 扣除考核学费
	const GoldTx tx = delGold(*p, GoldReason::kRideExamFee, req.cost_gold, /*trans=*/0, /*corr=*/0, s);
	if (tx.disposition == GoldDisposition::kRejected)
		return RideExamResultCode::kInsufficientGold;

	// 对齐原版 npc_riderman.c:234,311,388,464: w.takegold / 5 (20%) 注资到对应庄园家族金库
	if (req.associated_manor != FamilyManor::kNone)
	{
		const auto fid = manorOwnerFamily(req.associated_manor);
		if (fid != 0)
		{
			auto fit = s.families.find(fid);
			if (fit != s.families.end())
			{
				const std::int32_t share = tx.applied / 5;
				fit->second.family_gold = static_cast<std::int32_t>(
				    std::min<std::uint64_t>(kMaxFamilyGold, static_cast<std::uint64_t>(fit->second.family_gold) + static_cast<std::uint64_t>(share)));
			}
		}
	}

	grantRideCert(session, cert_type);
	return RideExamResultCode::kSuccess;
}

bool World::grantRideCert(SA::Net::SessionId session, RideCertType cert_type)
{
	auto &certs = _impl->player_ride_certs[session];
	const auto res = certs.insert(cert_type);
	return res.second;
}

bool World::revokeRideCert(SA::Net::SessionId session, RideCertType cert_type)
{
	auto it = _impl->player_ride_certs.find(session);
	if (it == _impl->player_ride_certs.end())
		return false;
	const auto count = it->second.erase(cert_type);
	if (count == 0)
		return false;

	if (isPlayerRiding(session))
	{
		const int rslot = playerRidePetSlot(session);
		if (!canPlayerRide(session, rslot))
		{
			dismountPet(session);
		}
	}
	return true;
}

bool World::hasRideCert(SA::Net::SessionId session, RideCertType cert_type) const
{
	const auto it = _impl->player_ride_certs.find(session);
	if (it == _impl->player_ride_certs.end())
		return false;
	return it->second.find(cert_type) != it->second.end();
}

std::vector<RideCertType> World::playerRideCerts(SA::Net::SessionId session) const
{
	std::vector<RideCertType> res;
	const auto it = _impl->player_ride_certs.find(session);
	if (it != _impl->player_ride_certs.end())
	{
		res.assign(it->second.begin(), it->second.end());
	}
	return res;
}

std::optional<RideAffinityStats> World::calculateRideAffinity(SA::Net::SessionId session, int pet_slot) const
{
	const auto pit = _impl->player_of_session.find(session);
	if (!pit.valid())
		return std::nullopt;
	const auto *p = _impl->players.resolve(pit);
	if (p == nullptr)
		return std::nullopt;
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return std::nullopt;
	const auto ph = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!ph.valid())
		return std::nullopt;
	const auto *pet = _impl->pets.resolve(ph);
	if (pet == nullptr)
		return std::nullopt;

	// 1. 基准契合度: 宠物忠诚度基础折算 (0..100)
	int affinity = std::clamp(petLoyalty(pet->uid), 0, 100);

	// 2. 等级差修正: 角色驾驭能力
	// 若宠物等级高于角色，产生驾驭折损 (每高 1 级 -2%); 若角色高等级，产生驾驭增益 (每高 1 级 +1%, 最大 +10%)
	if (pet->level > p->level)
	{
		const int diff = pet->level - p->level;
		affinity -= diff * 2;
	}
	else
	{
		const int diff = std::min(10, p->level - pet->level);
		affinity += diff;
	}

	// 3. 认证专精加成
	const auto cert_it = _impl->player_ride_certs.find(session);
	if (cert_it != _impl->player_ride_certs.end())
	{
		const auto &certs = cert_it->second;
		if (certs.find(RideCertType::kMaster) != certs.end())
		{
			affinity += 20; // 宗师全能认证 +20%
		}
		else
		{
			const std::string pname = pet->name.c_str();
			if (matchesManorPet(FamilyManor::kSamo, pname) && certs.find(RideCertType::kManorSamo) != certs.end())
			{
				affinity += 15;
			}
			else if (matchesManorPet(FamilyManor::kMarina, pname) && certs.find(RideCertType::kManorMarina) != certs.end())
			{
				affinity += 15;
			}
			else if (matchesManorPet(FamilyManor::kJaja, pname) && certs.find(RideCertType::kJaja) != certs.end())
			{
				affinity += 15;
			}
			else if (matchesManorPet(FamilyManor::kKarutana, pname) && certs.find(RideCertType::kKarutana) != certs.end())
			{
				affinity += 15;
			}
			else if (certs.find(RideCertType::kBasic) != certs.end())
			{
				affinity += 5;
			}
		}
	}

	affinity = std::clamp(affinity, 10, 100);

	// 4. 四维属性共鸣折算 (对齐 char.c/battle.c 骑宠属性加成)
	const auto pet_stats = SA::Rules::deriveBaseStats(pet->vital, pet->str, pet->tough, pet->dex);
	RideAffinityStats stats{};
	stats.affinity_rate = affinity;
	stats.bonus_hp = (pet_stats.max_hp * affinity) / 100;
	stats.bonus_attack = (pet_stats.attack * affinity) / 200;
	stats.bonus_defense = (pet_stats.defense * affinity) / 200;
	stats.effective_dex = (p->dex * 60 + (pet_stats.quick * 40 * affinity) / 100) / 100;

	return stats;
}

} // namespace SA::World
