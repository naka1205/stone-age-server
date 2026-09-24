// src/world/ExChangeMan.cpp —— ExChangeMan 脚本解析与条件表达式求值器 (批次 W.9)
//
// 依据 stoneage-plan/docs/09-npc-event-dsl.md:
// - §2.3 C5: key:value 提取与子串匹配行为
// - §3.1 C9 / §3.3 C11: EBNF 条件语法与原子求值顺序
// - §3.4 C12: 变量表 (LV, NOWEV, ENDEV)
// - §3.7 C18: ',' 分支选择器 (返回 1-based 序号)
// - §4 C20: TYPE 分派 (MESSAGE, ACCEPT)
// - §7 C28-C30: 任务旗标位图与安全边界

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

inline std::string_view trim(std::string_view s) noexcept
{
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
		s.remove_prefix(1);
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
		s.remove_suffix(1);
	return s;
}

// 辅助切分
std::vector<std::string_view> split(std::string_view s, char delim)
{
	std::vector<std::string_view> parts;
	std::size_t start = 0;
	while (start < s.size())
	{
		const std::size_t pos = s.find(delim, start);
		if (pos == std::string_view::npos)
		{
			parts.push_back(s.substr(start));
			break;
		}
		parts.push_back(s.substr(start, pos - start));
		start = pos + 1;
	}
	if (start == s.size() && !s.empty() && s.back() == delim)
	{
		parts.push_back("");
	}
	return parts;
}

