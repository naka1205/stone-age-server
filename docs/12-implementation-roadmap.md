# 2026-09-12 实施计划与进度

用户在项目进展评估后要求“按计划继续执行”。按以下顺序完成发布收尾、基础反击、最小可玩流程及所需资源。架构与玩法口径沿用 `00`、`11`；新实现逐项回原始源码核实，未覆盖内容继续登记。

| 批次 | 交付与验收 | 状态 |
|---|---|---|
| P0 发布收尾 | 两仓提交、`shared-v0.22.0`/`shared-v0.23.0` 同步 Gitee/GitHub；独立远端 FetchContent；当前提交三平台 CI；刷新入口进度 | 已完成，见[发布验证](audits/2026-09-12-release-verification.md) |
| P1 基础反击 | 当前 `react == NONE` 路径的判定、伤害、行动顺序和事件消费；源码反例、世界接线及双端回归 | 已完成，见[反击验收](journal/16-counterattack.md) |
| P2 最小可玩流程 | 登录选角 → 一张真实地图 → 移动遇敌 → 手动战斗/捕获 → 奖励 → 退出重登仍保留；存储与真实内容导入沿用既有架构 | 已完成：原生 GUI、保存重登/服务端重启、真实 MySQL/Redis、独立远端取源及 7 项 CI 均通过，见[验收](audits/2026-09-12-playable-loop.md) |
| P3 所需资源 | F7 调色板 → F5 覆盖关系/F6 图集分组；只转换 P2 的地图、玩家和敌人所需资产 | 已完成本切片：3,910 图元/9 图集；上下方向、地图投影与敌左上/我右下站位均已验证，随 v0.26.0 发布 |

P2/P3 的完成以真实客户端操作和重启后的持久化结果为准；测试 fixture 和占位场景只用于开发验证。P1 的特殊反应写入者、完整职业/宠物技能、骑乘、经济和社交不因本计划自动计为完成。

## 起点

- 服务端 `2f6ae48`、客户端 `267ac92`；共享运行代码 `6a2882f` / `shared-v0.23.0`。
- 服务端 CTest 17/17、客户端 5/5；上一批实际 TCP + 图形成功/失败场景 5/5。
- [修复台账](audits/2026-09-12-remediation-results.md)的 19 项工程处置已完成；U01 已采用 SSRC80 的 0.3 扰动、整数 dex 最低 1。
- [反击排期及真实依赖](journal/15-status.md)：五个特殊反应 work 字段由技能写入，基础反击可独立推进。

## 实施记录

后续只在完成对应验证后更新上表和记录；发布结果、玩法依据与测试覆盖分别记载，不以历史绿色结果替代当前提交验收。

---

## 当前批次指针（2026-09-16 · A8 滚动指针，替代口头交接）

> 本表是**唯一滚动入口**：每批完成后更新一行，下一批从「下一批」列认领。
> 上文四批（P0–P3）为 09-12 快照，此处续记。

| 日期 | 批次 | 结果 | 下一批 |
|---|---|---|---|
| 09-15 | I\|（指令入口校验）+ A4（经济地基）+ B1/B2/B3（宠技三连发） | ✅ 已入 journal §9.0.57/58/61/63/65，推送窗口 v0.27–v0.30 闭合 | **批次 A 余项，按下方子批简报 A-α 起步** |
| 09-16 | A1 本机验证链路复验 + A2 工作树清点 | ✅ win_validate 11/11（ctest 22/22 · OpenSSL 探测生效）；civ.log / bak 报告归档 `docs/audits/evidence/2026-09-15/`（提交 `63d04d4`） | A3 首子批 → A5 → A6 |

**环境事实**（A1 复验结论）：09-15 10:54 的 `civ.log` 失败发生在 12:53 探测修复（`fde500d`，§9.0.59）**之前**，为历史证据非现存故障；09-16 02:19 在 `c213060`/shared-v0.30.0 上复跑 win_validate **11/11 全绿**（服务端 ctest 22/22、0 告警、OpenSSL 4.0.2 探测命中、断言防线反向验证通过）。

---

## 批次 A 余项 —— 指令分发分支子批切分简报（2026-09-16，A3 交付）

