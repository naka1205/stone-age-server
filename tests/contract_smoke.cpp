// tests/contract_smoke.cpp —— shared/ 契约层冒烟测试
//
// 本文件验证的不是"功能"(L3 的实现属阶段 1.1),而是**契约本身是否自洽**:
//   ① shared/ 的头在 **C++17** 下能编译 —— D2 的硬前提(GameStudio 是 C++17)
//   ② ★ 相克矩阵的**重排**正确 —— 数组顺序(地水火风)≠ 文档表头顺序(无火水地风)
//   ③ 注入式随机源**可回放** —— 同种子 + 同调用序列 ⇒ 逐位相同
//   ④ shared/rules 能与 IDL 的 domain/ 类型互操作(D2 的接口面)
//   ⑤ M10:句柄带 generation
//
// ⚠️ 不调用 battle.h 里尚未实现的函数 —— 它们是阶段 1.1 的交付物。
//    本文件只保证**声明可编译、类型可互操作**。

#include "model/handle.h"
#include "rules/battle.h"
#include "rules/combatant.h"
#include "rules/config.h"
#include "rules/constants.h"
#include "rules/random.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace sa;

// ── ② 相克矩阵重排的回归保护 ──────────────────────────────────
//
// ★★ constants.h 里那张矩阵是**人工重排**的结果,是本次实现中最容易出错的一步:
//      数组顺序(Element)   = 地 水 火 风 无
//      文档表头顺序        = 无 火 水 地 风
//    照抄文档而不重排,会得到一个"看起来对、算出来错"的矩阵 —— 且不会报错。
//
// ⇒ 这里把 05-battle.md §3.4 的**原表按原顺序**抄一份,再逐格比对重排结果。
//   两份都错成同一个样子的概率极低;任何一侧被改动都会被抓住。

// 原表列序:无 火 水 地 风
enum DocIdx { kDocNone = 0, kDocFire = 1, kDocWater = 2, kDocEarth = 3, kDocWind = 4 };

// 原表行序:火 水 地 风 无
static constexpr double kDocMatrix[5][5] = {
    /* 攻 火 */ {1.5, 1.0, 0.6, 1.0, 1.5},
    /* 攻 水 */ {1.5, 1.5, 1.0, 0.6, 1.0},
    /* 攻 地 */ {1.5, 1.0, 1.5, 1.0, 0.6},
    /* 攻 风 */ {1.5, 0.6, 1.0, 1.5, 1.0},
    /* 攻 无 */ {1.0, 0.6, 0.6, 0.6, 0.6},
};
static constexpr int kDocRowOf[5] = {
    /* 攻 火 */ 0, /* 攻 水 */ 1, /* 攻 地 */ 2, /* 攻 风 */ 3, /* 攻 无 */ 4,
};

// Element(地=0 水=1 火=2 风=3 无=4) → 原表的行下标 / 列下标
static constexpr int kElemToDocRow[5] = {
    kDocRowOf[2],  // 地 → 原表第 3 行
    kDocRowOf[1],  // 水 → 原表第 2 行
    kDocRowOf[0],  // 火 → 原表第 1 行
    kDocRowOf[3],  // 风 → 原表第 4 行
    kDocRowOf[4],  // 无 → 原表第 5 行
};
static constexpr int kElemToDocCol[5] = {
    kDocEarth, kDocWater, kDocFire, kDocWind, kDocNone,
};

static void CheckElementMatrix() {
  for (int atk = 0; atk < rules::kElementCount; ++atk) {
    for (int def = 0; def < rules::kElementCount; ++def) {
      const double got = rules::kElementMatrix[atk][def];
      const double want = kDocMatrix[kElemToDocRow[atk]][kElemToDocCol[def]];
      if (got != want) {
        std::printf("★ 相克矩阵重排错误 [攻%d][守%d]: 得 %.1f 应 %.1f\n",
                    atk, def, got, want);
        assert(false && "相克矩阵与 05-battle.md §3.4 原表不符");
      }
    }
  }
  // 量纲自洽:全无属性时系数应为 1.0(§3.4「因 Σatk = Σdef = 100」)
  assert(rules::kElementMatrix[4][4] == 1.0);
}

// ── ⑤ M10:句柄带 generation ──────────────────────────────────
static_assert(std::is_trivially_copyable_v<model::EntityHandle>);
static_assert(sizeof(model::EntityHandle) == 8);

static void CheckHandle() {
  assert(!model::kNullHandle.valid());

  // 同一个池槽位被复用:index 相同、generation 递增 ⇒ 旧句柄必须不等于新句柄。
  // ★ 这正是 17 §7.2 那个真实故障(fdid 归零后迟到应答命中新连接)的进程内对应物。
  const model::EntityHandle old_ref{42, 1};
  const model::EntityHandle reused{42, 2};
  assert(old_ref != reused);
  assert(old_ref.valid() && reused.valid());
}

