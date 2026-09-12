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

// ══ 四维生成(批次 M.4a)══════════════════════════════════════════════
//
// ★★ 顺序就是语义 —— `ENEMY_createEnemy`(`char/enemy.c`)四步,**一步都不能挪**:
//
//   :1045-1048  四维基数各 `+= RAND(0,4) - 2`        ← ±2 抖动
//   :1052-1056  **成长率打包**                        ← ★ 用的是**此刻**的基数
//   :1058-1064  `for i<10`:`RAND(0,3)` 各 ++          ← 再撒 10 点
//   :1067-1070  `PARAM_CAL` 算四维                    ← 用的是**加完 10 点**的基数
//
// ⚠️★★ **成长率取「+10 之前」、四维取「+10 之后」** —— 这是照抄时最容易做错的一处:
//    把成长率挪到循环后面,或把两者合成一次计算,成长率会系统性偏大(期望 +2.5/维)。
//    ⇒ 本文件把两处赋值的先后关系用代码结构固定下来,并有用例反向钉住
//      (实测把打包挪到循环之后:`growth_vital` 当场 30 ≠ 20 报红)。
//
// ⚠️ `:1040` 与 `:1042` 是 `#if 1` / `#else` 两个版本的 `PARAM_CAL`,**只有 :1040 生效**
//    (差别:生效版读局部 `level`,另一版读 `E_PAR(ENEMY_LV)`)。unifdef 不解析 `#if 1`,
//    读展开视图时两行都在,别抄错那一行。
SpawnStats rollSpawnStats(const SpawnTemplate &tmpl, std::int32_t level,
                          Random &rng, const RulesConfig &cfg) noexcept
{
	// ── 第 ① 步:四维基数 ±2 抖动(:1045-1048)────────────────────────
	//
	// ★ `RAND(0,4) - 2` ⇒ 取值 {−2,−1,0,1,2},**闭区间**(Random::rand 的契约)。
	// ⚠️ 四次调用的顺序 vital → str → tough → dex 必须与源码一致:
	//    换顺序不会有任何一处报错,但同种子下产出不同 ⇒ 可回放性静默失效。
	std::int32_t base_vital = tmpl.base_vital + rng.rand(0, 4) - 2;
	std::int32_t base_str = tmpl.base_str + rng.rand(0, 4) - 2;
	std::int32_t base_tough = tmpl.base_tough + rng.rand(0, 4) - 2;
	std::int32_t base_dex = tmpl.base_dex + rng.rand(0, 4) - 2;

	SpawnStats out{};

	// ── 第 ② 步:成长率打包(:1052-1056)★ 必须在第 ③ 步之前 ────────────
	//
	// SSRC80 char/enemy.c:1165–1169：四个移位值相加，低位负数会跨字节借位。
	// 用无符号模 2^32 算术保留实际位模式，避免 C++ 的负数左移未定义行为。
	const std::uint32_t packed =
	    (static_cast<std::uint32_t>(base_vital) << 24) +
	    (static_cast<std::uint32_t>(base_str) << 16) +
	    (static_cast<std::uint32_t>(base_tough) << 8) +
	    static_cast<std::uint32_t>(base_dex);
	out.growth_vital = static_cast<std::uint8_t>(packed >> 24);
	out.growth_str = static_cast<std::uint8_t>(packed >> 16);
	out.growth_tough = static_cast<std::uint8_t>(packed >> 8);
	out.growth_dex = static_cast<std::uint8_t>(packed);

	// ── 第 ③ 步:再撒 10 点(:1058-1064)──────────────────────────────
	//
	// ★ 原版是四条独立的 `if`(不是 else-if 链),但四个分支互斥 ⇒ 行为等价于
	//   switch。这里照原样写 if 链没有收益,用 switch 更明确;
	//   ⚠️ **rng 的调用次数与顺序不变**(每轮恰好一次 `rand(0,3)`),这才是要紧的。
	for (int i = 0; i < 10; ++i)
	{
		switch (rng.rand(0, 3))
		{
		case 0:
			++base_vital;
			break;
		case 1:
			++base_str;
			break;
		case 2:
			++base_tough;
			break;
		default:
			++base_dex;
			break;
		}
	}

	// ── 第 ④ 步:PARAM_CAL(:1040 + :1067-1070)───────────────────────
	//
	//   PARAM_CAL(base) = ((level − 1) × lvup_point + init_num) × base
	//
	// ★ DR-DT1:`lvup_point` 是**浮点**,默认复刻截断;开关打开则先截断成整数
	//   再算(那才是原版 `atoi` 的行为)。
	// ⚠️★ 截断**只发生在最后一次**(赋给 int32 时),与原版一致 ——
	//    原版整条表达式是 int 运算,而本实现是 double 运算后截断一次。
	//    ★ 两者对整数 `lvup_point` **逐位相同**:本域量级上界
	//      `((160−1) × 10 + 500) × 400 ≈ 8.4e5`,远在 double 的精确整数范围(2^53)内
	//      ⇒ double 运算对整数输入是精确的,不存在"多一次舍入"。
	const double lvup = cfg.replicate_atoi_truncation
	                        ? static_cast<double>(static_cast<std::int32_t>(tmpl.lvup_point))
	                        : tmpl.lvup_point;
	const double coef = (level - 1) * lvup + tmpl.init_num;

	out.vital = static_cast<std::int32_t>(coef * base_vital);
	out.str = static_cast<std::int32_t>(coef * base_str);
	out.tough = static_cast<std::int32_t>(coef * base_tough);
	out.dex = static_cast<std::int32_t>(coef * base_dex);

	return out;
}

// ══ 评级档位(批次 M.4b)══════════════════════════════════════════════
//
// 1:1 移植 `ENEMY_getRank`(`char/enemy.c:802-840`)。判据与陷阱见 Progression.h 声明处
// ——最要紧的一条:读的是**模板原始基数**,不是 `ENEMY_createEnemy` 里被 ±2 改过的局部拷贝。
std::int32_t enemyRank(const SpawnTemplate &tmpl) noexcept
{
	// 源码 :825-828:四维基数直接相加(注释称其为「总成长率」)。
	const std::int32_t sum = tmpl.base_vital + tmpl.base_str + tmpl.base_tough + tmpl.base_dex;

	// 源码 :812-819 的 `ranktbl`。★ 只取 `num` 一列 —— `rank` 那列源码从未读(见声明处)。
	static constexpr std::int32_t kRankThresholds[] = {100, 95, 90, 85, 80, 0};

	// 源码 :830-836:`ranknum` 初值 0,首个满足即 break。
	// ⚠️ 初值 0 兼作"循环走完也没命中"的结果(仅负和可能走到)⇒ 照抄。
	std::int32_t ranknum = 0;
	for (std::int32_t i = 0; i < static_cast<std::int32_t>(sizeof(kRankThresholds) / sizeof(kRankThresholds[0])); ++i)
	{
		if (sum >= kRankThresholds[i])
		{
			ranknum = i;
			break;
		}
	}
	return ranknum;
}

} // namespace SA::Rules
