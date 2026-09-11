# journal — 开发流程账总索引

> 逐批次的**取证 / 交付 / 复验 / 教训**。原 `00-architecture.md` §9.0.1–9.0.55 整体迁入,
> **编号一律沿用,搬家未改号** —— 全仓约 600 处 `§9.0.x` 引用因此继续有效。
>
> 查一条旧引用(如 `00 §9.0.35`):在下表按编号找到文件,文件内小节标题仍是 `### 9.0.35 …`。

## 按功能模块

| 文件 | 收录 | 节数 |
|---|---|---|
| [`01-infra-bootstrap.md`](01-infra-bootstrap.md) | 工程骨架 · 阶段 0/1 起步 · `shared/wire/` · 1.4 服务端侧 | 8 |
| [`02-infra-d2-crosscompile.md`](02-infra-d2-crosscompile.md) | D2 两端编译实证 · 跨编译器出清 · Windows 验证 · CI 覆盖 GCC/MSVC | 5 |
| [`03-infra-ci-guards.md`](03-infra-ci-guards.md) | CI 挂载 · 五个守卫脚本的立案与它们抓到的真问题 | 6 |
| [`04-infra-naming.md`](04-infra-naming.md) | 项目定位澄清 · SA/SG 前缀 · P0–P6 命名改造 | 2 |
| [`10-battle-core.md`](10-battle-core.md) | L3 战斗:批次 0.5 调度 · A.1 逃跑 · A.2 捕获 · A.3 暴击 · A.4 打飞 | 5 |
| [`11-model-attr.md`](11-model-attr.md) | L2 实体池与实体族 · M.1–M.4b 属性推导与四维公式 · 换宠 | 7 |
| [`12-encounter.md`](12-encounter.md) | M.5 敌人表 · R.1 随机源 · M.6/M.7 遇敌链 · 战果结算 | 5 |
| [`13-world.md`](13-world.md) | W.1 移动视野 · W.2/W.3 刷怪游荡 · W.4 暗雷 · W.5 明雷 | 4 |
| [`14-item.md`](14-item.md) | 道具域 I.1 背包 · I.2 扣道具 · I.3 掉落 · I.4 使用 | 4 |
| [`15-status.md`](15-status.md) | L4.1 状态异常系统 | 1 |
| [`90-push-windows.md`](90-push-windows.md) | 九次 `shared-v0.x.0` 推送窗口的闭合核实 | 9 |
| [`99-changelog.md`](99-changelog.md) | 变更记录:原 `00` §12(62 条)+ `11` §15(25 条) | — |
| **合计** | | **56** |

## 全量映射表(按编号)

