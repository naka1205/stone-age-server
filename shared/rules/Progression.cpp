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
#include <algorithm>

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

// ══ 经验与等级成长 (批次 P.1)═════════════════════════════════════════

// 官方 200 项经验门限表（csa8.0/data/exp.txt 与 char_data.c:1231 逐项比对完全一致）。
// 下标 0..199 对应升到等级 1..200 所需累计经验。
static constexpr std::int32_t kNeedLevelUpTbls[200] = {
    0, 2, 6, 18, 37, 67, 110, 170,
    246, 344, 464, 610, 782, 986, 1221, 1491,
    1798, 2146, 2534, 2968, 3448, 3978, 4558, 5194,
    5885, 6635, 7446, 8322, 9262, 10272, 11352, 12506,
    13734, 15042, 16429, 17899, 19454, 21098, 22830, 24656,
    26576, 28594, 30710, 32930, 35253, 37683, 40222, 42874,
    45638, 48520, 51520, 54642, 57886, 61258, 64757, 68387,
    72150, 76050, 80086, 84264, 106110, 113412, 121149, 129352,
    138044, 147256, 157019, 167366, 178334, 189958, 202282, 215348,
    229205, 243901, 259495, 276041, 293606, 312258, 332071, 353126,
    375511, 399318, 424655, 451631, 480370, 511007, 543686, 578571,
    615838, 655680, 698312, 743971, 792917, 845443, 901868, 962554,
    1027899, 1098353, 1174420, 1256663, 1345723, 1442322, 1547281, 1661531,
    1786143, 1922340, 2071533, 2235351, 2415689, 2614754, 2835137, 3079892,
    3352633, 3657676, 4000195, 4386445, 4824041, 5322323, 5892866, 6550125,
    12326614, 15496114, 20025638, 26821885, 37698249, 56734876, 68097265, 68290815,
    68487425, 68687119, 68889921, 69095855, 69304945, 69517215, 69732689, 69951391,
    70173345, 70398575, 70627105, 70858959, 71244161, 71342735, 71584705, 71830095,
    72078929, 72331231, 72587025, 72846335, 73109185, 73615599, 73655601, 73929215,
    74206465, 74487375, 74771969, 75060271, 75352305, 75648095, 75947665, 76421039,
    76563241, 76874295, 77189225, 77508055, 77830809, 78157511, 78488185, 78822855,
    79161545, 79724279, 79856081, 80206975, 80561985, 80921135, 81284449, 81651951,
    82023665, 82399615, 82779825, 83434319, 83558121, 83951255, 84348745, 84750615,
    85156889, 85567591, 85982745, 86402375, 86826505, 87575159, 87693361, 88131135,
    88573505, 89020495, 89472129, 89928431, 90389425, 90855135, 91325585, 91800799};

std::int32_t getNeedLevelUpExp(std::int32_t target_level) noexcept
{
	if (target_level <= 1)
		return 0;
	if (target_level > 200)
		return -1;
	return kNeedLevelUpTbls[target_level - 1];
}

PlayerLevelUpResult checkPlayerLevelUp(std::int32_t current_level, std::int32_t current_exp,
                                       std::int32_t max_level) noexcept
{
	PlayerLevelUpResult res{};
	res.old_level = current_level;
	res.new_level = current_level;
	if (current_level >= max_level || current_level >= 200)
		return res;

	std::int32_t lvl = current_level;
	while (lvl < max_level && lvl < 200)
	{
		const std::int32_t next_exp = getNeedLevelUpExp(lvl + 1);
		if (next_exp < 0 || current_exp < next_exp)
			break;
		++lvl;
	}

	res.new_level = lvl;
	res.levels_gained = lvl - current_level;
	res.skillup_points_gained = res.levels_gained * 3;
	res.charm_gained = res.levels_gained;
	return res;
}

PetLevelUpStats rollPetLevelUp(std::uint8_t growth_vital, std::uint8_t growth_str,
                               std::uint8_t growth_tough, std::uint8_t growth_dex,
                               std::int32_t petrank, Random &rng) noexcept
{
	struct RankRange
	{
		std::int32_t min;
		std::int32_t max;
	};
	static constexpr RankRange kRankRandTbl[6] = {
	    {450, 500},
	    {470, 520},
	    {490, 540},
	    {510, 560},
	    {530, 580},
	    {550, 600},
	};

	if (petrank < 0 || petrank > 5)
		petrank = 0;

	double param[4] = {0.0, 0.0, 0.0, 0.0};
	for (int i = 0; i < 10; ++i)
	{
		const int slot = rng.rand(0, 3);
		if (slot >= 0 && slot < 4)
			param[slot] += 1.0;
	}

	const double fRand = static_cast<double>(rng.rand(kRankRandTbl[petrank].min, kRankRandTbl[petrank].max)) * 0.01;

	PetLevelUpStats out{};
	const double v = static_cast<double>(growth_vital) * fRand + param[0] * fRand;
	const double s = static_cast<double>(growth_str) * fRand + param[1] * fRand;
	const double t = static_cast<double>(growth_tough) * fRand + param[2] * fRand;
	const double d = static_cast<double>(growth_dex) * fRand + param[3] * fRand;

	out.added_vital = std::max(0, static_cast<std::int32_t>(v));
	out.added_str = std::max(0, static_cast<std::int32_t>(s));
	out.added_tough = std::max(0, static_cast<std::int32_t>(t));
	out.added_dex = std::max(0, static_cast<std::int32_t>(d));

	return out;
}