> **派单对象**：下一实现批次窗口。**本简报是输入不是规格**——每子批开工前必须回源码复核锚点，凡与 `tools/rescope_battle_port.json`（2026-08-31 度量）或本文冲突，以源码为准。
> **规模底表**：`rescope_battle_port.json`「★ 批次表」A 行 = 76 函数 / 8,007 行 / 真状态写 270 / 串缓冲 280 / rng 46。
> **⚠️ 现状核对义务**：底表量于 2026-08-31；此后 A.1–A.4、DR-BT21（PetIn/PetOut 世界侧）、B2（Charge）、L4.1（Status 系）、B3（含 `PETSKILL_MagicStatusChange_Battle` 铁壁）已从消费端移植了 A 批内约 12 个函数的语义。**每个子批第一步：对照下表「已被消费端覆盖」列逐函数核对现状，已落的不重做，只补缺口分支。**

### 已被消费端覆盖（开工前逐函数确认，不重做）

| 函数 | 覆盖批次 |
|---|---|
| `BATTLE_Escape` / `BATTLE_EscapeCheck`（D 批内被调） | A.1（DR-BT15，双重计数语义） |
| `BATTLE_Capture` 及捕获链 | A.2 + I.2（CaptureItemCheck/DelAll） |
| 暴击 / 打飞路径 | A.3 / A.4（DR-BT17/18） |
| `BATTLE_PetIn` / `BATTLE_PetOut` | DR-BT21（世界侧已落；`S_PetOut` 是**另一入口**，见勘误节） |
| `BATTLE_Charge` | B2（三拍状态机） |
| `BATTLE_MagicStatusSeq` / `PETSKILL_MagicStatusChange_Battle` | L4.1 + B3（铁壁链路） |
| `BATTLE_StatusSeq` 毒相关分支 | L4.1（单槽状态机）——**977 行里其余 32 种状态分支未落** |

### 子批表（按依赖与验证面切分，每子批独立 ctest 增量 + 反向验证）

| 子批 | 成员（≈行数） | 触点摘要 | 依赖 / 数据 | 验收口径 |
|---|---|---|---|---|
| **A-α 战果总装** | `BATTLE_AddProfit#1/#2`（115）· `BATTLE_getBattleDieIndex`（19） | 真状态 0 · 串 0 · rng 0——最便宜；挂接既有 GoldLedger / 掉落 / 经验路径 | 无新依赖；A4 经济批已立观察面 | BattleResult 装配用例：多目标经验分配、决斗点怪不给金（DR-EC6 门复用）、死亡名单；反向验证 ≥1 处 |
| **A-β 目标选择与守护** | `BATTLE_MultiList`（228）· `TargetListSet`（85）· `TargetAdjust/TargetCheck/Index2No/No2Index/DefaultAttacker/CountAlive/CheckSameSide/CanMoveCheck`（≈269）· `BattleModel`（139）· `Guard`（20）· 小工具 `GetWepon/talkToCli/TargetAdjust` 收尾 | 真状态 ~8 · 串 ~5 · rng ~5，多为纯函数——黄金用例最友好 | 无；B3 铁壁「全」不展开的机制在 `MultiList` | 位次换算黄金用例组；`MultiList` 全体/单体展开断言；**A5 守卫 Guardian 指令在本子批落地**（含 B1 表 `Guardian` 行消费） |
| **A-γ1 状态序列收口** | `BATTLE_StatusSeq` 余下分支（977，毒已落）· `ProfessionStatusSeq`（112）· `ProfessionStatus_init`（186）· `LerChange`（28）· `MagicStatusSeq` 缺口核对 | ★ 最大单体：真状态 58 · 串 48 · rng 8 | L4.1 地基在；状态 12..43 的**施加者**仍属宠技/职技域（本批只落推进与解除面） | `rules_battle` 状态推进增量用例（每回合行动位递减、阵亡暂停口径与 B3 对齐）；互斥矩阵复验 |
| **A-γ2 职业宿主** | `battle_profession_attack_fun`（490）· `battle_profession_status_chang_fun`（778）· `battle_profession_assist_fun`（306）· `PROFESSION_BATTLE_StatusAttackCheck`（40）· `attack_magic_fun`（29） | 真状态 ~74 · 串 ~76 · rng ~4——C 批次 64 函数的运行时宿主（净移植量 = 2 函数 + 1 张表的判据在此复核） | 职技效果表 fixture（手工构造，D 线不写运行时加载器） | C 表接线用例（53 同构薄包装走映射表）；`PROFESSION_escape/track` 两真实现逐行对照 |
| **A-δ 宠技战斗侧 + 特殊指令** | `BATTLE_S_Barrier/Nocast/Roar/Weaken/Deeppoison/Refresh/Sacrifice/Explode/FallGround`（≈683）· `PETSKILL_SetMagicPet/SetDuckChange/SetStrength_Battle`（129）· `BATTLE_S_PetOut`（45）· `AttReverse/NoAction/EarthRoundHide`（69） | 真状态 ~45 · 串 ~30 · rng ~8 | ⚠️ **A7 勘误适用**：`S_PetOut` = 技能驱动强制换宠（`_PETOUT_PETSKILL` 8.0 其实有），勿按旧记载跳过 | 每技能独立用例（fixture 用真数据行）；反验注入先问「改变哪个可观察值」（§9.0.62 教训） |
| **A-ε 攻击魔法与杂项** | `MultiAttMagic_Fire`（208）· `Attack_FIREKILL`（346）· `Abduct`（112）· `Steal/StealMoney`（235）· `Combo` 三件（353）· `DivideAttack`（88）· `E_ENEMYHELP/REFILE/REHP`（149）· `LostEscape`（63）· `PetLoyalCheck`（147）· `MultiRessurect` + OFFLINE（134）· `S_GBreak/GBreak2` 宿主核对（334） | 真状态 ~40 · 串 ~70 · rng ~20 | 攻击魔法部分依赖 `__ATTACK_MAGIC` 管线裁定（宠技表 36 行魔法系同源）——若管线未裁，本子批先落 steal/combo/divide/enemy 三事件 | combo 连锁用例、偷窃金币/道具双路径、DivideAttack 分摊；GBreak 宿主与 B1 消费端语义逐位对齐断言 |

