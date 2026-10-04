// shared/rules/ManorPrivilege.h —— 庄园特权体系、金库税收分红与骑乘认证纯函数规则层
//
// 依据石器时代 8.0 四大庄园进阶体系设计。
// 严守 01 §4 / 05 §1.5 纯函数约束：零外部依赖，禁止包含 <chrono> / <ctime>，严禁 I/O。

#ifndef __SA_RULES_MANOR_PRIVILEGE_H__
#define __SA_RULES_MANOR_PRIVILEGE_H__

#include <cstdint>
#include <string_view>

namespace SA::Rules
{

// 四大庄园枚举
enum class ManorKind : std::uint8_t
{
	kNone = 0,     // 无庄园
	kSamo = 1,     // 萨姆吉尔庄园 (暴龙系)
	kMarina = 2,   // 玛丽娜斯庄园 (绿暴系)
	kJaja = 3,     // 加加庄园 (飞龙系)
	kKarutana = 4, // 卡鲁它那庄园 (雷龙系)
};

// 家族职位枚举 (用于金库分红计算, 1:1 对齐官方 FMMEMBER_*)
enum class ManorMemberRole : std::int8_t
{
	kNone = -1,  // 无家族
	kMember = 1, // 普通成员 (FMMEMBER_MEMBER)
	kApply = 2,  // 申请加入中 (FMMEMBER_APPLY)
	kLeader = 3, // 族长 (FMMEMBER_LEADER)
	kElder = 4,  // 长老/副族长 (FMMEMBER_ELDER)
};

// 庄园专属特权光环加成 (Manor Aura)
struct ManorAuraBonus
{
	float attack_percent = 0.0f;   // 攻击力加成 (+%)
	float defense_percent = 0.0f;  // 防御力加成 (+%)
	float quick_percent = 0.0f;    // 敏捷加成 (+%)
	int escape_bonus_rate = 0;     // 遇敌逃跑率加成 (+%)
	float damage_reduction = 0.0f; // 战中减伤比例 (0.0f ~ 1.0f)
};

// 骑乘理论考核考题结构
struct ExamQuestion
{
	int question_id = 0;
	const char *question = "";
	const char *options[4] = {};
	int correct_option = 0; // 0..3
};

// 金库常量
inline constexpr std::uint32_t kMaxManorTreasury = 50000000; // 金库硬上限: 5000万石币
inline constexpr int kMinTaxRatePercent = 1;                 // 最低交易税率 1%
inline constexpr int kMaxTaxRatePercent = 10;                // 最高交易税率 10%
inline constexpr int kDefaultTaxRatePercent = 5;             // 默认交易税率 5%

// 计算庄园成员专属光环加成
// in_domain: 是否身处本庄园属地领地/村庄
ManorAuraBonus computeManorAura(ManorKind manor, bool in_domain) noexcept;

// 计算市场交易向庄园金库划拨的税收金额
std::uint32_t calculateTradeTax(std::uint32_t trade_volume, int tax_rate_percent = kDefaultTaxRatePercent) noexcept;

// 计算骑乘考官考核学费中注入金库的份额 (默认 20%)
std::uint32_t calculateRideExamManorShare(std::uint32_t exam_fee, int share_rate_percent = 20) noexcept;

// 计算家族成员分红份额 (保证总和不超过 dividend_pool)
std::uint32_t calculateDividendShare(
    std::uint32_t dividend_pool,
    ManorMemberRole role,
    int contribution,
    int total_members) noexcept;

// 骑乘考核理论问答题库查询
const ExamQuestion *getRideExamQuestion(int question_id) noexcept;

// 验证考题答案
bool verifyRideExamAnswer(int question_id, int selected_option) noexcept;

// 获取庄园名称
const char *getManorKindName(ManorKind manor) noexcept;

} // namespace SA::Rules

#endif // __SA_RULES_MANOR_PRIVILEGE_H__
