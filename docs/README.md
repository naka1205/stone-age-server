# stone-age-server / docs — 文档总导航

> 石器时代 8.0 服务端移植项目的服务端文档。**本文件是唯一入口。**

> **2026-09-11 现有实现审计**：[两仓 8.0 玩法忠实性审计](audits/2026-09-11-8.0-fidelity.md)登记 19 项问题、既有主动差异及修复验收建议。当前先处理已有问题；阶段进度与测试通过不直接代表原版玩法已还原。

> **2026-09-12 修复实施**：[实施计划](audits/2026-09-12-remediation-plan.md)已落实到代码，逐项结果、完整验证、共享引用和未决事实见[修复结果](audits/2026-09-12-remediation-results.md)。

> **当前执行计划**：用户已要求按计划继续推进。[实施进度](12-implementation-roadmap.md)跟踪发布收尾、基础反击、最小可玩流程及资源管线；[发布验证](audits/2026-09-12-release-verification.md)记录本次远端验收。

## 按问题查文档

| 我想知道… | 去 | 性质 |
|---|---|---|
| **怎么设计的**(架构、分层、部署、玩法范围) | [`00-architecture.md`](00-architecture.md) · [`01-server-architecture.md`](01-server-architecture.md) | 架构真源 |
| **原版是怎么回事**(协议、模型、公式、数值) | [`02`](02-protocol.md)–[`10`](10-world-map.md) 功能文档 | 取证真源 |
| **为什么这么定**(每条决策的裁定与依据) | [`11-decision-register.md`](11-decision-register.md) | 决策真源 |
| **做过什么**(逐批次的取证 / 交付 / 复验 / 教训) | [`journal/`](journal/) | 流程账 |
| **还欠什么**(欠债 / 划外 / 待拍板 / 做不了) | [`backlog/`](backlog/) | 遗留 |
| **和原版哪里不一样** | [`deviations/`](deviations/) | 差异 |
| **现有实现有哪些已核实的问题、如何防止玩法漂移** | [`audits/`](audits/README.md) | 审计快照与复现证据 |

⚠️★★ **真源 vs 派生**:上表前三行是**真源**(内容只此一份);
[`journal/`](journal/) 是**原文搬家**(逐字迁自 `00` §9.0.x);
[`backlog/`](backlog/) 与 [`deviations/`](deviations/) 有一部分是**派生索引** ——
每份文件头都标了自己是哪种,**派生索引与真源冲突时以真源为准**。

---

## 架构真源

| 文档 | 主题 |
|---|---|
| [`00-architecture.md`](00-architecture.md) | **总架构定稿**:§0 四层前提 · §1 D1–D8 裁定总表 · §2 分层硬约束 · §3 服务边界 · §4 双部署形态 · §5 存储 · §6 一致性账单 · §7 D8 玩法范围 · §8 技术栈 · §9 阶段计划 · §10.4 三类静默错误 · §11 上游证据索引 |
| [`01-server-architecture.md`](01-server-architecture.md) | **服务端详情**:模块 / 线程 / tick 顺序 / 目录结构 / 会话 / 协议层 / 领域模型 / 存储 / 配置 / 可观测性 / 启动关闭 / 构建部署 |

★ **D2 是唯一预留的可撤销点**(`00` §1.1)—— 两半均已关闭,但覆盖面有窄边界,动 `shared/` 前先读。

## 取证真源(原版是怎么回事)

| 文档 | 主题 |
|---|---|
| [`02-protocol.md`](02-protocol.md) | IDL 设计与协议清单映射;窗口协议与战斗事件流 |
| [`03-domain-model.md`](03-domain-model.md) | L2 逐实体形状(693 slot / 65 别名 / 80 死字段)与 M1–M10 硬约束 |
| [`04-storage-schema.md`](04-storage-schema.md) | 库划分、表形状、T1–T13 事务对策、内容导入管线 |
| [`05-battle.md`](05-battle.md) | L3 纯函数边界 · 公式 · 状态异常 · 指令体系 |
| [`06-progression.md`](06-progression.md) | 经验双路径 · 等级上限 · 宠物成长 · 融合 |
| [`07-npc-quest.md`](07-npc-quest.md) | NPC 注册面 · 内容 DSL · 旗标系统 · 遇敌触发 |
| [`08-economy.md`](08-economy.md) | 五载体四上限 · `GoldLedger` · 源汇清单 · 审计事件 |
| [`09-social.md`](09-social.md) | 家族 · 频道订阅制 · 排行 · GM 命令 |
| [`10-world-map.md`](10-world-map.md) | 三源合并 · `LS2MAP` · 角色循环摊还 · 视野 529 格扇出 |

