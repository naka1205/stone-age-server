// src/world/WorldPartyTrade.cpp —— 组队、决斗与玩家间安全交易系统实现
//
// 对应原版 char_party.c, battle.c (PVP段), char/trade.c

#include "WorldImpl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace SA::World
{

// ══ 组队系统 (阶段 2: 队伍与协同) ══════════════════════════════════
bool World::joinParty(SA::Net::SessionId requester, SA::Net::SessionId target)
{
	Impl &s = *_impl;
	if (requester == target)
		return false;

	if (isPlayerVending(requester) || isPlayerVending(target))
		return false;

	// 1. 会话有效性与 L2 Player 实体检查
	if (s.conns.find(requester) == s.conns.end() || s.conns.find(target) == s.conns.end())
		return false;

	SA::Model::Player *req_p = s.players.resolve(s.player_of_session.find(requester));
	SA::Model::Player *tar_p = s.players.resolve(s.player_of_session.find(target));
	if (req_p == nullptr || tar_p == nullptr)
		return false;

	// 2. 存活与非战斗态检查 (移植 char_party.c:220)
	if (req_p->hp <= 0 || tar_p->hp <= 0)
		return false;
	if (s.inBattle(requester) || s.inBattle(target))
		return false;

	// 3. requester 必须处于无队伍状态 (char_party.c:152)
	if (s.partyModeOf(requester) != PartyMode::kNone)
		return false;

	// 4. 地图与距离检查: 同一地图且切比雪夫距离 <= 2 (移植 char_party.c:215 / npcutil.c:575)
	if (req_p->floor != tar_p->floor)
		return false;
	const int dx = std::abs(req_p->x - tar_p->x);
	const int dy = std::abs(req_p->y - tar_p->y);
	if (std::max(dx, dy) > 2)
		return false;

	// 5. 加入目标队伍或创建新队伍
	auto tar_it = s.party_of_session.find(target);
	if (tar_it == s.party_of_session.end())
	{
		// target 未组队，target 成为 leader，requester 成为 member (char_party.c:88-91)
		const std::uint64_t pid = s.next_party_id++;
		Impl::Party party{};
		party.party_id = pid;
		party.leader = target;
		party.members = {target, requester};
		s.parties[pid] = std::move(party);
		s.party_of_session[target] = pid;
		s.party_of_session[requester] = pid;
		return true;
	}

	// target 已在队伍中: 加入 target 所在队伍
	auto pit = s.parties.find(tar_it->second);
	if (pit == s.parties.end())
		return false;

	Impl::Party &party = pit->second;
	if (party.members.size() >= kPartyMaxMembers)
		return false; // 人数达上限 (CHAR_PARTYMAX = 5)

	party.members.push_back(requester);
	s.party_of_session[requester] = party.party_id;
	return true;
}

bool World::leaveParty(SA::Net::SessionId session)
{
	Impl &s = *_impl;
	auto it = s.party_of_session.find(session);
	if (it == s.party_of_session.end())
		return false;

	if (s.inBattle(session))
		return false;

	const std::uint64_t pid = it->second;
	auto pit = s.parties.find(pid);
	if (pit == s.parties.end())
	{
		s.party_of_session.erase(it);
		return false;
	}

	Impl::Party &party = pit->second;
	if (party.leader == session)
	{
		// 队长离开: 队伍整体解散 (移植 char_party.c:377 CHAR_DischargePartySub)
		for (auto mid : party.members)
		{
			s.party_of_session.erase(mid);
		}
		s.parties.erase(pit);
		return true;
	}

	// 队员离开: 从成员列表中移除 (char_party.c:485)
	auto mit = std::find(party.members.begin(), party.members.end(), session);
	if (mit != party.members.end())
	{
		party.members.erase(mit);
	}
	s.party_of_session.erase(it);

	// 若剩余成员仅剩队长一人，队伍解散 (char_party.c:534-545)
	if (party.members.size() <= 1)
	{
		s.party_of_session.erase(party.leader);
		s.parties.erase(pit);
	}
	return true;
}

bool World::kickPartyMember(SA::Net::SessionId leader, SA::Net::SessionId member)
{
	Impl &s = *_impl;
	if (leader == member)
		return false;

	if (s.inBattle(leader) || s.inBattle(member))
		return false;

	auto lit = s.party_of_session.find(leader);
	if (lit == s.party_of_session.end())
		return false;

	auto pit = s.parties.find(lit->second);
	if (pit == s.parties.end() || pit->second.leader != leader)
		return false; // 只有队长能踢人

	Impl::Party &party = pit->second;
	auto mit = std::find(party.members.begin(), party.members.end(), member);
	if (mit == party.members.end())
		return false; // 队员不在队伍中

	party.members.erase(mit);
	s.party_of_session.erase(member);

	if (party.members.size() <= 1)
	{
		s.party_of_session.erase(party.leader);
		s.parties.erase(pit);
	}
	return true;
}

PartyMode World::playerPartyMode(SA::Net::SessionId session) const noexcept
{
	return _impl->partyModeOf(session);
}

SA::Net::SessionId World::playerPartyLeader(SA::Net::SessionId session) const noexcept
{
	return _impl->partyLeaderOf(session);
}

std::vector<SA::Net::SessionId> World::playerPartyMembers(SA::Net::SessionId session) const
{
	return _impl->partyMembersOf(session);
}

std::size_t World::partyCount() const noexcept
{
	return _impl->parties.size();
}

// ══ 决斗切磋系统 (Duel / PVP System) ═════════════════════════════════════
bool World::requestDuel(SA::Net::SessionId requester, SA::Net::SessionId target)
{
	Impl &s = *_impl;
	if (requester == target)
		return false;

	if (isPlayerVending(requester) || isPlayerVending(target))
		return false;

	if (s.conns.find(requester) == s.conns.end() || s.conns.find(target) == s.conns.end())
		return false;

	SA::Model::Player *req_p = s.players.resolve(s.player_of_session.find(requester));
	SA::Model::Player *tar_p = s.players.resolve(s.player_of_session.find(target));
	if (req_p == nullptr || tar_p == nullptr)
		return false;

	// 存活检查
	if (req_p->hp <= 0 || tar_p->hp <= 0)
		return false;

	// 战斗态检查
	if (s.inBattle(requester) || s.inBattle(target))
		return false;

	// 地图与距离检查 (同一地图且切比雪夫距离 <= 2)
	if (req_p->floor != tar_p->floor)
		return false;
	const int dx = std::abs(req_p->x - tar_p->x);
	const int dy = std::abs(req_p->y - tar_p->y);
	if (std::max(dx, dy) > 2)
		return false;

	// 同队检查: 同队队员之间严禁决斗 (对齐官方 battle.c:3071 BATTLE_ERR_SAMEPARTY)
	const auto req_pid = s.party_of_session.find(requester);
	const auto tar_pid = s.party_of_session.find(target);
	if (req_pid != s.party_of_session.end() && tar_pid != s.party_of_session.end() &&
	    req_pid->second == tar_pid->second)
	{
		return false;
	}

	// 发起方与目标方若在队伍中，必须为队长，队员不可被单独发起或发起 (对齐官方 battle.c:3061)
	if (s.partyModeOf(requester) == PartyMode::kMember || s.partyModeOf(target) == PartyMode::kMember)
		return false;

	// 组装 Side 0 成员 (发起方全队)
	std::vector<SA::Net::SessionId> party0;
	if (s.partyModeOf(requester) == PartyMode::kLeader)
		party0 = s.partyMembersOf(requester);
	else
		party0 = {requester};

	// 组装 Side 1 成员 (目标方全队)
	std::vector<SA::Net::SessionId> party1;
	if (s.partyModeOf(target) == PartyMode::kLeader)
		party1 = s.partyMembersOf(target);
	else
		party1 = {target};

	// 检查双方阵营中是否有任何人处于战斗态
	for (auto m : party0)
		if (s.inBattle(m))
			return false;
	for (auto m : party1)
		if (s.inBattle(m))
			return false;

	// 中断双方阵营中可能存在的未完成交易
	for (auto m : party0)
		cancelTrade(m);
	for (auto m : party1)
		cancelTrade(m);

	// 构造对决战场
	SA::Rules::BattleField field{};
	for (std::size_t i = 0; i < party0.size() && i < SA::Rules::kBattlePlayerMax; ++i)
	{
		const auto sid = party0[i];
		field.at(static_cast<int>(i)) = makePlayerCombatant(
		    s.players.resolve(s.player_of_session.find(sid)),
		    playerEquipModifiers(sid),
		    s.getRidingPet(sid),
		    s.hasSpiritBlessing(sid));
	}
	for (std::size_t i = 0; i < party1.size() && i < SA::Rules::kBattlePlayerMax; ++i)
	{
		const auto sid = party1[i];
		field.at(static_cast<int>(SA::Rules::kSideOffset + i)) = makePlayerCombatant(
		    s.players.resolve(s.player_of_session.find(sid)),
		    playerEquipModifiers(sid),
		    s.getRidingPet(sid),
		    s.hasSpiritBlessing(sid));
	}

	const BattleId battle = startBattle(field);
	auto &b = s.battles[battle];
	b.is_pvp = true;
	b.dp_battle = true;

	for (std::size_t i = 0; i < party0.size() && i < SA::Rules::kBattlePlayerMax; ++i)
	{
		(void)joinBattle(battle, party0[i], static_cast<std::uint8_t>(i));
	}
	for (std::size_t i = 0; i < party1.size() && i < SA::Rules::kBattlePlayerMax; ++i)
	{
		(void)joinBattle(battle, party1[i], static_cast<std::uint8_t>(SA::Rules::kSideOffset + i));
	}
	return true;
}

bool World::isDuelBattle(BattleId battle) const noexcept
{
	const auto it = _impl->battles.find(battle);
	if (it != _impl->battles.end())
		return it->second.is_pvp;
	const auto done = _impl->finished_battles.find(battle);
	return done != _impl->finished_battles.end() && done->second.is_pvp;
}

// ══ 玩家间安全交易系统 (Trade System) ═════════════════════════════════════
bool World::requestTrade(SA::Net::SessionId requester, SA::Net::SessionId target)
{
	Impl &s = *_impl;
	if (requester == target)
		return false;

	if (isPlayerVending(requester) || isPlayerVending(target))
		return false;

	if (s.conns.find(requester) == s.conns.end() || s.conns.find(target) == s.conns.end())
		return false;

	SA::Model::Player *req_p = s.players.resolve(s.player_of_session.find(requester));
	SA::Model::Player *tar_p = s.players.resolve(s.player_of_session.find(target));
	if (req_p == nullptr || tar_p == nullptr)
		return false;

	if (req_p->hp <= 0 || tar_p->hp <= 0)
		return false;

	if (s.inBattle(requester) || s.inBattle(target))
		return false;

	if (s.trade_of_session.count(requester) > 0 || s.trade_of_session.count(target) > 0)
		return false;

	if (req_p->floor != tar_p->floor)
		return false;

	const int dx = std::abs(req_p->x - tar_p->x);
	const int dy = std::abs(req_p->y - tar_p->y);
	if (std::max(dx, dy) > 2)
		return false;

	// 若对方此前已向发起方发起过交易请求，双方意愿达成，直接接受进入交易
	auto it = s.pending_trade_requests.find(requester);
	if (it != s.pending_trade_requests.end() && it->second == target)
	{
		return acceptTrade(requester, target);
	}

	s.pending_trade_requests[target] = requester;
	return true;
}

bool World::acceptTrade(SA::Net::SessionId target, SA::Net::SessionId requester)
{
	Impl &s = *_impl;
	auto it = s.pending_trade_requests.find(target);
	if (it == s.pending_trade_requests.end() || it->second != requester)
		return false;

	s.pending_trade_requests.erase(it);

	if (s.conns.find(requester) == s.conns.end() || s.conns.find(target) == s.conns.end())
		return false;

	SA::Model::Player *req_p = s.players.resolve(s.player_of_session.find(requester));
	SA::Model::Player *tar_p = s.players.resolve(s.player_of_session.find(target));
	if (req_p == nullptr || tar_p == nullptr || req_p->hp <= 0 || tar_p->hp <= 0)
		return false;

	if (s.inBattle(requester) || s.inBattle(target))
		return false;

	if (s.trade_of_session.count(requester) > 0 || s.trade_of_session.count(target) > 0)
		return false;

	if (req_p->floor != tar_p->floor)
		return false;

	const int dx = std::abs(req_p->x - tar_p->x);
	const int dy = std::abs(req_p->y - tar_p->y);
	if (std::max(dx, dy) > 2)
		return false;

	const std::uint64_t tid = s.next_trade_id++;
	Impl::TradeSession ts{};
	ts.trade_id = tid;
	ts.player_a = requester;
	ts.player_b = target;
	s.trades[tid] = ts;
	s.trade_of_session[requester] = tid;
	s.trade_of_session[target] = tid;
	return true;
}

bool World::cancelTrade(SA::Net::SessionId session)
{
	Impl &s = *_impl;
	s.pending_trade_requests.erase(session);
	for (auto it = s.pending_trade_requests.begin(); it != s.pending_trade_requests.end();)
	{
		if (it->second == session)
			it = s.pending_trade_requests.erase(it);
		else
			++it;
	}

	auto it = s.trade_of_session.find(session);
	if (it == s.trade_of_session.end())
		return false;

	const std::uint64_t tid = it->second;
	auto tit = s.trades.find(tid);
	if (tit != s.trades.end())
	{
		s.trade_of_session.erase(tit->second.player_a);
		s.trade_of_session.erase(tit->second.player_b);
		s.trades.erase(tit);
	}
	else
	{
		s.trade_of_session.erase(it);
	}
	return true;
}

bool World::offerTradeItem(SA::Net::SessionId session, int inventory_slot)
{
	Impl &s = *_impl;
	auto it = s.trade_of_session.find(session);
	if (it == s.trade_of_session.end())
		return false;
	auto tit = s.trades.find(it->second);
	if (tit == s.trades.end())
		return false;

	Impl::TradeSession &ts = tit->second;
	const bool is_a = (ts.player_a == session);
	const bool locked = is_a ? ts.a_locked : ts.b_locked;
	if (locked)
		return false;

	if (inventory_slot < static_cast<int>(SA::Model::kStartItemArray) ||
	    static_cast<std::size_t>(inventory_slot) >= SA::Model::kMaxItemHave)
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	if (!p->items[static_cast<std::size_t>(inventory_slot)].valid() ||
	    s.items.resolve(p->items[static_cast<std::size_t>(inventory_slot)]) == nullptr)
		return false;

	auto &items = is_a ? ts.a_items : ts.b_items;
	if (std::find(items.begin(), items.end(), inventory_slot) != items.end())
		return false;

	items.push_back(inventory_slot);
	ts.a_confirmed = false;
	ts.b_confirmed = false;
	return true;
}

bool World::removeTradeItem(SA::Net::SessionId session, int inventory_slot)
{
	Impl &s = *_impl;
	auto it = s.trade_of_session.find(session);
	if (it == s.trade_of_session.end())
		return false;
	auto tit = s.trades.find(it->second);
	if (tit == s.trades.end())
		return false;

	Impl::TradeSession &ts = tit->second;
	const bool is_a = (ts.player_a == session);
	const bool locked = is_a ? ts.a_locked : ts.b_locked;
	if (locked)
		return false;

	auto &items = is_a ? ts.a_items : ts.b_items;
	auto vit = std::find(items.begin(), items.end(), inventory_slot);
	if (vit == items.end())
		return false;

	items.erase(vit);
	ts.a_confirmed = false;
	ts.b_confirmed = false;
	return true;
}

bool World::offerTradePet(SA::Net::SessionId session, int pet_slot)
{
	Impl &s = *_impl;
	auto it = s.trade_of_session.find(session);
	if (it == s.trade_of_session.end())
		return false;
	auto tit = s.trades.find(it->second);
	if (tit == s.trades.end())
		return false;

	Impl::TradeSession &ts = tit->second;
	const bool is_a = (ts.player_a == session);
	const bool locked = is_a ? ts.a_locked : ts.b_locked;
	if (locked)
		return false;

	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	if (!p->pets[static_cast<std::size_t>(pet_slot)].valid() ||
	    s.pets.resolve(p->pets[static_cast<std::size_t>(pet_slot)]) == nullptr)
		return false;

	auto &pets = is_a ? ts.a_pets : ts.b_pets;
	if (std::find(pets.begin(), pets.end(), pet_slot) != pets.end())
		return false;

	if (playerRidePetSlot(session) == pet_slot)
	{
		dismountPet(session);
	}

	pets.push_back(pet_slot);
	ts.a_confirmed = false;
	ts.b_confirmed = false;
	return true;
}

bool World::removeTradePet(SA::Net::SessionId session, int pet_slot)
{
	Impl &s = *_impl;
	auto it = s.trade_of_session.find(session);
	if (it == s.trade_of_session.end())
		return false;
	auto tit = s.trades.find(it->second);
	if (tit == s.trades.end())
		return false;

	Impl::TradeSession &ts = tit->second;
	const bool is_a = (ts.player_a == session);
	const bool locked = is_a ? ts.a_locked : ts.b_locked;
	if (locked)
		return false;

	auto &pets = is_a ? ts.a_pets : ts.b_pets;
	auto vit = std::find(pets.begin(), pets.end(), pet_slot);
	if (vit == pets.end())
		return false;

	pets.erase(vit);
	ts.a_confirmed = false;
	ts.b_confirmed = false;
	return true;
}

bool World::offerTradeGold(SA::Net::SessionId session, std::uint32_t gold)
{
	Impl &s = *_impl;
	auto it = s.trade_of_session.find(session);
	if (it == s.trade_of_session.end())
		return false;
	auto tit = s.trades.find(it->second);
	if (tit == s.trades.end())
		return false;

	Impl::TradeSession &ts = tit->second;
	const bool is_a = (ts.player_a == session);
	const bool locked = is_a ? ts.a_locked : ts.b_locked;
	if (locked)
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	if (static_cast<std::uint32_t>(std::max(0, p->gold)) < gold)
		return false;

	if (is_a)
		ts.a_gold = gold;
	else
		ts.b_gold = gold;

	ts.a_confirmed = false;
	ts.b_confirmed = false;
	return true;
}

bool World::lockTrade(SA::Net::SessionId session)
{
	Impl &s = *_impl;
	auto it = s.trade_of_session.find(session);
	if (it == s.trade_of_session.end())
		return false;
	auto tit = s.trades.find(it->second);
	if (tit == s.trades.end())
		return false;

	Impl::TradeSession &ts = tit->second;
	if (ts.player_a == session)
		ts.a_locked = true;
	else if (ts.player_b == session)
		ts.b_locked = true;
	else
		return false;

	return true;
}

bool World::unlockTrade(SA::Net::SessionId session)
{
	Impl &s = *_impl;
	auto it = s.trade_of_session.find(session);
	if (it == s.trade_of_session.end())
		return false;
	auto tit = s.trades.find(it->second);
	if (tit == s.trades.end())
		return false;

	Impl::TradeSession &ts = tit->second;
	if (ts.player_a != session && ts.player_b != session)
		return false;

	ts.a_locked = false;
	ts.b_locked = false;
	ts.a_confirmed = false;
	ts.b_confirmed = false;
	return true;
}

bool World::confirmTrade(SA::Net::SessionId session)
{
	Impl &s = *_impl;
	auto it = s.trade_of_session.find(session);
	if (it == s.trade_of_session.end())
		return false;
	auto tit = s.trades.find(it->second);
	if (tit == s.trades.end())
		return false;

	Impl::TradeSession &ts = tit->second;
	// 双方必须全部处于锁定状态方可确认 (char/trade.c:876 TRADE_SwapItem)
	if (!ts.a_locked || !ts.b_locked)
		return false;

	if (ts.player_a == session)
		ts.a_confirmed = true;
	else if (ts.player_b == session)
		ts.b_confirmed = true;
	else
		return false;

	if (!ts.a_confirmed || !ts.b_confirmed)
		return true; // 等待对方确认

	// ── 双方均已确认: 执行原子容量校验与资产互换 ────────────────────
	SA::Model::Player *pA = s.players.resolve(s.player_of_session.find(ts.player_a));
	SA::Model::Player *pB = s.players.resolve(s.player_of_session.find(ts.player_b));
	if (pA == nullptr || pB == nullptr || pA->hp <= 0 || pB->hp <= 0)
	{
		cancelTrade(session);
		return false;
	}

	if (pA->floor != pB->floor ||
	    std::max(std::abs(pA->x - pB->x), std::abs(pA->y - pB->y)) > 2)
	{
		cancelTrade(session);
		return false;
	}

	// 1. 背包容量前置预检
	int used_items_a = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
		if (pA->items[i].valid() && s.items.resolve(pA->items[i]) != nullptr)
			++used_items_a;

	int used_items_b = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
		if (pB->items[i].valid() && s.items.resolve(pB->items[i]) != nullptr)
			++used_items_b;

	const int kept_items_a = used_items_a - static_cast<int>(ts.a_items.size());
	const int kept_items_b = used_items_b - static_cast<int>(ts.b_items.size());
	const int max_inv_capacity = static_cast<int>(SA::Model::kMaxItemHave - SA::Model::kStartItemArray);

	if (kept_items_a + static_cast<int>(ts.b_items.size()) > max_inv_capacity)
		return false;
	if (kept_items_b + static_cast<int>(ts.a_items.size()) > max_inv_capacity)
		return false;

	// 2. 宠物栏容量前置预检
	int used_pets_a = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
		if (pA->pets[i].valid() && s.pets.resolve(pA->pets[i]) != nullptr)
			++used_pets_a;

	int used_pets_b = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
		if (pB->pets[i].valid() && s.pets.resolve(pB->pets[i]) != nullptr)
			++used_pets_b;

	const int kept_pets_a = used_pets_a - static_cast<int>(ts.a_pets.size());
	const int kept_pets_b = used_pets_b - static_cast<int>(ts.b_pets.size());
	if (kept_pets_a + static_cast<int>(ts.b_pets.size()) > static_cast<int>(SA::Model::kMaxPetHave))
		return false;
	if (kept_pets_b + static_cast<int>(ts.a_pets.size()) > static_cast<int>(SA::Model::kMaxPetHave))
		return false;

	// 3. 石币持有与上限前置预检
	if (static_cast<std::uint32_t>(std::max(0, pA->gold)) < ts.a_gold ||
	    static_cast<std::uint32_t>(std::max(0, pB->gold)) < ts.b_gold)
		return false;

	const std::int64_t new_gold_a = static_cast<std::int64_t>(pA->gold) - ts.a_gold + ts.b_gold;
	const std::int64_t new_gold_b = static_cast<std::int64_t>(pB->gold) - ts.b_gold + ts.a_gold;
	if (new_gold_a > maxHaveGold(0) || new_gold_b > maxHaveGold(0))
		return false;

	// 4. 抵押物真实存在性与槽位有效性校验
	for (int slot : ts.a_items)
		if (!pA->items[static_cast<std::size_t>(slot)].valid() ||
		    s.items.resolve(pA->items[static_cast<std::size_t>(slot)]) == nullptr)
			return false;
	for (int slot : ts.b_items)
		if (!pB->items[static_cast<std::size_t>(slot)].valid() ||
		    s.items.resolve(pB->items[static_cast<std::size_t>(slot)]) == nullptr)
			return false;

	for (int slot : ts.a_pets)
		if (!pA->pets[static_cast<std::size_t>(slot)].valid() ||
		    s.pets.resolve(pA->pets[static_cast<std::size_t>(slot)]) == nullptr)
			return false;
	for (int slot : ts.b_pets)
		if (!pB->pets[static_cast<std::size_t>(slot)].valid() ||
		    s.pets.resolve(pB->pets[static_cast<std::size_t>(slot)]) == nullptr)
			return false;

	// ── 全部预检通过: 执行原子互换事务 ──────────────────────────────
	const std::uint64_t tid = ts.trade_id;

	// A. 石币互换 (严格经唯一入口 GoldLedger 审计)
	if (ts.a_gold > 0)
	{
		(void)delGold(*pA, GoldReason::kTradeGive, static_cast<std::int32_t>(ts.a_gold), 0, tid, s);
		(void)addGold(*pB, GoldReason::kTradeReceive, static_cast<std::int32_t>(ts.a_gold), 0, tid, s);
	}
	if (ts.b_gold > 0)
	{
		(void)delGold(*pB, GoldReason::kTradeGive, static_cast<std::int32_t>(ts.b_gold), 0, tid, s);
		(void)addGold(*pA, GoldReason::kTradeReceive, static_cast<std::int32_t>(ts.b_gold), 0, tid, s);
	}

	// B. 道具互换
	std::vector<SA::Model::EntityHandle> handles_from_a;
	for (int slot : ts.a_items)
	{
		handles_from_a.push_back(pA->items[static_cast<std::size_t>(slot)]);
		pA->items[static_cast<std::size_t>(slot)] = SA::Model::kNullHandle;
	}

	std::vector<SA::Model::EntityHandle> handles_from_b;
	for (int slot : ts.b_items)
	{
		handles_from_b.push_back(pB->items[static_cast<std::size_t>(slot)]);
		pB->items[static_cast<std::size_t>(slot)] = SA::Model::kNullHandle;
	}

	std::size_t b_idx = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave && b_idx < handles_from_b.size(); ++i)
	{
		if (!pA->items[i].valid())
		{
			pA->items[i] = handles_from_b[b_idx++];
		}
	}

	std::size_t a_idx = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave && a_idx < handles_from_a.size(); ++i)
	{
		if (!pB->items[i].valid())
		{
			pB->items[i] = handles_from_a[a_idx++];
		}
	}

	// C. 宠物互换
	std::vector<SA::Model::EntityHandle> pet_handles_from_a;
	for (int slot : ts.a_pets)
	{
		pet_handles_from_a.push_back(pA->pets[static_cast<std::size_t>(slot)]);
		pA->pets[static_cast<std::size_t>(slot)] = SA::Model::kNullHandle;
		if (pA->default_pet == slot)
			pA->default_pet = -1;
	}

	std::vector<SA::Model::EntityHandle> pet_handles_from_b;
	for (int slot : ts.b_pets)
	{
		pet_handles_from_b.push_back(pB->pets[static_cast<std::size_t>(slot)]);
		pB->pets[static_cast<std::size_t>(slot)] = SA::Model::kNullHandle;
		if (pB->default_pet == slot)
			pB->default_pet = -1;
	}

	std::size_t pb_idx = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave && pb_idx < pet_handles_from_b.size(); ++i)
	{
		if (!pA->pets[i].valid())
		{
			pA->pets[i] = pet_handles_from_b[pb_idx++];
		}
	}

	std::size_t pa_idx = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave && pa_idx < pet_handles_from_a.size(); ++i)
	{
		if (!pB->pets[i].valid())
		{
			pB->pets[i] = pet_handles_from_a[pa_idx++];
		}
	}

	// D. 事务结束清理
	s.trade_of_session.erase(ts.player_a);
	s.trade_of_session.erase(ts.player_b);
	s.trades.erase(tit);
	return true;
}