// 解析单个原子 (09 §3.3 C11):
// 1. "PET" 先截获 (无论 op 是什么, 走 NPC_PetLvCheck 逻辑)
// 2. '<' -> '>' -> '!=' -> '='
bool evaluateAtom(std::string_view atom_raw, const EventCheckContext &ctx)
{
	const std::string_view atom = trim(atom_raw);
	if (atom.empty())
		return false;

	// 1. strstr(buf, "PET") 截获 (09 §3.3 C11, §3.5 C16)
	if (atom.find("PET") != std::string_view::npos)
	{
		// 语法: PET<op><level>-<petid>[*<count>] 或 PET=<petid> 或 PET!=<petid>
		int pet_id = 0;
		int req_count = 1;
		int req_level = 0;
		char op = '=';

		const std::size_t hyphen = atom.find('-');
		if (hyphen != std::string_view::npos)
		{
			const std::string_view left = trim(atom.substr(0, hyphen));
			const std::string_view right = trim(atom.substr(hyphen + 1));

			// 右半段: pet_id[*count]
			const std::size_t star = right.find('*');
			if (star != std::string_view::npos)
			{
				pet_id = std::atoi(std::string(trim(right.substr(0, star))).c_str());
				req_count = std::atoi(std::string(trim(right.substr(star + 1))).c_str());
			}
			else
			{
				pet_id = std::atoi(std::string(right).c_str());
				req_count = 1;
			}

			// 左半段: PET<op><level>
			if (left.find('<') != std::string_view::npos)
			{
				op = '<';
				const std::size_t p = left.find('<');
				req_level = std::atoi(std::string(trim(left.substr(p + 1))).c_str());
			}
			else if (left.find('>') != std::string_view::npos)
			{
				op = '>';
				const std::size_t p = left.find('>');
				req_level = std::atoi(std::string(trim(left.substr(p + 1))).c_str());
			}
			else if (left.find("!=") != std::string_view::npos)
			{
				op = '!';
				const std::size_t p = left.find("!=");
				req_level = std::atoi(std::string(trim(left.substr(p + 2))).c_str());
			}
			else if (left.find('=') != std::string_view::npos)
			{
				op = '=';
				const std::size_t p = left.find('=');
				req_level = std::atoi(std::string(trim(left.substr(p + 1))).c_str());
			}
		}
		else
		{
			// 无 hyphen: 如 PET=95 或 PET!=95
			if (atom.find("!=") != std::string_view::npos)
			{
				op = '!';
				const std::size_t p = atom.find("!=");
				pet_id = std::atoi(std::string(trim(atom.substr(p + 2))).c_str());
			}
			else if (atom.find('=') != std::string_view::npos)
			{
				op = '=';
				const std::size_t p = atom.find('=');
				pet_id = std::atoi(std::string(trim(atom.substr(p + 1))).c_str());
			}
			else
			{
				return false;
			}
		}

		const int has_pet =
		    ctx.count_pet ? ctx.count_pet(ctx.player, pet_id, req_level, ctx.userdata) : 0;
		if (op == '!')
		{
			return has_pet == 0;
		}
		return has_pet >= req_count;
	}

	enum class Op
	{
		kLt,
		kGt,
		kNe,
		kEq
	};
	Op op = Op::kEq;
	std::size_t op_pos = std::string_view::npos;
	std::size_t op_len = 1;

	// 依据 C11 顺序探测:
	// 1. '<'
	// 2. '>'
	// 3. '!='
	// 4. '='
	if ((op_pos = atom.find('<')) != std::string_view::npos)
	{
		op = Op::kLt;
		op_len = 1;
	}
	else if ((op_pos = atom.find('>')) != std::string_view::npos)
	{
		op = Op::kGt;
		op_len = 1;
	}
	else if ((op_pos = atom.find("!=")) != std::string_view::npos)
	{
		op = Op::kNe;
		op_len = 2;
	}
	else if ((op_pos = atom.find('=')) != std::string_view::npos)
	{
		op = Op::kEq;
		op_len = 1;
	}
	else
	{
		return false; // 无比较符
	}

	const std::string_view var = trim(atom.substr(0, op_pos));
	const std::string_view val_str = trim(atom.substr(op_pos + op_len));

	// 道具数量 reduce 判定: ITEM=1234*2 (09 §3.6 C17)
	if (var == "ITEM")
	{
		int item_id = 0;
		int req_count = 1;
		const std::size_t star_pos = val_str.find('*');
		if (star_pos != std::string_view::npos)
		{
			item_id = std::atoi(std::string(trim(val_str.substr(0, star_pos))).c_str());
			req_count = std::atoi(std::string(trim(val_str.substr(star_pos + 1))).c_str());
		}
		else
		{
			item_id = std::atoi(std::string(val_str).c_str());
		}

		const int has_count =
		    ctx.count_item ? ctx.count_item(ctx.player, item_id, ctx.userdata) : 0;
		switch (op)
		{
		case Op::kEq:
			return has_count >= req_count;
		case Op::kNe:
			return has_count == 0;
		case Op::kLt:
			return has_count < req_count;
		case Op::kGt:
			return has_count > req_count;
		}
	}
	else if (var == "reITEM")
	{
		int free_slots = 0;
		if (ctx.count_free_item_slots != nullptr)
		{
			free_slots = ctx.count_free_item_slots(ctx.player, ctx.userdata);
		}
		else
		{
			for (std::size_t i = SA::Model::kStartItemArray; i < SA::Model::kMaxItemHave; ++i)
			{
				if (!ctx.player.items[i].valid())
					++free_slots;
			}
		}
		const int val = std::atoi(std::string(val_str).c_str());
		switch (op)
		{
		case Op::kLt:
			return free_slots < val;
		case Op::kGt:
			return free_slots > val;
		case Op::kNe:
			return free_slots != val;
		case Op::kEq:
			return free_slots == val;
		}
	}
	else if (var == "rePET")
	{
		int free_slots = 0;
		if (ctx.count_free_pet_slots != nullptr)
		{
			free_slots = ctx.count_free_pet_slots(ctx.player, ctx.userdata);
		}
		else
		{
			for (std::size_t i = 0; i < SA::Model::kMaxPetHave; ++i)
			{
				if (!ctx.player.pets[i].valid())
					++free_slots;
			}
		}
		const int val = std::atoi(std::string(val_str).c_str());
		switch (op)
		{
		case Op::kLt:
			return free_slots < val;
		case Op::kGt:
			return free_slots > val;
		case Op::kNe:
			return free_slots != val;
		case Op::kEq:
			return free_slots == val;
		}
	}
	else if (var == "GOLD" || var == "gold")
	{
		const int val = std::atoi(std::string(val_str).c_str());
		switch (op)
		{
		case Op::kLt:
			return ctx.player.gold < val;
		case Op::kGt:
			return ctx.player.gold > val;
		case Op::kNe:
			return ctx.player.gold != val;
		case Op::kEq:
			return ctx.player.gold == val;
		}
	}
	else if (var == "LV")
	{
		const int val = std::atoi(std::string(val_str).c_str());
		switch (op)
		{
		case Op::kLt:
			return ctx.player.level < val;
		case Op::kGt:
			return ctx.player.level > val;
		case Op::kNe:
			return ctx.player.level != val;
		case Op::kEq:
			return ctx.player.level == val;
		}
	}
	else if (var == "NOWEV")
	{
		const int val = std::atoi(std::string(val_str).c_str());
		switch (op)
		{
		case Op::kEq:
			return ctx.player.hasNowEvent(val);
		case Op::kNe:
			return !ctx.player.hasNowEvent(val);
		default:
			return false;
		}
	}
	else if (var == "ENDEV")
	{
		const int val = std::atoi(std::string(val_str).c_str());
		switch (op)
		{
		case Op::kEq:
			return ctx.player.hasEndEvent(val);
		case Op::kNe:
			return !ctx.player.hasEndEvent(val);
		default:
			return false;
		}
	}

	return false;
}

} // namespace

