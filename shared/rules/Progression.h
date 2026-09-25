// shared/rules/Progression.h —— 成长养成:属性推导(DR-DT9)
//
// ★★ 与 Battle.h 同为 D2 双端共享的纯函数层(shared/rules,客户端经 FetchContent +
//    锁定 tag 编译同一份源码)。本文件回答 Combatant.h「三围是**回合准备阶段已重算
//    完毕的值**」(§169-176)里的**那次重算的基础段**:把角色的原始四维
//    (vital / str / tough / dex)推导成基础战斗三围(attack / defense / quick)+ 最大 HP。
//
// ── 移植来源 ────────────────────────────────────────────────────
//   `CHAR_complianceParameter`(展开视图 `char/char.c:3525`)本身是**编排器** ——
//   它调 `CHAR_initcharWorkInt` 建基础值 → `ITEM_equipEffect` / `Other_DefcharWorkInt`
//   叠装备 → 把 `WORKFIX{STR,TOUGH,DEX}` 拷进 `WORKATTACKPOWER/DEFENCEPOWER/QUICK`。
//   ★ 真正的公式在 `CHAR_initcharWorkInt`(`char/char.c:3419-3487`),本文件移植它的
//     **基础段**。装备 / 套装 / 技能加成(`ITEM_equipEffect` + `Other_DefcharWorkInt`,
//     item.c:1599)对**无装备单位输入全 0 即恒等** ⇒ 本批不含,接口留待装备系统(DR-DT9)。
//
// ⚠️★ **公式里没有 `level`。** 欠债 23 / DR-BT19-21 都转述为「从原始属性 **+ 等级** +
//    装备推导」—— 但源码里三围推导**全程不读等级**。等级的作用在「四维随等级成长」
//    (`PARAM_CAL`,成长率消费,另一批),不在这一步。回源码复核纠正的又一处文档分叉。
//
// ── 契约(比 Battle 更严)──────────────────────────────────────────
//   ① 纯函数:只吃四维、只吐派生值。不读全局 / 时钟 / 随机 —— **连 rng 都不需要**。
//   ② 可回放:同输入逐位相同。依赖 shared/CMakeLists.txt 的 `-ffp-contract=off`
//      (公式含 `*0.01` 浮点,FMA 合并会让两端逐位不同)。
//
// ⚠️ shared/ 只依赖标准库(01 §4);本文件连 domain/ 都不引,是最小依赖面。

#ifndef __SA_Progression_H__
#define __SA_Progression_H__

#include <cstdint>

#include "rules/Config.h"
#include "rules/RandomSource.h"

namespace SA::Rules
{

// 属性推导的产物 —— 基础战斗三围 + 最大 HP。
//
// ⚠️★ **不含 max_mp**:原版 `WORKMAXMP = CHAR_MAXMP`(`char.c:3488`)是**直接读字段**,
//    不推导 ⇒ 建进来会造出第二个真源。调用方直接用实体的 `max_mp`。
struct DerivedStats
{
	std::int32_t attack = 0;  // 原 WORKATTACKPOWER ← WORKFIXSTR
	std::int32_t defense = 0; // 原 WORKDEFENCEPOWER ← WORKFIXTOUGH
	std::int32_t quick = 0;   // 原 WORKQUICK ← WORKFIXDEX
	std::int32_t max_hp = 0;  // 原 WORKMAXHP
};

// 属性推导 —— 原始四维 → 基础战斗三围 + 最大 HP。
// 1:1 移植 `CHAR_initcharWorkInt`(`char/char.c:3419-3487`)的基础段。
//
// ★ DR-DT9:截断语义 = **复刻原版整数截断**(用户 2026-09-08 拍板)。原版每个
//   `CHAR_setWorkInt` 第三参是 `int`,浮点表达式传入即截断一次 ⇒ 本函数每个输出
//   都是「double(或 float)公式算完 `static_cast<std::int32_t>` 截断一次」。
//   DR-DT1 的成长系数同样恢复了原版实际 atoi 截断（2026-09-12）；
//   这里另外保留每个战斗属性赋给整数时的截断位置。
DerivedStats deriveBaseStats(std::int32_t vital, std::int32_t str,
                             std::int32_t tough, std::int32_t dex) noexcept;

// ══ 四维的来源:模板 + 等级 → 原始四维 + 成长率(批次 M.4a)══════════════
//
// ★★ 这是 `deriveBaseStats` 的**上游**。M.3 补完推导公式后宠物照旧打不动,
//    原因不是缺公式而是**缺四维** —— 0 是推导的不动点(`00` §9.0.29 ③)。
//    本组函数补的就是那个非 0 来源。
//
// ⚠️★ 别把这里的「成长率」与 `deriveBaseStats` 的「三围」搞混,它们是两件事:
//      本函数(`PARAM_CAL`)     = 模板 + 等级 → **四维**,并顺带产出成长率
//      `deriveBaseStats`       = 四维 → **三围**(不读等级)
//    ⇒ 等级只在这一步起作用。`06` §3.4 已把这条分工写进文档。

// 生成模板里参与四维生成的那几列 —— `enemybase1.txt` 的**子集**。
//
// ★ 有意只取 6 列而不是把 56 列的模板整个搬进 L3:那 56 列里除这几列之外
//   全是图号 / 宠技 / 状态抗性 / 融合码,属 L4 内容与别的子系统。
//   ⇒ L3 的输入面只含**算四维真正要用到的**东西,内容表的形状变化不波及纯函数层。
//   (同 `rollCapture` 只吃判定要用的十来个数,不吃整个 Char。)
struct SpawnTemplate
{
	// E_T_LVUPPOINT —— 四维成长系数。
	//
	// ★★ **是浮点,不是 int**(DR-DT1)。原版载入器 `ENEMYTEMP_initEnemy`
	//   (`char/enemy.c:305`)用 `atoi` 读 ⇒ `4.50` 截断成 `4`;
	//   DR-DT1 裁定**浮点建模 + 配置开关**,默认复刻 atoi 截断（2026-09-12）
	//   (`RulesConfig::replicate_atoi_truncation`)。
	// ⚠️ 实测(2026-09-08,csa8.0 数据包 1,053 行):**569 行(54.0%)** 受影响 ——
	//    `4.50` 538 行(截断损失 −11.1%)· `5.50` 31 行(−9.1%);其余 484 行本就是
	//    `x.00`。★ 且系数被乘以 `(level−1)` ⇒ **截断按等级累积,不是一次性的 0.5**。
	double lvup_point = 0.0;

