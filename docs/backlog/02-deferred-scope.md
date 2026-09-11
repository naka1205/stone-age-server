# backlog / 02-deferred-scope — 划外与登记残缺(派生索引)

> ★ 本目录 = **还欠什么**(遗留 / 暂缓 / 未决 / 做不了)。
> 流程账看 [`../journal/`](../journal/) · 架构裁定看 [`../00-architecture.md`](../00-architecture.md) ·
> 决策看 [`../11-decision-register.md`](../11-decision-register.md) · 与原版差异看 [`../deviations/`](../deviations/)。

> ⚠️★★ **本文件是派生索引,不是真源。** 每条只给一句话 + 指回 journal 的锚点,**正文在那里**。
> 同一事实两处存放必然漂移 —— 若本文件与 journal 冲突,**以 journal 为准**。
>
> **收录口径**:各批次「⚠️ 未做 / 登记残缺 / 边界」小节里**有据划出**的项。
> ★ 这些**不是遗漏**,是当时就写明了判据的暂缓;判据本身(为什么现在不做)在 journal 原文里。
>
> **归类维度 = 「在等什么」**,不是「哪批划出的」—— 因为解锁一个前置往往能一次性关掉多条。

---

## 1. 等「某个域移植」

| 项 | 在等 | 出处 |
|---|---|---|
| 状态 12..43(晕眩 / 天罗 / 冰爆术族等 32 种) | 宠技 / 职技域 | [§9.0.55](../journal/15-status.md) |
| 职业技能施加路径(能绕过全局互斥的那 **11 种**) | 职技域 + 技能表 | [§9.0.55](../journal/15-status.md) |
| 精灵 / 魔法两组调用参数(`Range=30` / `Bai=1.0`) | 技能表 | [§9.0.55](../journal/15-status.md) |
| 酒醉解除的骑宠分支(`quick += 骑宠 quick`) | 骑乘系统 | [§9.0.55](../journal/15-status.md) ⚠️ 见下「会静默引爆」 |
| `CanCureFlg` 不可治疗门 | 状态药路径 | [§9.0.55](../journal/15-status.md) |
| 反击(`BATTLE_GetDamageReact` 的 5 个 work 字段) | 宠技 / 职技 / 魔法(**不是状态系统**,见 deviations) | [§9.0.21](../journal/10-battle-core.md) · [§9.0.55](../journal/15-status.md) |
| 全收技 `PickAllPet` · 敌人捕获难度取模板 · 睡眠读独立 work 字段 | 技能链路 / 敌人模板 / 状态细化 | [§9.0.20](../journal/10-battle-core.md) |
| 骑宠经验(源码 `× 0.6` 分骑宠) | 宠物成长域(`Model::Pet` 无 `exp` 字段) | [§9.0.40](../journal/12-encounter.md) |
| 升级 / 金钱 / 掉落 / 决斗点分配 | 各有落脚域(升级走 `exp.txt` · 金钱归 `GoldLedger`) | [§9.0.40](../journal/12-encounter.md) · `11` §2.17 ⑤ |
| `ENEMY_STYLE` 敌人武器 | 敌人模板域 ⚠️ **现状比原版更易触发空手多段** | [§9.0.33](../journal/11-model-attr.md) |
| 脏槽跟踪 | 建议独立小批(`kCharLoop` 已实装,可对齐) | [§9.0.42](../journal/13-world.md) |
| `CharAppear` 无名字 / 图号 · 玩家进场四维占位 | 选角来源(1.5 的 `Player` 无四维 / level) | [§9.0.42](../journal/13-world.md) · [§9.0.44](../journal/13-world.md) |
| 战场态宠物 / 捕获宠物的战斗三围 | ~~属性推导~~ ✅ **已由 M.4b 关闭**(欠债 23/25) | [§9.0.27](../journal/11-model-attr.md) → [§9.0.33](../journal/11-model-attr.md) |

## 2. 等「D6 脚本层」

| 项 | 出处 |
|---|---|
| `CHAR_LOOPFUNC` 完整 AI(追击 / 攻击玩家)· `RunCharLoopEvent`(Lua) | [§9.0.45](../journal/13-world.md) |
| NPC `argstr` 脚本门(`gym`/`item`/`startmsg`/`steal`/`deniedmsg`) | [§9.0.46](../journal/13-world.md) |
| 明雷胜利掉落(`NPC_NPCEnemy_Dying` 的 additem) | [§9.0.46](../journal/13-world.md) · [§9.0.51](../journal/14-item.md) |
| NPC 事件改组 / 随机替换(`ENEMY_RandomEnemyArray`) | [§9.0.39](../journal/12-encounter.md) |
| 组队移动(`CHAR_PARTY_CLIENT`)· 重叠事件(`RunCharOverlapEvent`) | [§9.0.42](../journal/13-world.md) |

## 3. 等「D 线内容导入(终点是入库)」

★★ 这一族有一条**共同判据,被重复援引了四次**:
**D 线的终点是入库(`04` §7.1),做一个运行时读 txt 的加载器是将来要删的第二次改动。**
⇒ 所以这些批次一律「用例手工构造 fixture,不写加载器」。

