// src/world/WorldNpcDialog.cpp —— NPC 对话交互、任务剧情派发、商店买卖与技能导师
//
// 承担职责:
//   ① NPC 对话与事件入口派发 (onEvent: Healer, TownPeople, ExChangeMan, Shop, PetShop,
//      PetSkillShop, SignBoard, WarpMan, PetFusionMan, PetTransMan, FameShop, Craftsman, RideMaster)
//   ② 窗口交互与异步回调派发 (onWindowReply: 对话分支推进、任务接取与交付、商店购买、技能学习、传送确认)
//   ③ 商店买卖与技能导师业务实现 (sellItemToShop, buyPetFromShop, sellPetToShop, learnPetSkill)
//   ④ 任务兑换前置判定与副作用执行 (checkExChangePreconditions, applyExChangeEffects)
//   ⑤ NPC 实体管理与查询 (loadNpcEntities, npcCount, findNpc, playerHasActiveWindow, ...)

#include "WorldImpl.h"
#include "rules/Progression.h"
#include "world/Api.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace SA::World
{
namespace
{

// ── 批次 W.10: ExChangeMan 道具与宠物交付/奖励解析辅助 ─────────
struct ExchangeItemEntry
{
	std::int32_t item_id = 0;
	std::int32_t count = 1;
};

inline std::vector<ExchangeItemEntry> parseExchangeItems(std::string_view str)
{
	std::vector<ExchangeItemEntry> result;
	std::size_t start = 0;
	while (start < str.size())
	{
		const std::size_t comma = str.find(',', start);
		std::string_view part =
		    (comma == std::string_view::npos) ? str.substr(start) : str.substr(start, comma - start);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
			part.remove_prefix(1);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
			part.remove_suffix(1);
		if (!part.empty() && part != "EVDEL")
		{
			const std::size_t star = part.find('*');
			if (star != std::string_view::npos)
			{
				const int id = std::atoi(std::string(part.substr(0, star)).c_str());
				const int cnt = std::atoi(std::string(part.substr(star + 1)).c_str());
				if (id > 0)
					result.push_back({id, std::max(1, cnt)});
			}
			else
			{
				const int id = std::atoi(std::string(part).c_str());
				if (id > 0)
					result.push_back({id, 1});
			}
		}
		if (comma == std::string_view::npos)
			break;
		start = comma + 1;
	}
	return result;
}

struct ExchangePetEntry
{
	std::int32_t pet_id = 0;
	std::int32_t count = 1;
};

inline std::vector<ExchangePetEntry> parseExchangePets(std::string_view str)
{
	std::vector<ExchangePetEntry> result;
	std::size_t start = 0;
	while (start < str.size())
	{
		const std::size_t comma = str.find(',', start);
		std::string_view part =
		    (comma == std::string_view::npos) ? str.substr(start) : str.substr(start, comma - start);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
			part.remove_prefix(1);
		while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
			part.remove_suffix(1);
		if (!part.empty() && part != "EVDEL")
		{
			const std::size_t star = part.find('*');
			if (star != std::string_view::npos)
			{
				const int id = std::atoi(std::string(part.substr(0, star)).c_str());
				const int cnt = std::atoi(std::string(part.substr(star + 1)).c_str());
				if (id > 0)
					result.push_back({id, std::max(1, cnt)});
			}
			else
			{
				const int id = std::atoi(std::string(part).c_str());
				if (id > 0)
					result.push_back({id, 1});
			}
		}
		if (comma == std::string_view::npos)
			break;
		start = comma + 1;
	}
	return result;
}

inline std::vector<ExchangeItemEntry> resolveDelItems(const ExChangeBlock &blk, int branch_idx)
{
	auto dels = parseExchangeItems(blk.del_item);
	const std::string_view cond{blk.condition};
	if (blk.del_item.find("EVDEL") != std::string_view::npos && !cond.empty())
	{
		std::size_t start = 0;
		int cur_branch = 1;
		std::string_view matched_branch{};
		while (start < cond.size())
		{
			const std::size_t comma = cond.find(',', start);
			const std::string_view branch =
			    (comma == std::string_view::npos) ? cond.substr(start)
			                                      : cond.substr(start, comma - start);
			if (cur_branch == branch_idx)
			{
				matched_branch = branch;
				break;
			}
			++cur_branch;
			if (comma == std::string_view::npos)
				break;
			start = comma + 1;
		}
		if (!matched_branch.empty())
		{
			std::size_t astart = 0;
			while (astart < matched_branch.size())
			{
				const std::size_t amp = matched_branch.find('&', astart);
				const std::string_view atom =
				    (amp == std::string_view::npos) ? matched_branch.substr(astart)
				                                    : matched_branch.substr(astart, amp - astart);
				if (atom.find("ITEM") != std::string_view::npos &&
				    atom.find('=') != std::string_view::npos)
				{
					const std::size_t eq = atom.find('=');
					const std::string_view val = atom.substr(eq + 1);
					const std::size_t star = val.find('*');
					if (star != std::string_view::npos)
					{
						const int id = std::atoi(std::string(val.substr(0, star)).c_str());
						const int cnt = std::atoi(std::string(val.substr(star + 1)).c_str());
						if (id > 0)
							dels.push_back({id, std::max(1, cnt)});
					}
					else
					{
						const int id = std::atoi(std::string(val).c_str());
						if (id > 0)
							dels.push_back({id, 1});
					}
				}
				if (amp == std::string_view::npos)
					break;
				astart = amp + 1;
			}
		}
	}
	return dels;
}

inline std::vector<ExchangePetEntry> resolveDelPets(const ExChangeBlock &blk, int branch_idx)
{
	auto dels = parseExchangePets(blk.del_pet);
	const std::string_view cond{blk.condition};
	if (blk.del_pet.find("EVDEL") != std::string_view::npos && !cond.empty())
	{
		std::size_t start = 0;
		int cur_branch = 1;
		std::string_view matched_branch{};
		while (start < cond.size())
		{
			const std::size_t comma = cond.find(',', start);
			const std::string_view branch =
			    (comma == std::string_view::npos) ? cond.substr(start)
			                                      : cond.substr(start, comma - start);
			if (cur_branch == branch_idx)
			{
				matched_branch = branch;
				break;
			}
			++cur_branch;
			if (comma == std::string_view::npos)
				break;
			start = comma + 1;
		}
		if (!matched_branch.empty())
		{
			std::size_t astart = 0;
			while (astart < matched_branch.size())
			{
				const std::size_t amp = matched_branch.find('&', astart);
				const std::string_view atom =
				    (amp == std::string_view::npos) ? matched_branch.substr(astart)
				                                    : matched_branch.substr(astart, amp - astart);
				if (atom.find("PET") != std::string_view::npos)
				{
					const std::size_t hyphen = atom.find('-');
					if (hyphen != std::string_view::npos)
					{
						const std::string_view right = atom.substr(hyphen + 1);
						const std::size_t star = right.find('*');
						if (star != std::string_view::npos)
						{
							const int id = std::atoi(std::string(right.substr(0, star)).c_str());
							const int cnt = std::atoi(std::string(right.substr(star + 1)).c_str());
							if (id > 0)
								dels.push_back({id, std::max(1, cnt)});
						}
						else
						{
							const int id = std::atoi(std::string(right).c_str());
							if (id > 0)
								dels.push_back({id, 1});
						}
					}
					else if (atom.find('=') != std::string_view::npos)
					{
						const std::size_t eq = atom.find('=');
						const int id = std::atoi(std::string(atom.substr(eq + 1)).c_str());
						if (id > 0)
							dels.push_back({id, 1});
					}
				}
				if (amp == std::string_view::npos)
					break;
				astart = amp + 1;
			}
		}
	}
	return dels;
}

} // namespace

// ── ExChange 任务与背包/宠物槽位统计辅助 ─────────────────────────

std::int32_t World::Impl::countPlayerItems(const SA::Model::Player &p, std::int32_t item_id) const
{
	std::int32_t cnt = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (p.items[i].valid())
		{
			if (const auto *it = items.resolve(p.items[i]))
			{
				if (it->item_id == item_id)
				{
					cnt += std::max(1, it->current_pile);
				}
			}
		}
	}
	return cnt;
}

std::int32_t World::Impl::countPlayerPets(const SA::Model::Player &p, std::int32_t pet_id,
                                          std::int32_t min_lvl) const
{
	std::int32_t cnt = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
	{
		if (p.pets[i].valid())
		{
			if (const auto *pet = pets.resolve(p.pets[i]))
			{
				if (pet->pet_id == pet_id && pet->level >= min_lvl)
				{
					++cnt;
				}
			}
		}
	}
	return cnt;
}

