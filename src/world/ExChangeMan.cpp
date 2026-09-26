// src/world/ExChangeMan.cpp —— ExChangeMan 脚本解析与条件表达式求值器 (批次 W.9, 阶段 2 扩展 §9.0.97)
//
// 依据 stoneage-plan/docs/09-npc-event-dsl.md:
// - §2.3 C5: key:value 提取与子串匹配行为
// - §3.1 C9 / §3.3 C11: EBNF 条件语法与原子求值顺序
// - §3.4 C12: 变量表 (LV, NOWEV, ENDEV, TRANS, FAME, FM, PROF, GOLD, HP, MP, SP, reITEM, rePET)
// - §3.7 C18: ',' 分支选择器 (返回 1-based 序号)
// - §4 C20: TYPE 分派 (MESSAGE, ACCEPT, REQUEST, CLEAN)
// - §7 C28-C30: 任务旗标位图与安全边界
// - §5.1 / §5.2: 动作全集与多步状态机跳转

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

// 顶层逗号分段 (忽略括号内部逗号)
std::vector<std::string_view> splitTopLevelBranches(std::string_view s)
{
	std::vector<std::string_view> branches;
	std::size_t start = 0;
	int paren_depth = 0;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '(')
			++paren_depth;
		else if (s[i] == ')' && paren_depth > 0)
			--paren_depth;
		else if (s[i] == ',' && paren_depth == 0)
		{
			branches.push_back(s.substr(start, i - start));
			start = i + 1;
		}
	}
	branches.push_back(s.substr(start));
	return branches;
}

enum class CmpOp
{
	kEq,
	kNe,
	kLt,
	kLe,
	kGt,
	kGe
};

inline bool compareInt(int lhs, CmpOp op, int rhs) noexcept
{
	switch (op)
	{
	case CmpOp::kEq:
		return lhs == rhs;
	case CmpOp::kNe:
		return lhs != rhs;
	case CmpOp::kLt:
		return lhs < rhs;
	case CmpOp::kLe:
		return lhs <= rhs;
	case CmpOp::kGt:
		return lhs > rhs;
	case CmpOp::kGe:
		return lhs >= rhs;
	}
	return false;
}