// ── ③ 随机源可回放 ───────────────────────────────────────────
static void CheckReplayable() {
  // 同种子 + 同调用序列 ⇒ 逐位相同。这是黄金用例集成立的前提(05 §1.5)。
  rules::SeededRandom a(20260831u);
  rules::SeededRandom b(20260831u);
  for (int i = 0; i < 1000; ++i) {
    assert(a.Rand(0, 100) == b.Rand(0, 100));
    assert(a.RandMod(37) == b.RandMod(37));
  }

  // 不同种子应给出不同序列(否则种子没起作用)。
  rules::SeededRandom c(1u), d(2u);
  bool differs = false;
  for (int i = 0; i < 64 && !differs; ++i) {
    if (c.Rand(0, 1000000) != d.Rand(0, 1000000)) differs = true;
  }
  assert(differs);

  // Rand 是**闭区间** [lo, hi] —— 原版 RAND 语义(§3.1 第三步「只能造成 0 或 1」)。
  rules::SeededRandom e(7u);
  bool saw_lo = false, saw_hi = false;
  for (int i = 0; i < 512; ++i) {
    const int v = e.Rand(0, 1);
    assert(v == 0 || v == 1);
    if (v == 0) saw_lo = true;
    if (v == 1) saw_hi = true;
  }
  assert(saw_lo && saw_hi && "RAND(0,1) 必须能取到两端");

  // 退化输入不得越界。
  rules::SeededRandom f(9u);
  assert(f.Rand(5, 5) == 5);
  assert(f.RandMod(0) == 0);
  assert(f.RandMod(-3) == 0);

  // IRandom 是可注入的抽象:通过基类引用调用应得到同样的序列。
  rules::SeededRandom g(123u);
  rules::IRandom& via_base = g;
  rules::SeededRandom h(123u);
  for (int i = 0; i < 100; ++i) assert(via_base.Rand(1, 9) == h.Rand(1, 9));
}

// ── 战场快照的形状 ───────────────────────────────────────────
static void CheckBattleField() {
  static_assert(rules::kSlotCount == 20, "2 side × BATTLE_ENTRY_MAX(10)，== 就绪位图宽度");
  static_assert(std::is_trivially_copyable_v<rules::Combatant>);
  static_assert(std::is_trivially_copyable_v<rules::BattleField>);

  rules::BattleField field{};
  field.battle_id = 1;
  field.turn = 1;

  rules::Combatant& me = field.at(0);
  me.occupied = true;
  me.kind = rules::CombatantKind::kPlayer;
  me.slot = 0;
  me.hp = 300; me.max_hp = 300;
  me.attack = 120; me.defense = 80; me.quick = 40; me.luck = 25;
  // 四属:地水火风。Σ = 60 ⇒ 无属性余量 = 40
  me.elements[0] = 30; me.elements[1] = 10; me.elements[2] = 20; me.elements[3] = 0;
  assert(me.NoneElement() == 40);

  rules::Combatant& foe = field.at(rules::kSideOffset);
  foe.occupied = true;
  foe.kind = rules::CombatantKind::kEnemy;
  foe.slot = static_cast<std::uint8_t>(rules::kSideOffset);
  foe.hp = 200; foe.max_hp = 200;
  // 满火属 ⇒ 无属性余量为 0(上限 100)
  foe.elements[2] = rules::kAttrMax;
  assert(foe.NoneElement() == 0);
  assert(foe.IsEnemy() && !foe.IsPlayer());

  // 阵营划分:0..9 vs 10..19
  assert(rules::BattleField::SameSide(0, 9));
  assert(rules::BattleField::SameSide(10, 19));
  assert(!rules::BattleField::SameSide(9, 10));

  // 超出四属上限时余量钳到 0,不得为负。
  rules::Combatant over{};
  over.elements[0] = 80; over.elements[1] = 80;
  assert(over.NoneElement() == 0);
}

// ── ④ 与 IDL domain/ 类型的互操作 ────────────────────────────
static void CheckIdlInterop() {
  // shared/rules 的契约以 IDL 事件类型为输出 —— 这是 D2 的接口面(02 §9)。
  domain::BattleEvents out{};
  out.battle_id = 99;
  out.turn = 1;

  domain::BattleEvent e{};
  e.body_kind = domain::BattleEvent::BodyKind::DAMAGE;
  e.body.damage.target = static_cast<std::uint32_t>(rules::kSideOffset);
  e.body.damage.hp_delta = -25;
  out.events.push_back(e);
  assert(out.events.size() == 1);

  // DR-BT5 的 CannotActReason 来自 IDL,由 shared/rules 的 CheckCanAct 返回。
  const domain::CannotActReason ok = domain::CannotActReason::CANNOT_ACT_NONE;
  assert(ok == domain::CannotActReason::CANNOT_ACT_NONE);

  // 状态枚举容量与 constants.h 的记载一致(DR-BT4 补齐到 44)。
  static_assert(rules::kBattleStatusCount == 44);
  assert(static_cast<int>(domain::BattleStatus::BATTLE_ST_ICECRACK10) == 43);

  // TurnCommands 的槽宽与战场一致。
  rules::TurnCommands cmds{};
  cmds.present[0] = true;
  cmds.commands[0].battle_id = 99;
  cmds.commands[0].turn = 1;
  cmds.commands[0].command_kind = domain::BattleCommand::CommandKind::ATTACK;
  cmds.commands[0].command.attack.target = static_cast<std::uint32_t>(rules::kSideOffset);
  assert(cmds.present[0]);
}

// ── 配置默认值:两个最容易写错的数 ────────────────────────────
static void CheckConfigDefaults() {
  const rules::RulesConfig cfg{};
  // ★★ getDamageCalc() 兜底是 70 不是 100 —— 8.0 投产下全部物理伤害统一乘 0.70。
  //    写成 100 会让全局伤害偏高 43%。
  assert(cfg.damage_calc_percent == 70);
  // DR-BT1 裁定照抄,默认 true。
  assert(cfg.unarmed_multihit_full_damage);
  // DR-DT1 裁定按设计意图(浮点),不复刻 atoi 截断。
  assert(!cfg.replicate_atoi_truncation);
  // DR-WM1 取 23;DR-WM2 取 10。
  assert(cfg.sight_radius == 23);
  assert(cfg.enemy_move_num == 10);
}

int main() {
  CheckElementMatrix();
  CheckHandle();
  CheckReplayable();
  CheckBattleField();
  CheckIdlInterop();
  CheckConfigDefaults();
  std::printf("OK  contract smoke: 相克矩阵/句柄/可回放/战场/IDL互操作/配置默认值\n");
  return 0;
}