std::int32_t World::Impl::countFreeItemSlots(const SA::Model::Player &p) const
{
	std::int32_t free_cnt = 0;
	for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
	{
		if (!p.items[i].valid())
			++free_cnt;
	}
	return free_cnt;
}

std::int32_t World::Impl::countFreePetSlots(const SA::Model::Player &p) const
{
	std::int32_t free_cnt = 0;
	for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
	{
		if (!p.pets[i].valid())
			++free_cnt;
	}
	return free_cnt;
}

void World::Impl::sendExChangeWindow(SA::Net::SessionId id, std::uint64_t npc_id,
                                     const std::string &raw_text, std::uint32_t buttons)
{
	auto it = conns.find(id);
	if (it == conns.end())
		return;

	SA::Domain::WindowOpen win{};
	win.window_id = ++next_window_id;
	win.kind = SA::Domain::WindowKind::WINDOW_KIND_MESSAGE;
	win.buttons = buttons;
	win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
	win.source.entity_id = static_cast<std::uint32_t>(npc_id);
	win.body_kind = SA::Domain::WindowOpen::BodyKind::MESSAGE;
	win.body.message.wide = false;

	std::size_t lstart = 0;
	while (lstart < raw_text.size() && win.body.message.lines.size() < 16)
	{
		const std::size_t nl = raw_text.find('\n', lstart);
		std::string line = (nl == std::string::npos) ? raw_text.substr(lstart)
		                                             : raw_text.substr(lstart, nl - lstart);
		if (line.size() > 255)
			line.resize(255);
		if (auto *slot = win.body.message.lines.push_back())
			slot->assign(line.data(), line.size());
		if (nl == std::string::npos)
			break;
		lstart = nl + 1;
	}
	if (win.body.message.lines.empty())
	{
		std::string line = raw_text;
		if (line.size() > 255)
			line.resize(255);
		if (auto *slot = win.body.message.lines.push_back())
			slot->assign(line.data(), line.size());
	}

	it->second.active_window_id = win.window_id;
	it->second.active_window_npc_id = static_cast<std::uint64_t>(npc_id);
	it->second.last_window_text = raw_text;
	sendTo(id, win);
}

bool World::Impl::checkExChangePreconditions(const SA::Model::Player &p,
                                             const ExChangeBlock &blk, int branch_idx,
                                             std::string &msg_out,
                                             SA::Net::SessionId session_id)
{
	// 1. 石币不足门 (DelStone vs p.gold)
	if (blk.del_stone > 0 && p.gold < blk.del_stone)
	{
		msg_out = !blk.stone_less_msg.empty() ? blk.stone_less_msg : "石币不足。";
		return false;
	}

	// 2. 石币超限门 (GetStone vs maxHaveGold)
	if (blk.get_stone > 0 && (p.gold + blk.get_stone > maxHaveGold(0)))
	{
		if (!blk.stone_full_msg.empty())
		{
			msg_out = blk.stone_full_msg;
			return false;
		}
	}

	// 3. 声望不足门 (DelFame)
	if (blk.del_fame > 0)
	{
		int fame = 0;
		const auto fit = player_extra_stats.find(session_id);
		if (fit != player_extra_stats.end())
			fame = fit->second.fame;
		if (fame < blk.del_fame)
		{
			msg_out = "声望不足。";
			return false;
		}
	}

	// 4. 交付道具持有校验 (必须在背包中拥有足够的道具)
	const auto req_dels = resolveDelItems(blk, branch_idx);
	for (const auto &d : req_dels)
	{
		if (countPlayerItems(p, d.item_id) < d.count)
		{
			msg_out = "缺少所需道具。";
			return false;
		}
	}

	// 5. 交付宠物持有校验 (必须在随行宠物中拥有足够的宠物)
	const auto req_del_pets = resolveDelPets(blk, branch_idx);
	for (const auto &d : req_del_pets)
	{
		if (countPlayerPets(p, d.pet_id, 0) < d.count)
		{
			msg_out = "缺少所需宠物。";
			return false;
		}
	}

	// 6. 背包容量门 (ItemFullCheck, 09 §4)
	if (!blk.get_item.empty())
	{
		const auto gets = parseExchangeItems(blk.get_item);
		std::int32_t get_slots = 0;
		for (const auto &g : gets)
			get_slots += g.count;

		const auto dels = req_dels;
		std::int32_t del_slots = 0;
		for (const auto &d : dels)
		{
			std::int32_t rem = d.count;
			for (std::size_t i = SA::Model::kStartItemArray;
			     i < SA::Model::kMaxItemHave && rem > 0; ++i)
			{
				if (p.items[i].valid())
				{
					if (const auto *item = items.resolve(p.items[i]))
					{
						if (item->item_id == d.item_id)
						{
							++del_slots;
							--rem;
						}
					}
				}
			}
		}

		const std::int32_t free_slots = countFreeItemSlots(p);
		if (free_slots + del_slots < get_slots)
		{
			msg_out = !blk.item_full_msg.empty() ? blk.item_full_msg : "道具栏已满。";
			return false;
		}
	}

	// 7. 宠物槽容量门 (PetFullCheck, 09 §4)
	if (!blk.get_pet.empty())
	{
		const auto gets = parseExchangePets(blk.get_pet);
		std::int32_t get_pet_slots = 0;
		for (const auto &g : gets)
			get_pet_slots += g.count;

		const auto dels = req_del_pets;
		std::int32_t del_pet_slots = 0;
		for (const auto &d : dels)
		{
			std::int32_t rem = d.count;
			for (std::size_t i = 0; i < SA::Model::kMaxPetHave && rem > 0; ++i)
			{
				if (p.pets[i].valid())
				{
					if (const auto *pet = pets.resolve(p.pets[i]))
					{
						if (pet->pet_id == d.pet_id)
						{
							++del_pet_slots;
							--rem;
						}
					}
				}
			}
		}

		const std::int32_t free_pet_slots = countFreePetSlots(p);
		if (free_pet_slots + del_pet_slots < get_pet_slots)
		{
			msg_out = !blk.pet_full_msg.empty() ? blk.pet_full_msg : "宠物栏已满。";
			return false;
		}
	}

	return true;
}

