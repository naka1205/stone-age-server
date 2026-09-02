// shared/rules/constants.h —— L3 规则层的常量
//
// ★★ 本文件的每个常量都标注了它的血统。理由见 00-architecture.md §0:
//    ③ 层「规则与公式」能写出来但**无法自证正确**,④ 层「表现与手感」永远无法验证。
//    ⇒ 唯一能做的是**把每个数字的来源钉死**,让后来者知道哪些是实测、
//      哪些是选定、哪些是配置。
//
// 血统标记:
//   [8/8]   跨版本核对 8/8 一致(05-battle.md §3 卷首)—— 置信度最高
//   [8.0]   取 8.0 基线,与 8.5 冲突时按 D7 以 B80 裁定为准
//   [DR-*]  由决策登记表裁定
//   [配置]  运行时可调,不是常量意义上的"规则"
//
// ⚠️ shared/ 只依赖标准库(01 §4)。本文件不得 #include 任何项目内的传输类型。

#ifndef SG_SHARED_RULES_CONSTANTS_H
#define SG_SHARED_RULES_CONSTANTS_H

#include <cstdint>

namespace sg::rules {

// ── 伤害与回避的基本系数 ──────────────────────────────────────────
// 全部 [8/8] —— 05-battle.md §3 卷首:「已跨版本核对 8/8 一致」。

inline constexpr double kDamageRate   = 2.0;   // 三分段主公式第三段的线性放大
inline constexpr double kDefenseRate  = 0.5;
inline constexpr double kCriticalRate = 1.0;
inline constexpr int    kKawashiMaxRate = 75;  // 回避率上限(%)
inline constexpr double kAjUp   = 1.5;         // 相克:克制
inline constexpr double kAjSame = 1.0;         // 相克:同属
inline constexpr double kAjDown = 0.6;         // 相克:被克
inline constexpr int    kAttrMax = 100;        // 单属上限,且 Σ 四属 + 无属 == 100

// ★ [8.0] 防御系数由 `_BATTLE_NEWPOWER` 门控,8.0 **开**:
//     defense = 0.70 × 防御力
// ⚠️⚠️ 关闭时是**完全不同的公式**:0.45·DEF + 0.2·QUICK + 0.1·FIXVITAL
//     —— 敏捷与体力也参与防御。这是「宏切换改变公式形状」最典型的一处。
//     D7 裁定不迁移宏 ⇒ 8.0 单基线,只实现开启态。**不要为另一形状留分支。**
inline constexpr double kDefenseCoefNewPower = 0.70;

// ★★ [8.0] 攻防比的**质变阈值**。低于它伤害几乎为 0,超过它线性放大 2 倍。
//
// ⚠️★ **一处已被实测推翻的文档说法(2026-08-31 更正)**:
//   `05-battle.md` §3.1 原称「第二分支上界用浮点 `defense*8.0/7.0`、第三分支下界用
//   **整数除法** `defense*8/7` ⇒ 边界处存在**两分支都不命中的窄缝**,damage 保持 0」。
//   逐条核对源码 `battle_event.c:1225-1235` 后,**两个论断都不成立**:
//
//   ① **不是整数除法** —— `float attack, defense;`(`:1164`)⇒ `defense*8/7` 是
//      **float 运算**,不是整数除法;
//   ② ★★ **没有窄缝,方向恰好相反** —— 实测(float 口径扫 2,862 万组:**0 组窄缝**;
//      即便按原文的整数除法口径扫 20 万 defense:**0 组窄缝、171,429 组重叠**)。
//      整数除法只会让下界**变小**,产生的是**重叠**而非缝;而三分支是 `else if` 串联,
//      重叠区间由**第二分支**接管。
//
//   ⇒ **边界值走第二分支 `RAND(0, attack/16)`,不是 0。**
//   ⚠️ 若照原文实现"窄缝返回 0",会**引入原版没有的行为**。
//   证据用例:`tests/rules_battle_test.cpp` 的 "三分段:无窄缝,边界走第二分支"。
inline constexpr int kAttackDefenseThresholdNum = 8;
inline constexpr int kAttackDefenseThresholdDen = 7;

// ── 骑宠的攻击力合成 ────────────────────────────────────────────
// [8.0] 05-battle.md §3.1 第 1 步。
inline constexpr double kRideMeleeSelf  = 0.8;  // 有骑宠·近战:0.8·人 + 0.8·宠
inline constexpr double kRideMeleePet   = 0.8;
inline constexpr double kRideThrowSelf  = 1.0;  // 有骑宠·投掷:1.0·人 + 0.4·宠
inline constexpr double kRideThrowPet   = 0.4;

// ── 回避 ────────────────────────────────────────────────────────
// [8.0] 05-battle.md §3.2。
inline constexpr double kKawashiParaNormal = 0.02;   // 一般
inline constexpr double kKawashiParaSpell  = 0.027;  // ★ 守方指令是咒术时更易被闪
inline constexpr double kTypeModPetVsEnemy  = 0.8;   // 敌→宠 / 非敌→宠
inline constexpr double kTypeModPlayerCross = 0.6;   // 非玩→玩 / 玩→非玩
inline constexpr int    kDodgeBonusBow      = 20;    // 攻方持弓

// ── 防御减伤:六档随机 ──────────────────────────────────────────
//
// ★ [8.0] 05-battle.md §3.5:**不是固定系数**,是按 RAND(1,100) 分六档。
//   期望系数 ≈ 0.155,且 **25% 概率完全免伤**。
//   触发条件:守方指令 = 防御 **且 混乱值 ≤ 0**。
struct GuardTier { int upper_bound; double factor; };
inline constexpr GuardTier kGuardTiers[] = {
    { 25,  0.00},   // 25%
    { 50,  0.10},   // 25%
    { 70,  0.20},   // 20%
    { 85,  0.30},   // 15%
    { 95,  0.40},   // 10%
    {100,  0.50},   //  5%
};

// 反击伤害 = damage × 0.75,下限 1。[8.0] §3.5
inline constexpr double kCounterDamageRate = 0.75;
inline constexpr int    kCounterDamageMin  = 1;

// ── 属性 ────────────────────────────────────────────────────────
//
// ⚠️★ **两个顺序不同,这是本文件最容易出错的地方。**
//
//   数组顺序(T_pow[0..3]) = 地 水 火 风,T_pow[4] = 无属性余量
//   相克矩阵的表头顺序    = 无 火 水 地 风
//
// ⇒ 本枚举**采用数组顺序**(与源码的 T_pow 一致),相克矩阵按本枚举重排后给出。
//   照抄文档里那张表而不重排,会得到一个看起来对、算出来错的矩阵。
enum class Element : std::uint8_t {
  kEarth = 0,   // 地
  kWater = 1,   // 水
  kFire  = 2,   // 火
  kWind  = 3,   // 风
  kNone  = 4,   // 无(= max(0, 100 − Σ其余),不是独立配置项)
};
inline constexpr int kElementCount = 5;

// 相克系数矩阵 [攻][守],已按上面的 Element 顺序重排。
//
// 原表(05-battle.md §3.4,表头「无 火 水 地 风」):
//     火: 1.5 1.0 0.6 1.0 1.5      水: 1.5 1.5 1.0 0.6 1.0
//     地: 1.5 1.0 1.5 1.0 0.6      风: 1.5 0.6 1.0 1.5 1.0
//     无: 1.0 0.6 0.6 0.6 0.6
//
// ★ 因 Σatk = Σdef = 100,全无属性时系数为 1.0 ⇒ 量纲自洽。
inline constexpr double kElementMatrix[kElementCount][kElementCount] = {
    // 守:      地     水     火     风     无
    /* 攻 地 */ {1.0,  1.5,  1.0,  0.6,  1.5},
    /* 攻 水 */ {0.6,  1.0,  1.5,  1.0,  1.5},
    /* 攻 火 */ {1.0,  0.6,  1.0,  1.5,  1.5},
    /* 攻 风 */ {1.5,  1.0,  0.6,  1.0,  1.5},
    /* 攻 无 */ {0.6,  0.6,  0.6,  0.6,  1.0},
};

// 结果 = damage × Σ(攻方各属 × 守方各属 × 系数) / 10000
inline constexpr int kElementDivisor = 10000;

// ⚠️ 四属结界按**地水火风顺序 `else if` 串联** ⇒ **只有第一个命中的结界生效,不叠加**。
//   这是原版行为,不是 bug ——实现时须保留短路语义。

// ── ★★ 场地属性:与 Element **不同的编码**,不可混用 ──────────────
//
// ⚠️⚠️ 这是本文件的**第二处顺序陷阱**(第一处是 Element vs 相克表头)。
//   原版 `battle.h:581-585` 的 `BATTLE_ATTR_*` 是:
//       NONE = 0 · EARTH = 1 · WATER = 2 · FIRE = 3 · WIND = 4
//   而 `Element` 是:
//       地 = 0 · 水 = 1 · 火 = 2 · 风 = 3 · 无 = 4
//   ⇒ **两者的 0 含义相反**(一个是"无属性场地",一个是"地属性")。
//
// ★ 实测代价:移植 `BATTLE_FieldAttAdjust` 时若直接拿 `Element` 当场地编码,
//   「无属性场地」会被当成「地属性场地」⇒ 纯地攻方的场地系数从 0.5 变成 1.0,
//   与守方的比值翻倍 ⇒ **伤害整体 ×2,而且只在特定属性组合下出现**。
//   这个 bug 在 2026-08-31 移植时真的发生了,被相克用例的手算基准接住
//   (期望 150、实得 300)。⇒ 显式建枚举,不复用 Element。
enum class FieldAttribute : std::uint8_t {
  kNone  = 0,   // ★ 无属性场地 —— 与 Element::kEarth 同为 0,含义完全不同
  kEarth = 1,
  kWater = 2,
  kFire  = 3,
  kWind  = 4,
};

// 场地属性:power = 0.5(默认)或 0.5 + 该属值·att_pow·0.0001·0.5
// 最终 damage × (At_FieldPow / Df_FieldPow)。★ 分母最小 0.5,不会除零。
inline constexpr double kFieldPowBase = 0.5;

// ── 武器类(反击相性表的维度)────────────────────────────────────
// [8.0] 05-battle.md §3.5 的 CounterTbl 是 7 行 × 8 列。
enum class WeaponClass : std::uint8_t {
  kNone = 0, kClaw = 1, kAxe = 2, kRod = 3, kSpear = 4, kBow = 5, kThrow = 6, kOther = 7,
};
inline constexpr int kWeaponClassCount = 8;
// ⚠️ 攻方只有 7 类(kOther 不作为攻方出现在原表里)—— 移植时须核对第 8 行的处置,
//    05 §3.5 的表只给了 7 行。已登记为移植期核对项。

// ── 入场与槽位 ──────────────────────────────────────────────────
//
// [8.0] 05-battle.md §2.4:`_MULTIPLAYER_` 在 8.0 为**关**,
// SSRC80 连 #ifdef 都没有(硬编码 10/5/10)。客户端基线亦为关 ⇒ 双端一致。
inline constexpr int kBattleEntryMax = 10;   // 每侧槽位
inline constexpr int kBattlePlayerMax = 5;
inline constexpr int kSideOffset = 10;       // 敌方槽号 = 10 + i
inline constexpr int kSlotCount = kBattleEntryMax * 2;  // 20,== 就绪位图宽度

// ── 状态 ────────────────────────────────────────────────────────
// DR-BT4:补齐到 44,同源生成。枚举真源在 idl/schema/domain/battle_status.proto,
// 此处只记容量,避免两处各写一份。
inline constexpr int kBattleStatusCount = 44;

// [8.0] 超时。05-battle.md §2.2 步骤 3。
inline constexpr int kBattleTimeLimitSeconds = 3600;

}  // namespace sg::rules

#endif  // SG_SHARED_RULES_CONSTANTS_H