⚠️★★ **这些文档已被实证至少 7 次与源码分叉** —— 动手前回原版源码核实判定阈,
**别凭文档转述**(纪律 ①)。已抓到的分叉见 [`deviations/04-doc-vs-source.md`](deviations/04-doc-vs-source.md)。

## 决策真源

| 文档 | 主题 |
|---|---|
| [`11-decision-register.md`](11-decision-register.md) | **DR 决策寄存器**(跨双端单一收敛口):§1 技术选型 · §2 数据精度 · §3 战斗 · §4–§12 各域 · §13 阻塞映射 |

⚠️★★ **`11` 的 §1–§13 结构被 ctest 锁死** —— `tools/check_dr_table.py`(测试项 `dr_table`)
硬编码了该文件路径与 `## 1. ` / `## 13.` 两个锚点。**改章节标题必须同步改脚本**,
否则它会变成一条永远通过的检查。

## 流程账 · 遗留 · 差异

| 目录 | 内容 | 规模 |
|---|---|---|
| [`journal/`](journal/) | 逐批次执行记录,按功能模块分 11 个文件 | **56 节**(§9.0.1–9.0.55) |
| [`backlog/`](backlog/) | 欠债 33 条 · 划外索引 · 待拍板 0 / 待数据 7 · 永久不可判定 6 | 4 份 |
| [`deviations/`](deviations/) | 有意改变 · 照抄的缺陷 · 净核划外 · 文档与源码分叉 | 4 份 |
| [`audits/`](audits/README.md) | 当前实现的源码对照、问题复现与修复建议 | 2026-09-11 首轮：19 项问题 |

★★ **`§9.0.x` 编号一律沿用,搬家未改号** —— 全仓约 600 处旧引用继续有效。
查一条旧引用(如 `00 §9.0.35`):到 [`journal/README.md`](journal/README.md) 的全量映射表按编号找文件。

---

## 当前进度速览

| 维度 | 状态 |
|---|---|
| 阶段 | **阶段 1 双端 demo 已完成；阶段 2 按实施计划继续推进** |
| 已落地域 | L3 战斗(0/0.5/A.1–A.4)· L2 模型与属性(M.1–M.4b)· 遇敌与战果(M.5–M.7/R.1)· 世界(W.1–W.5)· 道具(I.1–I.4)· 状态(L4.1) |
| 最新批次 | 19 项问题处置后，按用户采纳的 SSRC80 口径实施 [U01 速度修复](audits/2026-09-12-speed-resolution.md) |
| 锁定 ref | **`shared-v0.23.0`**；本地验证已通过，当前远端状态以[发布验证](audits/2026-09-12-release-verification.md)为准；旧 `v0.22.0` 保留 |
| 欠债 | 保留 33 个历史编号，按 [backlog](backlog/01-debts.md) 的修复补记追踪子项 |
| 待拍板 | U01 已采纳，本轮新增待裁定项归零；原版事实边界和既有待数据项继续登记 |

## 相关仓

| 仓 | 定位 |
|---|---|
| `stone-age-client/` | 客户端(cocos2d-x 派 GameStudio 引擎);`shared/` 经 FetchContent 锁定 ref 消费 |
| `stoneage-plan/` | 取证与建议(**只出证据不做决断**);⚠️ 是取证工作流的**入口线索**,抓到分叉须回写勘误 |
| `StoneAge/` · `stoneage85/` · `csa8.0/` | 原版源码与数据(⚠️ **数据基准是 `csa8.0/gmsv/data/`**,与 `StoneAge/` 同名不同内容) |
