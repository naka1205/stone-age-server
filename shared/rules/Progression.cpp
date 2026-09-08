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
	// 原版:`ALLOCPOINT = (vital<<24) + (str<<16) + (tgh<<8) + dex`,消费侧
	// 一律 `(p >> shift) & 0xFF` 取回 ⇒ 语义是**四个独立的 8 bit 槽**。
	//
	// ⚠️★★ **一处有意的偏离(用户 2026-09-08 裁定,判据同 DR-DT7)**:
	//    原版用的是 `+` 而不是 `|` ⇒ 基数为负时,**低位会向高位借位、污染相邻字段**。
	//    实测 `[vital=0, str=−1, tgh=−2, dex=−2]`:
	//        原版解回 [255, 254, 253, 254]   ← 矿石类宠物的 vital 成长率 0 变成 255
	//        本实现   [  0, 255, 254, 254]   ← 保住「0 基数 ⇒ 0 成长」
	//    ⇒ 裁定 **各字段独立截断**:跨字段借位是 `+` 写成 `|` 的纯算术 bug,
	//      没有任何设计意图会让一块矿石的 vital 成长率是 255(判据同 DR-DT7
	//      「它没有任何玩法语义,是纯内存 bug ⇒ 修正」)。
	//    ⚠️ 偏离面已量化:1,053 行模板中**低三位**含 ≤1 基数的 **39 行(3.7%)是上界**,
	//      且需该字段实际被摇成负(基数 0 时约 40%)。
	//    ★ 保留的是**字段内**回绕:基数 300 ⇒ 44、−2 ⇒ 254,与原版逐位一致
	//      (实测 10 组边界,只有跨字段借位那两组分歧)。
	//
	// ⚠️ 转换写法:`static_cast<std::uint32_t>` 再窄化,**不写 `v & 0xFF`** ——
	//    后者对负数依赖二进制补码表示,而 `shared/` 锁 C++17(补码到 C++20 才强制)。
	//    负 → unsigned 的模 2^32 转换在所有版本都有定义。
	const auto pack = [](std::int32_t v) noexcept -> std::uint8_t
	{
		return static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) & 0xFFu);
	};
	out.growth_vital = pack(base_vital);
	out.growth_str = pack(base_str);
	out.growth_tough = pack(base_tough);
	out.growth_dex = pack(base_dex);

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
	// ★ DR-DT1:`lvup_point` 是**浮点**,默认不截断;开关打开则先截断成整数
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

} // namespace SA::Rules
