// src/world/WorldGm.cpp —— GM 管理员指令通道与个人银行存取系统
//
// 依据: 00 §3.1 / §4.3 / §7 (D8 核心子系统 S06 GM 命令 7,357 行 & NPC:Bankman 599 行)
// 包含:
//   1. S06 游戏内 GM 文本指令解析与调度分发 (对齐 chatmagic.c 核心指令)
//      - /warp, /heal, /gold, /level, /setfame, /additem, /delitem, /title, /announce, /shutdown, /help
//      - 兼容 [cmd ...] 格式 (原版 chatmagic 语法)
//   2. NPC:Bankman 个人钱庄/银行个人金库独立载体 (DR-EC1 银行独立载体, 上限 1 亿石币)
//      - bankDeposit: 随身石币转存银行 (GoldLedger kBankDeposit 审计)
//      - bankWithdraw: 银行存款提取至随身 (GoldLedger kBankWithdraw 审计, 100 万上限防御)
//      - playerBankGold: 查询银行余额

#include "WorldImpl.h"
#include "world/Api.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace SA::World
{

// ── S06 GM 指令通道 ──────────────────────────────────────────

bool World::isShutdownRequested() const noexcept
{
	return _impl->shutdown_requested;
}

bool World::isGmCommand(const std::string &text) const noexcept
{
	if (text.empty())
		return false;

	if (text.front() == '/')
		return true;

	if (text.front() == '[' && text.find(']') != std::string::npos)
	{
		const auto close_pos = text.find(']');
		std::string inner = text.substr(1, close_pos - 1);
		std::istringstream iss(inner);
		std::string cmd;
		if (iss >> cmd)
		{
			std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c)
			               { return std::tolower(c); });
			if (cmd == "warp" || cmd == "heal" || cmd == "gold" || cmd == "setgold" ||
			    cmd == "level" || cmd == "setlevel" || cmd == "setfame" || cmd == "fame" ||
			    cmd == "additem" || cmd == "delitem" || cmd == "title" || cmd == "announce" ||
			    cmd == "shutdown" || cmd == "help")
			{
				return true;
			}
		}
	}

	return false;
}