| 项 | 出处 |
|---|---|
| 敌人模板 / 遇敌两表 / 编组表的**文件加载器** | [§9.0.33](../journal/11-model-attr.md) · [§9.0.35](../journal/12-encounter.md) · [§9.0.37](../journal/12-encounter.md) · [§9.0.39](../journal/12-encounter.md) |
| 真实地图导入(LS2MAP)· 多 floor · 真实刷怪点 | [§9.0.42](../journal/13-world.md) · [§9.0.45](../journal/13-world.md) |
| 模板与遇敌行配对的外键校验(原版在**载入期**丢弃,不是运行期检查) | [§9.0.35](../journal/12-encounter.md) |

## 4. 等「客户端批次」

| 项 | 出处 |
|---|---|
| 客户端消费 `BattleResult` 的演出 | [§9.0.40](../journal/12-encounter.md) |
| 客户端地图场景表现层(消费 CA/CD/Move) | [§9.0.42](../journal/13-world.md) |
| 客户端 EV 实装 + 坐标纠正 XYD | [§9.0.46](../journal/13-world.md) |
| 中毒图标 / 掉血飘字 | [§9.0.55](../journal/15-status.md) |
| 客户端仓 `ci_verify.py` 同样不查格式(P6 宣告的「五仓字节一致」有空洞) | [§9.0.30](../journal/03-infra-ci-guards.md) |

## 5. ⚠️ 会静默引爆的(不是「等」,是「接上时会走错」)

★★ 这一族最该单列 —— 它们**现在不可达所以不报错**,而前置一旦接上就**照旧走错分支且没有任何东西会红**。

| 项 | 引爆条件 | 出处 |
|---|---|---|
| ⚠️★★ **酒醉解除的骑宠分支** —— 只实现了无骑宠那支(`× 2`) | 骑乘系统接上、`has_ride` 变真 ⇒ **照旧走 ×2** | [§9.0.55](../journal/15-status.md) · 欠债 33 ⑤ |
| ⚠️ 敌人池容量取 `othercharnum = 10000`,而原版第三段是**敌人与 NPC 共用** | 建 NPC 族那一批若各取 10000 ⇒ 预算翻倍 | [§9.0.33](../journal/11-model-attr.md) |
| ⚠️ `_FIX_CHAR_LOOP` 的 other 段上限 50 未核 | 敌人规模大且需精确摊还时 | [§9.0.45](../journal/13-world.md) |
| ⚠️ `CaptureAct.flags` 的语义在两段之间不对称 | — | 欠债 22 |

## 6. 判定为「走不到 / 不实现」(有判据,非遗漏)

| 项 | 判据 | 出处 |
|---|---|---|
| 时间预算截断路径(`_CHAR_LOOP_TIME` 那支) | 8.0 **关**;且墙钟不可确定性复现 | [§9.0.45](../journal/13-world.md) |
| `cep` 累积 `cep++` | 在**战斗态**分支,玩家走路恒非战斗态 ⇒ 走不到 | [§9.0.44](../journal/13-world.md) |
| 施加当场清目标指令(麻痹/睡眠/石化/魔障四种) | 本批唯一施加者产出的是**毒**,不在那四种里 ⇒ **无输入能执行** ⇒ 只建判据函数 + 单元用例,不写不可反验的死代码 | [§9.0.55](../journal/15-status.md) |
| `ENEMY_ID` 未写进 `Model::Enemy` | 读它的三个功能(GM 制宠 / 宠物领取 / 问答奖励)**全未移植** | [§9.0.35](../journal/12-encounter.md) |
| 25 个随机源调用点未逐个复核 | 退化区间现在是**合法输入** ⇒ 不再需要调用方保证 `lo<=hi` | [§9.0.36](../journal/12-encounter.md) |

## 7. 平台覆盖(已由 CI 常态化,历史条目)

★ 早期批次反复出现「只在 Apple clang 21 跑过,GCC/MSVC 交 CI」——
自 CI 挂载([§9.0.9](../journal/03-infra-ci-guards.md))后这已是**常态流程**而非欠债。
⚠️ 但 [`../00-architecture.md`](../00-architecture.md) §1.1 记的两项**仍未验**:emcc(wasm)/ Android NDK,
以及 **D2 ② 的覆盖面是「按文件」不是「按目录」** —— 客户端没有任何 TU include `shared/model/`。

---

## 另见

- **欠债表 33 条** → [`01-debts.md`](01-debts.md)(含已闭合的 12 条及其关闭方式)
- **待拍板 / 待数据** → [`03-pending-decisions.md`](03-pending-decisions.md)(⚠️ 待拍板 0 · ⏳ 待数据 7)
- **永久不可判定 / 实现期待补 69 项** → [`04-undecidable.md`](04-undecidable.md)
- **自由服魔改等「恒等划外」** → [`../deviations/03-not-ported.md`](../deviations/03-not-ported.md)
  (⚠️ 与本文件的区别:那边是**有意不做**,这边是**暂时没做**)