void World::Impl::applyExChangeEffects(SA::Net::SessionId id, SA::Model::Player &p,
                                       const ExChangeBlock &blk, int branch_idx)
{
	// ① 扣除石币 (必须走 delGold, 守卫 check_gold_writes)
	if (blk.del_stone > 0)
	{
		(void)delGold(p, GoldReason::kQuestFee, blk.del_stone, /*trans=*/0, /*corr=*/0, *this);
	}

	// ② 给予石币 (必须走 addGold, 守卫 check_gold_writes)
	if (blk.get_stone > 0)
	{
		(void)addGold(p, GoldReason::kQuestReward, blk.get_stone, /*trans=*/0, /*corr=*/0, *this);
	}

	// ③ 声望结算
	if (blk.del_fame > 0)
	{
		player_extra_stats[id].fame = std::max(0, player_extra_stats[id].fame - blk.del_fame);
	}
	if (blk.add_fame > 0)
	{
		player_extra_stats[id].fame += blk.add_fame;
	}

	// ④ 经验与属性点奖励
	if (blk.add_exp > 0)
	{
		p.exp += blk.add_exp;
	}
	if (blk.add_skill_points > 0)
	{
		p.skillup_points += blk.add_skill_points;
	}

	// ⑤ 生命与法力恢复
	if (blk.heal_hp > 0)
	{
		const auto max_hp = SA::Rules::deriveBaseStats(p.vital, p.str, p.tough, p.dex).max_hp;
		if (max_hp > 0)
			p.hp = std::min(max_hp, p.hp + blk.heal_hp);
		else
			p.hp += blk.heal_hp;
	}
	if (blk.heal_mp > 0)
	{
		if (p.max_mp > 0)
			p.mp = std::min(p.max_mp, p.mp + blk.heal_mp);
		else
			p.mp += blk.heal_mp;
	}

	// ⑥ 扣除道具
	const auto dels = resolveDelItems(blk, branch_idx);
	for (const auto &d : dels)
	{
		std::int32_t remaining = d.count;
		for (std::size_t i = SA::Model::kStartItemArray;
		     i < SA::Model::kMaxItemHave && remaining > 0; ++i)
		{
			if (p.items[i].valid())
			{
				auto *item = items.resolve(p.items[i]);
				if (item != nullptr && item->item_id == d.item_id)
				{
					if (item->current_pile > remaining)
					{
						item->current_pile -= remaining;
						remaining = 0;
					}
					else
					{
						remaining -= std::max(1, item->current_pile);
						const auto h = p.items[i];
						p.clearItemSlot(static_cast<int>(i));
						items.release(h);
					}
				}
			}
		}
	}

	// ⑦ 给予道具
	if (!blk.get_item.empty())
	{
		const auto gets = parseExchangeItems(blk.get_item);
		for (const auto &g : gets)
		{
			for (int c = 0; c < g.count; ++c)
			{
				SA::Model::Item new_item{};
				new_item.uid = ++next_window_id;
				new_item.item_id = g.item_id;
				new_item.current_pile = 1;
				new_item.use_pile_nums = 1;
				(void)giveItemIntoPlayer(p, new_item, items);
			}
		}
	}

	// ⑧ 扣除宠物
	const auto del_pets = resolveDelPets(blk, branch_idx);
	for (const auto &d : del_pets)
	{
		std::int32_t remaining = d.count;
		for (std::size_t i = 0; i < SA::Model::kMaxPetHave && remaining > 0; ++i)
		{
			if (p.pets[i].valid())
			{
				const auto *pet = pets.resolve(p.pets[i]);
				if (pet != nullptr && pet->pet_id == d.pet_id)
				{
					const auto h = p.pets[i];
					p.clearPetSlot(static_cast<int>(i));
					pets.release(h);
					--remaining;
				}
			}
		}
	}

	// ⑨ 给予宠物
	if (!blk.get_pet.empty())
	{
		const auto gets = parseExchangePets(blk.get_pet);
		for (const auto &g : gets)
		{
			for (int c = 0; c < g.count; ++c)
			{
				const int slot = p.findFreePetSlot();
				if (slot >= 0)
				{
					const SA::Model::EntityHandle ph = pets.allocate();
					if (ph.valid())
					{
						if (auto *pet_dst = pets.resolve(ph))
						{
							pet_dst->uid = ++next_window_id;
							pet_dst->pet_id = g.pet_id;
							pet_dst->level = 1;
							pet_dst->hp = 100;
							pet_dst->mp = 100;
							pet_dst->max_mp = 100;
							pet_dst->owner = player_of_session.find(id);
							p.pets[static_cast<std::size_t>(slot)] = ph;
						}
					}
				}
			}
		}
	}

	// ⑩ 旗标操作
	auto parse_and_apply_flags = [](std::string_view flg_str, auto &&func)
	{
		if (flg_str.empty())
			return;
		std::size_t start = 0;
		while (start < flg_str.size())
		{
			const std::size_t comma = flg_str.find(',', start);
			const std::string s_flag =
			    (comma == std::string_view::npos) ? std::string(flg_str.substr(start))
			                                      : std::string(flg_str.substr(start, comma - start));
			const int f = std::atoi(s_flag.c_str());
			if (f >= 0 && f < 256)
				func(f);
			if (comma == std::string_view::npos)
				break;
			start = comma + 1;
		}
	};

	parse_and_apply_flags(blk.end_set_flg, [&](int f)
	                      { p.setEndEvent(f); });
	parse_and_apply_flags(blk.set_now_flg, [&](int f)
	                      { p.setNowEvent(f); });
	parse_and_apply_flags(blk.clean_flg, [&](int f)
	                      { p.clearNowEvent(f); p.clearEndEvent(f); });
	parse_and_apply_flags(blk.clean_now_flg, [&](int f)
	                      { p.clearNowEvent(f); });
	parse_and_apply_flags(blk.clean_end_flg, [&](int f)
	                      { p.clearEndEvent(f); });

	if (blk.event_no != -1)
	{
		if (!blk.end_set_flg.empty())
			p.clearNowEvent(blk.event_no);
		else
			p.setNowEvent(blk.event_no);
	}

	// ⑪ 传送效果 (NpcWarp: floor,x,y)
	if (!blk.npc_warp.empty())
	{
		const std::size_t c1 = blk.npc_warp.find(',');
		if (c1 != std::string::npos)
		{
			const std::size_t c2 = blk.npc_warp.find(',', c1 + 1);
			if (c2 != std::string::npos)
			{
				const int wf = std::atoi(blk.npc_warp.substr(0, c1).c_str());
				const int wx = std::atoi(blk.npc_warp.substr(c1 + 1, c2 - c1 - 1).c_str());
				const int wy = std::atoi(blk.npc_warp.substr(c2 + 1).c_str());
				warpSinglePlayer(id, wf, wx, wy);
			}
		}
	}
}

// ── NPC 实体管理与查询 ──────────────────────────────────────────

void World::loadNpcEntities(std::vector<NpcEntity> npcs)
{
	for (auto &npc : npcs)
	{
		if (npc.born_x == 0 && npc.born_y == 0)
		{
			npc.born_x = npc.x;
			npc.born_y = npc.y;
		}
		if (npc.wander_interval_ms > 0 && npc.next_wander_at_ms == 0)
		{
			npc.next_wander_at_ms = _impl->now_ms + npc.wander_interval_ms;
		}
	}
	_impl->npc_entities = std::move(npcs);
}

std::size_t World::npcCount() const noexcept
{
	return _impl->npc_entities.size();
}

const NpcEntity *World::findNpc(std::uint64_t id) const noexcept
{
	for (const auto &npc : _impl->npc_entities)
	{
		if (npc.id == id)
			return &npc;
	}
	return nullptr;
}

bool World::playerHasActiveWindow(SA::Net::SessionId id) const noexcept
{
	const auto it = _impl->conns.find(id);
	return it != _impl->conns.end() && it->second.active_window_id != 0;
}

std::uint32_t World::playerActiveWindowId(SA::Net::SessionId id) const noexcept
{
	const auto it = _impl->conns.find(id);
	if (it != _impl->conns.end())
		return it->second.active_window_id;
	return 0;
}

std::string World::playerLastWindowText(SA::Net::SessionId id) const
{
	const auto it = _impl->conns.find(id);
	if (it != _impl->conns.end())
		return it->second.last_window_text;
	return {};
}

bool World::playerHasNowEvent(SA::Net::SessionId id, int flag) const noexcept
{
	const SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(id));
	if (p == nullptr)
		return false;
	return p->hasNowEvent(flag);
}

bool World::playerHasEndEvent(SA::Net::SessionId id, int flag) const noexcept
{
	const SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(id));
	if (p == nullptr)
		return false;
	return p->hasEndEvent(flag);
}

// ── 事件交互派发 (onEvent) ──────────────────────────────────────