bool World::executeGmCommand(SA::Net::SessionId sender, const std::string &cmd_text, std::string &out_reply)
{
	Impl &s = *_impl;
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(sender));
	if (p == nullptr)
	{
		out_reply = "未找到玩家实体";
		return false;
	}

	std::string inner = cmd_text;
	if (!inner.empty() && inner.front() == '/')
	{
		inner = inner.substr(1);
	}
	else if (!inner.empty() && inner.front() == '[')
	{
		const auto close_pos = inner.find(']');
		if (close_pos != std::string::npos)
		{
			inner = inner.substr(1, close_pos - 1);
		}
		else
		{
			inner = inner.substr(1);
		}
	}

	std::istringstream iss(inner);
	std::string cmd;
	if (!(iss >> cmd))
	{
		out_reply = "指令为空，输入 /help 查看帮助";
		return false;
	}

	std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c)
	               { return std::tolower(c); });

	if (cmd == "help")
	{
		out_reply = "石器时代 GM 指令集:\n"
		            "/warp <floor> <x> <y> - 瞬移传送\n"
		            "/heal - 满血满蓝与宠物恢复\n"
		            "/gold <amount> - 增减随身石币\n"
		            "/level <lv> - 调整角色等级与属性\n"
		            "/setfame <amount> - 设定角色声望\n"
		            "/additem <id> - 添加指定道具进背包\n"
		            "/delitem <slot> - 删除背包槽位道具\n"
		            "/title <id> - 授予称号\n"
		            "/announce <msg> - 全服系统公告\n"
		            "/shutdown - 停机维护\n"
		            "注: 同时支持 [cmd ...] 格式";
		return true;
	}

	if (cmd == "warp")
	{
		std::int32_t floor = 0, x = 0, y = 0;
		if (!(iss >> floor >> x >> y))
		{
			out_reply = "参数错误: /warp <floor> <x> <y>";
			return false;
		}
		s.warpPlayer(sender, floor, x, y);
		out_reply = "已成功传送至地图 " + std::to_string(floor) + " (" + std::to_string(x) + ", " + std::to_string(y) + ")";
		return true;
	}

	if (cmd == "heal")
	{
		const auto stats = SA::Rules::deriveBaseStats(std::max(p->vital, 20) * 100, p->str * 100, p->tough * 100, p->dex * 100);
		p->hp = std::max(stats.max_hp, 500);
		p->mp = p->max_mp;
		if (p->default_pet >= 0 && static_cast<std::size_t>(p->default_pet) < SA::Model::kMaxPetHave)
		{
			const auto ph = p->pets[static_cast<std::size_t>(p->default_pet)];
			if (ph.valid())
			{
				auto *pet = s.pets.resolve(ph);
				if (pet != nullptr)
				{
					const auto pstats = SA::Rules::deriveBaseStats(std::max(pet->vital, 20) * 100, pet->str * 100, pet->tough * 100, pet->dex * 100);
					pet->hp = std::max(pstats.max_hp, 500);
				}
			}
		}
		out_reply = "角色与出战宠物生命魔法已完全恢复 (HP: " + std::to_string(p->hp) + ", MP: " + std::to_string(p->mp) + ")";
		return true;
	}

	if (cmd == "gold" || cmd == "setgold")
	{
		std::int32_t amount = 0;
		if (!(iss >> amount))
		{
			out_reply = "参数错误: /gold <amount>";
			return false;
		}
		if (amount >= 0)
		{
			addGold(*p, GoldReason::kGmAdjust, amount, /*trans=*/0, /*corr=*/sender, s);
		}
		else
		{
			delGold(*p, GoldReason::kGmAdjust, -amount, /*trans=*/0, /*corr=*/sender, s);
		}
		out_reply = "石币调整成功，当前随身石币: " + std::to_string(p->gold);
		return true;
	}

	if (cmd == "level" || cmd == "setlevel")
	{
		std::int32_t lv = 0;
		if (!(iss >> lv) || lv < 1 || lv > 140)
		{
			out_reply = "参数错误: /level <1..140>";
			return false;
		}
		p->level = lv;
		p->skillup_points = (lv - 1) * 3;
		p->vital = 20 + lv * 2;
		p->str = 20 + lv * 2;
		p->tough = 20 + lv * 2;
		p->dex = 20 + lv * 2;
		const auto stats = SA::Rules::deriveBaseStats(p->vital, p->str, p->tough, p->dex);
		p->hp = std::max(stats.max_hp, 100);
		p->mp = p->max_mp;
		out_reply = "等级已调整为 " + std::to_string(lv) + "，未分配点数: " + std::to_string(p->skillup_points);
		return true;
	}

	if (cmd == "setfame" || cmd == "fame")
	{
		std::int32_t fame = 0;
		if (!(iss >> fame))
		{
			out_reply = "参数错误: /setfame <amount>";
			return false;
		}
		setPlayerFame(sender, fame);
		out_reply = "声望已设置为 " + std::to_string(fame);
		return true;
	}

	if (cmd == "additem")
	{
		std::int32_t item_id = 0;
		if (!(iss >> item_id))
		{
			out_reply = "参数错误: /additem <item_id>";
			return false;
		}
		const int free_slot = p->findFreeItemSlot();
		if (free_slot < 0)
		{
			out_reply = "背包已满，无法添加道具";
			return false;
		}
		const auto h = s.items.allocate();
		if (!h.valid())
		{
			out_reply = "道具对象池已耗尽";
			return false;
		}
		auto *item = s.items.resolve(h);
		item->uid = ++s.next_window_id;
		item->item_id = item_id;
		item->name.assign(("道具_" + std::to_string(item_id)).c_str());
		p->items[static_cast<std::size_t>(free_slot)] = h;
		out_reply = "道具 " + std::to_string(item_id) + " 已添加至背包槽位 " + std::to_string(free_slot);
		return true;
	}

	if (cmd == "delitem")
	{
		int slot = -1;
		if (!(iss >> slot) || slot < 0 || static_cast<std::size_t>(slot) >= SA::Model::kMaxItemHave)
		{
			out_reply = "参数错误: /delitem <slot(0..53)>";
			return false;
		}
		const auto h = p->items[static_cast<std::size_t>(slot)];
		if (!h.valid())
		{
			out_reply = "指定槽位无道具";
			return false;
		}
		s.items.release(h);
		p->clearItemSlot(slot);
		out_reply = "已删除背包槽位 " + std::to_string(slot) + " 的道具";
		return true;
	}

	if (cmd == "title")
	{
		int title_id = 0;
		if (!(iss >> title_id))
		{
			out_reply = "参数错误: /title <title_id>";
			return false;
		}
		if (findTitle(title_id) == std::nullopt)
		{
			TitleDefinition td{};
			td.title_id = title_id;
			td.name = "GM称号_" + std::to_string(title_id);
			registerTitle(td);
		}
		if (!grantTitle(sender, title_id))
		{
			out_reply = "授予称号失败 (可能已拥有或超过上限)";
			return false;
		}
		out_reply = "已成功授予称号 ID: " + std::to_string(title_id);
		return true;
	}

	if (cmd == "announce")
	{
		std::string msg;
		std::getline(iss, msg);
		if (!msg.empty() && msg.front() == ' ')
		{
			msg.erase(0, 1);
		}
		if (msg.empty())
		{
			out_reply = "参数错误: /announce <text>";
			return false;
		}
		broadcastSystemAnnouncement(msg, 0xFFFF00);
		out_reply = "系统公告已广播: " + msg;
		return true;
	}

	if (cmd == "shutdown")
	{
		requestShutdown();
		out_reply = "已触发服务器维护关服停机标记";
		return true;
	}

	out_reply = "未知 GM 指令，输入 /help 查看可用指令";
	return false;
}

