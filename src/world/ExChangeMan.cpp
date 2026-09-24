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
// 顺序: '<' -> '>' -> '!=' -> '='
bool evaluateAtom(std::string_view atom_raw, const SA::Model::Player &player)
{
	const std::string_view atom = trim(atom_raw);
	if (atom.empty())
		return false;

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
	const int val = std::atoi(std::string(val_str).c_str());

	if (var == "LV")
	{
		switch (op)
		{
		case Op::kLt:
			return player.level < val;
		case Op::kGt:
			return player.level > val;
		case Op::kNe:
			return player.level != val;
		case Op::kEq:
			return player.level == val;
		}
	}
	else if (var == "NOWEV")
	{
		switch (op)
		{
		case Op::kEq:
			return player.hasNowEvent(val);
		case Op::kNe:
			return !player.hasNowEvent(val);
		default:
			return false;
		}
	}
	else if (var == "ENDEV")
	{
		switch (op)
		{
		case Op::kEq:
			return player.hasEndEvent(val);
		case Op::kNe:
			return !player.hasEndEvent(val);
		default:
			return false;
		}
	}

	return false;
}

} // namespace

int evaluateEventCondition(std::string_view condition_raw, const SA::Model::Player &player)
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
			if (!evaluateAtom(atom, player))
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
		}
	}

	flush_block();
	return blocks;
}

} // namespace SA::World
