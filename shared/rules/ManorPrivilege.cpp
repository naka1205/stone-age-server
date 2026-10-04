// shared/rules/ManorPrivilege.cpp —— 庄园特权体系、金库税收分红与骑乘认证纯函数规则层实现

#include "rules/ManorPrivilege.h"

#include <algorithm>

namespace SA::Rules
{

namespace
{

const ExamQuestion kQuizQuestions[] = {
    {1, "萨姆吉尔庄园专属骑乘是哪一种系别的暴龙？", {"绿暴", "红暴(巴朵兰恩)", "飞龙", "雷龙"}, 1},
    {2, "玛丽娜斯庄园骑乘考官位于尼斯大陆的哪个村庄？", {"玛丽娜斯(渔村)", "萨姆吉尔村", "加加村", "卡鲁它那村"}, 0},
    {3, "骑乘宠物在战斗中承担主人伤害的契合度比例，受到什么数值的核心影响？", {"宠物忠诚度", "宠物敏捷", "主人魅力", "宠物转生等级"}, 0},
    {4, "四大庄园家族成员在属地内享有何种专属特权？", {"专属免考骑乘与庄园光环", "无限购买道具", "免受任何死亡掉落", "随意瞬移任意地图"}, 0},
};

inline constexpr int kQuestionCount = sizeof(kQuizQuestions) / sizeof(kQuizQuestions[0]);

} // namespace

ManorAuraBonus computeManorAura(ManorKind manor, bool in_domain) noexcept
{
	ManorAuraBonus bonus{};
	if (manor == ManorKind::kNone)
	{
		return bonus;
	}

	if (in_domain)
	{
		bonus.attack_percent = 5.0f;
		bonus.defense_percent = 5.0f;
		bonus.quick_percent = 5.0f;
		bonus.escape_bonus_rate = 10;
		bonus.damage_reduction = 0.05f;
	}
	else
	{
		bonus.attack_percent = 2.0f;
		bonus.defense_percent = 2.0f;
		bonus.quick_percent = 2.0f;
		bonus.escape_bonus_rate = 5;
		bonus.damage_reduction = 0.02f;
	}

	return bonus;
}

std::uint32_t calculateTradeTax(std::uint32_t trade_volume, int tax_rate_percent) noexcept
{
	if (trade_volume == 0)
	{
		return 0;
	}
	const int rate = std::clamp(tax_rate_percent, kMinTaxRatePercent, kMaxTaxRatePercent);
	const std::uint64_t tax = (static_cast<std::uint64_t>(trade_volume) * static_cast<std::uint64_t>(rate)) / 100;
	return static_cast<std::uint32_t>(std::min<std::uint64_t>(tax, trade_volume));
}

std::uint32_t calculateRideExamManorShare(std::uint32_t exam_fee, int share_rate_percent) noexcept
{
	if (exam_fee == 0)
	{
		return 0;
	}
	const int rate = std::clamp(share_rate_percent, 10, 50);
	const std::uint64_t share = (static_cast<std::uint64_t>(exam_fee) * static_cast<std::uint64_t>(rate)) / 100;
	return static_cast<std::uint32_t>(std::min<std::uint64_t>(share, exam_fee));
}

std::uint32_t calculateDividendShare(
    std::uint32_t dividend_pool,
    ManorMemberRole role,
    int contribution,
    int total_members) noexcept
{
	if (dividend_pool == 0 || total_members <= 0)
	{
		return 0;
	}

	const std::uint64_t pool = dividend_pool;
	std::uint64_t share = 0;
	const int contrib_bonus = std::clamp(contribution / 100, 0, 10); // 每 100 点贡献 +1% (最高 +10%)

	switch (role)
	{
	case ManorMemberRole::kLeader:
	{
		const std::uint64_t rate = static_cast<std::uint64_t>(25 + contrib_bonus); // 25% ~ 35%
		share = (pool * rate) / 100;
		break;
	}
	case ManorMemberRole::kElder:
	{
		const std::uint64_t rate = static_cast<std::uint64_t>(15 + contrib_bonus / 2); // 15% ~ 20%
		share = (pool * rate) / 100;
		break;
	}
	case ManorMemberRole::kNone:
	case ManorMemberRole::kApply:
		return 0;
	case ManorMemberRole::kMember:
	default:
	{
		// 剩余 50% 由普通成员平分
		const std::uint64_t member_pool = (pool * 50) / 100;
		const std::uint64_t base_share = member_pool / static_cast<std::uint64_t>(std::max(1, total_members));
		const std::uint64_t bonus_amount = (base_share * static_cast<std::uint64_t>(contrib_bonus)) / 100;
		share = base_share + bonus_amount;
		break;
	}
	}

	return static_cast<std::uint32_t>(std::min<std::uint64_t>(share, pool));
}

const ExamQuestion *getRideExamQuestion(int question_id) noexcept
{
	for (int i = 0; i < kQuestionCount; ++i)
	{
		if (kQuizQuestions[i].question_id == question_id)
		{
			return &kQuizQuestions[i];
		}
	}
	return nullptr;
}

bool verifyRideExamAnswer(int question_id, int selected_option) noexcept
{
	const auto *q = getRideExamQuestion(question_id);
	if (q == nullptr)
	{
		return false;
	}
	return q->correct_option == selected_option;
}

const char *getManorKindName(ManorKind manor) noexcept
{
	switch (manor)
	{
	case ManorKind::kSamo:
		return "萨姆吉尔庄园";
	case ManorKind::kMarina:
		return "玛丽娜斯庄园";
	case ManorKind::kJaja:
		return "加加庄园";
	case ManorKind::kKarutana:
		return "卡鲁它那庄园";
	default:
		return "无庄园";
	}
}

} // namespace SA::Rules