// ── NPC:Bankman 个人钱庄/银行个人金库系统 ──────────────────────

std::int32_t World::playerBankGold(SA::Net::SessionId session) const
{
	const auto *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return 0;
	auto it = _impl->bank_accounts.find(p->name.c_str());
	return (it != _impl->bank_accounts.end()) ? it->second : 0;
}

bool World::bankDeposit(SA::Net::SessionId session, std::int32_t amount)
{
	if (amount <= 0)
		return false;

	Impl &s = *_impl;
	auto *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr || p->gold < amount)
		return false;

	const std::string name = p->name.c_str();
	const std::int32_t cur_bank = s.bank_accounts[name];
	if (static_cast<std::int64_t>(cur_bank) + amount > Impl::kMaxBankGold)
		return false; // 超出银行 1 亿上限

	const auto tx = delGold(*p, GoldReason::kBankDeposit, amount, /*trans=*/0, /*corr=*/session, s);
	if (tx.applied != amount)
		return false;

	s.bank_accounts[name] += amount;
	return true;
}

bool World::bankWithdraw(SA::Net::SessionId session, std::int32_t amount)
{
	if (amount <= 0)
		return false;

	Impl &s = *_impl;
	auto *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	const std::string name = p->name.c_str();
	auto it = s.bank_accounts.find(name);
	if (it == s.bank_accounts.end() || it->second < amount)
		return false; // 银行存款不足

	const std::int32_t cap = maxHaveGold(0); // 1,000,000
	if (static_cast<std::int64_t>(p->gold) + amount > cap)
		return false; // 随身钱包上限防御

	it->second -= amount;
	const auto tx = addGold(*p, GoldReason::kBankWithdraw, amount, /*trans=*/0, /*corr=*/session, s);
	return tx.applied == amount;
}

} // namespace SA::World