| 批次 | 主题 | 日期 | 文件 |
|---|---|---|---|
| §9.0.1 | 0.1 与 0.2 并行 | 2026-08-31 | [`01-infra-bootstrap.md`](01-infra-bootstrap.md) |
| §9.0.2 | 0.1 的前置比原先记载的多一项 | 2026-08-31 | [`01-infra-bootstrap.md`](01-infra-bootstrap.md) |
| §9.0.3 | 0.1 的产物形态 —— 一遍到位,不做中间态 | 2026-08-31 | [`01-infra-bootstrap.md`](01-infra-bootstrap.md) |
| §9.0.4 | 1.4 依赖的 L0/L1 被整块划在阶段 2 —— 补为 1.5 | 2026-08-31 | [`01-infra-bootstrap.md`](01-infra-bootstrap.md) |
| §9.0.5 | D2 的两端编译已实证 —— 比计划早了一个阶段 | 2026-09-02 | [`02-infra-d2-crosscompile.md`](02-infra-d2-crosscompile.md) |
| §9.0.6 | 跨编译器验证 —— 在 mac 上关掉 MSVC 的两类风险 | 2026-09-02 | [`02-infra-d2-crosscompile.md`](02-infra-d2-crosscompile.md) |
| §9.0.7 | Windows 一次性验证 | 2026-09-02 | [`02-infra-d2-crosscompile.md`](02-infra-d2-crosscompile.md) |
| §9.0.7.1 | 执行结果 | 2026-09-02 | [`02-infra-d2-crosscompile.md`](02-infra-d2-crosscompile.md) |
| §9.0.8 | 批次 0.5 —— 回合调度落地,并暴露文档的两处缺口 | 2026-09-03 | [`10-battle-core.md`](10-battle-core.md) |
| §9.0.9 | CI 挂载 | 2026-09-03 | [`03-infra-ci-guards.md`](03-infra-ci-guards.md) |
| §9.0.10 | CI 首跑归因 | 2026-09-03 | [`03-infra-ci-guards.md`](03-infra-ci-guards.md) |
| §9.0.11 | 项目定位澄清与命名前缀裁定 | 2026-09-04 | [`04-infra-naming.md`](04-infra-naming.md) |
| §9.0.12 | 改名后首跑归因 | 2026-09-04 | [`03-infra-ci-guards.md`](03-infra-ci-guards.md) |
| §9.0.13 | 两条守卫补齐 | 2026-09-04 | [`03-infra-ci-guards.md`](03-infra-ci-guards.md) |
| §9.0.14 | 1.5 收尾 —— TcpTransport 接入入口,并修掉一处会污染构建目录的反向验证 | 2026-09-05 | [`01-infra-bootstrap.md`](01-infra-bootstrap.md) |
| §9.0.15 | DR-TS9 落地 —— 新立 `shared/wire/`,成帧与信封成为双端共编的第二份源码 | 2026-09-06 | [`01-infra-bootstrap.md`](01-infra-bootstrap.md) |
| §9.0.16 | 1.4 服务端侧装配 —— 会话就绪即入场,并补上一个静默失效的配置面 | 2026-09-06 | [`01-infra-bootstrap.md`](01-infra-bootstrap.md) |
| §9.0.17 | CI 首次覆盖 GCC/MSVC —— 一条注释承诺了三年、代码从未兑现的语义 | 2026-09-06 | [`02-infra-d2-crosscompile.md`](02-infra-d2-crosscompile.md) |
| §9.0.18 | 1.4 收口 —— 客户端侧落地,阶段 1 的验收点达成 | 2026-09-06 | [`01-infra-bootstrap.md`](01-infra-bootstrap.md) |
| §9.0.19 | 批次 A.1 —— 逃跑落地,阶段 2 第一个玩法指令 | 2026-09-06 | [`10-battle-core.md`](10-battle-core.md) |
| §9.0.20 | 批次 A.2 —— 捕获落地,并把「换宠」从 A.2 移到 L2 之后 | 2026-09-06 | [`10-battle-core.md`](10-battle-core.md) |
| §9.0.21 | 批次 A.3 —— 暴击落地,§9.0.8 留空的理由反过来被源码推翻 | 2026-09-06 | [`10-battle-core.md`](10-battle-core.md) |
| §9.0.22 | 批次 A.4 —— 打飞 / 究极一击落地 | 2026-09-06 | [`10-battle-core.md`](10-battle-core.md) |
| §9.0.23 | 命名改造 P0–P6 —— 全盘对齐引擎 GameStudio | 2026-09-07 | [`04-infra-naming.md`](04-infra-naming.md) |
| §9.0.24 | L2 实体池地基 —— 阶段 2 领域模型层起步 | 2026-09-07 | [`11-model-attr.md`](11-model-attr.md) |
| §9.0.25 | 阶段 1 之后的提交审查 —— 33 个提交 × 三仓文档的一次交叉核对 | 2026-09-07 | [`03-infra-ci-guards.md`](03-infra-ci-guards.md) |
| §9.0.26 | L2 实体族接线 —— 批次 M.1,并当场推翻三处记载 | 2026-09-07 | [`11-model-attr.md`](11-model-attr.md) |
| §9.0.27 | 批次 M.2 —— 战场态宠物入场地基,纠正命名方向 + 绕开两个源码陷阱 | 2026-09-07 | [`11-model-attr.md`](11-model-attr.md) |
| §9.0.28 | 批次 DR-BT21 —— 换宠指令 PET_IN / PET_OUT,激活 M.2 的入场机制 | 2026-09-07 | [`11-model-attr.md`](11-model-attr.md) |
| §9.0.29 | 批次 M.3 —— 属性推导落地,并证伪了 demo 自己的数值 | 2026-09-08 | [`11-model-attr.md`](11-model-attr.md) |
| §9.0.30 | `.clang-format` 纪律终于有了执行者 —— 欠债 24 关闭 | 2026-09-08 | [`03-infra-ci-guards.md`](03-infra-ci-guards.md) |
| §9.0.31 | 批次 M.4a —— 四维的来源公式落地 | 2026-09-08 | [`11-model-attr.md`](11-model-attr.md) |
| §9.0.32 | 推送窗口执行记录 —— `shared-v0.12.0` | 2026-09-08 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.33 | 批次 M.4b —— 敌人 L2 实体族落地,欠债 23 与 25 双双关闭 | 2026-09-09 | [`11-model-attr.md`](11-model-attr.md) |
| §9.0.34 | 推送窗口执行记录 —— `shared-v0.13.0` | 2026-09-09 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.35 | 批次 M.5 —— 敌人表 `enemy1.txt`:等级摇号落地,并撞出四处"名字在骗人" | 2026-09-09 | [`12-encounter.md`](12-encounter.md) |
| §9.0.36 | 批次 R.1 —— 随机源退化区间的消耗语义 | 2026-09-09 | [`12-encounter.md`](12-encounter.md) |
| §9.0.37 | 批次 M.6 —— 遇敌:坐标 → 区域 → 编组 | 2026-09-09 | [`12-encounter.md`](12-encounter.md) |
| §9.0.38 | 推送窗口执行记录 —— `shared-v0.14.0` | 2026-09-09 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.39 | 批次 M.7 —— 遇敌:编组 → 敌人列表 | 2026-09-09 | [`12-encounter.md`](12-encounter.md) |
| §9.0.40 | 批次 战果结算 —— 打赢野怪拿经验:EXP + 战斗结束分配 + BattleResult 下发 | 2026-09-09 | [`12-encounter.md`](12-encounter.md) |
| §9.0.41 | 推送窗口执行记录 —— shared-v0.15.0 | 2026-09-09 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.42 | 批次 W.1 —— 移动系统:玩家移动 + 529 格视野广播 | 2026-09-09 | [`13-world.md`](13-world.md) |
| §9.0.43 | 推送窗口执行记录 —— shared-v0.16.0 | 2026-09-09 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.44 | 批次 W.4 —— 遇敌触发闭环:走动 → 遇敌 → 战斗 → 拿经验 | 2026-09-09 | [`13-world.md`](13-world.md) |
| §9.0.45 | 批次 W.2+W.3 —— 世界敌人:地图刷怪 + 条数制摊还游荡 + 视野扩到"玩家看敌人" | 2026-09-10 | [`13-world.md`](13-world.md) |
| §9.0.46 | 批次 W.5 —— 明雷触发战斗:EV 事件开战 + 撞明雷退回 | 2026-09-10 | [`13-world.md`](13-world.md) |
| §9.0.47 | 推送窗口执行记录 —— shared-v0.17.0 + shared-v0.18.0 | 2026-09-10 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.48 | 批次 I.1 —— 背包 L2 地基:Item 族 + 道具池 + Player 背包槽 | 2026-09-10 | [`14-item.md`](14-item.md) |
| §9.0.49 | 推送窗口执行记录 —— shared-v0.19.0 | 2026-09-10 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.50 | 批次 I.2 —— 捕获扣道具:CaptureItemCheck 前置门 + CaptureItemDelAll … | 2026-09-10 | [`14-item.md`](14-item.md) |
| §9.0.51 | 批次 I.3 —— 野怪掉落:spawn 千分率摇 → 结算逐件随机拾取 → 灌背包 | 2026-09-10 | [`14-item.md`](14-item.md) |
| §9.0.52 | 推送窗口执行记录 —— shared-v0.20.0 | 2026-09-10 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.53 | 批次 I.4 —— 使用道具:战斗内 HP 恢复药 | 2026-09-11 | [`14-item.md`](14-item.md) |
| §9.0.54 | 推送窗口执行记录 —— shared-v0.21.0 | 2026-09-11 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.55 | 批次 L4.1 —— 状态异常系统:单槽状态机 + 带毒装备 + 每回合推进 | 2026-09-11 | [`15-status.md`](15-status.md) |

---

## 相关

| 去处 | 看什么 |
|---|---|
| [`../00-architecture.md`](../00-architecture.md) | 架构裁定 D1–D8 · 分层 · 部署 · 阶段计划(真源) |
| [`../01-server-architecture.md`](../01-server-architecture.md) | 模块 / 线程 / tick / 协议 / 存储 / 配置 |
| [`../11-decision-register.md`](../11-decision-register.md) | DR 决策寄存器(跨双端单一收敛口) |
| [`../backlog/`](../backlog/) | 还欠什么:欠债表 · 划外顺延 · 待拍板 · 永久不可判定 |
| [`../deviations/`](../deviations/) | 和原版哪里不一样:有意改变 · 照抄的缺陷 · 净核划外 · 文档与源码分叉 |

