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
| 09-16 | A6（D8 覆盖台账 `13-d8-coverage.md`：核心 29 = ✅3/🔄9/⬜17）+ **批次 A-α 战果总装**（§9.0.67：expForKill 纯函数化 · ISDIE 门 finished 落点钉 · world_tick 139 例/2,504 断言 · 反向验证 2 处精确转红）+ **A-β d1 尾摇重排**（§9.0.68：B1 GBREAK 测试原样转绿 · rules_battle 135 例/2,930 断言 · 反向验证 2 处） | ✅ A-β d2 = Guardian + MultiList + CheckSameSide（诊断规格已备）；~~推送窗口待执行（gitee 凭据侧阻塞）~~ **该窗口已于 09-16 07:39 闭合**（`shared-v0.31.0` 双远端 + 客户端 pin `6f68f4b`，补记 §9.0.69b） |
| 09-17 | **批次 A-β d2**（§9.0.69：目标判定族 `targetCheck/TargetCheckDead/CountAlive/DefaultAttacker/CheckSameSide/TargetAdjust` + 忠犬守护全链 —— 指令接收时建链 · 打击接管 · `==0⇒1` 下限 · 七道否决 · 每回合清空；`rules_battle` 147 例/3,035 断言 · `world_tick` 142 例/2,542 断言 · 反向验证 5 处，其中 RV-5 为**负结果**并查明原因） | ✅ A-β d3 = MultiList 多目标展开（**本轮复核后判定为「有意划外」**，理由见 §9.0.69 ④①：唯一消费者——魔法/精灵术 S19——为 `⬜`，现在移植只产生死代码） |
| 09-18 | **推送窗口 `shared-v0.32.0`**（§9.0.69c：服务端 `6c8601b` + tag 双远端 · 客户端 pin `3cea798` 双远端 · 干净克隆发布态复验 8/8 · 服务端 22/22）＋ **`docs_index` 守卫补两处盲区**（字母后缀编号两边同时瞎 ⇒ 判据③恒绿；per-file「节数」列纯手写无断言 ⇒ 实测已烂到 10 vs 15，反向验证 4 处精确转红） | A-γ1 核心状态序列（混乱目标重定向） |
| 09-24 | **批次 A-γ1**（§9.0.70：核心状态序列混乱重定向 `rollConfusionRedirect` 落地，80% 触发普攻重定向至全场存活目标；`rules_battle` 147→149 例/3,035→3,048 断言，RV-1 反向验证通过）＋ **CI 工具 macOS SDK 自动适配** | 阶段 2: 世界运转基石 NPC 最小切片 (W.6 WARP / W.7 Healer) |
| 09-25 | **阶段 2 最小切片 W.6 + W.7**（§9.0.71/§9.0.72：WARP 传送点踩踏瞬移与视野刷新 · NPC 实体框架与不可穿透碰撞 · Healer 满血满蓝与 `GoldLedger` 扣费 · `world_map` 30→38 例/368→466 断言 · RV-1/RV-2 精准通过） | 推送窗口准备（`shared-v0.33.0`） |
| 09-25 | **推送窗口 `shared-v0.33.0`**（§9.0.72b：服务端 `7e8ef23` + tag · 客户端 pin `6daed5a` · 消除 D2 漂移 · 双端 CI 全过） | 阶段 2 对话与窗口骨架 W.8 |
| 09-25 | **阶段 2 对话与窗口骨架 W.8**（§9.0.73：TownPeople 面对交互与多文案随机选择 · WindowOpen/WindowReply 窗口会话闭环 · `world_map` 38→42 例/466→639 断言 · RV-1/RV-2 精准通过） | 推进阶段 2 下一步：任务旗标与 ExChangeMan 脚本解析骨架（NOWEV/ENDEV/EventEnd） |
| 09-25 | **阶段 2 任务地基 W.9**（§9.0.74：任务旗标位图 NOWEV/ENDEV 256 位空间与安全边界 · ExChangeMan 脚本 EventEnd 解析 · MESSAGE 与 ACCEPT 交互与副作用闭环 · `world_map` 42→47 例/639→1336 断言 · RV-1/RV-2/RV-3 精准通过） | 推送窗口准备（`shared-v0.34.0`，消除客户端 D2 漂移） |
| 09-25 | **推送窗口 `shared-v0.34.0`**（§9.0.74b：服务端 `7dbd0c3` + tag · 客户端 pin `124cf62` · 消除 D2 漂移 · 双端 CI 全过） | 推进阶段 2 下一步：任务交付与奖励结算 W.10 |
| 09-25 | **阶段 2 任务交付与结算 W.10**（§9.0.75：ExChangeMan 道具/宠物交付与奖励结算 · GoldLedger 闭环 · Pet 扩展 pet_id · `world_map` 47→52 例/1336→1432 断言 · RV-1/RV-2 精准通过） | 推送窗口准备（`shared-v0.35.0`） |
| 09-25 | **推送窗口 `shared-v0.35.0`**（§9.0.75b：服务端 `9aa23f8` + tag · 客户端 pin `a4f6b7a` · 消除 D2 漂移 · 双端 CI 全过） | 阶段 2 NPC 巡逻漫游 W.11 |
| 09-25 | **阶段 2 NPC 漫游巡逻 W.11**（§9.0.76：8 方向寻路 · 碰撞退回 · 对话打断 · 增量视野广播 · `world_map` 52→57 例/1432→1535 断言 · RV-1/RV-2 精准通过） | 阶段 2 道具商店与买卖交易 W.12 |
| 09-25 | **阶段 2 NPC 商店系统 W.12**（§9.0.77：ShopMan/Buy/Sell · WindowOpen 商店界面 · GoldLedger 买卖扣加金 · `world_map` 57→62 例/1535→1638 断言 · RV-1/RV-2 精准通过） | 阶段 2 宠物商店与技能导师 W.13 |
| 09-25 | **阶段 2 宠物商店与技能导师 W.13**（§9.0.78：PetShop 买卖宠物 · PetSkillShop 学宠技七槽与扣费 · `world_map` 62→67 例/1638→1741 断言 · RV-1/RV-2 精准通过） | 阶段 2 告示牌与传送员 W.14 |
| 09-25 | **阶段 2 告示牌与传送员 W.14**（§9.0.79：SignBoard 告示交互 · WarpMan 传送与扣费 · `world_map` 67→72 例/1741→1843 断言 · RV-1/RV-2 精准通过） | 阶段 2 真实大地图与村庄 NPC 数据管线 D.1 |
| 09-25 | **阶段 2 原生 LS2MAP 解析与萨伊那斯 D.1**（§9.0.80：原生 LS2MAP 二进制解析 · 萨伊那斯 100 地图 · 59 Warp + 48 NPC 装载 · 跨图通行解耦 · `world_map` 72→74 例/1843→1896 断言 · RV-1/RV-2 精准通过） | 阶段 2 四大村庄多地图与 NPC 全量接入 D.2 |
| 09-25 | **阶段 2 四大村庄多地图与全量 NPC 管线 D.2**（§9.0.81：四大村庄 1000/2000/3000/4000 地图构建与装载 · 跨图视野完全隔离与双向传送闭环 · 164 Warp + 254 NPC 属性抽取 · `world_map` 74→75 例/1896→1952 断言 · RV-1/RV-2 精准通过） | 阶段 2 角色与宠物成长闭环（经验升级体系与装备穿戴加成） |
| 09-25 | **批次 P.1 成长与装备体系**（§9.0.82：经验门限与玩家/宠物升级结算 · 装备槽位穿戴与战斗三围加成 · 锁定 ref `shared-v0.36.0` 闭环 · 双端 CI 全过） | 阶段 2 属性点分配系统 P.2 |
| 09-25 | **批次 P.2 属性点分配系统**（§9.0.83：SkillUp 消费与四维加点换算 · 非战斗非阵亡门禁 · 战斗三围即时重算 · 锁定 ref `shared-v0.37.0` 闭环 · 双端 CI 全过） | 战斗子批 A-γ2 职业宿主与 64 职技映射 |
| 09-25 | **战斗子批 A-γ1 核心状态推进与职业被动联动**（§9.0.85：挑拨/附身重定向与取消防御 · 火附体每回合结算 · 9大状态逆境回复生命 · 职业被动在场生效：回避/专精/格挡/熟练度 · 锁定 ref `shared-v0.39.0` 闭环 · 双端 CI 全过） | 战斗子批 A-δ 宠技战斗侧与特殊指令 / A-ε 攻击魔法与杂项 |
| 09-26 | **战斗子批 A-ε 攻击魔法与杂项**（§9.0.87：火杀物理/魔法 · 拐骗 · 偷金 · 合击 · 分摊 · 求援 · 忠诚判定 · 复活 · GBreak 宿主对齐 · 锁定 ref `shared-v0.41.0` 闭环 · 双端 CI 全过） | 阶段 2 真实世界地图深化 D.3 与客户端场景交互补齐 |
| 09-26 | **批次 D.3 世界地图全景深化与客户端交互演进**（§9.0.88：加鲁卡主岛 200 · 四大附属村 3100/3200/3300/3400 · 琉璃地下城 20801..20807 · 碧青 21201/21215 全量 18 图 · 311 Warp + 399 NPC · 客户端 WindowDispatcher 弹窗/选项/商店交互闭环 · BattlePresenter 宠技表现补齐 · 锁定 ref `shared-v0.42.0` 闭环 · 双端 CI 全过） | 阶段 2 队伍系统与组队协同 (Party System) |
| 09-26 | **阶段 2 队伍系统与组队协同 (Party System)**（§9.0.89：5人队伍上限 · 队长/队员数据结构与生命周期绑定 · 贪吃蛇足迹跟随算法 · 队员自主移动与遇敌门禁 · 队长传送同步拉取 · 组队暗雷/明雷全员拉入战斗 · 5..9宠物站位与经验协同分配 · `world_map` 78→82 例/2094→2236 断言 · RV-1/RV-2 精准通过） | 阶段 2 玩家间交易系统 (Trade System) / 决斗切磋 (Duel System) |
| 09-26 | **阶段 2 安全交易与决斗切磋 (Trade & Duel)**（§9.0.90：距离/存活/同队/双队长发起门禁 · 双向锁定确认防诈状态机 · 背包/宠物/石币三层容量防刷预检 · 原子置换与出战宠重置 · PVP结算无收益与战败HP钳位保护 · `world_map` 82→89 例/2236→2452 断言 · RV-1/RV-2 精准通过） | 阶段 2 核心社交与通信系统 (AddressBook / Mail / Chat) |
| 09-26 | **阶段 2 核心社交与通信系统 (AddressBook / Mail / Chat)**（§9.0.91：80张名片上限 · 上下线感知 · 20封邮箱上限 · 附件资产流转与提取三层预检 · 四大分级广播与黑名单拦截 · `world_map` 89→96 例/2452→2960 断言 · RV-1/RV-2 精准通过） | 阶段 2 家族系统 (Family System) |
| 09-26 | **阶段 2 家族管理与庄园系统 (Family System)**（§9.0.92：30级门限 · 1万扣币 · 50人满员门禁 · 1亿金库上限 · 随身容量预检 · 4大庄园据点占领与争夺 · `world_map` 96→103 例/2960→3147 断言 · RV-1/RV-2 精准通过） | 阶段 2 摆摊与拍卖交易市场 (Street Stall / Market System) |
| 09-26 | **阶段 2 玩家摆摊与拍卖市场系统 (Street Stall & Consignment Market)**（§9.0.93：原地开摊与货架配置 · 摆摊移动/组队/交易拦截门禁 · 标价购买与距离<=3门限 · 寄售上架与100挂牌费 · 5%交易税与资产解耦快照 · 卖家随身溢出与离线系统邮件到账保全 · `world_map` 103→110 例/3147→3350 断言 · RV-1/RV-2 精准通过） | 阶段 2 骑乘系统 (Ride System) |
| 09-26 | **阶段 2 骑乘系统 (Ride System)**（§9.0.94：骑乘学习证资质与四大庄园特权门限 · [RV-1] 濒死拦截 · 复合外观算力 `100700 + (base_p%500) + (pet_image%100)` 与原形精准恢复 · 出战宠与骑宠互斥 · 大世界资产流转安全脱钩（摆摊/寄售/交易/邮件/离线） · 进战投影与战中人宠生命分摊 · [RV-2] 战后血量回写与濒死落马下马还原 · 消除 World.cpp 酒醉敏捷欠债 · `world_map` 110→117 例/3350→3512 断言 · RV-1/RV-2 精准通过） | 阶段 2 宠物融合转生 (Pet Fusion) 或 阶段 2 选角流程与多角色槽位 |
| 09-26 | **阶段 2 选角流程与多角色槽位系统 (Multi-character Slots & Creation Flow)**（§9.0.95：2 槽位查询与选角 · [RV-1] 槽位超限防御阻断 ACCOUNT_CONFLICT · 12 种原型与 48 种配色外观合法性校验与头像映射 · [RV-2] 初始四维 20 点与地水火风 10 点分配校验 · 四大新手村出生地分配与兜底 · 在线中防重选/防重创 · 跨图多楼层恢复登入 · `world_persistence` 8→15 例/158→377 断言 · RV-1/RV-2 精准通过） | 阶段 2 宠物转生与融合系统 (Pet Fusion) 或 阶段 2 任务引擎脚本全景扩展 |
| 09-26 | **阶段 2 宠物融合与转生系统 (Pet Fusion & Rebirth)**（§9.0.96：三表投影目标宠物 PetTable/PropertyTable/FusionTable · 资质继承与等级惩罚 · 技能遗传与非法技能过滤 · [RV-1] 融合资格与转生等级门禁/次数上限/状态互斥 · [RV-2] 转生五次方与Fx档位算力精确验证 · 融合师与转生师NPC交互闭环 · `world_map` 117→125 例/3512→3650 断言 · RV-1/RV-2 精准通过） | 阶段 2 任务引擎脚本全景扩展 (ExChangeMan Script Expansion) |
| 09-26 | **阶段 2 任务引擎脚本全景扩展 (ExChangeMan Script Expansion)**（§9.0.97：递归下降复合条件解析器 ExprParser · 变量表全集覆盖 LV/TRANS/FAME/FM/PROF/GOLD/HP/MP/SP/reITEM/rePET/ITEM/PET/NOWEV/ENDEV · 全动作集执行闭环 AddExp/AddSkillPoint/Heal/AddFame/DelFame/NpcWarp/SetNowEvent/ClearNowEvent/ClearEndEvent/CleanFlg · 多步对话树推进 NextBlock 与委托/清除状态机 kRequest/kClean · [RV-1] 复合条件门禁防御拦截 · [RV-2] 动作原子执行与资产事务一致性 · `world_map` 125→130 例/3650→3807 断言 · RV-1/RV-2 精准通过） | 阶段 2 称号系统与声望商城体系 (Title & Fame Shop) |
| 09-26 | **阶段 2 称号系统与声望商城体系 (Title & Fame Shop)**（§9.0.98：称号元数据注册 TitleDefinition 与四维加成 TitleStatsBonus · 称号授予 grantTitle · 移除 revokeTitle · 拥有判定 hasTitle · 佩戴 equipTitle 与卸下 unequipTitle · 30 称号位上限 · 声望商城 NPC kFameShop 交互与商品弹窗 · 称号/道具/宠物原子兑换闭环 buyFromFameShop · [RV-1] 称号佩戴门限与防重购/超限防御拦截 · [RV-2] 声望商城兑换原子事务一致性（声望/背包/宠物栏严格 0 损耗拦截） · `world_map` 130→135 例/3807→4013 断言 · RV-1/RV-2 精准通过） | 阶段 2 道具制造与生活技能（料理/合成系统与素材加工） |
| 09-27 | **阶段 2 道具制造与生活技能 (Cooking & Crafting / Synthesis)**（§9.0.99：料理烹饪与合成精炼配方注册 CraftingRecipe · 制造类别 CraftingType · 堆叠材料原子扣减与空槽保全 · 存活宠物协助特化加成 · 工匠导师 NPC kCraftsman 交互与引导 · [RV-1] 状态互斥/食材混杂/槽位作弊防御拦截 · [RV-2] 制造资产事务一致性反向变异验证（材料不足/手续费不足/背包满严格 0 扣减） · `world_map` 135→140 例/4013→4105 断言 · RV-1/RV-2 精准通过） | 阶段 2 宠物进阶成长与骑乘认证体系 |
| 09-27 | **阶段 2 宠物进阶成长与骑乘认证体系 (Pet Advanced Growth & Ride Certification)**（§9.0.100：原版 `npc_riderman.c` 对齐 · 庄园骑乘认证 `RideCertType`（基础/萨姆吉尔暴龙/玛丽娜斯绿暴/加加飞龙/卡鲁它那雷龙/宗师全能） · 认证考核流程 `takeRideExam` · 学费 20% 原子注资庄园家族金库（原版 `w.takegold / 5` 对齐） · 骑乘门禁扩展与宗师全系特权 · 契合度相性与攻防敏属性共鸣推导 `calculateRideAffinity` · 骑乘考官 NPC `kRideMaster = 13` · [RV-1] 庄园专属骑宠认证门禁与未授权拦截反向变异实证 · [RV-2] 认证考核原子事务与庄园金库 20% 分成一致性反向变异实证 · `world_map` 140→145 例/4105→4201 断言 · RV-1/RV-2 精准通过） | 阶段 2 宠物进阶技能与忠诚度交互体系 |
| 09-27 | **阶段 2 宠物进阶技能与忠诚度交互体系 (Pet Loyalty & Advanced Skills)**（§9.0.101：宠物喂食交互全流程 `feedPet` · 食物/料理类别门禁 `item.type == 20` · 等级压制与忠诚度上限封顶计算（原版 `char.c` 规则） · 堆叠食材原子消耗与生命/忠诚度提升 · 技能遗忘与技能栏释放 `forgetPetSkill` · 顺服与失控状态判定 `checkPetObedience`（60/20 顺服度阶梯） · 战后胜负与出战/骑乘生死战损结算（阵亡/落马 -5，获胜存活 +1） · [RV-1] 食物门禁与等级压制封顶拦截反向变异实证 · [RV-2] 摆摊互斥与资产原子扣减一致性反向变异实证 · `world_map` 145→150 例/4201→4276 断言 · RV-1/RV-2 精准通过） | 阶段 2.5 持久化 DDL 补齐与大世界覆盖收拢 |
| 10-02 | **阶段 2.5 存储层 DDL 补齐与覆盖台账收拢**（§9.0.102：输出 `deploy/sql/002-world-features.sql` 覆盖名片/称号/家族/邮件/寄售市场 DDL · 刷新 `13-d8-coverage.md` 94 行主表与覆盖率（核心严格口径 15.8%→43.7%，宽口径 71.4%→92.6%） · 全量 CTest 22/22 保持全绿） | 阶段 2 庄园家族战体系与骑乘战备闭环 |
| 10-02 | **阶段 2 庄园家族战体系与骑乘战备闭环 (Manor War & Duel Scheduling)**（§9.0.102：四大庄园据点争夺与决斗调度 · 族长门禁与10万押金原子扣除 · 无主进驻与约战排期状态机 · 决斗比分累加与交战推进 · 庄园归属过户与押金100%注资守方金库 · [RV-1] 族长门禁与已有庄园互斥拦截 · [RV-2] 资金不足零扣减与结算交割100%注资一致性 · `world_map` 150→155 例/4276→4373 断言 · RV-1/RV-2 精准通过） | 阶段 2.6 称号与名片持久化闭环贯通及 World.cpp 单体解耦重构 |
| 10-03 | **阶段 2.7 遇敌与战斗子系统解耦重构 (Encounter & Battle Subsystem Decomposition)**（§9.0.104：大世界暗雷/明雷遇敌链提取至 `WorldEncounter.cpp` · 战斗生命周期/指令/事件推进提取至 `WorldBattle.cpp` · `World.cpp` 降至 4,994 行（累计削减 65%） · 严格保持 `include/world/Api.h` 单一公共头不变量 · 全量 CTest 22/22 保持 100% 绿灯） | 阶段 2.8 大世界移动与传送子系统深度解耦 |
| 10-03 | **阶段 2.8 大世界移动、多楼层与传送子系统解耦 (Movement & Warp Decomposition)**（§9.0.105：移动推进 `advanceMovement` · 步进碰撞与墙角阻挡 `walkStep` · WarpPoint瞬移与队伍同步跟随 `warpPlayer` · 多楼层地图管理与传送点维护提取至 `WorldMovement.cpp` · `World.cpp` 降至 4,381 行（累计削减 69.3%） · 全量 CTest 22/22 保持 100% 绿灯） | 阶段 2.9 NPC对话/剧情/商店/技能解耦 |
| 10-03 | **阶段 2.9 NPC对话事件、任务剧情、商店买卖与技能导师子系统解耦 (NPC Dialog & Quest Decomposition)**（§9.0.106：NPC 对话事件派发 `onEvent` · 窗口交互应答 `onWindowReply` · ExChange 前置求值与副作用执行 · NPC 商店与宠物商店买卖 · 宠物技能导师传授提取至 `WorldNpcDialog.cpp` · `World.cpp` 锐降至 2,300 行（累计削减 83.9%） · 严格遵循模块边界与 `GoldLedger` 经济审计守卫 · 全量 CTest 22/22 保持 100% 绿灯） | 阶段 2.10 服务端战斗观战系统落地与广播链路全覆盖 |
| 10-03 | **阶段 2.10 服务端战斗观战系统落地与广播链路全覆盖 (Battle Spectating System)**（§9.0.107：观战席位与状态抽象 `spectators` · 进场下发三件套 `BattleSelfInfo(slot 20)` + `BattleSnapshot` + `BattleTurnBegin` · 战况多播流广播 `flush` · 切比雪夫距离 $\le 5$ 面向观战 · `ESCAPE` 随时撤出脱离 · `world_tick` 3 个全覆盖用例 · 全量 CTest 22/22 保持 100% 绿灯） | 阶段 2.11 战斗救援与乱入系统落地 |
| 10-03 | **阶段 2.11 战斗救援与乱入系统落地 (Battle Rescue & Join-in-Progress System)**（§9.0.108：入场严格门禁（存活/未参战/未摆摊/未组队/同层距离 $\le 5$/PVE限定/Side 0 容量） · 动态槽位分配与装备/骑乘/出战宠带入 · `pushBattleSnapshot` 全场快照广播 · 大世界协议级事件流 `onEvent(event_type=3)` 接入 · `world_tick` 用例与全量 CTest 22/22 保持 100% 绿灯） | 阶段 3.1 大世界全量资产批处理管线与地图/NPC/传送门编目落地 |
| 10-03 | **阶段 3.1 大世界全量资产批处理管线与地图/NPC/传送门编目落地 (Batch Content Pipeline & World Catalogs)**（§9.0.109：`tools/import_all_world_content.py` 批处理工具链 · 1,185 张有效 LS2MAP 地图解析 · 9,011 条双轨传送网络去重合并 · 3,450 个具名功能 NPC 全量结构化分类（ExChangeMan 861/Enemy 533/TownPeople 531/Shop 360/WarpMan 334/SignBoard 290/Healer 47） · `test_content_catalog.py` 自动化测试 · 全量 CTest 22/22 保持 100% 绿灯） | 阶段 3.2 战中掉线断网保护与重连接管系统落地 |
| 10-03 | **阶段 3.2 战中掉线断网保护与重连接管系统 (Battle Disconnect Grace & Re-attach System)**（§9.0.110：`disconnectBattleMember` 参战槽位保全 · `advanceBattles` 离线自动防御托管 · 新会话重登接管 `reattachBattle` · 战场快照与回合就绪状态恢复 · `battleOfSession` / `battleOfPlayer` 定位 · `WorldTickTest` 单元测试双向验证 · 全量 CTest 22/22 保持 100% 绿灯） | 阶段 3.3 四大新手村全域场景与社交组队合击网络 |
| 10-03 | **阶段 3.3 四大新手村全域场景室内外贯通与组队合击协同机制 (Four Villages World & Party Combo Synergy)**（§9.0.111：LS2MAP 原生头部偏移 6 floor ID 解析修复 · 73 张地图（含四大村庄 1000/2000/3000/4000 及室内医院/道具店/村长家等）全量阻挡图装载 · 815 处 Warp 传送网络与 818 个功能 NPC 接入 · 依据官方源码 `battle.c` 还原组队近战合击 Combo 机制 · 伤害累加与 `combo_acted` 协同去重 · `WorldMapTest` 与 `WorldTickTest` 全绿 · 全量 CTest 22/22 保持 100% 绿灯） | 阶段 4.0 客户端原生石刻 UI 资产换装与全域大世界端到端可玩闭环 |
| 10-03 | **阶段 4.0 客户端原生石刻 UI 质感换装、四大新手村全域交互与双端闭环验证 (Client Stone Aesthetic UI & Four Villages E2E Verification)**（§9.0.112：客户端双层石刻框线、深褐沉降槽与古朴石板底色 · `StoneButton` 按下凹陷视差反馈 · 主界面经典石刻生命/气力槽与状态徽标 · 功能快捷坞与战斗轮盘排版优化 · 四大新手村 1000/2000/3000/4000 医院 Healer 满血满蓝、道具店 Shop 货架结账与传送员 WarpMan 跨村传送双端端到端跑通 · 双端 CTest 全绿） | 阶段 4.1 四大庄园守护战决胜排期与骑乘特权双端端到端可玩演练 |
| 10-03 | **阶段 4.1 四大庄园守护战决胜排期、胜负过户与全系骑乘特权及野外暗雷战果掉落双端闭环验证 (Manor War, Ride System & Wild Loot E2E)**（§9.0.113：四大庄园约战排期与比分决胜交割 · 挑战方过户与押金注资 · 庄园专属免考骑乘特权 · 骑乘考官20%学费注资庄园金库 · 骑乘外观解构与契合度相性共鸣 · 战中人宠生命分摊与濒死落马下马 · 野外暗雷遇敌概率累加 · 残血捕获野生宠入槽 · 战后战利品/魔石原子交付入包 · 双端CTest 100%全绿） | 阶段 4.2 玩家多线摆摊与拍卖行行商网络及大世界经济生态闭环 |
| 10-03 | **阶段 4.2 玩家多线摆摊行商网络、拍卖行寄售全景流通与系统邮箱到账双端闭环验证 (Multi-Stall, Auction Market & Mail E2E)**（§9.0.114：集市多摊位并发与货架配置 · 移动/组队/决斗/交易全方位状态机封锁 · 近身切比雪夫距离采购 · 随身100万石币上限溢出防爆仓与邮件保全 · 跨图拍卖行寄售与检索过滤 · 5%交易税清算 · 离线卖家净收益系统邮件到账 · 邮箱附件提取防刷状态机 · 双端测试全绿） | 阶段 4.3 全域剧情任务引擎脚本全景演进与四大新手村经典成人礼 / 生活技能制造生态闭环 |
| 10-03 | **阶段 4.3 全域剧情任务引擎脚本全景演进与四大新手村经典成人礼 / 生活技能制造生态闭环 (Adult Ceremony, Quests & Lifestyle Crafting E2E)**（§9.0.115：审判官考验接取 · 差使领玉 · 15空槽容量门前置防御 · 正向领取15仪玉 · 交付换取成人礼首饰 · NOWEV/ENDEV旗标原子交割与永久成人见证 · 加鲁卡救父草药支线闭环 · 料理烹饪与装备精炼合成全景闭环 · 状态机互斥与严格0扣减防御 · 产物入包与声望累加 · 双端测试全绿） | 阶段 4.4 宠物转生与融合进阶生态、声望商城与称号加成大世界双端全流程贯通验证 |
| 10-03 | **阶段 4.4 宠物转生与融合进阶生态、声望商城与称号加成大世界双端全流程贯通验证 (Pet Fusion/Rebirth, Titles & Fame Shop E2E)**（§9.0.116：三宠融合全流程 · 继承主副宠技能与资质 · 摆摊/出战/同槽多重状态互斥 · 100级宠物转生与辅助宠原子销毁 · 五次方成长资质重算与转生次数累加 · 声望商城兑换称号/道具/宠物 · 声望不足/已拥有/满包/满宠严格0扣减防御 · 称号佩戴声望门限与四维攻防敏加成即时生效与卸下收口 · 双端测试全绿 · RV反向变异实证通过） | 阶段 4.5 远端同步、推送窗口闭合与阶段 4 收官审计 |
| 10-03 | **阶段 4.5 远端同步、推送窗口闭合与阶段 4 收官审计 (Remote Synchronization & Push Window Closure)**（§9.0.117：服务端 master `6a4d54a` 与客户端 master `69ab3a4` 双端全量 43+30 commits 远端同步完成 · `shared-v0.36.0`..`v0.42.0` 全量 7 个 release tag 同步推送至 GitHub · 远端 HEAD 零积压零分叉 · watched 路径纯洁性确认无漂移 · 双端全量 CTest 22/22 与 7/7 保持全绿 · 全仓守卫 100% 绿灯） | 阶段 5.0 S19 攻击魔法与精灵术全管线（MultiList 多目标展开与精灵术规则引擎） |
| 10-03 | **阶段 5.0 S19 攻击魔法与精灵术全管线 (MultiList Target & Magic Rules Engine)**（§9.0.118：多目标展开 `expandMultiTarget` 纯函数 1:1 复刻原版 `BATTLE_MultiList`（单体/整侧/全体/前后排灭绝回退/贯穿穿透） · `checkSameSide` 升级接入多目标判断 · 四系攻击魔法通用计算 `computeMagicDamage`（地水火风相克倍率、属性加权、精神力抗性压制） · 魔法闪避判定 `rollMagicDodge` · 恩惠恢复/净化/反转精灵术 · `rules_battle` 174 组/3,338 断言全部绿灯 · RV-1 反向变异验证通过） | 阶段 5.1 战中精灵术道具使用与大世界魔法熟练度成长管线贯通 |
| 10-03 | **阶段 5.1 战中精灵术道具使用与大世界魔法熟练度成长管线贯通 (Spirit Magic Items & World Magic Proficiency)**（§9.0.119：战中 SPELL 完整管线（MP 充足性门禁、消耗扣减、expandMultiTarget 多目标展开、四系攻击魔法计算与闪避判定、恩惠恢复、状态净化、四系属性反转） · USE_ITEM 扩展气力恢复药与道具附带精灵术（净化草/反转等） · 大世界玩家四系熟练度模型 `magic_exp[4]` / `magic_level[4]` 与施法升级管线 · 规则用例 174→177 组/3,338→3,367 断言 · RV-1 反向变异实证 · 双端 CTest 22/22 与 7/7 100% 绿灯） | 阶段 5.2 魔法与精灵术表现层对接 |
| 10-03 | **阶段 5.2 客户端魔法与精灵术事件流表现层解析与展示 (Client Magic Event Presentation & D2 Verification)**（§9.0.120：`BattlePresenter` 增强 `AttackKind` 攻击类型解析，对 ATTACK_KIND_SPELL 呈现精灵术/魔法施法标识 · `ClientNetTest` 增补魔法施法 HIT、恩惠恢复 SET_HP、净化 STATUS_CHANGE、属性反转 REVERSE 事件表现测试 · 客户端 7/7 CTest 100% 绿灯 · 双端 master 与 tags 远端同步闭环） | 阶段 6.0 核心子系统 S11 精灵/天使系统架构设计与落地规划 |
| 10-03 | **阶段 6.0 S11 精灵/天使系统架构落地与双向瞬移契约管线 (Angel & Spirit System Pipeline)**（§9.0.121：L3 纯函数规则引擎 `Angel.h/cpp` · 使命注册与双向契约状态字典 · 2884 使者信物 / 2885 勇者信物 · 使者与勇者双向瞬移拉取 · 使者信物 100% 抑制大世界暗雷（1:1 复刻 `CHAR_WORKANGELMODE`） · 战中神佑守护 10% 减伤 / 15% 防御增益 · 使者与勇者双向独立领奖交付闭环 · `rules_battle` 178 例/3,408 断言 · `world_map` 153 例/2,731 断言 · RV-Angel-1/2 双变异实证验证通过 · 锁定 ref `shared-v0.45.0` 闭环） | 阶段 6.1 客户端天使契约与神佑状态表现层对接 |
| 10-03 | **阶段 6.1 客户端天使契约与神佑状态表现层对接 (Client Angel System & Spirit Blessing Presentation)**（§9.0.122：`AngelContractView` / `AngelRoleKind` 契约数据模型与状态机 · 2884 使者信物 / 2885 勇者信物角色与阶段门禁校验 · 使者模式大世界暗雷抑制感知（`isEncounterSuppressed`） · 战中神佑守护状态（`unitSpiritBlessing`）与 `DAMAGE_FLAG_GUARDIAN` 伤害事件流结构化文本解析 · `ClientNetTest` 端到端全覆盖 · RV-Angel-Client-1 反向变异验证通过 · 客户端 7/7 CTest 全绿） | 阶段 7 核心子系统 S22 saac 通信与账号服务 |





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
| **A-γ1 状态序列收口** | ✅ 已于 §9.0.70 & §9.0.85 收口完成：混乱/挑拨/附身重定向全链 · 火附体每回合结算 · 9大异常状态逆境回复纯函数与结算 · 四职被动纯函数与管线接入（回避/专精/格挡/熟练度） | 真状态 58 · 串 48 · rng 8 | L4.1 地基在；状态 12..43 的**施加者**仍属宠技/职技域（本批只落推进与解除面） | `rules_battle` 157 用例/3,166 断言全绿；RV-1/RV-2 双向反向验证通过 |
| **A-γ2 职业宿主** | `battle_profession_attack_fun`（490）· `battle_profession_status_chang_fun`（778）· `battle_profession_assist_fun`（306）· `PROFESSION_BATTLE_StatusAttackCheck`（40）· `attack_magic_fun`（29） | 真状态 ~74 · 串 ~76 · rng ~4——C 批次 64 函数的运行时宿主（净移植量 = 2 函数 + 1 张表的判据在此复核） | 职技效果表 fixture（手工构造，D 线不写运行时加载器） | C 表接线用例（53 同构薄包装走映射表）；`PROFESSION_escape/track` 两真实现逐行对照 |
| **A-δ 宠技战斗侧 + 特殊指令** | ✅ 已于 §9.0.86 收口完成：`BATTLE_S_Barrier/Nocast/Roar/Weaken/Deeppoison/Refresh/Sacrifice/Explode/FallGround` · `PETSKILL_SetMagicPet/SetDuck` · `AttReverse/EarthRoundHide` · 锁定 ref `shared-v0.40.0` 闭环 | 真状态 ~45 · 串 ~30 · rng ~8 | 纯函数计算 + 战斗宿主指令执行 | `rules_battle` 164 用例/3,226 断言全绿；RV-1/RV-2 双向反向验证通过 |
| **A-ε 攻击魔法与杂项** | ✅ 已于 §9.0.87 收口完成：`Attack_FIREKILL` / `MultiAttMagic_Fire`（火杀物理与魔法）· `Abduct`（拐骗）· `StealMoney`（偷金）· `Combo`（合击）· `DivideAttack`（分摊）· `E_ENEMYHELP`（求援）· `PetLoyalCheck`（忠诚）· `MultiRessurect`（复活）· `GBreak/GBreak2` 宿主对齐 · 锁定 ref `shared-v0.41.0` 闭环 | 真状态 ~40 · 串 ~70 · rng ~20 | 纯函数计算 + 战斗宿主指令执行 | `rules_battle` 170 用例/3,275 断言全绿；RV-1/RV-2 双向反向验证通过 |

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