TradeState World::playerTradeState(SA::Net::SessionId session) const noexcept
{
	auto it = _impl->trade_of_session.find(session);
	if (it == _impl->trade_of_session.end())
		return TradeState::kNone;
	auto tit = _impl->trades.find(it->second);
	if (tit == _impl->trades.end())
		return TradeState::kNone;
	const auto &ts = tit->second;
	const bool is_a = (ts.player_a == session);
	const bool self_locked = is_a ? ts.a_locked : ts.b_locked;
	const bool self_confirmed = is_a ? ts.a_confirmed : ts.b_confirmed;
	if (self_confirmed)
		return TradeState::kConfirmed;
	if (self_locked)
		return TradeState::kLocked;
	return TradeState::kTrading;
}

SA::Net::SessionId World::playerTradePartner(SA::Net::SessionId session) const noexcept
{
	auto it = _impl->trade_of_session.find(session);
	if (it == _impl->trade_of_session.end())
		return 0;
	auto tit = _impl->trades.find(it->second);
	if (tit == _impl->trades.end())
		return 0;
	return (tit->second.player_a == session) ? tit->second.player_b : tit->second.player_a;
}

std::optional<TradeStatus> World::playerTradeStatus(SA::Net::SessionId session) const
{
	auto it = _impl->trade_of_session.find(session);
	if (it == _impl->trade_of_session.end())
		return std::nullopt;
	auto tit = _impl->trades.find(it->second);
	if (tit == _impl->trades.end())
		return std::nullopt;

	const auto &ts = tit->second;
	const bool is_a = (ts.player_a == session);
	TradeStatus st{};
	st.state = playerTradeState(session);
	st.partner = is_a ? ts.player_b : ts.player_a;
	st.self_locked = is_a ? ts.a_locked : ts.b_locked;
	st.partner_locked = is_a ? ts.b_locked : ts.a_locked;
	st.self_confirmed = is_a ? ts.a_confirmed : ts.b_confirmed;
	st.partner_confirmed = is_a ? ts.b_confirmed : ts.a_confirmed;
	st.self_item_slots = is_a ? ts.a_items : ts.b_items;
	st.partner_item_slots = is_a ? ts.b_items : ts.a_items;
	st.self_pet_slots = is_a ? ts.a_pets : ts.b_pets;
	st.partner_pet_slots = is_a ? ts.b_pets : ts.a_pets;
	st.self_gold = is_a ? ts.a_gold : ts.b_gold;
	st.partner_gold = is_a ? ts.b_gold : ts.a_gold;
	return st;
}

std::size_t World::activeTradeCount() const noexcept
{
	return _impl->trades.size();
}

} // namespace SA::World