void World::onEvent(SA::Net::SessionId id, const SA::Domain::EventRequest &req)
{
	// 移植 lssproto_EV_recv(callfromcli.c:1405)→ EVENT_main(event.c:37)净核:
	//   算面前格 → 扫该格事件对象 → 命中明雷则开战。★ 本批只接 ENTITY_ENEMY 一路
	//   (原版 functbl[event] 是通用派发,传送点 warppoint 等其他事件族为后续预留)。
	Impl &s = *_impl;
	if (s.partyModeOf(id) == PartyMode::kMember)
		return; // 队员不能自主触发事件与明雷开战 (移植 npc_npcenemy.c:358)
	bool ok = false;

	// 只处理明雷(ENTITY_ENEMY);其他 event_type ⇒ ok=false(未接的事件族,不报错、不断连)。
	if (req.event_type ==
	    static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_ENEMY))
	{
		const auto it = s.conns.find(id); // 1.5:SessionId == ConnectionId
		SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
		if (it != s.conns.end() && p != nullptr && req.dir < 8)
		{
			// 面前格 = 玩家**权威**坐标朝 dir 前一格(不信 req.x/y,同 onWalk 防瞬移;原版
			//   callfromcli.c:1402 CHAR_getCoordinationDir(dir, CHAR_X, CHAR_Y, 1, &fx, &fy))。
			const std::int32_t fx = p->x + kDirDelta[req.dir].dx;
			const std::int32_t fy = p->y + kDirDelta[req.dir].dy;
			const std::size_t we = s.worldEnemyAt(p->floor, fx, fy);
			if (we != s.world_enemies.size())
				ok = triggerNpcEnemyBattle(id, we);
		}
	}
	else if (req.event_type ==
	         static_cast<std::uint32_t>(SA::Domain::EntityType::ENTITY_NPC))
	{
		const auto it = s.conns.find(id);
		SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
		if (it != s.conns.end() && p != nullptr && req.dir < 8)
		{
			const std::int32_t fx = p->x + kDirDelta[req.dir].dx;
			const std::int32_t fy = p->y + kDirDelta[req.dir].dy;
			const std::size_t ni = s.npcAt(p->floor, fx, fy);
			if (ni != s.npc_entities.size())
			{
				const NpcEntity &npc = s.npc_entities[ni];
				if (npc.type == NpcType::kHealer)
				{
					// 1. 检查并扣除费用 (唯一写入口 GoldLedger, DR-EC3 余额不足拒绝)
					bool can_pay = true;
					if (npc.cost > 0)
					{
						if (p->gold < npc.cost)
						{
							can_pay = false;
						}
						else
						{
							const GoldTx tx = delGold(*p, GoldReason::kHealerFee, npc.cost,
							                          /*trans=*/0, static_cast<std::uint64_t>(id), s);
							if (tx.disposition != GoldDisposition::kApplied)
								can_pay = false;
						}
					}

					// 2. 满状态恢复 (原版 NPC_HealerAllHeal, npc_healer.c:109-141)
					if (can_pay)
					{
						// 玩家自身满血满蓝
						const auto p_stats = SA::Rules::deriveBaseStats(p->vital, p->str,
						                                                p->tough, p->dex);
						p->hp = p_stats.max_hp > 0 ? p_stats.max_hp : std::max(p->hp, 1);
						p->mp = p->max_mp;

						// 随行宠物满血满蓝
						for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
						{
							if (p->pets[i].valid())
							{
								SA::Model::Pet *pet = s.pets.resolve(p->pets[i]);
								if (pet != nullptr)
								{
									const auto pet_stats = SA::Rules::deriveBaseStats(
									    pet->vital, pet->str, pet->tough, pet->dex);
									pet->hp = pet_stats.max_hp > 0 ? pet_stats.max_hp : std::max(pet->hp, 1);
									pet->mp = pet->max_mp;
								}
							}
						}
						ok = true;
					}
				}
				else if (npc.type == NpcType::kTownPeople)
				{
					// 城镇居民对话 (原版 npc_townpeople.c:28-52)
					// 1. 切分逗号分隔的文案候选
					std::vector<std::string> candidates;
					std::size_t start = 0;
					while (start < npc.message.size())
					{
						const std::size_t comma = npc.message.find(',', start);
						if (comma == std::string::npos)
						{
							candidates.push_back(npc.message.substr(start));
							break;
						}
						candidates.push_back(npc.message.substr(start, comma - start));
						start = comma + 1;
					}
					if (candidates.empty() && !npc.message.empty())
						candidates.push_back(npc.message);

					// 2. 选择文案(多条文案按 world_rng 随机摇选，对应原版 rand() % tokennum + 1)
					std::string chosen;
					if (!candidates.empty())
					{
						if (candidates.size() == 1)
						{
							chosen = candidates[0];
						}
						else
						{
							const std::size_t idx = static_cast<std::size_t>(
							    s.world_rng.randMod(static_cast<int>(candidates.size())));
							chosen = candidates[idx];
						}
					}

					// 3. 组装并下发 WindowOpen 消息 (kind = MESSAGE, buttons = OK)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_MESSAGE;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::MESSAGE;
					win.body.message.wide = false;

					// 按换行符切分为多行 (MessageBody.lines 最多 16 行，每行最大 255 字符)
					std::size_t lstart = 0;
					while (lstart < chosen.size() && win.body.message.lines.size() < 16)
					{
						const std::size_t nl = chosen.find('\n', lstart);
						std::string line = (nl == std::string::npos)
						                       ? chosen.substr(lstart)
						                       : chosen.substr(lstart, nl - lstart);
						if (line.size() > 255)
							line.resize(255);
						if (auto *slot = win.body.message.lines.push_back())
							slot->assign(line.data(), line.size());
						if (nl == std::string::npos)
							break;
						lstart = nl + 1;
					}
					if (win.body.message.lines.empty())
					{
						std::string line = chosen;
						if (line.size() > 255)
							line.resize(255);
						if (auto *slot = win.body.message.lines.push_back())
							slot->assign(line.data(), line.size());
					}

					// 记录窗口会话状态 (DR-PR3)
					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = chosen;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kExChangeMan)
				{
					// ExChangeMan 任务事件 NPC (原版 npc_exchangeman.c, 09 §4)
					int matched_block_idx = -1;
					int matched_branch_idx = 0;

					struct ExChangeEvalUserData
					{
						const World::Impl *impl = nullptr;
						const World *world = nullptr;
						SA::Net::SessionId session = 0;
					};
					ExChangeEvalUserData eval_ud{&s, this, id};

					auto count_item_cb = [](const SA::Model::Player &pl, std::int32_t item_id,
					                        void *userdata) -> std::int32_t
					{
						return static_cast<const ExChangeEvalUserData *>(userdata)->impl->countPlayerItems(
						    pl, item_id);
					};
					auto count_pet_cb = [](const SA::Model::Player &pl, std::int32_t pet_id,
					                       std::int32_t min_lvl, void *userdata) -> std::int32_t
					{
						return static_cast<const ExChangeEvalUserData *>(userdata)->impl->countPlayerPets(
						    pl, pet_id, min_lvl);
					};
					auto count_free_items_cb = [](const SA::Model::Player &pl,
					                              void *userdata) -> std::int32_t
					{
						return static_cast<const ExChangeEvalUserData *>(userdata)->impl->countFreeItemSlots(pl);
					};
					auto count_free_pets_cb = [](const SA::Model::Player &pl,
					                             void *userdata) -> std::int32_t
					{
						return static_cast<const ExChangeEvalUserData *>(userdata)->impl->countFreePetSlots(pl);
					};
					auto get_trans_cb = [](const SA::Model::Player &, void *userdata) -> std::int32_t
					{
						const auto *ud = static_cast<const ExChangeEvalUserData *>(userdata);
						const auto it = ud->impl->player_extra_stats.find(ud->session);
						return (it != ud->impl->player_extra_stats.end()) ? it->second.transmigration : 0;
					};
					auto get_fame_cb = [](const SA::Model::Player &, void *userdata) -> std::int32_t
					{
						const auto *ud = static_cast<const ExChangeEvalUserData *>(userdata);
						const auto it = ud->impl->player_extra_stats.find(ud->session);
						return (it != ud->impl->player_extra_stats.end()) ? it->second.fame : 0;
					};
					auto get_fm_cb = [](const SA::Model::Player &, void *userdata) -> std::uint32_t
					{
						const auto *ud = static_cast<const ExChangeEvalUserData *>(userdata);
						return ud->world->playerFamilyId(ud->session);
					};

					const EventCheckContext check_ctx{*p, count_item_cb, count_pet_cb,
					                                  count_free_items_cb, count_free_pets_cb,
					                                  get_trans_cb, get_fame_cb, get_fm_cb, &eval_ud};

					for (std::size_t bi = 0; bi < npc.exchange_blocks.size(); ++bi)
					{
						const auto &blk = npc.exchange_blocks[bi];
						// 前置门: 若 event_no != -1 且已完成, 跳过该块 (C21)
						if (blk.event_no != -1 && p->hasEndEvent(blk.event_no))
							continue;

						const int branch = evaluateEventCondition(blk.condition, check_ctx);
						if (branch > 0)
						{
							matched_block_idx = static_cast<int>(bi);
							matched_branch_idx = branch;
							break;
						}
					}

					if (matched_block_idx >= 0)
					{
						const auto &blk = npc.exchange_blocks[static_cast<std::size_t>(matched_block_idx)];
						std::string door_msg;
						if (!s.checkExChangePreconditions(*p, blk, matched_branch_idx, door_msg, id))
						{
							it->second.pending_exchange = {};
							s.sendExChangeWindow(id, npc.id, door_msg,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							ok = true;
						}
						else if (blk.type == ExChangeType::kMessage)
						{
							// 立即结算全部副作用 (石币/道具/宠物/旗标/经验/点数/血蓝/传送)
							s.applyExChangeEffects(id, *p, blk, matched_branch_idx);

							std::string msg = blk.nomal_window_msg;
							if (msg.empty())
								msg = blk.nomal_msg;
							if (msg.empty())
								msg = blk.thanks_msg;

							if (blk.next_block_index >= 0)
							{
								it->second.pending_exchange = {npc.id, matched_block_idx, matched_branch_idx};
							}
							else
							{
								it->second.pending_exchange = {};
							}
							s.sendExChangeWindow(id, npc.id, msg,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							ok = true;
						}
						else if (blk.type == ExChangeType::kAccept)
						{
							// 弹出接取/交付确认窗
							std::string msg = blk.accept_msg;
							if (msg.empty())
								msg = blk.nomal_window_msg;
							if (msg.empty())
								msg = blk.nomal_msg;

							const std::uint32_t buttons =
							    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
							    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
							s.sendExChangeWindow(id, npc.id, msg, buttons);
							it->second.pending_exchange = {npc.id, matched_block_idx, matched_branch_idx};
							ok = true;
						}
						else if (blk.type == ExChangeType::kRequest)
						{
							// 09 §4: 委托型。已在进行中走进度文案(OK)，未接取走接取文案(YES/NO)
							if (blk.event_no != -1 && p->hasNowEvent(blk.event_no))
							{
								std::string msg = blk.nomal_window_msg;
								if (msg.empty())
									msg = blk.nomal_msg;
								if (msg.empty())
									msg = "任务正在进行中，请加油！";

								it->second.pending_exchange = {};
								s.sendExChangeWindow(id, npc.id, msg,
								                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
								ok = true;
							}
							else
							{
								std::string msg = blk.request_msg;
								if (msg.empty())
									msg = blk.accept_msg;
								if (msg.empty())
									msg = blk.nomal_window_msg;
								if (msg.empty())
									msg = blk.nomal_msg;

								const std::uint32_t buttons =
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
								s.sendExChangeWindow(id, npc.id, msg, buttons);
								it->second.pending_exchange = {npc.id, matched_block_idx, matched_branch_idx};
								ok = true;
							}
						}
						else if (blk.type == ExChangeType::kClean)
						{
							// 09 §4: 清除旗标型
							std::string msg = blk.nomal_window_msg;
							if (msg.empty())
								msg = blk.nomal_msg;
							if (msg.empty())
								msg = "是否确定放弃任务并清除任务记录？";

							const std::uint32_t buttons =
							    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
							    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
							s.sendExChangeWindow(id, npc.id, msg, buttons);
							it->second.pending_exchange = {npc.id, matched_block_idx, matched_branch_idx};
							ok = true;
						}
					}
					else if (!npc.nomal_main_msg.empty())
					{
						// 全部块不满足 ⇒ 随机选择兜底对白
						std::vector<std::string> candidates;
						std::size_t start = 0;
						while (start < npc.nomal_main_msg.size())
						{
							const std::size_t comma = npc.nomal_main_msg.find(',', start);
							if (comma == std::string::npos)
							{
								candidates.push_back(npc.nomal_main_msg.substr(start));
								break;
							}
							candidates.push_back(npc.nomal_main_msg.substr(start, comma - start));
							start = comma + 1;
						}
						std::string chosen;
						if (candidates.size() == 1)
							chosen = candidates[0];
						else if (!candidates.empty())
						{
							const std::size_t idx = static_cast<std::size_t>(
							    s.world_rng.randMod(static_cast<int>(candidates.size())));
							chosen = candidates[idx];
						}
						else
							chosen = npc.nomal_main_msg;

						it->second.pending_exchange = {};
						s.sendExChangeWindow(id, npc.id, chosen,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						ok = true;
					}
				}
				else if (npc.type == NpcType::kShop)
				{
					// 商店 NPC 交互 (批次 W.12, 移植 npc_itemshop.c)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_ITEM_SHOP;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::SHOP;

					auto &shop = win.body.shop;
					shop.header.can_buy = true;
					shop.header.reuse_previous = false;

					std::string shop_name = npc.shop_name.empty() ? "道具商店" : npc.shop_name;
					if (shop_name.size() > 63)
						shop_name.resize(63);
					shop.header.shop_name.assign(shop_name.data(), shop_name.size());

					std::string msg = npc.main_msg.empty() ? "欢迎光临！请选择你要购买的道具。" : npc.main_msg;
					if (msg.size() > 255)
						msg.resize(255);
					shop.header.message.assign(msg.data(), msg.size());

					std::string full_msg = npc.item_full_msg.empty() ? "道具栏已满！" : npc.item_full_msg;
					if (full_msg.size() > 255)
						full_msg.resize(255);
					shop.header.item_full_message.assign(full_msg.data(), full_msg.size());

					// 填充在售道具列表 (最多 32 个)
					const std::size_t limit = std::min<std::size_t>(npc.shop_products.size(), 32);
					for (std::size_t i = 0; i < limit; ++i)
					{
						const auto &prod = npc.shop_products[i];
						if (auto *entry = shop.entries.push_back())
						{
							entry->entry_id = static_cast<std::uint32_t>(i + 1); // 1-based ID
							entry->item_id = static_cast<std::uint32_t>(prod.item_id);
							entry->image_id = prod.image_id;
							entry->level = prod.level;
							entry->price = std::max(1, static_cast<std::int32_t>(prod.cost * npc.buy_rate));
							entry->purchasable = (p->gold >= entry->price);
						}
					}

					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = msg;
					it->second.pending_shop.npc_id = npc.id;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kPetShop)
				{
					// 宠物商店 NPC 交互 (批次 W.13, 移植 npc_petshop.c)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_ITEM_SHOP;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::SHOP;

					auto &shop = win.body.shop;
					shop.header.can_buy = true;
					shop.header.reuse_previous = false;

					std::string shop_name = npc.shop_name.empty() ? "宠物商店" : npc.shop_name;
					if (shop_name.size() > 63)
						shop_name.resize(63);
					shop.header.shop_name.assign(shop_name.data(), shop_name.size());

					std::string msg = npc.main_msg.empty() ? "欢迎光临宠物商店！请挑选您心仪的宠物。" : npc.main_msg;
					if (msg.size() > 255)
						msg.resize(255);
					shop.header.message.assign(msg.data(), msg.size());

					std::string full_msg = npc.pet_full_msg.empty() ? "宠物栏已满！" : npc.pet_full_msg;
					if (full_msg.size() > 255)
						full_msg.resize(255);
					shop.header.item_full_message.assign(full_msg.data(), full_msg.size());

					// 填充在售宠物列表 (最多 32 个)
					const std::size_t limit = std::min<std::size_t>(npc.pet_products.size(), 32);
					for (std::size_t i = 0; i < limit; ++i)
					{
						const auto &prod = npc.pet_products[i];
						if (auto *entry = shop.entries.push_back())
						{
							entry->entry_id = static_cast<std::uint32_t>(i + 1); // 1-based ID
							entry->item_id = static_cast<std::uint32_t>(prod.pet_id);
							entry->image_id = static_cast<std::uint32_t>(prod.image);
							entry->level = static_cast<std::uint32_t>(prod.level);
							entry->price = std::max(1, static_cast<std::int32_t>(prod.cost * npc.buy_rate));
							entry->purchasable = (p->gold >= entry->price);
						}
					}

					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = msg;
					it->second.pending_pet_shop.npc_id = npc.id;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kPetSkillShop)
				{
					// 宠物技能导师 NPC 交互 (批次 W.13, 移植 npc_petskillshop.c)
					SA::Domain::WindowOpen win{};
					win.window_id = ++s.next_window_id;
					win.kind = SA::Domain::WindowKind::WINDOW_KIND_PET_SKILL_SHOP;
					win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
					win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
					win.source.entity_id = static_cast<std::uint32_t>(npc.id);
					win.body_kind = SA::Domain::WindowOpen::BodyKind::SHOP;

					auto &shop = win.body.shop;
					shop.header.can_buy = true;
					shop.header.reuse_previous = false;

					std::string shop_name = npc.shop_name.empty() ? "宠物技能导师" : npc.shop_name;
					if (shop_name.size() > 63)
						shop_name.resize(63);
					shop.header.shop_name.assign(shop_name.data(), shop_name.size());

					std::string msg = npc.main_msg.empty() ? "你好！我可以传授你的宠物强大的技能。" : npc.main_msg;
					if (msg.size() > 255)
						msg.resize(255);
					shop.header.message.assign(msg.data(), msg.size());

					std::string full_msg = npc.skill_full_msg.empty() ? "宠物技能栏已满！" : npc.skill_full_msg;
					if (full_msg.size() > 255)
						full_msg.resize(255);
					shop.header.item_full_message.assign(full_msg.data(), full_msg.size());

					// 填充教授技能列表 (最多 32 个)
					const std::size_t limit = std::min<std::size_t>(npc.pet_skill_products.size(), 32);
					for (std::size_t i = 0; i < limit; ++i)
					{
						const auto &prod = npc.pet_skill_products[i];
						if (auto *entry = shop.entries.push_back())
						{
							entry->entry_id = static_cast<std::uint32_t>(i + 1); // 1-based ID
							entry->item_id = static_cast<std::uint32_t>(prod.skill_id);
							entry->image_id = 0;
							entry->level = static_cast<std::uint32_t>(prod.level);
							entry->price = std::max(1, static_cast<std::int32_t>(prod.cost * npc.buy_rate));
							entry->purchasable = (p->gold >= entry->price);
						}
					}

					it->second.active_window_id = win.window_id;
					it->second.active_window_npc_id = npc.id;
					it->second.last_window_text = msg;
					it->second.pending_pet_skill_shop.npc_id = npc.id;

					s.sendTo(id, win);
					ok = true;
				}
				else if (npc.type == NpcType::kSignBoard)
				{
					// 告示牌 NPC 交互 (批次 W.14, 移植 npc_signboard.c)
					std::string title = npc.sign_title.empty() ? "＜　看板　＞\n" : (npc.sign_title + "\n");
					std::string body = npc.message.empty() ? npc.name : npc.message;
					std::string sign_text = title + body;
					s.sendExChangeWindow(id, npc.id, sign_text,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kWarpMan)
				{
					// 传送员 NPC 交互 (批次 W.14, 移植 npc_warpman.c)
					if (npc.warp_destinations.empty())
					{
						std::string msg = npc.warp_msg.empty() ? "暂无可以前往的目的地。" : npc.warp_msg;
						s.sendExChangeWindow(id, npc.id, msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						ok = true;
					}
					else if (npc.warp_destinations.size() == 1)
					{
						// 单目的地: 弹出确认窗口 (YES / NO)
						const auto &dest = npc.warp_destinations[0];
						std::string msg = npc.warp_msg;
						if (msg.empty())
						{
							msg = "确定要前往 " + dest.name + " 吗？需要花费 " + std::to_string(dest.cost) + " 石币。";
						}
						it->second.pending_warpman.npc_id = npc.id;
						it->second.pending_warpman.dest_idx = 0;
						s.sendExChangeWindow(id, npc.id, msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
						                         static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO));
						ok = true;
					}
					else
					{
						// 多目的地: 弹出 SELECT 窗口
						SA::Domain::WindowOpen win{};
						win.window_id = ++s.next_window_id;
						win.kind = SA::Domain::WindowKind::WINDOW_KIND_SELECT;
						win.buttons = static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL);
						win.source.source = SA::Domain::EntitySource::ENTITY_SOURCE_ENTITY;
						win.source.entity_id = static_cast<std::uint32_t>(npc.id);
						win.body_kind = SA::Domain::WindowOpen::BodyKind::SELECT;

						std::string msg = npc.warp_msg.empty() ? "请选择你想前往的目的地：" : npc.warp_msg;
						if (msg.size() > 255)
							msg.resize(255);
						if (auto *slot = win.body.select.lines.push_back())
							slot->assign(msg.data(), msg.size());

						const std::size_t limit = std::min<std::size_t>(npc.warp_destinations.size(), 32);
						for (std::size_t i = 0; i < limit; ++i)
						{
							const auto &dest = npc.warp_destinations[i];
							if (auto *choice = win.body.select.choices.push_back())
							{
								choice->choice_id = static_cast<std::uint32_t>(i + 1); // 1-based choice_id
								std::string item_text = dest.name;
								if (dest.cost > 0)
								{
									item_text += " (" + std::to_string(dest.cost) + "石币)";
								}
								if (item_text.size() > 255)
									item_text.resize(255);
								choice->text.assign(item_text.data(), item_text.size());
								choice->enabled = (p->gold >= dest.cost && p->level >= dest.level);
							}
						}

						it->second.active_window_id = win.window_id;
						it->second.active_window_npc_id = npc.id;
						it->second.last_window_text = msg;
						it->second.pending_warpman.npc_id = npc.id;
						it->second.pending_warpman.dest_idx = -1;

						s.sendTo(id, win);
						ok = true;
					}
				}
				else if (npc.type == NpcType::kPetFusionMan)
				{
					// 宠物融合师 NPC 交互 (批次 §9.0.96)
					std::string msg = npc.message.empty() ? "欢迎来到宠物融合所！请选择要融合的主宠与副宠。" : npc.message;
					s.sendExChangeWindow(id, npc.id, msg,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kPetTransMan)
				{
					// 宠物转生师 NPC 交互 (批次 §9.0.96)
					std::string msg = npc.message.empty() ? "我是宠物转生师。只有达到 100 级以上的忠诚宠物才能进行转生仪式。" : npc.message;
					s.sendExChangeWindow(id, npc.id, msg,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kFameShop)
				{
					// 声望商城 / 荣誉兑换使者 NPC 交互 (批次 §9.0.98)
					std::string msg = npc.message.empty() ? "欢迎来到荣誉殿堂！可以使用声望兑换珍稀称号与宝物。" : npc.message;
					s.sendExChangeWindow(id, npc.id, msg,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kCraftsman)
				{
					// 工匠 / 料理大师 NPC 交互 (批次 §9.0.99)
					std::string msg = npc.message.empty() ? "欢迎来到工坊！在这里可以烹饪美味料理与合成精炼装备。" : npc.message;
					s.sendExChangeWindow(id, npc.id, msg,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
				else if (npc.type == NpcType::kRideMaster)
				{
					// 骑乘导师 / 骑乘考官 NPC 交互 (批次 §9.0.100)
					std::string msg = npc.message.empty() ? "欢迎来到骑乘训练所！通过严苛的考核，你将获得驾驭凶猛骑宠的专属认证资格。" : npc.message;
					s.sendExChangeWindow(id, npc.id, msg,
					                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
					ok = true;
				}
			}
		}
	}

	// 回执(原版 lssproto_EV_send(fd, seqno, rc)):seqno 原样带回,ok = 是否命中并开战。
	//   ★ 靠 seqno 关联(不依赖传输层 corr_id),同原版 EV 的 seqno 机制。
	SA::Domain::EventResult res{};
	res.seqno = req.seqno;
	res.ok = ok;
	s.sendTo(id, res);
}

// ── 窗口交互回调派发 (onWindowReply) ──────────────────────────

void World::onWindowReply(SA::Net::SessionId id, const SA::Domain::WindowReply &reply)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return;

	// 校验 window_id 是否匹配当前会话开启的活动窗口 (DR-PR3 / DR-PR8)
	if (it->second.active_window_id != 0 && it->second.active_window_id == reply.window_id)
	{
		// 检查是否存在待决 ExChange 上下文 (批次 W.9, 阶段 2 扩展 §9.0.97)
		if (it->second.pending_exchange.npc_id != 0 &&
		    (reply.source.entity_id == 0 || it->second.pending_exchange.npc_id == reply.source.entity_id))
		{
			const auto pending = it->second.pending_exchange;
			it->second.pending_exchange = {};

			const NpcEntity *npc = findNpc(pending.npc_id);
			if (npc != nullptr && pending.block_index >= 0 &&
			    static_cast<std::size_t>(pending.block_index) < npc->exchange_blocks.size())
			{
				const auto &blk = npc->exchange_blocks[static_cast<std::size_t>(pending.block_index)];
				const bool is_yes = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES)) != 0 ||
				                    reply.button == static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);

				if (is_yes)
				{
					if (blk.type == ExChangeType::kAccept)
					{
						std::string door_msg;
						if (!s.checkExChangePreconditions(*p, blk, pending.branch_idx, door_msg, id))
						{
							s.sendExChangeWindow(id, npc->id, door_msg,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							return;
						}

						// 执行全部副作用 (石币/道具/宠物/旗标/经验/点数/血蓝/传送)
						s.applyExChangeEffects(id, *p, blk, pending.branch_idx);

						// 多步对话树跳转 (next_block_index)
						if (blk.next_block_index >= 0 &&
						    static_cast<std::size_t>(blk.next_block_index) < npc->exchange_blocks.size())
						{
							const auto &next_blk = npc->exchange_blocks[static_cast<std::size_t>(blk.next_block_index)];
							std::string next_msg = next_blk.nomal_window_msg;
							if (next_msg.empty())
								next_msg = next_blk.accept_msg;
							if (next_msg.empty())
								next_msg = next_blk.nomal_msg;

							if (next_blk.type == ExChangeType::kAccept)
							{
								const std::uint32_t buttons =
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
								s.sendExChangeWindow(id, npc->id, next_msg, buttons);
								it->second.pending_exchange = {npc->id, blk.next_block_index, 1};
							}
							else
							{
								s.applyExChangeEffects(id, *p, next_blk, 1);
								if (next_blk.next_block_index >= 0)
									it->second.pending_exchange = {npc->id, blk.next_block_index, 1};
								s.sendExChangeWindow(id, npc->id, next_msg,
								                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							}
							return;
						}

						std::string thanks = blk.thanks_msg;
						if (thanks.empty())
							thanks = blk.nomal_window_msg;

						if (!thanks.empty())
						{
							s.sendExChangeWindow(id, npc->id, thanks,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							return; // 保持活动新窗口
						}
					}
					else if (blk.type == ExChangeType::kRequest)
					{
						// 接取委托任务
						if (blk.event_no != -1)
							p->setNowEvent(blk.event_no);

						s.applyExChangeEffects(id, *p, blk, pending.branch_idx);

						if (blk.next_block_index >= 0 &&
						    static_cast<std::size_t>(blk.next_block_index) < npc->exchange_blocks.size())
						{
							const auto &next_blk = npc->exchange_blocks[static_cast<std::size_t>(blk.next_block_index)];
							std::string next_msg = next_blk.nomal_window_msg;
							if (next_msg.empty())
								next_msg = next_blk.nomal_msg;

							if (next_blk.next_block_index >= 0)
								it->second.pending_exchange = {npc->id, blk.next_block_index, 1};
							s.sendExChangeWindow(id, npc->id, next_msg,
							                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							return;
						}

						std::string thanks = blk.thanks_msg;
						if (thanks.empty())
							thanks = "委托任务已接受，请前往完成！";
						s.sendExChangeWindow(id, npc->id, thanks,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}
					else if (blk.type == ExChangeType::kClean)
					{
						// 清除任务旗标
						if (blk.event_no != -1)
						{
							p->clearNowEvent(blk.event_no);
							p->clearEndEvent(blk.event_no);
						}
						s.applyExChangeEffects(id, *p, blk, pending.branch_idx);

						std::string thanks = blk.thanks_msg;
						if (thanks.empty())
							thanks = "任务记录已清除。";
						s.sendExChangeWindow(id, npc->id, thanks,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}
					else if (blk.type == ExChangeType::kMessage)
					{
						// 多步推进中的 kMessage
						if (blk.next_block_index >= 0 &&
						    static_cast<std::size_t>(blk.next_block_index) < npc->exchange_blocks.size())
						{
							const auto &next_blk = npc->exchange_blocks[static_cast<std::size_t>(blk.next_block_index)];
							std::string next_msg = next_blk.nomal_window_msg;
							if (next_msg.empty())
								next_msg = next_blk.accept_msg;
							if (next_msg.empty())
								next_msg = next_blk.nomal_msg;

							if (next_blk.type == ExChangeType::kAccept)
							{
								const std::uint32_t buttons =
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES) |
								    static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO);
								s.sendExChangeWindow(id, npc->id, next_msg, buttons);
								it->second.pending_exchange = {npc->id, blk.next_block_index, 1};
							}
							else
							{
								s.applyExChangeEffects(id, *p, next_blk, 1);
								if (next_blk.next_block_index >= 0)
									it->second.pending_exchange = {npc->id, blk.next_block_index, 1};
								s.sendExChangeWindow(id, npc->id, next_msg,
								                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
							}
							return;
						}
					}
				}
			}
		}

		// 检查是否存在待决 Shop 商店交互 (批次 W.12)
		if (it->second.pending_shop.npc_id != 0 &&
		    it->second.pending_shop.npc_id == reply.source.entity_id)
		{
			const std::uint64_t shop_npc_id = it->second.pending_shop.npc_id;
			it->second.pending_shop = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel && reply.result_kind == SA::Domain::WindowReply::ResultKind::ENTRY_ID)
			{
				const NpcEntity *npc = findNpc(shop_npc_id);
				const std::uint32_t entry_id = reply.result.entry_id;
				if (npc != nullptr && entry_id >= 1 && entry_id <= npc->shop_products.size())
				{
					const auto &prod = npc->shop_products[entry_id - 1];
					const std::int32_t price = std::max(1, static_cast<std::int32_t>(prod.cost * npc->buy_rate));

					// 门 ①: 石币是否充足 (DR-EC3 余额不足拒绝)
					if (p->gold < price)
					{
						std::string less_msg = npc->stone_less_msg.empty() ? "石币不足！" : npc->stone_less_msg;
						s.sendExChangeWindow(id, npc->id, less_msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}

					// 门 ②: 背包是否有空槽
					if (p->findFreeItemSlot() < 0)
					{
						std::string full_msg = npc->item_full_msg.empty() ? "道具栏已满！" : npc->item_full_msg;
						s.sendExChangeWindow(id, npc->id, full_msg,
						                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
						return;
					}

					// 门 ③: 道具池分配
					const auto h = s.items.allocate();
					if (h.valid())
					{
						auto *new_item = s.items.resolve(h);
						if (new_item != nullptr)
						{
							// 扣除石币 (走 GoldLedger, 汇 kShopBuy)
							(void)delGold(*p, GoldReason::kShopBuy, price,
							              /*trans=*/0, static_cast<std::uint64_t>(id), s);

							// 填充道具信息并入包
							new_item->uid = ++s.next_window_id;
							new_item->item_id = prod.item_id;
							new_item->name.assign(prod.name.c_str());
							new_item->cost = prod.cost;
							new_item->level = static_cast<std::int32_t>(prod.level);
							new_item->current_pile = 1;
							new_item->use_pile_nums = 1;

							const int slot = p->findFreeItemSlot();
							if (slot >= 0)
							{
								p->items[static_cast<std::size_t>(slot)] = h;
								s.sendExChangeWindow(id, npc->id, "购买成功！欢迎下次光临。",
								                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
								return;
							}
							else
							{
								s.items.release(h);
							}
						}
						else
						{
							s.items.release(h);
						}
					}
				}
			}
		}

		// 检查是否存在待决 PetShop 宠物商店交互 (批次 W.13)
		if (it->second.pending_pet_shop.npc_id != 0 &&
		    it->second.pending_pet_shop.npc_id == reply.source.entity_id)
		{
			const std::uint64_t shop_npc_id = it->second.pending_pet_shop.npc_id;
			it->second.pending_pet_shop = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel && reply.result_kind == SA::Domain::WindowReply::ResultKind::ENTRY_ID)
			{
				(void)buyPetFromShop(id, shop_npc_id, reply.result.entry_id);
				return;
			}
		}

		// 检查是否存在待决 PetSkillShop 技能导师交互 (批次 W.13)
		if (it->second.pending_pet_skill_shop.npc_id != 0 &&
		    it->second.pending_pet_skill_shop.npc_id == reply.source.entity_id)
		{
			const std::uint64_t shop_npc_id = it->second.pending_pet_skill_shop.npc_id;
			it->second.pending_pet_skill_shop = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel && reply.result_kind == SA::Domain::WindowReply::ResultKind::ENTRY_ID)
			{
				const NpcEntity *npc = findNpc(shop_npc_id);
				const std::uint32_t entry_id = reply.result.entry_id;
				if (npc != nullptr && entry_id >= 1 && entry_id <= npc->pet_skill_products.size())
				{
					const auto &prod = npc->pet_skill_products[entry_id - 1];
					int chosen_pet_slot = -1;
					for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
					{
						if (p->pets[i].valid())
						{
							chosen_pet_slot = static_cast<int>(i);
							break;
						}
					}
					if (chosen_pet_slot >= 0)
					{
						(void)learnPetSkill(id, shop_npc_id, chosen_pet_slot, prod.skill_id, /*skill_slot=*/-1);
						return;
					}
				}
			}
		}

		// 检查是否存在待决 WarpMan 传送员交互 (批次 W.14)
		if (it->second.pending_warpman.npc_id != 0 &&
		    it->second.pending_warpman.npc_id == reply.source.entity_id)
		{
			const auto pending = it->second.pending_warpman;
			it->second.pending_warpman = {};

			const bool is_cancel = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_CANCEL)) != 0 ||
			                       (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_NO)) != 0;

			if (!is_cancel)
			{
				int target_idx = -1;
				if (pending.dest_idx >= 0)
				{
					// 单目的地 (Yes/No 确认弹窗)
					const bool is_yes = (reply.button & static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_YES)) != 0 ||
					                    reply.button == static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK);
					if (is_yes)
					{
						target_idx = pending.dest_idx;
					}
				}
				else if (reply.result_kind == SA::Domain::WindowReply::ResultKind::CHOICE_ID)
				{
					// 多目的地 (SELECT 选项列表)
					if (reply.result.choice_id >= 1)
					{
						target_idx = static_cast<int>(reply.result.choice_id - 1);
					}
				}

				if (target_idx >= 0)
				{
					(void)warpPlayerByNpc(id, pending.npc_id, static_cast<std::size_t>(target_idx));
					return;
				}
			}
		}

		it->second.active_window_id = 0;
		it->second.active_window_npc_id = 0;
	}
}

// ── 商店与技能导师业务操作 ──────────────────────────────────────

bool World::sellItemToShop(SA::Net::SessionId id, std::uint64_t npc_id, int slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 槽位有效性与道具存在性检查
	if (slot < static_cast<int>(SA::Model::kStartItemArray) ||
	    slot >= static_cast<int>(SA::Model::kMaxItemHave))
		return false;

	const auto h = p->items[static_cast<std::size_t>(slot)];
	if (!h.valid())
		return false;

	auto *item = s.items.resolve(h);
	if (item == nullptr)
		return false;

	// 计算回购价格: 道具原价 * sell_rate (保底 1 石币)
	std::int32_t base_cost = item->cost;
	if (base_cost <= 0)
	{
		for (const auto &prod : npc->shop_products)
		{
			if (prod.item_id == item->item_id && prod.cost > 0)
			{
				base_cost = prod.cost;
				break;
			}
		}
	}
	if (base_cost <= 0)
		base_cost = 1;

	const std::int32_t unit_price = std::max(1, static_cast<std::int32_t>(base_cost * npc->sell_rate));
	const std::int32_t total_price = unit_price * std::max(1, item->current_pile);

	// 门: 随身石币上限检查 (DR-EC3 拒绝, 零改动)
	const std::int32_t cap = maxHaveGold(0);
	if (static_cast<std::int64_t>(p->gold) + total_price > cap)
	{
		std::string full_msg = npc->stone_full_msg.empty() ? "钱包装不下这么多石币！" : npc->stone_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 执行出售原子操作: 扣除并释放道具 + 增加石币 (走 GoldLedger, 源 kShopSell)
	p->clearItemSlot(slot);
	s.items.release(h);

	(void)addGold(*p, GoldReason::kShopSell, total_price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);
	return true;
}

bool World::buyPetFromShop(SA::Net::SessionId id, std::uint64_t npc_id, std::uint32_t entry_id)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kPetShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	if (entry_id < 1 || entry_id > npc->pet_products.size())
		return false;

	const auto &prod = npc->pet_products[entry_id - 1];
	const std::int32_t price = std::max(1, static_cast<std::int32_t>(prod.cost * npc->buy_rate));

	// 门 ①: 石币是否充足 (DR-EC3 余额不足拒绝)
	if (p->gold < price)
	{
		std::string less_msg = npc->stone_less_msg.empty() ? "石币不足！" : npc->stone_less_msg;
		s.sendExChangeWindow(id, npc->id, less_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ②: 宠物栏是否有空槽
	const int pet_slot = p->findFreePetSlot();
	if (pet_slot < 0)
	{
		std::string full_msg = npc->pet_full_msg.empty() ? "宠物栏已满！" : npc->pet_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ③: 宠物池分配
	const auto h = s.pets.allocate();
	if (!h.valid())
		return false;

	auto *dst = s.pets.resolve(h);
	if (dst == nullptr)
	{
		s.pets.release(h);
		return false;
	}

	// 扣除石币 (走 GoldLedger, 汇 kPetShopBuy)
	(void)delGold(*p, GoldReason::kPetShopBuy, price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);

	// 填充新宠物数据并落池
	dst->uid = ++s.next_window_id;
	dst->pet_id = prod.pet_id;
	dst->name.assign(prod.name.c_str());
	dst->level = prod.level;
	dst->hp = prod.hp;
	dst->mp = prod.mp;
	dst->max_mp = prod.mp;
	dst->vital = prod.vital;
	dst->str = prod.str;
	dst->tough = prod.tough;
	dst->dex = prod.dex;
	dst->origin_image = prod.image;
	dst->base_image = prod.image;
	dst->owner = s.player_of_session.find(id);
	dst->owner_char_name = p->name;

	p->pets[static_cast<std::size_t>(pet_slot)] = h;

	s.sendExChangeWindow(id, npc->id, "购买宠物成功！好好照顾它哦。",
	                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
	return true;
}

bool World::sellPetToShop(SA::Net::SessionId id, std::uint64_t npc_id, int pet_slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kPetShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 槽位有效性与宠物存在性检查
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return false;

	const auto h = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!h.valid())
		return false;

	auto *pet = s.pets.resolve(h);
	if (pet == nullptr)
		return false;

	// 计算回购价格: 宠物原价 * sell_rate (保底 1 石币)
	std::int32_t base_cost = 0;
	for (const auto &prod : npc->pet_products)
	{
		if (prod.pet_id == pet->pet_id && prod.cost > 0)
		{
			base_cost = prod.cost;
			break;
		}
	}
	if (base_cost <= 0)
	{
		base_cost = std::max(1, pet->level * 100);
	}

	const std::int32_t unit_price = std::max(1, static_cast<std::int32_t>(base_cost * npc->sell_rate));

	// 门: 随身石币上限检查 (DR-EC3 拒绝, 零改动)
	const std::int32_t cap = maxHaveGold(0);
	if (static_cast<std::int64_t>(p->gold) + unit_price > cap)
	{
		std::string full_msg = npc->stone_full_msg.empty() ? "钱包装不下这么多石币！" : npc->stone_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 执行出售原子操作: 扣除并释放宠物 + 增加石币 (走 GoldLedger, 源 kPetShopSell)
	if (playerRidePetSlot(id) == pet_slot)
	{
		dismountPet(id);
	}
	p->clearPetSlot(pet_slot);
	s.pets.release(h);

	(void)addGold(*p, GoldReason::kPetShopSell, unit_price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);
	return true;
}

bool World::learnPetSkill(SA::Net::SessionId id, std::uint64_t npc_id, int pet_slot,
                          std::int32_t skill_id, int skill_slot)
{
	Impl &s = *_impl;
	const auto it = s.conns.find(id);
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(id));
	if (it == s.conns.end() || p == nullptr)
		return false;

	const NpcEntity *npc = findNpc(npc_id);
	if (npc == nullptr || npc->type != NpcType::kPetSkillShop)
		return false;

	// 距离检查: 玩家与 NPC 距离 <= 3 格 (原版 NPC_Util_CharDistance <= 3)
	if (std::abs(p->x - npc->x) > 3 || std::abs(p->y - npc->y) > 3)
		return false;

	// 槽位有效性检查
	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return false;

	const auto h = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!h.valid())
		return false;

	auto *pet = s.pets.resolve(h);
	if (pet == nullptr)
		return false;

	// 查找导师技能条目
	const PetSkillProduct *target_prod = nullptr;
	for (const auto &prod : npc->pet_skill_products)
	{
		if (prod.skill_id == skill_id)
		{
			target_prod = &prod;
			break;
		}
	}
	if (target_prod == nullptr)
		return false;

	// 门 ①: 宠物等级是否达标 (移植 npc_petskillshop.c 门槛)
	if (pet->level < target_prod->level)
	{
		std::string low_msg = npc->level_low_msg.empty() ? "宠物等级不足以学习此技能！" : npc->level_low_msg;
		s.sendExChangeWindow(id, npc->id, low_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ②: 是否已习得该技能 (不可重复学同一技能)
	for (std::size_t i = 0; i < SA::Model::Pet::kPetSkillSlots; ++i)
	{
		if (pet->pet_skills[i] == skill_id)
			return false;
	}

	// 门 ③: 目标技能槽位解析
	int target_slot = -1;
	if (skill_slot >= 0 && static_cast<std::size_t>(skill_slot) < SA::Model::Pet::kPetSkillSlots)
	{
		target_slot = skill_slot;
	}
	else
	{
		for (std::size_t i = 0; i < SA::Model::Pet::kPetSkillSlots; ++i)
		{
			if (pet->pet_skills[i] <= 0) // 0 或 -1 表示空槽
			{
				target_slot = static_cast<int>(i);
				break;
			}
		}
	}
	if (target_slot < 0)
	{
		std::string full_msg = npc->skill_full_msg.empty() ? "宠物技能栏已满！" : npc->skill_full_msg;
		s.sendExChangeWindow(id, npc->id, full_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 门 ④: 石币是否充足
	const std::int32_t price = std::max(1, static_cast<std::int32_t>(target_prod->cost * npc->buy_rate));
	if (p->gold < price)
	{
		std::string less_msg = npc->stone_less_msg.empty() ? "石币不足！" : npc->stone_less_msg;
		s.sendExChangeWindow(id, npc->id, less_msg,
		                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
		return false;
	}

	// 执行扣除石币 (走 GoldLedger, 汇 kPetSkillFee)
	(void)delGold(*p, GoldReason::kPetSkillFee, price,
	              /*trans=*/0, static_cast<std::uint64_t>(id), s);

	// 写入宠物技能槽
	pet->pet_skills[static_cast<std::size_t>(target_slot)] = skill_id;

	s.sendExChangeWindow(id, npc->id, "宠物成功学会了新技能！",
	                     static_cast<std::uint32_t>(SA::Domain::ButtonFlag::BUTTON_FLAG_OK));
	return true;
}

} // namespace SA::World
