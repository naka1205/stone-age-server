# deviations / 01-intentional-changes — 有意改变(架构性)

> ★ 本目录 = **和原版哪里不一样**。流程账看 [`../journal/`](../journal/) ·
> 架构裁定看 [`../00-architecture.md`](../00-architecture.md) ·
> 决策看 [`../11-decision-register.md`](../11-decision-register.md) · 还欠什么看 [`../backlog/`](../backlog/)。

> ⚠️★★ **本文件是派生索引,不是真源。** 每条只给一句话 + 指回 journal / `11` 的锚点。
> 若本文件与 journal 或 `11` 冲突,**以那边为准**。
>
> **收录口径**:**行为保留、实现方式有意改变**的项 —— 即「原版这么写是实现方式,不是玩法」。
> ⚠️ 与 [`02-faithful-quirks.md`](02-faithful-quirks.md) 正相反:那边是**明知有问题也照抄**。

---

## 判据:什么时候可以改

★★ 本项目反复援引的一条判据(DR-BT11 立):
**「硬编码图号是实现方式,不是玩法」** —— 玩法效果必须逐位保留,实现方式可以换成数据驱动。

⇒ 推论:**改的是"怎么做到",不是"做到什么"**。凡改动能被返回值 / rng 序列观察到的,一律不算本类,
属 bug 或必须回退。

---

## 1. 硬编码 → 数据驱动

| 项 | 原版 | 我方 | 依据 |
|---|---|---|---|
| **雷尔免疫暴击与打飞** | 按图号 `101813`/`101814` 硬编码 | 免疫标记进**敌人数值表**,不写图号 | `11` DR-BT11 |
| **游荡 AI** | 角色身上的**函数指针** `CHAR_LOOPFUNC` + `RunCharLoopEvent`(Lua) | **数据驱动 enum 分派**(POD 架构不可照搬函数指针) | [§9.0.45](../journal/13-world.md) · `11` DR-DT18 |
| **敌人捕获所需道具** | `_NEED_ITEM_ENEMY` 关 ⇒ 源码硬编码 `NeedEnemy[9]` 数组 | `shared/rules/CaptureItem.h` 纯规则表 | [§9.0.50](../journal/14-item.md) · `11` DR-DT21 |

## 2. 分层归属的改变(L3 纯函数化,D2 的直接后果)

★★ 这一族是 **D2「一份规则两端编译」** 的必然代价:原版的规则代码直接读全局世界态,
而 L3 必须是纯函数 ⇒ **凡规则需要世界态的,一律改为「World 预先投影到入参」**。

| 项 | 原版 | 我方 | 依据 |
|---|---|---|---|
| **捕获所需道具检查** | `CaptureItemCheck` 直接读攻方背包 | World 在每个 `resolveAction` 前重取现态,投影到 `Combatant::mods.capture_item_ok`,L3 只做 `&&` 门 | [§9.0.50](../journal/14-item.md) |
| **装备暴击 / 免疫打飞 / 带毒装备** | 规则里读装备 | 同上,投影为 `mods.equip_critical` / `immune_knockback` / `suit_poison` | [§9.0.21](../journal/10-battle-core.md) · [§9.0.22](../journal/10-battle-core.md) · [§9.0.55](../journal/15-status.md) |
| **状态原始四维** | 规则直接读 `Char` | ⚠️ **例外:四维直拷进 `Combatant`,不投影推导值** —— 状态两条公式**直接读四维**,由 World 预算后投影等于**把一条公式切成两半** | [§9.0.55](../journal/15-status.md) |
| **经验公式 `enemyExp`** | 服务端算 | ★ **全放 world**(用户裁定,客户端不算经验)⇒ world 首个本地浮点公式 ⇒ `-ffp-contract=off` 要落到 `sa_world` 自己 | [§9.0.40](../journal/12-encounter.md) · `11` DR-DT15 |

## 3. 计算位置的改变(行为等价,位置更早/更晚)

| 项 | 原版位置 | 我方位置 | 等价性判据 |
|---|---|---|---|
| **敌人表两条归一** | 载入期(`enemy.c:479-486`) | 摇号前 | ★ 三条:**幂等 · 不消耗 rng · 只碰这两列**;⚠️ 实测原版**一次都不触发**,仍然移植 —— 判据不是"照抄源码"而是上述三条 | [§9.0.35](../journal/12-encounter.md) |
| **收益暂存与交付** | 每次行动结算新死亡，结束/离场交付 | 已恢复对应时点；旧的统一延后已撤销 | [修复记录](../audits/2026-09-12-remediation-results.md) · DR-DT15/DT22 |

## 4. 数据结构的改变

| 项 | 原版 | 我方 | 依据 |
|---|---|---|---|
| **相克矩阵** | 文档表头「无火水地风」 | 按 `Element`(地水火风)**重排** —— ⚠️ 照抄不重排会得到"看起来对、算出来错"的矩阵 | [§9.0.5](../journal/02-infra-d2-crosscompile.md) 附近 · `shared/rules/constants.h` |
| **对外和类型,对内扁平池** | 693 slot 的 `Char` 结构 + 65 处别名合并 | L2 必须 **65 个独立字段**,不合并 | `01` §2.1 · §7.1 |
| **协议** | 明文分隔符文本协议 | IDL 定长 POD(DR-TS1),不沿用三个历史构造 | `01` §6.2 |
| **道具背包** | `ITEM_item[]` 全局数组成员 | **独立池**,Item **不进** `EntityKind` 五族(背包道具不是 Char);但**背包槽位照原版连续布局** | [§9.0.48](../journal/14-item.md) · `11` DR-DT20 |

## 5. 不复刻的原版机制(判定为"实现方式"而非玩法)

| 项 | 为什么不复刻 | 依据 |
|---|---|---|
| **Lua 集成**(`CaptureOkFunction` / `CaptureCheckFunction` 族) | ⚠️★★ 8.0 **无 Lua**;unifdef 展开视图残留的 Lua 回调是**宏误判**(`_ALLBLUES_LUA_1_8` 实际关) | [§9.0.50](../journal/14-item.md) |
| **主循环** | 不复刻,直接用平台(客户端侧) | 客户端 `01` §3.2 |
| **`GROUP_getInt` 越界返回 -1 的副作用**(坏组处理) | 原版依赖越界副作用 ⇒ 跳过不照抄 | [§9.0.37](../journal/12-encounter.md) |
| ★ **一处原版缺陷,明确不照抄** | `encount.c:214-215` 与 `:271-272` 两处的写法 | [§9.0.37](../journal/12-encounter.md) |

---

## 另见

- **照抄的原版缺陷与冗余** → [`02-faithful-quirks.md`](02-faithful-quirks.md)
- **净核划外 / 未移植** → [`03-not-ported.md`](03-not-ported.md)
- **我方文档与原版源码分叉** → [`04-doc-vs-source.md`](04-doc-vs-source.md)
