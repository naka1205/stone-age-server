// shared/rules/Progression.cpp —— 成长养成:属性推导的实现(DR-DT9)
//
// ★★ 逐位复刻纪律(与 Battle.cpp 的 ApplyElementMatrix 同理由:「形状也是公式的
//    一部分」)—— 本文件三处照原版**原样**保留,任何一处"化简"都会让两端逐位不同:
//
//   ① **表达式保留原形** `x*0.01*0.1`,不预乘成 `x*0.001`。
//      `x*0.01*0.1` 是两次运行期乘法,`x*0.001` 是一次 —— IEEE754 下结果可逐位不同。
//   ② **attack / defense / quick 的中间是 `double`**:原版无 float 中间变量,
//      `CHAR_getInt(...)*0.01` 里 int 提升为 double,整个表达式 double,传给
//      `CHAR_setWorkInt` 的 int 形参时截断一次。
//   ③ ★ **max_hp 经 `float` 中间变量**:原版 `char.c:3397` 是 `float hp;`,
//      `hp = (...)*0.01` 有一次 double→float 收窄,再 `(int)hp` 截断 ——
//      与三围的纯 double 路径**不同**。类型语义是公式的一部分,保留 float。
//
// ⚠️ 逐位一致最终依赖 shared/CMakeLists.txt 的 `-ffp-contract=off` / `/fp:precise`。

#include "rules/Progression.h"

namespace SA::Rules
{

DerivedStats deriveBaseStats(std::int32_t vital, std::int32_t str,
                             std::int32_t tough, std::int32_t dex) noexcept
{
	DerivedStats out{};

	// ── attack ← WORKFIXSTR(char.c:3441-3450)─────────────────────
	//   力量为主(×1.0),耐力 / 体力各 ×0.1,速度 ×0.05。
	const double workfix_str = str * 0.01 * 1.0 + tough * 0.01 * 0.1 + vital * 0.01 * 0.1 + dex * 0.01 * 0.05;
	out.attack = static_cast<std::int32_t>(workfix_str);

	// ── defense ← WORKFIXTOUGH(char.c:3452-3461)──────────────────
	//   耐力为主(×1.0),力量 / 体力各 ×0.1,速度 ×0.05(与 attack 对称,主项互换)。
	const double workfix_tough = tough * 0.01 * 1.0 + str * 0.01 * 0.1 + vital * 0.01 * 0.1 + dex * 0.01 * 0.05;
	out.defense = static_cast<std::int32_t>(workfix_tough);

	// ── quick ← WORKFIXDEX(char.c:3438-3439)──────────────────────
	//   仅速度 ×0.01。⚠️ 速度 < 100 时截断为 0 —— 反直觉但照源码(DR-DT9)。
	out.quick = static_cast<std::int32_t>(dex * 0.01);

	// ── max_hp(char.c:3483-3487)──────────────────────────────────
	//   ⚠️★ 原版 `float hp`(:3397):体力 ×4 + 其余三维各 ×1,总和 ×0.01。
	//     必须先 double→float 收窄再截断(见文件头 ③)。
	const float hp = static_cast<float>((vital * 4 + str + tough + dex) * 0.01);
	out.max_hp = static_cast<std::int32_t>(hp);

	return out;
}

} // namespace SA::Rules