### 执行纪律（全部沿用既有守则，此处只为免查）

1. **具名实参**不是风格，是防位置错位污染基线（§9.0.65 过程纠偏）。
2. 反向验证一律**全新构建目录**；还原文件后 bump mtime（§9.0.57 第 7 形态）。
3. 注入先问「改变了哪个可观察值」，0 条转红 ≠ 无缺口（§9.0.62 教训）。
4. `shared/` 或 `idl/generated` 有实质改动 ⇒ 打 tag 前推 + 客户端换 pin（§9.0.34 判据）；纯 `src/world` 批次（如 A-α 预期）不需前推。
5. 每子批完成：journal 新增 §9.0.N（N 接续 §9.0.66）+ README 映射表补行 + 本表「当前批次指针」更新。

### A7 勘误并入（取证仓 → 实现仓消费记录）

| 勘误 | 出处 | 影响子批 | 消费动作 |
|---|---|---|---|
| `_PETOUT_PETSKILL` 8.0 **其实有**（原「命中 0/2」是符号命中法漏判）；`BATTLE_S_PetOut`@battle_event.c:3914 · `BATTLE_COM_S_PETOUT`@battle.c:8138，是**技能驱动的强制换宠**，与玩家 PET_IN/PET_OUT 不同入口 | stoneage-plan `c9ad86b`（07 §12.1 勘误行） | **A-δ** | A-δ 开工简报引用该锚点；`S_PetOut` 逐行移植，不按「8.0 无」旧判跳过 |
| 逃跑「escape_cnt 首次即 1」错，**首次是 2（双重计数）**——已由 DR-BT15 照抄 | stoneage-plan `0f9149e` ① | —（已消费） | 无 |
| 捕获 Df_Level/Df_Dex 是**浮点除法**非整数——已由 DR-BT16 照抄 | 同上 ② | —（已消费） | 无 |
| ★ **行号基准**：07 文档行号 = unifdef_80 展开视图；实现仓 DR 行号 = 原始 stoneage85 树，两套差约 340 行（4346 落进 `S_GBreak2` 的实测教训） | 同上 ③ | **全部子批** | 派单与代码注释标注所用基准，禁止两套混引 |
| R3/R4 改判指针 + 03-assets §7 格式来源部分失效 | 同上 ④ | D 线（A9 评估时） | A9 评估文档引用 |

