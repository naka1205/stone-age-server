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
| [`10-battle-core.md`](10-battle-core.md) | L3 战斗:批次 0.5 调度 · A.1 逃跑 · A.2 捕获 · A.3 暴击 · A.4 打飞 · A-β d1 尾摇 · A-β d2 目标判定族与忠犬守护 · A-γ2 职业宿主与64职技映射 · A-ε 攻击魔法与杂项 | 9 |
| [`11-model-attr.md`](11-model-attr.md) | L2 实体池与实体族 · M.1–M.4b 属性推导与四维公式 · 换宠 · P.1/P.2 成长装备加点 | 9 |
| [`12-encounter.md`](12-encounter.md) | M.5 敌人表 · R.1 随机源 · M.6/M.7 遇敌链 · 战果结算 · A-α 战果总装 | 6 |
| [`13-world.md`](13-world.md) | W.1 移动视野 · W.2/W.3 刷怪游荡 · W.4 暗雷 · W.5 明雷 · W.6 传送点 · W.7 NPC实体Healer · W.8 TownPeople对话与窗口 · W.9 任务旗标与ExChangeMan · W.10 道具宠物交付与奖励结算 · W.11 NPC巡逻与漫游 · W.12 NPC商店与买卖 · W.13 宠物商店与技能导师 · W.14 告示牌与传送员 · D.1 真实地图LS2MAP与萨伊那斯NPC · D.2 四大村庄多地图与NPC/Warp · D.3 加鲁卡南岛与地下城 · 阶段 2 队伍系统与组队协同 · 玩家间安全交易与决斗切磋 · 名片夹与好友/邮件/分级聊天频道 · 家族管理与四大庄园据点占领 · 玩家摆摊与拍卖市场系统 · 骑乘系统 · 选角流程与多角色槽位 · 宠物融合与转生系统 · 任务引擎脚本全景扩展 · 称号系统与声望商城体系 · 道具制造与生活技能（料理/合成系统与素材加工） · 宠物进阶成长与骑乘认证体系 · 宠物进阶技能与忠诚度交互体系 · 庄园家族战体系与骑乘战备闭环 · 阶段 2.6 称号与名片持久化贯通及 World.cpp 单体解耦重构 · 阶段 2.7 遇敌与战斗子系统深度解耦 · 阶段 2.8 大世界移动/多楼层/传送子系统深度解耦 · 阶段 2.9 NPC对话/剧情/商店/技能解耦 · 阶段 2.10 战斗观战系统落地与广播链路全覆盖 · 阶段 2.11 战斗救援与乱入系统落地 · 阶段 3.1 大世界全量资产批处理管线与地图/NPC/传送门编目落地 · 阶段 3.2 战中掉线断网保护与重连接管系统落地 | 38 |
| [`14-item.md`](14-item.md) | 道具域 I.1 背包 · I.2 扣道具 · I.3 掉落 · I.4 使用 · I\| 指令入口校验 | 5 |
| [`15-status.md`](15-status.md) | L4.1 状态异常系统 · A-γ1 混乱重定向与核心状态推进/职业被动联动 | 3 |
| [16-counterattack.md](16-counterattack.md) | 基础反击、连锁时序与战果归属 | 1 |
| [17-economy.md](17-economy.md) | 经济域:GoldLedger 单入口 · 战斗产币 · OpenSSL 根探测 | 2 |
| [`18-petskill.md`](18-petskill.md) | 宠技三连发:B1 直攻系 · B2 宠技槽 + CHARGE · B3 状态系 + 铁壁 · A-δ 特殊宠技 | 4 |
| [`90-push-windows.md`](90-push-windows.md) | 二十五次 `shared-v0.x.0` 推送窗口的闭合核实 | 25 |
| [`99-changelog.md`](99-changelog.md) | 变更记录:原 `00` §12(62 条)+ `11` §15(25 条) | — |
| **合计** | | **123** |

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
| §9.0.56 | 基础反击：整次普攻后最多五次交替反击与真实战果归属 | 2026-09-12 | [`16-counterattack.md`](16-counterattack.md) |
| §9.0.57 | 批次 I\| —— 战斗指令入口校验:指令接收时的持有与目标门 | 2026-09-15 | [`14-item.md`](14-item.md) |
| §9.0.58 | 批次 A4 —— 经济地基:GoldLedger 单入口 + 战斗产币 | 2026-09-15 | [`17-economy.md`](17-economy.md) |
| §9.0.59 | Windows 本机 OpenSSL 根自动探测 —— configure 阻塞解除 | 2026-09-15 | [`17-economy.md`](17-economy.md) |
| §9.0.60 | 推送窗口执行记录 —— shared-v0.27.0(I| / A4 / OpenSSL 探测三笔同窗) | 2026-09-15 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.61 | 批次 B1 —— 直攻系宠技:RENZOKU / GBREAK(2) / MIGHTY / POWERBALANCE | 2026-09-15 | [`18-petskill.md`](18-petskill.md) |
| §9.0.62 | 推送窗口执行记录 —— shared-v0.28.0(批次 B1) | 2026-09-15 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.63 | 批次 B2 —— 宠技槽(B2a)+ CHARGE 集气(B2b);宠技列校正 c25..c31 | 2026-09-15 | [`18-petskill.md`](18-petskill.md) |
| §9.0.64 | 推送窗口执行记录 —— shared-v0.29.0(批次 B2) | 2026-09-15 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.65 | 批次 B3 —— 状态系宠技 + 铁壁;宠技状态实参核正(per=30/Range=40/Bai=2.0) | 2026-09-15 | [`18-petskill.md`](18-petskill.md) |
| §9.0.66 | 推送窗口执行记录 —— shared-v0.30.0(批次 B3) | 2026-09-15 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.67 | 批次 A-α —— 战果总装:AddProfit 对账 + expForKill 纯函数化 + ISDIE 门 finished 落点钉 | 2026-09-16 | [`12-encounter.md`](12-encounter.md) |
| §9.0.68 | 批次 A-β d1 —— AttackSeq 尾补摇忠实重排 + GBREAK 清零回归包装层 + 尾摇钉 | 2026-09-16 | [`10-battle-core.md`](10-battle-core.md) |
| §9.0.69 | 批次 A-β d2 —— 目标判定族 + 忠犬守护全链(MultiList 有意划外;RV-5 负结果登记) | 2026-09-17 | [`10-battle-core.md`](10-battle-core.md) |
| §9.0.69b | 推送窗口执行记录 —— `shared-v0.31.0`(批次 A-β d1;补记) | 2026-09-16 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.69c | 推送窗口执行记录 —— `shared-v0.32.0`(批次 A-β d2;注释里复述值必然烂) | 2026-09-18 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.70 | 批次 A-γ1 —— 核心状态序列:混乱目标重定向全链(battle.c:5644-5665) | 2026-09-24 | [`15-status.md`](15-status.md) |
| §9.0.71 | 批次 W.6 —— 静态事件格与 WARP 传送点(npc_warp.c) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.72 | 批次 W.7 —— NPC 实体框架与 Healer 恢复员(npc_healer.c) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.72b | 推送窗口执行记录 —— `shared-v0.33.0`(批次 A-γ1 + W.6/W.7;双端 CI 与 D2 消除漂移) | 2026-09-25 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.73 | 批次 W.8 —— 城镇居民 NPC 对话(TownPeople)与对白/窗口骨架(WindowOpen/WindowReply) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.74 | 批次 W.9 —— 任务旗标空间与 ExChangeMan 基础事件块解析骨架(NOWEV/ENDEV/EventEnd) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.74b | 推送窗口执行记录 —— `shared-v0.34.0`(批次 W.9;Player 任务旗标扩展与双端漂移消除) | 2026-09-25 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.75 | 批次 W.10 —— ExChangeMan 道具/宠物交付与奖励结算(GetItem/DelItem/GetPet/DelPet/GoldLedger) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.75b | 推送窗口执行记录 —— `shared-v0.35.0`(批次 W.10;Pet.h 增加 pet_id 与双端漂移消除) | 2026-09-25 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.76 | 批次 W.11 —— 世界 NPC 巡逻与随机移动漫游(npc_wanderer/NPC_walk/route/wander_radius) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.77 | 批次 W.12 —— NPC 商店与道具交易系统(ShopMan/Buy/Sell/sellItemToShop) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.78 | 批次 W.13 —— 宠物商店与宠物技能商人(PetShop/PetSkillShop/buyPet/sellPet/learnSkill) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.79 | 批次 W.14 —— 告示牌与传送员 NPC(SignBoard/WarpMan/warpPlayerByNpc/kWarpFee) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.80 | 批次 D.1 —— 真实地图 LS2MAP 原生解析与萨伊那斯 NPC 数据接入(LS2MAP/parseLs2Map/world.json/WarpPoint/NpcEntity) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.81 | 批次 D.2 —— 四大村庄全量地图与 NPC/Warp 批量导入与多地图管理(MultiFloor/decodeBase64/loadFloorMap/findFloorMap) | 2026-09-25 | [`13-world.md`](13-world.md) |
| §9.0.82 | 批次 P.1 —— 成长与装备闭环: 经验曲线与角色/宠物升级体系及装备槽穿戴加成 | 2026-09-25 | [`11-model-attr.md`](11-model-attr.md) |
| §9.0.82b | 推送窗口执行记录 —— `shared-v0.36.0`(批次 P.1;Progression/Item/Pet/Player 扩展与双端漂移消除) | 2026-09-25 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.83 | 批次 P.2 —— 属性点分配系统: SKILLUP 消费与四维加点闭环 | 2026-09-25 | [`11-model-attr.md`](11-model-attr.md) |
| §9.0.83b | 推送窗口执行记录 —— `shared-v0.37.0`(批次 P.2;Progression 加点纯函数与双端漂移消除) | 2026-09-25 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.84 | 批次 A-γ2 —— 职业宿主与职业属性上限 / 非战斗职技 / 64 职技映射表与直攻宿主执行闭环 | 2026-09-25 | [`10-battle-core.md`](10-battle-core.md) |
| §9.0.84b | 推送窗口执行记录 —— `shared-v0.38.0`(批次 A-γ2;职业系统与 64 职技映射表 / 双端漂移消除) | 2026-09-25 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.85 | 批次 A-γ1 —— 核心状态序列推进与解除收口 / 职业被动在场生效与状态联动 | 2026-09-25 | [`15-status.md`](15-status.md) |
| §9.0.85b | 推送窗口执行记录 —— `shared-v0.39.0`(批次 A-γ1;核心状态推进/挑拨附身/火附体/职业被动与逆境回复) | 2026-09-25 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.86 | 批次 A-δ —— 宠技战斗侧与特殊指令(舍身/自爆/落马/大吼/状态释放/状态回复/属性反转/地球一周/增益回血) | 2026-09-26 | [`18-petskill.md`](18-petskill.md) |
| §9.0.86b | 推送窗口执行记录 —— `shared-v0.40.0`(批次 A-δ;宠技战斗侧特殊指令与双端漂移消除) | 2026-09-26 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.87 | 批次 A-ε —— 攻击魔法与杂项(火杀物理/魔法 · 拐骗 · 偷窃金币 · 合击累加 · 恩惠削减分摊 · 敌人求援 · 忠诚判定 · 群体复活 · GBreak宿主对齐) | 2026-09-26 | [`10-battle-core.md`](10-battle-core.md) |
| §9.0.87b | 推送窗口执行记录 —— `shared-v0.41.0`(批次 A-ε;攻击魔法与杂项闭环) | 2026-09-26 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.88 | 批次 D.3 —— 加鲁卡南岛、附属村庄与深渊地下城多地图深化与客户端窗口/战斗表现补齐 | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.88b | 推送窗口执行记录 —— `shared-v0.42.0`(批次 D.3;Bundle.cpp 16MB 静态安全上限提升) | 2026-09-26 | [`90-push-windows.md`](90-push-windows.md) |
| §9.0.89 | 阶段 2 —— 队伍系统与组队协同: 5人队伍上限 · 贪吃蛇跟随算法 · 队员位移门禁 · 组队传送与组队战斗闭环 | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.90 | 阶段 2 —— 玩家间安全交易系统 (Trade System) 与 决斗切磋 (Duel / PVP System): 距离/存活/同队/双队长发起门禁 · 双向锁定确认防诈状态机 · 背包/宠物/石币三层容量防刷预检 · 原子置换与出战宠重置 | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.91 | 阶段 2 —— 核心社交与通信系统: 名片夹与好友 (AddressBook) · 邮件/离线信件 (Mail) · 分级聊天频道 (Chat & Channel): 80张名片上限 · 上下线感知 · 20封邮箱上限 · 附件资产流转与提取三层预检 · 四大分级广播与黑名单拦截 | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.92 | 阶段 2 —— 家族管理与庄园系统: 家族创建与解散 · 职位任免与50人上限 · 家族金库与贡献 · 四大庄园据点占领 · 家族专属频道 (Family System): 30级门限 · 1万扣币 · 50人满员门禁 · 1亿金库上限 · 随身容量预检 · 4大庄园占领与争夺 | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.93 | 阶段 2 —— 玩家摆摊与拍卖市场系统: 原地摆摊与状态锁定 · 寄售上架与100挂牌费 · 5%交易税与资产解耦 · 离线与溢出系统邮件到账保全 (Street Stall & Consignment Market System): 摆摊移动/组队/交易拦截 · 出战宠安全重置 · 10件在售上限 · 买家满包满宠栏阻断 · 离线与溢出邮件保全 | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.94 | 阶段 2 —— 骑乘系统 (Ride System): 资质门限与濒死拦截 · 复合外观切换与下马还原 · 出战宠互斥 · 战斗生命分摊与战后血量回写 · 资产流转脱钩与庄园特权联动 | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.95 | 阶段 2 —— 选角流程与多角色槽位系统 (Multi-character Slots & Creation Flow): 2槽位查询与选角 · [RV-1] 槽位超限拦截 · 12种原型与48种配色外观校验与头像映射 · [RV-2] 初始四维与地水火风属性分配合法性校验 · 四大新手村出生地分配 · 多楼层大世界跨图恢复登入 | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.96 | 阶段 2 —— 宠物融合与转生系统 (Pet Fusion & Rebirth): 三表投影目标宠物 (PetTable/PropertyTable/FusionTable) · 四维成长继承与等级削弱惩罚 · 技能遗传与非法技能过滤 · [RV-1] 融合资格与转生等级门禁/次数上限/状态互斥 · [RV-2] 转生五次方与Fx档位算力精确验证 · 融合师与转生师NPC交互闭环 | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.97 | 阶段 2 —— 任务引擎脚本全景扩展 (ExChangeMan Script Expansion): 递归下降复合条件解析器 (与/或/非/括号优先级/关系运算符) · 变量表全集 (LV/TRANS/FAME/FM/PROF/GOLD/HP/MP/SP/reITEM/rePET/ITEM/PET/NOWEV/ENDEV) · 动作集执行闭环 (经验/点数/血蓝/声望/传送/旗标) · 多步对话树推进 (NextBlock) 与委托/清除状态机 (REQUEST/CLEAN) · [RV-1] 复合条件门禁防御拦截 · [RV-2] 动作原子执行与资产事务一致性 | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.98 | 阶段 2 —— 称号系统与声望商城体系 (Title & Fame Shop): 称号元数据注册与四维加成 (TitleStatsBonus) · 称号佩戴/卸下与30个上限 · 声望商城NPC交互与弹窗 · 称号/道具/宠物兑换闭环 · [RV-1] 称号佩戴门限与防重购/超限防御拦截 · [RV-2] 声望商城兑换原子事务一致性 (声望/背包/宠物栏严格0损耗拦截) | 2026-09-26 | [`13-world.md`](13-world.md) |
| §9.0.99 | 阶段 2 —— 道具制造与生活技能 (Cooking & Crafting / Synthesis): 料理烹饪与合成精炼配方注册 (CraftingRecipe) · 堆叠消耗与空槽预检 · 存活宠物协助加成 · 工匠NPC交互 · [RV-1] 状态互斥/食材混杂/槽位作弊防御拦截 · [RV-2] 制造资产事务一致性反向变异验证 (材料/手续费不足/背包满严格0扣减) | 2026-09-27 | [`13-world.md`](13-world.md) |
| §9.0.100 | 阶段 2 —— 宠物进阶成长与骑乘认证体系 (Pet Advanced Growth & Ride Certification): 庄园骑乘认证 (RideCertType) · 考核流程闭环 (takeRideExam) · 考核学费 20% 原子注资庄园家族金库 (原版 w.takegold / 5 对齐) · 骑乘门禁与进阶相性共鸣 (calculateRideAffinity) · 骑乘考官 NPC (kRideMaster) · [RV-1] 庄园专属骑宠认证门禁与未授权拦截 · [RV-2] 认证考核原子事务与庄园金库 20% 分成一致性 | 2026-09-27 | [`13-world.md`](13-world.md) |
| §9.0.101 | 阶段 2 —— 宠物进阶技能与忠诚度交互体系 (Pet Loyalty & Advanced Skills): 宠物喂食交互全流程 (feedPet) · 食物类型门禁 (item_type == 20) · 等级压制与忠诚上限封顶 · 技能遗忘 (forgetPetSkill) · 顺服失控状态机 (checkPetObedience) · 战后胜负与生死战损结算 · [RV-1] 食物门禁与等级压制封顶拦截 · [RV-2] 摆摊互斥与资产原子扣减一致性 | 2026-09-27 | [`13-world.md`](13-world.md) |
| §9.0.102 | 阶段 2 —— 庄园家族战体系与骑乘战备闭环 (Manor War & Ride Battle Preparation): 四大庄园据点争夺与决斗调度 · 族长门禁与10万押金原子扣减 · 无主进驻与约战排期状态机 · 决斗比分累加与交战推进 · 庄园归属过户与押金100%注资守方金库 · [RV-1] 族长门禁与已有庄园互斥拦截 · [RV-2] 资金不足零扣减与结算交割100%注资一致性 | 2026-10-02 | [`13-world.md`](13-world.md) |
| §9.0.103 | 阶段 2.6 —— 称号与名片持久化闭环贯通及 World.cpp 单体解耦重构 (World Persistence & Domain Decomposition): titles/address_book 双向持久化贯通 · 14,265 行单体拆分为 WorldImpl.h 与 7 大领域编译单元 · 保持模块边界与审计守卫 · CTest 22/22 绿灯 | 2026-10-02 | [`13-world.md`](13-world.md) |
| §9.0.104 | 阶段 2.7 —— 遇敌与战斗子系统深度解耦 (WorldEncounter.cpp & WorldBattle.cpp): 暗雷/明雷遇敌链与敌人生成提取至 WorldEncounter · 战斗生命周期/推进循环/指令处理/纯函数族提取至 WorldBattle · World.cpp 降至 4,994 行 · CTest 22/22 全绿 | 2026-10-03 | [`13-world.md`](13-world.md) |
| §9.0.105 | 阶段 2.8 —— 大世界移动、多楼层与传送子系统深度解耦 (WorldMovement.cpp): 玩家移动推进/物理步进/碰撞阻挡/WarpPoint瞬移/多楼层管理提取至 WorldMovement · World.cpp 降至 4,381 行 · CTest 22/22 全绿 | 2026-10-03 | [`13-world.md`](13-world.md) |
| §9.0.106 | 阶段 2.9 —— NPC 对话事件、任务剧情、商店买卖与技能导师子系统解耦 (WorldNpcDialog.cpp): onEvent事件派发/onWindowReply窗口应答/ExChange前置求值与副作用/商店买卖/宠物技能学习解耦至 WorldNpcDialog · World.cpp 锐降至 2,300 行 · CTest 22/22 全绿 | 2026-10-03 | [`13-world.md`](13-world.md) |
| §9.0.107 | 阶段 2.10 —— 战斗观战系统落地与广播链路全覆盖 (Battle Spectating & Multicast Pipeline): 观战席位抽象 · 入场三件套与槽位 20 · 逐行动与快照全量多播 · 回合就绪同步 · 决胜结算与离场恢复 · 面前同屏观战 (World::spectatePlayer) · CTest 22/22 全绿 | 2026-10-03 | [`13-world.md`](13-world.md) |
| §9.0.108 | 阶段 2.11 —— 战斗救援与乱入系统落地 (Battle Rescue & Join-in-Progress System): 乱入门禁校验 · 0..4 槽位动态分配 · 穿戴与乘骑推导 · 出战宠协同带出 · 全场快照广播 · 协议级事件流接入 (onEvent event_type=3) · CTest 22/22 全绿 | 2026-10-03 | [`13-world.md`](13-world.md) |
| §9.0.109 | 阶段 3.1 —— 大世界全量资产批处理管线与地图/NPC/传送门编目落地 (Batch Content Pipeline & World Catalogs): 1,185 张有效 LS2MAP 解析 · 9,011 条双轨传送网络去重合并 · 3,450 个具名功能 NPC 全量结构化分类 · 独立回归测试套件 · CTest 22/22 全绿 | 2026-10-03 | [`13-world.md`](13-world.md) |
| §9.0.110 | 阶段 3.2 —— 战中掉线断网保护与重连接管系统落地 (Battle Disconnect Grace & Re-attach System): 断线槽位保全 · 离线自动防御托管 · 新会话重登接管 (World::reattachBattle) · 快照与回合恢复 · 单元测试验证 · CTest 22/22 全绿 | 2026-10-03 | [`13-world.md`](13-world.md) |


---

## 相关

| 去处 | 看什么 |
|---|---|
| [`../00-architecture.md`](../00-architecture.md) | 架构裁定 D1–D8 · 分层 · 部署 · 阶段计划(真源) |
| [`../01-server-architecture.md`](../01-server-architecture.md) | 模块 / 线程 / tick / 协议 / 存储 / 配置 |
| [`../11-decision-register.md`](../11-decision-register.md) | DR 决策寄存器(跨双端单一收敛口) |
| [`../backlog/`](../backlog/) | 还欠什么:欠债表 · 划外顺延 · 待拍板 · 永久不可判定 |
| [`../deviations/`](../deviations/) | 和原版哪里不一样:有意改变 · 照抄的缺陷 · 净核划外 · 文档与源码分叉 |