// 解析单个原子 (09 §3.3 C11):
// 1. "PET" 先截获 (无论 op 是什么, 走 NPC_PetLvCheck 逻辑)
// 2. '<=' -> '>=' -> '!=' -> '<' -> '>' -> '='
bool evaluateAtom(std::string_view atom_raw, const EventCheckContext &ctx)
{
	const std::string_view atom = trim(atom_raw);
	if (atom.empty())
		return false;

	// 1. strstr(buf, "PET") 截获 (09 §3.3 C11, §3.5 C16)
	if (atom.find("rePET") == std::string_view::npos && atom.find("PET") != std::string_view::npos)
	{
		// 语法: PET<op><level>-<petid>[*<count>] 或 PET<op><level>-<petid>[^<count>]
		//       或 PET=<petid> 或 PET!=<petid> 或 PET*count=petid 或 PET^count=petid
		//       或 PET=petid*minlevel*count
		int pet_id = 0;
		int req_count = 1;
		int req_level = 0;
		char op = '=';
		bool exact_count = false;

		const std::size_t hyphen = atom.find('-');
		if (hyphen != std::string_view::npos)
		{
			const std::string_view left = trim(atom.substr(0, hyphen));
			const std::string_view right = trim(atom.substr(hyphen + 1));

			// 右半段: pet_id[*count] 或 pet_id[^count]
			const std::size_t star = right.find('*');
			const std::size_t caret = right.find('^');
			if (star != std::string_view::npos)
			{
				pet_id = std::atoi(std::string(trim(right.substr(0, star))).c_str());
				req_count = std::atoi(std::string(trim(right.substr(star + 1))).c_str());
			}
			else if (caret != std::string_view::npos)
			{
				pet_id = std::atoi(std::string(trim(right.substr(0, caret))).c_str());
				req_count = std::atoi(std::string(trim(right.substr(caret + 1))).c_str());
				exact_count = true;
			}
			else
			{
				pet_id = std::atoi(std::string(right).c_str());
				req_count = 1;
			}

			// 左半段: PET<op><level>
			if (left.find("<=") != std::string_view::npos)
			{
				op = 'L'; // <=
				const std::size_t p = left.find("<=");
				req_level = std::atoi(std::string(trim(left.substr(p + 2))).c_str());
			}
			else if (left.find(">=") != std::string_view::npos)
			{
				op = 'G'; // >=
				const std::size_t p = left.find(">=");
				req_level = std::atoi(std::string(trim(left.substr(p + 2))).c_str());
			}
			else if (left.find("!=") != std::string_view::npos)
			{
				op = '!';
				const std::size_t p = left.find("!=");
				req_level = std::atoi(std::string(trim(left.substr(p + 2))).c_str());
			}
			else if (left.find('<') != std::string_view::npos)
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
			else if (left.find('=') != std::string_view::npos)
			{
				op = '=';
				const std::size_t p = left.find('=');
				req_level = std::atoi(std::string(trim(left.substr(p + 1))).c_str());
			}
		}
		else
		{
			// 无 hyphen: 如 PET=95, PET!=95, PET*2=2001, PET^2=2001, PET=2001*50*2
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
				const std::string_view left = trim(atom.substr(0, p));
				const std::string_view right = trim(atom.substr(p + 1));

				if (left.rfind("PET*", 0) == 0)
				{
					req_count = std::atoi(std::string(left.substr(4)).c_str());
					pet_id = std::atoi(std::string(right).c_str());
				}
				else if (left.rfind("PET^", 0) == 0)
				{
					req_count = std::atoi(std::string(left.substr(4)).c_str());
					pet_id = std::atoi(std::string(right).c_str());
					exact_count = true;
				}
				else
				{
					// 检查右侧是否含有 '*' 或 '^'
					const std::size_t star1 = right.find('*');
					const std::size_t caret = right.find('^');
					if (star1 != std::string_view::npos)
					{
						pet_id = std::atoi(std::string(trim(right.substr(0, star1))).c_str());
						const std::string_view rest = trim(right.substr(star1 + 1));
						const std::size_t star2 = rest.find('*');
						if (star2 != std::string_view::npos)
						{
							req_level = std::atoi(std::string(trim(rest.substr(0, star2))).c_str());
							req_count = std::atoi(std::string(trim(rest.substr(star2 + 1))).c_str());
						}
						else
						{
							req_count = std::atoi(std::string(rest).c_str());
						}
					}
					else if (caret != std::string_view::npos)
					{
						pet_id = std::atoi(std::string(trim(right.substr(0, caret))).c_str());
						req_count = std::atoi(std::string(trim(right.substr(caret + 1))).c_str());
						exact_count = true;
					}
					else
					{
						pet_id = std::atoi(std::string(right).c_str());
					}
				}
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
		if (exact_count)
		{
			return has_pet == req_count;
		}
		return has_pet >= req_count;
	}

	CmpOp op = CmpOp::kEq;
	std::size_t op_pos = std::string_view::npos;
	std::size_t op_len = 1;

	// 关系运算符探测: 先匹配双字符 (<=, >=, !=), 再匹配单字符 (<, >, =)
	if ((op_pos = atom.find("<=")) != std::string_view::npos)
	{
		op = CmpOp::kLe;
		op_len = 2;
	}
	else if ((op_pos = atom.find(">=")) != std::string_view::npos)
	{
		op = CmpOp::kGe;
		op_len = 2;
	}
	else if ((op_pos = atom.find("!=")) != std::string_view::npos)
	{
		op = CmpOp::kNe;
		op_len = 2;
	}
	else if ((op_pos = atom.find('<')) != std::string_view::npos)
	{
		op = CmpOp::kLt;
		op_len = 1;
	}
	else if ((op_pos = atom.find('>')) != std::string_view::npos)
	{
		op = CmpOp::kGt;
		op_len = 1;
	}
	else if ((op_pos = atom.find('=')) != std::string_view::npos)
	{
		op = CmpOp::kEq;
		op_len = 1;
	}
	else
	{
		return false; // 无比较符
	}

	const std::string_view var = trim(atom.substr(0, op_pos));
	const std::string_view val_str = trim(atom.substr(op_pos + op_len));

	// 道具数量判定: ITEM=1234*2 (>=2), ITEM=1234^2 (==2) (09 §3.6 C17)
	// 以及 ITEM*2=1234, ITEM^2=1234
	if (var == "ITEM" || var.rfind("ITEM*", 0) == 0 || var.rfind("ITEM^", 0) == 0)
	{
		int item_id = 0;
		int req_count = 1;
		bool exact = false;
		if (var.rfind("ITEM*", 0) == 0)
		{
			req_count = std::atoi(std::string(var.substr(5)).c_str());
			item_id = std::atoi(std::string(val_str).c_str());
		}
		else if (var.rfind("ITEM^", 0) == 0)
		{
			req_count = std::atoi(std::string(var.substr(5)).c_str());
			item_id = std::atoi(std::string(val_str).c_str());
			exact = true;
		}
		else
		{
			const std::size_t star_pos = val_str.find('*');
			const std::size_t caret_pos = val_str.find('^');
			if (star_pos != std::string_view::npos)
			{
				item_id = std::atoi(std::string(trim(val_str.substr(0, star_pos))).c_str());
				req_count = std::atoi(std::string(trim(val_str.substr(star_pos + 1))).c_str());
			}
			else if (caret_pos != std::string_view::npos)
			{
				item_id = std::atoi(std::string(trim(val_str.substr(0, caret_pos))).c_str());
				req_count = std::atoi(std::string(trim(val_str.substr(caret_pos + 1))).c_str());
				exact = true;
			}
			else
			{
				item_id = std::atoi(std::string(val_str).c_str());
			}
		}

		const int has_count =
		    ctx.count_item ? ctx.count_item(ctx.player, item_id, ctx.userdata) : 0;
		if (exact)
		{
			return has_count == req_count;
		}
		if (op == CmpOp::kEq)
			return has_count >= req_count;
		if (op == CmpOp::kNe)
			return has_count == 0;
		return compareInt(has_count, op, req_count);
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
		return compareInt(free_slots, op, val);
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
		return compareInt(free_slots, op, val);
	}
	else if (var == "GOLD" || var == "gold")
	{
		const int val = std::atoi(std::string(val_str).c_str());
		return compareInt(ctx.player.gold, op, val);
	}
	else if (var == "LV")
	{
		const int val = std::atoi(std::string(val_str).c_str());
		return compareInt(ctx.player.level, op, val);
	}
	else if (var == "TRANS" || var == "TRANS7")
	{
		const int trans = ctx.get_transmigration ? ctx.get_transmigration(ctx.player, ctx.userdata) : 0;
		const int val = std::atoi(std::string(val_str).c_str());
		return compareInt(trans, op, val);
	}
	else if (var == "FAME")
	{
		const int fame = ctx.get_fame ? ctx.get_fame(ctx.player, ctx.userdata) : 0;
		const int val = std::atoi(std::string(val_str).c_str());
		return compareInt(fame, op, val);
	}
	else if (var == "FM" || var == "FAMILY")
	{
		const int fm_id = ctx.get_family_id ? static_cast<int>(ctx.get_family_id(ctx.player, ctx.userdata)) : 0;
		const int val = std::atoi(std::string(val_str).c_str());
		return compareInt(fm_id, op, val);
	}
	else if (var == "PROF" || var == "CLASS" || var == "PROFESSION")
	{
		const int prof = static_cast<int>(ctx.player.profession_class);
		const int val = std::atoi(std::string(val_str).c_str());
		return compareInt(prof, op, val);
	}
	else if (var == "HP" || var == "hp")
	{
		const int val = std::atoi(std::string(val_str).c_str());
		return compareInt(ctx.player.hp, op, val);
	}
	else if (var == "MP" || var == "mp")
	{
		const int val = std::atoi(std::string(val_str).c_str());
		return compareInt(ctx.player.mp, op, val);
	}
	else if (var == "SP" || var == "SKCP")
	{
		const int val = std::atoi(std::string(val_str).c_str());
		return compareInt(ctx.player.skillup_points, op, val);
	}
	else if (var.starts_with("NOWEV"))
	{
		// 支持 NOWEV:10=1 与 NOWEV=10
		int flag = -1;
		const std::size_t colon = var.find(':');
		if (colon != std::string_view::npos)
		{
			flag = std::atoi(std::string(var.substr(colon + 1)).c_str());
			if (flag < 0 || flag >= 256)
				return false;
			const int target = std::atoi(std::string(val_str).c_str());
			const bool has = ctx.player.hasNowEvent(flag);
			return (op == CmpOp::kEq) ? (has == (target != 0)) : (has != (target != 0));
		}
		else
		{
			flag = std::atoi(std::string(val_str).c_str());
			if (flag < 0 || flag >= 256)
				return false;
			const bool has = ctx.player.hasNowEvent(flag);
			if (op == CmpOp::kEq)
				return has;
			if (op == CmpOp::kNe)
				return !has;
			return false;
		}
	}
	else if (var.starts_with("ENDEV"))
	{
		// 支持 ENDEV:10=1 与 ENDEV=10
		int flag = -1;
		const std::size_t colon = var.find(':');
		if (colon != std::string_view::npos)
		{
			flag = std::atoi(std::string(var.substr(colon + 1)).c_str());
			if (flag < 0 || flag >= 256)
				return false;
			const int target = std::atoi(std::string(val_str).c_str());
			const bool has = ctx.player.hasEndEvent(flag);
			return (op == CmpOp::kEq) ? (has == (target != 0)) : (has != (target != 0));
		}
		else
		{
			flag = std::atoi(std::string(val_str).c_str());
			if (flag < 0 || flag >= 256)
				return false;
			const bool has = ctx.player.hasEndEvent(flag);
			if (op == CmpOp::kEq)
				return has;
			if (op == CmpOp::kNe)
				return !has;
			return false;
		}
	}

	return false;
}

// 递归下降复合条件解析器 (支持 &, |, !, 括号优先级)
class ExprParser
{
  public:
	ExprParser(std::string_view expr, const EventCheckContext &ctx)
	    : src_(expr), ctx_(ctx)
	{
		advance();
	}

	bool parse()
	{
		return parseOr();
	}

  private:
	enum class TokenType
	{
		kEof,
		kLParen,
		kRParen,
		kAnd,
		kOr,
		kNot,
		kAtom
	};

	std::string_view src_;
	const EventCheckContext &ctx_;
	std::size_t pos_ = 0;
	TokenType cur_type_ = TokenType::kEof;
	std::string_view cur_atom_{};

	void skipWhitespace()
	{
		while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_])))
			++pos_;
	}

	void advance()
	{
		skipWhitespace();
		if (pos_ >= src_.size())
		{
			cur_type_ = TokenType::kEof;
			cur_atom_ = {};
			return;
		}

		const char c = src_[pos_];
		if (c == '(')
		{
			cur_type_ = TokenType::kLParen;
			++pos_;
		}
		else if (c == ')')
		{
			cur_type_ = TokenType::kRParen;
			++pos_;
		}
		else if (c == '&')
		{
			cur_type_ = TokenType::kAnd;
			++pos_;
			if (pos_ < src_.size() && src_[pos_] == '&')
				++pos_;
		}
		else if (c == '|')
		{
			cur_type_ = TokenType::kOr;
			++pos_;
			if (pos_ < src_.size() && src_[pos_] == '|')
				++pos_;
		}
		else if (c == '!' && (pos_ + 1 >= src_.size() || src_[pos_ + 1] != '='))
		{
			cur_type_ = TokenType::kNot;
			++pos_;
		}
		else
		{
			// Atom token: 读取直到遇见 (, ), &, |, ! (非!=), 或结尾
			const std::size_t start = pos_;
			while (pos_ < src_.size())
			{
				const char ch = src_[pos_];
				if (ch == '(' || ch == ')' || ch == '&' || ch == '|')
					break;
				if (ch == '!' && pos_ + 1 < src_.size() && src_[pos_ + 1] != '=')
					break;
				++pos_;
			}
			cur_type_ = TokenType::kAtom;
			cur_atom_ = trim(src_.substr(start, pos_ - start));
		}
	}

	bool parseOr()
	{
		bool lhs = parseAnd();
		while (cur_type_ == TokenType::kOr)
		{
			advance();
			const bool rhs = parseAnd();
			lhs = lhs || rhs;
		}
		return lhs;
	}

	bool parseAnd()
	{
		bool lhs = parseUnary();
		while (cur_type_ == TokenType::kAnd)
		{
			advance();
			const bool rhs = parseUnary();
			lhs = lhs && rhs;
		}
		return lhs;
	}

	bool parseUnary()
	{
		if (cur_type_ == TokenType::kNot)
		{
			advance();
			return !parseUnary();
		}
		return parsePrimary();
	}

	bool parsePrimary()
	{
		if (cur_type_ == TokenType::kLParen)
		{
			advance();
			const bool res = parseOr();
			if (cur_type_ == TokenType::kRParen)
				advance();
			return res;
		}
		if (cur_type_ == TokenType::kAtom)
		{
			const std::string_view atom = cur_atom_;
			advance();
			return evaluateAtom(atom, ctx_);
		}
		return false;
	}
};

} // namespace