// ══ 装备属性修正 (批次 P.1)═══════════════════════════════════════════

DerivedStats deriveEquippedStats(std::int32_t vital, std::int32_t str,
                                 std::int32_t tough, std::int32_t dex,
                                 const EquipModifiers &equip) noexcept
{
	DerivedStats base = deriveBaseStats(vital, str, tough, dex);
	base.attack = std::max(0, base.attack + equip.modify_attack);
	base.defense = std::max(0, base.defense + equip.modify_defense);
	base.quick = std::max(1, base.quick + equip.modify_quick);
	base.max_hp = std::max(1, base.max_hp + equip.modify_hp);
	return base;
}

std::int32_t getEquipSlotForCategory(std::int32_t category) noexcept
{
	switch (category)
	{
	case 6:       // ITEM_HELM
		return 0; // kHead
	case 7:       // ITEM_ARMOUR
		return 1; // kBody
	case 0:       // ITEM_FIST
	case 1:       // ITEM_AXE
	case 2:       // ITEM_CLUB
	case 3:       // ITEM_SPEAR
	case 4:       // ITEM_BOW
	case 17:      // ITEM_BOOMERANG
	case 18:      // ITEM_BOUNDTHROW
	case 19:      // ITEM_BREAKTHROW
		return 2; // kArm
	case 5:       // ITEM_SHIELD
	case 25:      // ITEM_WSHIELD
		return 3; // kShield
	case 8:       // ITEM_BRACELET
	case 9:       // ITEM_MUSIC
	case 10:      // ITEM_NECKLACE
	case 11:      // ITEM_RING
	case 12:      // ITEM_BELT
	case 13:      // ITEM_EARRING
	case 14:      // ITEM_NOSERING
	case 15:      // ITEM_AMULET
		return 4; // kDecoration1
	case 26:      // ITEM_WSHOES
		return 6; // kShoes
	case 27:      // ITEM_WGLOVE
		return 7; // kGlove
	case 24:      // ITEM_WBELT
		return 8; // kBelt
	default:
		return -1;
	}
}

std::int32_t getProfessionStatCap(ProfessionClass profession, StatCategory category) noexcept
{
	switch (profession)
	{
	case ProfessionClass::kFighter:
		if (category == StatCategory::kDex)
			return 200 * 100;
		break;
	case ProfessionClass::kWizard:
		if (category == StatCategory::kStr || category == StatCategory::kTough)
			return 200 * 100;
		break;
	case ProfessionClass::kHunter:
		if (category == StatCategory::kStr || category == StatCategory::kTough)
			return 200 * 100;
		if (category == StatCategory::kDex)
			return 400 * 100;
		break;
	case ProfessionClass::kNone:
	default:
		break;
	}
	return -1;
}

std::int32_t computeHunterEncounterFix(int skill_level, int rate, bool is_track) noexcept
{
	const int eff_level = skill_level / 10;
	if (eff_level <= 0)
		return 0;
	const int per = eff_level * rate;
	return is_track ? per : -per;
}

StatAllocationResult applyStatAllocation(std::int32_t vital, std::int32_t str,
                                         std::int32_t tough, std::int32_t dex,
                                         std::int32_t skillup_points,
                                         StatCategory category,
                                         std::int32_t points_to_allocate,
                                         ProfessionClass profession) noexcept
{
	StatAllocationResult res{};
	res.remaining_skillup_points = skillup_points;
	res.new_vital = vital;
	res.new_str = str;
	res.new_tough = tough;
	res.new_dex = dex;

	if (points_to_allocate <= 0 || skillup_points < points_to_allocate)
	{
		res.success = false;
		return res;
	}

	const auto cat_val = static_cast<std::uint8_t>(category);
	if (cat_val > static_cast<std::uint8_t>(StatCategory::kDex))
	{
		res.success = false;
		return res;
	}

	const std::int32_t cap = getProfessionStatCap(profession, category);
	const std::int32_t delta = points_to_allocate * 100;
	switch (category)
	{
	case StatCategory::kVital:
		if (cap >= 0 && (vital >= cap || vital + delta > cap))
			return res;
		res.new_vital += delta;
		break;
	case StatCategory::kStr:
		if (cap >= 0 && (str >= cap || str + delta > cap))
			return res;
		res.new_str += delta;
		break;
	case StatCategory::kTough:
		if (cap >= 0 && (tough >= cap || tough + delta > cap))
			return res;
		res.new_tough += delta;
		break;
	case StatCategory::kDex:
		if (cap >= 0 && (dex >= cap || dex + delta > cap))
			return res;
		res.new_dex += delta;
		break;
	default:
		res.success = false;
		return res;
	}

	res.remaining_skillup_points -= points_to_allocate;
	res.success = true;
	return res;
}

} // namespace SA::Rules