	// E_T_INITNUM —— 初始系数(与 lvup_point 相加后再乘基数)。
	std::int32_t init_num = 0;

	// E_T_BASE{VITAL,STR,TGH,DEX} —— 四维基数。
	//
	// ⚠️★ **可以是 0**(实测 1,053 行里 36 行含 0 基数)⇒ 经 ±2 扰动后**可为负**,
	//    而负基数会让 `PARAM_CAL` 产出**负四维**。原版就是这样,本函数照抄
	//    (负四维的下游后果由 `deriveBaseStats` 承担,它对负输入照算)。
	std::int32_t base_vital = 0;
	std::int32_t base_str = 0;
	std::int32_t base_tough = 0;
	std::int32_t base_dex = 0;
};

// 生成产物 —— 原始四维 + 成长率。
struct SpawnStats
{
	// 原始四维(原 `CHAR_VITAL` / `STR` / `TOUGH` / `DEX`)。
	// ⚠️ 可为负,见 SpawnTemplate 的基数注释。
	std::int32_t vital = 0;
	std::int32_t str = 0;
	std::int32_t tough = 0;
	std::int32_t dex = 0;

	// 成长率(原 `CHAR_ALLOCPOINT` 的 4 × 8 bit 打包,与 `Model::Pet` 的 growth_* 同型)。
	//
	// ⚠️★★ **取的是「±2 扰动后、10 点分配前」那一刻的基数** —— 见 .cpp 里的顺序说明。
	std::uint8_t growth_vital = 0;
	std::uint8_t growth_str = 0;
	std::uint8_t growth_tough = 0;
	std::uint8_t growth_dex = 0;
};

// 四维生成 —— 1:1 移植 `ENEMY_createEnemy` 的四维段(`char/enemy.c:1040-1070`)。
//
// ★ 消耗 rng:**先 4 次 `rand(0,4)`,再 10 次 `rand(0,3)`** —— 顺序即原版顺序,
//   改动会破坏可回放性(黄金用例集是 ③ 层唯一补偿手段,`00` §0)。
//
// ⚠️★ **本函数不夹取成长率的 ≤60 / ≤50 上限。** `03` §6.1 的 M6 要求「逐调用点保留
//    各自的夹取上限」,而那两条属**别的路径**(≤60 进化/孵化 · ≤50 死亡扣属),
//    创建路径源码里一处夹取都没有 ⇒ 在这里加一个源码没有的夹取,
//    就是「照文档写会引入原版没有的行为」那一类(`Model::Pet` 同处已有相同结论)。
//    0..255 的字段容量由 `std::uint8_t` 类型本身兑现。
//
// 入参:
//   tmpl  —— 模板的六列。
//   level —— 生成等级。★ 原版由调用方给定(`baselevel > 0`)或
//            `RAND(ENEMY_LV_MIN, ENEMY_LV_MAX)` 摇出(`:1032`)——
//            ⚠️ **那次摇号在本函数之外**,属遇敌/刷怪逻辑,不进纯函数层。
//   rng   —— 注入式随机源。
//   cfg   —— 只用到 `replicate_atoi_truncation`(DR-DT1)。
SpawnStats rollSpawnStats(const SpawnTemplate &tmpl, std::int32_t level,
                          Random &rng, const RulesConfig &cfg) noexcept;

// 生成模板的**评级档位**(原 `CHAR_PETRANK`)—— 1:1 移植 `ENEMY_getRank`
// (`char/enemy.c:802-840`)。0 = 最好,5 = 最差(源码注释:「总成长率 >= 100 petrank=0」)。
//
// ★★ **判据是模板的原始四维基数之和,与本次摇号无关** —— 这一点必须回源码才看得出:
//    `ENEMY_getRank:825-828` 读的是 `ENEMYTEMP_enemy[tarray].intdata`,
//    **不是** `ENEMY_createEnemy` 里被 `±2` 改过的那份局部拷贝 `tp`
//    (源码 :1013-1015 才把模板拷进 `tp`,:1045-1048 改的是 `tp`)。
//    ⇒ 同一模板的两只敌人 rank **恒等**,不受 ±2 与撒 10 点影响。
//    ⚠️ 若照着 `ENEMY_createEnemy` 的行序想当然用扰动后的基数,同模板会摇出不同 rank,
//      **而没有任何一处会报错** —— 只有回源码看它读的是哪份数组才能发现。
//
// ⚠️★ 源码那张表里还有一列 `float rank`(`{100, 2.5}` / `{95, 2.0}` / …)——
//    **从未被使用**,函数只返回下标 `ranknum`。⇒ 本实现不建那一列(死数据,
//    照建等于把一个不存在的语义固化)。
//
// ★ 分档阈值 100 / 95 / 90 / 85 / 80 / 0 照抄:首个满足 `sum >= 阈值` 的下标即结果,
//   末档阈值 0 ⇒ 非负输入必有归属。⚠️ **负和会落空**(源码循环走完 `ranknum` 保持
//   初值 0 ⇒ 最好评级),这是原版行为,照抄并记明:模板基数可为 0 但不为负
//   (`enemybase1.txt` 实测 1,053 行无负基数)⇒ 现实中走不到。
std::int32_t enemyRank(const SpawnTemplate &tmpl) noexcept;

// ══ 经验与等级成长 (批次 P.1)═════════════════════════════════════════

// 升级所需累计经验值（原 NeedLevelUpTbls[]，由 csa8.0/data/exp.txt 与 char_data.c:1231 逐下标核实）。
// 入参 target_level: 目标等级。
// 返回: 升到 target_level 所需的最小累计经验值。
//   target_level <= 1 返回 0;
//   target_level >= 2 && target_level <= 200 返回对应门限;
//   target_level > 200 返回 -1 (已达配置上限，无法继续升级)。
std::int32_t getNeedLevelUpExp(std::int32_t target_level) noexcept;

struct PlayerLevelUpResult
{
	std::int32_t old_level = 1;
	std::int32_t new_level = 1;
	std::int32_t levels_gained = 0;
	std::int32_t skillup_points_gained = 0; // levels_gained * 3 (char_data.c / battle.c:4350)
	std::int32_t charm_gained = 0;          // +1 每级，上限 100 (CH_FIX_PLAYERLEVELUP)
};

// 检查玩家经验是否满足升级条件并计算晋级结果。
// 1:1 移植 CHAR_LevelUpCheck (char_data.c:1361-1415)。
PlayerLevelUpResult checkPlayerLevelUp(std::int32_t current_level, std::int32_t current_exp,
                                       std::int32_t max_level = 140) noexcept;

struct PetLevelUpStats
{
	std::int32_t added_vital = 0;
	std::int32_t added_str = 0;
	std::int32_t added_tough = 0;
	std::int32_t added_dex = 0;
};

// 宠物升级属性增长摇号。
// 1:1 移植 CHAR_PetLevelUp (char_data.c:1545-1610)。
//
// 消耗 rng: 10 次 rand(0, 3) 随机撒点，再 1 次 rand(min, max) 档位浮动比例。
// growth_* 为宠物成长率 (CHAR_ALLOCPOINT 展开的 4 项)。
// petrank 为评级档位 [0, 5] (CHAR_PETRANK，由 enemyRank 算出)。
PetLevelUpStats rollPetLevelUp(std::uint8_t growth_vital, std::uint8_t growth_str,
                               std::uint8_t growth_tough, std::uint8_t growth_dex,
                               std::int32_t petrank, Random &rng) noexcept;

// ══ 装备属性修正 (批次 P.1)═══════════════════════════════════════════

// 装备带来的属性修正值（原 ITEM_equipEffect / Other_DefcharWorkInt, item.c:1599-1650）。
struct EquipModifiers
{
	std::int32_t modify_attack = 0;
	std::int32_t modify_defense = 0;
	std::int32_t modify_quick = 0;
	std::int32_t modify_hp = 0;
	std::int32_t modify_mp = 0;
};

// 叠加上装备加成后的最终战斗数值。
// 1:1 移植 CHAR_complianceParameter (char/char.c:3525-3547)。
DerivedStats deriveEquippedStats(std::int32_t vital, std::int32_t str,
                                 std::int32_t tough, std::int32_t dex,
                                 const EquipModifiers &equip = {}) noexcept;

// 装备槽位映射（原 ITEM_getEquipPlace, item.c:1360-1420）。
// category: ITEM_CATEGORY (0:FIST, 1:AXE, 2:CLUB, 3:SPEAR, 4:BOW, 5:SHIELD, 6:HELM, 7:ARMOUR, etc.)
// 返回: 0..8 对应的装备位下标 (0:Head, 1:Body, 2:Arm, 3:Shield, 4:Deco1, 6:Shoes, 7:Glove, 8:Belt)，无法穿戴返回 -1。
std::int32_t getEquipSlotForCategory(std::int32_t category) noexcept;

// ══ 属性点分配系统 (批次 P.2)═════════════════════════════════════════

// 四维属性类别 (原 SkUpTbl[] = { CHAR_VITAL, CHAR_STR, CHAR_TOUGH, CHAR_DEX }, char/char.c:2556)。
enum class StatCategory : std::uint8_t
{
	kVital = 0, // 0: 体力 (CHAR_VITAL)
	kStr = 1,   // 1: 腕力/力量 (CHAR_STR)
	kTough = 2, // 2: 耐力 (CHAR_TOUGH)
	kDex = 3    // 3: 速度/敏捷 (CHAR_DEX)
};

// 属性点分配计算结果。
struct StatAllocationResult
{
	bool success = false;
	std::int32_t remaining_skillup_points = 0;
	std::int32_t new_vital = 0;
	std::int32_t new_str = 0;
	std::int32_t new_tough = 0;
	std::int32_t new_dex = 0;
};

// 职业类别定义 (原 PROFESSION_CLASS, profession_skill.h:18)。
enum class ProfessionClass : std::uint8_t
{
	kNone = 0,
	kFighter = 1, // 勇士/白狼
	kWizard = 2,  // 巫师/暗灵
	kHunter = 3   // 猎人/追猎
};

// 获取特定职业在特定属性上的加点上限（以内部定点数表示，1点=100）。
// 1:1 移植 char.c:2570-2640 的职业属性上限门禁。
// 勇士: 敏捷上限 200 (20000)
// 巫师: 腕力上限 200 (20000)，耐力上限 200 (20000)
// 猎人: 腕力上限 200 (20000)，耐力上限 200 (20000)，敏捷上限 400 (40000)
// 若无职业限制，返回 -1。
std::int32_t getProfessionStatCap(ProfessionClass profession, StatCategory category) noexcept;

// 计算猎人追寻敌踪/回避战斗技能的遇敌率修正幅度 (原 per = (skill_level / 10) * rate)。
// 1:1 移植 profession_skill.c:1332-1390 (PROFESSION_track / PROFESSION_escape)。
// 若 skill_level < 10 则返回 0。
// 若 is_track 为 true，返回 +per；若为 false (escape)，返回 -per。
std::int32_t computeHunterEncounterFix(int skill_level, int rate = 10, bool is_track = true) noexcept;

// 属性点分配纯函数。
// 1:1 移植 CHAR_SkillUp 核心计算段 (char/char.c:2550-2660)。
// 换算规则: 1 属性点 = 100 内部定点数 (CHAR_getInt + points * 100)。
//
// 契约:
//   ① points_to_allocate 必须 >= 1，且 skillup_points >= points_to_allocate；
//   ② category 必须在 [kVital, kDex] 范围内；
//   ③ 若指定职业且目标属性超出职业上限，返回 success = false，原数值保持不变；
//   ④ 校验不通过时返回 success = false，原数值保持不变；
//   ⑤ 校验通过时返回 success = true，扣减技能点数，对应四维增加 points * 100。
StatAllocationResult applyStatAllocation(std::int32_t vital, std::int32_t str,
                                         std::int32_t tough, std::int32_t dex,
                                         std::int32_t skillup_points,
                                         StatCategory category,
                                         std::int32_t points_to_allocate = 1,
                                         ProfessionClass profession = ProfessionClass::kNone) noexcept;

} // namespace SA::Rules

#endif // __SA_Progression_H__