int evaluateEventCondition(std::string_view condition_raw, const EventCheckContext &ctx)
{
	const std::string_view cond = trim(condition_raw);
	if (cond.empty())
		return 1; // 空条件默认命中第 1 分支

	const auto branches = splitTopLevelBranches(cond);
	for (std::size_t i = 0; i < branches.size(); ++i)
	{
		const std::string_view branch = trim(branches[i]);
		if (branch.empty())
			continue; // 空分支恒假，但保留位置序号 (09 §9.2 缺陷 4)

		ExprParser parser(branch, ctx);
		if (parser.parse())
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
		const std::size_t nl = argstr.find('\n', start);
		const std::string_view line = (nl == std::string_view::npos)
		                                  ? argstr.substr(start)
		                                  : argstr.substr(start, nl - start);
		start = (nl == std::string_view::npos) ? argstr.size() : nl + 1;

		const std::string_view trimmed_line = trim(line);
		if (trimmed_line.empty())
			continue;

		// 检查 EventEnd
		if (trimmed_line.find("EventEnd") != std::string_view::npos)
		{
			flush_block();
			continue;
		}

		// 按 '|' 再次分段
		const auto segments = split(trimmed_line, '|');
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
			else if (key.find("RequestMsg") != std::string_view::npos)
			{
				current.request_msg = std::string(val);
			}
			else if (key == "EndSetFlg" || key == "EvEnd" || key == "SetEndEvent")
			{
				current.end_set_flg = std::string(val);
			}
			else if (key == "CleanFlg" || key == "EvClr")
			{
				current.clean_flg = std::string(val);
			}
			else if (key == "SetNowEvent" || key == "EvNow")
			{
				current.set_now_flg = std::string(val);
			}
			else if (key == "ClearNowEvent")
			{
				current.clean_now_flg = std::string(val);
			}
			else if (key == "ClearEndEvent")
			{
				current.clean_end_flg = std::string(val);
			}
			else if (key == "GetItem" || key == "AddItem")
			{
				current.get_item = std::string(val);
			}
			else if (key == "DelItem")
			{
				current.del_item = std::string(val);
			}
			else if (key == "GetPet" || key == "AddPet")
			{
				current.get_pet = std::string(val);
			}
			else if (key == "DelPet")
			{
				current.del_pet = std::string(val);
			}
			else if (key == "GetStone" || key == "AddGold")
			{
				current.get_stone = std::atoi(std::string(val).c_str());
			}
			else if (key == "DelStone" || key == "DelGold")
			{
				current.del_stone = std::atoi(std::string(val).c_str());
			}
			else if (key == "AddExps" || key == "AddExp")
			{
				current.add_exp = std::atoi(std::string(val).c_str());
			}
			else if (key == "AddSkillPoint" || key == "AddPFSkillPoint")
			{
				current.add_skill_points = std::atoi(std::string(val).c_str());
			}
			else if (key == "Heal" || key == "HealHp")
			{
				current.heal_hp = std::atoi(std::string(val).c_str());
			}
			else if (key == "HealMp")
			{
				current.heal_mp = std::atoi(std::string(val).c_str());
			}
			else if (key == "NpcWarp" || key == "Warp")
			{
				current.npc_warp = std::string(val);
			}
			else if (key == "AddFame")
			{
				current.add_fame = std::atoi(std::string(val).c_str());
			}
			else if (key == "DelFame")
			{
				current.del_fame = std::atoi(std::string(val).c_str());
			}
			else if (key == "NextBlock" || key == "NextWindow")
			{
				current.next_block_index = std::atoi(std::string(val).c_str());
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
