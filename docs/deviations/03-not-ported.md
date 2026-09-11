# deviations / 03-not-ported — 净核划外与未移植

> ★ 本目录 = **和原版哪里不一样**。流程账看 [`../journal/`](../journal/) ·
> 架构裁定看 [`../00-architecture.md`](../00-architecture.md) ·
> 决策看 [`../11-decision-register.md`](../11-decision-register.md) · 还欠什么看 [`../backlog/`](../backlog/)。

> ⚠️★★ **本文件是派生索引,不是真源。** 每条只给一句话 + 指回 journal / `11` 的锚点。
> 若本文件与 journal 或 `11` 冲突,**以那边为准**。
>
> **收录口径**:**有意不做**的部分 —— 因为它不属于 8.0 净核,或已被 D8 裁定排除。
> ⚠️ 与 [`../backlog/02-deferred-scope.md`](../backlog/02-deferred-scope.md) 的区别:
> 那边是**暂时没做**(前置到位就做),这边是**不打算做**。

---

## 1. 「8.0 净核」是什么

**D7 = 8.0 单基线。** 手上的源码树有三种血统混在一起,必须逐一辨认:

| 血统 | 特征 | 处置 |
|---|---|---|
| **8.0 净核** | `StoneAge/gmsv/src/` 中 `version.h` 宏为**开**的分支 | ✅ 移植 |
| **8.5 形态** | 8.5 时代才有的写法(如 `mylua/function.c` 的 Lua 表) | ❌ 不移植 |
| **自由服魔改** | 私服运营方后加的(VIP / 倍率 / 反作弊) | ❌ **恒等划外** |

⚠️★★ **「8.5 是 8.0 超集」已被证伪 8 次**(`00` §10.4)——
如 `fmdplevelexp` 两代差 10~25 倍、随身上限 8.5 放大 **100 倍**(会把经济压力整个抹掉)。
⇒ 凡落在 **54 个冲突宏**上,以 **B80 裁定**为准。

## 2. 自由服魔改(恒等划外)

| 项 | 说明 | 出处 |
|---|---|---|
| `BATTLE_GetExp` 掺 VIP / `getBattleexp` / `Free*` / `EXPUP` | 非 8.0 净核 ⇒ 经验公式只取净核那支 | [§9.0.40](../journal/12-encounter.md) |
| `getEqNoenemy` / `getEqRandenemy` / Ra's amulet(`eqen`)· `getStayEncount` | 遇敌率的私服装备加成 | [§9.0.44](../journal/13-world.md) |
| **nuke 反作弊** | 私服后加 | [§9.0.42](../journal/13-world.md) |

## 3. 8.5 形态(宏关 ⇒ 8.0 不走这条)

| 项 | 8.0 实际形态 | 出处 |
|---|---|---|
| **Lua 集成**(`_ALLBLUES_LUA_1_8`) | ⚠️★★ unifdef 展开视图**误判为开**、残留 `CaptureOkFunction` 回调;实际 8.0 **无 Lua** | [§9.0.50](../journal/14-item.md) |
| **道具 usefunc 走 Lua 表**(`mylua/function.c`) | 8.0 是**硬编码 C 数组** `correspondStringAndFunctionTable[]` | [§9.0.53](../journal/14-item.md) · `11` DR-DT23 |
| **`_CHAR_LOOP_TIME` 时间预算制** | ⚠️★★ 8.0 **关** ⇒ 走 `#else` **条数制摊还**(`EnemyMoveNum` 上限 + 游标) | [§9.0.45](../journal/13-world.md) · `11` DR-DT18 |
| **`_NEED_ITEM_ENEMY` 读 `needitemeneny.txt`** | 8.0 **关** ⇒ 不读文件,用源码硬编码 `NeedEnemy[9]` | [§9.0.50](../journal/14-item.md) |

⚠️★★★ **展开视图会选错分支,不只是丢分支。** `unifdef_80` 的 `macros_80.json`
把编译期 `-D` 宏误当"开"过**两次**(`_CHAR_LOOP_TIME` · `_ALLBLUES_LUA_1_8`)⇒
**争议处必回 `StoneAge/` 与 `stoneage85/` 双核**。

## 4. D8 裁定排除(玩法范围)

**D8 = 核心 29 + 逐步补**(`00` §7):

| 类 | 数 | 处置 |
|---|---|---|
| 核心子系统 | **29** | ✅ 第一批 |
| 长尾 | **54** | ⏭ 延后 |
| 弃用 | **11** | ❌ **不做** |
| **拍卖** | — | ❌ 不做 —— 8.0 有 **0 个写点** |
| **跨线路聊天室** | — | ✅ **进第一批**(它是分布式拓扑的一部分) |

## 5. 历史构造:保留思想,不保留形式

| 项 | 原版 | 我方 | 依据 |
|---|---|---|---|
| **服务端驱动 UI** | 明文协议下发窗口 | ✅ **保留这条设计思想**,改变传输(IDL) | `01` §6.3 |
| 三个历史构造 | — | ❌ 不沿用 | `01` §6.2 |
| **D4 Web 端** | — | ⏭ **暂缓**(emcc 绑它解冻) | `00` §1 |

## 6. 当前未移植的子系统(影响可观察行为,已登记)

⚠️ 这些严格说属 [`../backlog/`](../backlog/),列在此处是因为**它们造成了与原版的可观察差异**:

| 项 | 造成的差异 | 出处 |
|---|---|---|
| ⚠️★ **`ENEMY_STYLE` 敌人武器** | 敌人一律空手 ⇒ **比接了武器的原版更容易触发空手多段**(可达 10 段) | [§9.0.33](../journal/11-model-attr.md) |
| **宠技 / 职技 / 魔法** | 5 个 work 字段恒 0 ⇒ `react` 恒 `NONE` ⇒ 反击路径走不到 | [§9.0.55](../journal/15-status.md) |
| **套装系统** | `suit_poison` 恒 0 ⇒ 普攻不附带状态、不摇 rng | [§9.0.55](../journal/15-status.md) |
| **骑乘系统** | `has_ride` 恒假 ⇒ ⚠️ 酒醉解除的骑宠分支不可达(**接上时会静默走错**) | [§9.0.55](../journal/15-status.md) |
| **选角来源** | 玩家四维 / 名字 / 图号是占位 | [§9.0.44](../journal/13-world.md) |
| **D6 脚本层** | NPC `argstr` 门 / 明雷掉落 / 完整 AI 走不到 | [§9.0.46](../journal/13-world.md) |

---

## 另见

- **有意改变(架构性)** → [`01-intentional-changes.md`](01-intentional-changes.md)
- **照抄的原版缺陷与冗余** → [`02-faithful-quirks.md`](02-faithful-quirks.md)
- **我方文档与原版源码分叉** → [`04-doc-vs-source.md`](04-doc-vs-source.md)
- **永久不可判定(做不了)** → [`../backlog/04-undecidable.md`](../backlog/04-undecidable.md)