int evaluateEventCondition(std::string_view condition_raw, const EventCheckContext &ctx)
{
	const std::string_view cond = trim(condition_raw);
	if (cond.empty())
		return 1; // 空条件默认命中第 1 分支

	const auto branches = split(cond, ',');
	for (std::size_t i = 0; i < branches.size(); ++i)
	{
		const std::string_view branch = trim(branches[i]);
		if (branch.empty())
			continue;

		const auto atoms = split(branch, '&');
		bool branch_ok = true;
		for (const auto &atom : atoms)
		{
			if (!evaluateAtom(atom, ctx))
			{
				branch_ok = false;
				break;
			}
		}

		if (branch_ok)
		{
			return static_cast<int>(i + 1); // 1-based 序号
		}
	}

	return 0; // 无分支满足
}

int evaluateEventCondition(std::string_view condition_raw, const SA::Model::Player &player)
{
	EventCheckContext ctx{player};
	return evaluateEventCondition(condition_raw, ctx);
}

std::vector<ExChangeBlock> parseExChangeBlocks(std::string_view argstr)
{
	std::vector<ExChangeBlock> blocks;
	ExChangeBlock current{};
	bool has_content = false;

	auto flush_block = [&]()
	{
		if (has_content)
		{
			blocks.push_back(std::move(current));
			current = ExChangeBlock{};
			has_content = false;
		}
	};

	// 块由 "EventEnd" 划分 (09 §3.2 C10)
	// 原版按行与 '|' 拼装 (09 §2.2 C3, §2.3 C5)
	std::size_t start = 0;
	while (start < argstr.size())
	{
		// 取一行
		std::size_t nl = argstr.find('\n', start);
		std::string_view line = (nl == std::string_view::npos)
		                            ? argstr.substr(start)
		                            : argstr.substr(start, nl - start);
		start = (nl == std::string_view::npos) ? argstr.size() : nl + 1;

		line = trim(line);
		if (line.empty())
			continue;

		// 检查 EventEnd
		if (line.find("EventEnd") != std::string_view::npos)
		{
			flush_block();
			continue;
		}

		// 按 '|' 再次分段
		const auto segments = split(line, '|');
		for (const auto &seg_raw : segments)
		{
			const std::string_view seg = trim(seg_raw);
			if (seg.empty())
				continue;
			if (seg.find("EventEnd") != std::string_view::npos)
			{
				flush_block();
				continue;
			}

			const std::size_t colon = seg.find(':');
			if (colon == std::string_view::npos)
				continue;

			const std::string_view key = trim(seg.substr(0, colon));
			const std::string_view val = trim(seg.substr(colon + 1));

			has_content = true;

			if (key == "EventNo")
			{
				current.event_no = std::atoi(std::string(val).c_str());
			}
			else if (key == "TYPE")
			{
				if (val.find("ACCEPT") != std::string_view::npos)
					current.type = ExChangeType::kAccept;
				else if (val.find("REQUEST") != std::string_view::npos)
					current.type = ExChangeType::kRequest;
				else if (val.find("CLEAN") != std::string_view::npos)
					current.type = ExChangeType::kClean;
				else
					current.type = ExChangeType::kMessage;
			}
			else if (key == "EVENT")
			{
				current.condition = std::string(val);
			}
			else if (key == "NomalMsg")
			{
				current.nomal_msg = std::string(val);
			}
			else if (key.find("NomalWindowMsg") != std::string_view::npos)
			{
				current.nomal_window_msg = std::string(val);
			}
			else if (key.find("AcceptMsg") != std::string_view::npos)
			{
				current.accept_msg = std::string(val);
			}
			else if (key.find("ThanksMsg") != std::string_view::npos)
			{
				current.thanks_msg = std::string(val);
			}
			else if (key == "EndSetFlg")
			{
				current.end_set_flg = std::string(val);
			}
			else if (key == "CleanFlg")
			{
				current.clean_flg = std::string(val);
			}
			else if (key == "GetItem")
			{
				current.get_item = std::string(val);
			}
			else if (key == "DelItem")
			{
				current.del_item = std::string(val);
			}
			else if (key == "GetPet")
			{
				current.get_pet = std::string(val);
			}
			else if (key == "DelPet")
			{
				current.del_pet = std::string(val);
			}
			else if (key == "GetStone")
			{
				current.get_stone = std::atoi(std::string(val).c_str());
			}
			else if (key == "DelStone")
			{
				current.del_stone = std::atoi(std::string(val).c_str());
			}
			else if (key.find("ItemFullMsg") != std::string_view::npos)
			{
				current.item_full_msg = std::string(val);
			}
			else if (key.find("PetFullMsg") != std::string_view::npos)
			{
				current.pet_full_msg = std::string(val);
			}
			else if (key.find("StoneLessMsg") != std::string_view::npos)
			{
				current.stone_less_msg = std::string(val);
			}
			else if (key.find("StoneFullMsg") != std::string_view::npos)
			{
				current.stone_full_msg = std::string(val);
			}
		}
	}

	flush_block();
	return blocks;
}

} // namespace SA::World
