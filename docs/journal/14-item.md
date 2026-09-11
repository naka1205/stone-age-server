# journal / 14-item — 道具域四批

> **本文件收录**:I.1 背包 L2 地基、I.2 捕获扣道具、I.3 野怪掉落、I.4 使用道具。⇒ 扣/掉/用三链全通。
>
> **批次编号**:§9.0.48 · §9.0.50 · §9.0.51 · §9.0.53(共 4 节,§9.0.48 – §9.0.53 区间内)
>
> ★ **编号沿用 `00-architecture.md` 原 §9.0.x 体系,搬家未改号** —— 全仓约 600 处 `§9.0.x` 引用因此继续有效。总映射见 [`README.md`](README.md)。
>
> ★ 本目录是**开发流程账**(逐批次的取证 / 交付 / 复验 / 教训),原文逐字迁入、未改写。
> 架构裁定看 [`../00-architecture.md`](../00-architecture.md);决策看 [`../11-decision-register.md`](../11-decision-register.md);
> 还欠什么看 [`../backlog/`](../backlog/);和原版哪里不一样看 [`../deviations/`](../deviations/)。

---

### 9.0.48 ★★ 批次 I.1 —— 背包 L2 地基:Item 族 + 道具池 + Player 背包槽(道具域第一批,2026-09-10)

**为什么是它**:道具域(使用道具 + 掉落 + 捕获扣道具,2026-09-10 用户拍板)三条链有一个**共同前置** —— 背包 L2 实体从未建。`Player.h` 文末 ④ 早登记「背包是捕获第 5 步(`BATTLE_CaptureItemDelAll`,DR-BT10「全删」)的落脚点」;`world/Api.h:513` 记遇敌两道道具门因「道具系统未移植」调用方传空背包(连带 2.58% 区域空背包下不遇敌)。⇒ 先建地基,后三批才有落脚点。

**交付**:`shared/model/Item.h`(Item 族 POD + `ItemHandle` + `ItemNameStr`)· `Player.h` 加 `items[kMaxItemHave]` + `findFreeItemSlot`/`clearItemSlot` + 背包常量 · `World` 挂 `ItemPool = EntityPool<Item,10000>` + 观察面 `itemCount`/`playerItemSlotsUsed`。**本批只建实体 + 池 + 观察面,不接扣/掉/用任一链路**。

**取证(三仓交叉核,纪律 ①)**:
- ★ 背包容量:展开视图 `char_base.h:314` 定 `CHAR_MAXITEMHAVE = CHAR_STARTITEMARRAY + CHAR_MAXITEMNUM*3` = 装备位 9 + 15×3 = **54**,8.0 走 `*3` 支(非宏关闭时代的 `*1`);装备位 `CHAR_EQUIPPLACENUM=9`(`CHAR_HEAD`..`CHAR_EQGLOVE`,含 `_ITEM_EQUITSPACE`/`_EQUIT_NEWGLOVE` 展开的 4 项)。⇒ **照原版连续布局**建整数组(装备位 + 背包),`kStartItemArray=9` 为背包起点(捕获扣道具循环 `for(i=CHAR_STARTITEMARRAY;…)` 依赖它)。
- ★ 道具池容量 `kMaxItems=10000`(`csa8.0/setup.cf:318` `itemnum=10000`,`ITEM_item[itemnum]` 维度 + 运行期硬边界 `ITEM_CHECKINDEX`,`15` §2 C5;不可配置化)。
- ★★ **Item 不进 `EntityKind` 五族**(用户拍板):五族判别键是 `CHAR_TYPE`(全是 Char 实体),而背包道具是原版**独立全局池** `ITEM_item[]` 成员、非 Char ⇒ 独立池 + `ItemHandle`(与 `EntityHandle` 同机制的语义别名)。
- ★★ **Item.h 最该被读的一段(functable/Lua 全段不复刻)**:`ITEM_Item` 的 `data[]`/`string[]` 后段是 `ITEM_INITFUNC`..`ITEM_LASTFUNCTION` 回调名字符串(`ITEM_DATACHARNUM == ITEM_LASTFUNCTION`),8.0 无 Lua ⇒ 不建。⚠️★ **顺带抓到 unifdef_80 第二次宏误判**:它把 `_ALLBLUES_LUA_1_8` 判为**开**、展开视图 `battle_event.c:3538` 残留 `CaptureOkFunction`;而 **StoneAge 全树无 `#define`(关)且该符号在 StoneAge `battle_event.c` 一次不出现** ⇒ 合 `04 §3.3.3`「8.0 无 Lua」,以 StoneAge 为准。★ 继 W.3 `_CHAR_LOOP_TIME` 之后,evidence-workflow「展开视图会把编译期宏误判当运行时基线」第二强样本 —— **争议处必回 StoneAge/stoneage85 双核**。

**复验**:`ctest` **16/16** · `ci_verify` 六项全过(`SA_WERROR` 0 告警,`code_format` 就地格式化后过)· `model_pool` 20→**35 例 / 238 断言**(+15 例:Item 池 / 背包槽 / POD / 名字上限 / 主人反指)· `world_tick` 77→**78 例 / 1076 断言**(+1:道具观察面恒 0)。
- ★ **反向验证两处**:① `findFreeItemSlot` 起点 `kStartItemArray`→`0`(误把装备位段当背包)⇒ `model_pool` 3 例转红,恢复绿;② `playerItemSlotsUsed` 的 `-1` 哨兵→`0`(丢「无实体≠真0」)⇒ `world_tick` 转红,恢复绿(⚠️ 又踩 make 秒级 mtime 坑,`sleep 2.5 + touch` 隔秒重编才采信真绿)。
- ⚠️★ **一处诚实的"注入不红"**:`playerItemSlotsUsed` 起点 `kStartItemArray`→`0` 在 world 层**不转红** —— 本批无写装备位段的公开路径 ⇒ 无法构造「装备位有、背包空」来区分两个起点(数 0..54 与 9..54 都是 0)。同 §9.0.35「断言的形状没有区分力」族;该起点的区分力由 `model_pool` 反向验证①覆盖(那里能直接往 `items[0..8]` 塞句柄)。

⚠️ **未做**:扣 / 掉 / 用任一链路(道具域后三批)· 当前堆叠数量 `current_pile`(运行期状态,写入侧建)· 装备加成 / 合成 / 镶嵌 / 魔法道具 / 状态附加(各属其域,Item.h 文末逐条登记)· `needitemeneny.txt` 敌人侧表(捕获扣道具批用)· GCC/MSVC 交 CI。
⚠️★ **本批动 `shared/model/`(新 Item.h + Player.h 改)⇒ watched 路径变更 ⇒ 锁定 ref 须前推 `shared-v0.19.0`**。⚠️ 行号基准 = 展开视图 `stoneage-plan/tools/unifdef_80/`。

⇒ ✅ **推送窗口 `shared-v0.19.0` 已闭合(§9.0.49)**。

---

### 9.0.50 ★★ 批次 I.2 —— 捕获扣道具:CaptureItemCheck 前置门 + CaptureItemDelAll 全删(DR-DT21,2026-09-10)

道具域第二批。补 §9.0.26 / §9.0.48 留下的「白给」缺口 —— 原版**扣了道具才给宠物**,此前捕获链缺了这一环。让 I.1 的 `itemCount` 从「恒 0」变成「能减」。

**取证(stoneage85 全宏 + StoneAge 双源交叉核,纪律 ①,已闭合)**:
- `_CAPTURE_FREES` **开**(三源一致)⇒ 全删语义 `BATTLE_CaptureItemDelAll`,DR-BT10 记载正确。
- ⚠️★★ **`_NEED_ITEM_ENEMY` 关**(双源无 `#define`)⇒ 读 `needitemeneny.txt` 的加载器 `need_item_eneny_init()` **根本不编译**,8.0 净核用**源码内硬编码的 `NeedEnemy[]` 表**(`battle_event.c:3890`)。⇒ 记忆/欠债表里「需 `needitemeneny.txt` 敌人侧表」这条**已更正** —— 文件躺在 `csa8.0/gmsv/data/` 但净核不读它(纪律 ①「文件存在 ≠ 被读」的又一例)。
- `_DEL_NOT_25_NEED_ITEM` 关 + `_WOLF_TAKE_AXE` 关(正式 `version.h` 无,仅 `.bak`)⇒ 净核有效行 **9 条**(524/961/953/962/777/796/812/1105/8;双头狼 145/146 不含)。
- `getDelNeedItem()` 门属 `_NEED_ITEM_ENEMY` 段 ⇒ 8.0 **无条件删**。Lua 回调族(`RunItemDetachEvent`/`CaptureOkFunction`/`_ALLBLUES_LUA_*`)**全关**,不复刻(与 I.1 同结论)。
- ★ **匹配键来源被源码钉死**:`IsNeedCaptureItem` 按 `CHAR_getInt(idx, CHAR_PETID)` 匹配,而敌人生成时 `CHAR_PETID = *(tp + E_T_TEMPNO)`(`enemy.c:1200` 等四处)⇒ 匹配键 = **模板号** = `EnemyTemplate::temp_no`。⇒ `Enemy.h` 文末 ② 把 `PETID` 记成「归 D 线不建」**是误判**(它的值在已导入的模板行里),本批建为 `Enemy::pet_id` 并已更正文末登记。

**交付**:
- `shared/model/Enemy.h`:加 `pet_id`(= `CHAR_PETID`,`spawnEnemy` 落值 = `tmpl.temp_no`,1:1 移植 `enemy.c:1200`)。
- `shared/rules/CaptureItem.h`(**新文件**,纯规则常量 + 纯函数,归 `sa_shared`):`kNeedItemEnemy[9]` 硬编码表 + `isNeedCaptureItem(pet_id)`。
- **前置门 ④ `CaptureItemCheck`**(§6.2):原版 `flg = ItemCheck && CaptureCheck`(`battle_event.c:4101`)。道具门读**攻方背包**(世界态),L3 纯函数看不到 ⇒ **与 `capturable` 同款**:World 在 `resolveTurn` **之前**据本回合 capture 指令算好、投影到攻方 `Combatant::mods.capture_item_ok`(`Battle.cpp` 捕获门加 `&& actor.mods.capture_item_ok`)。⚠️★ **必须在摇 rng 前** —— 无道具则不进 `rollCapture`、不消耗捕获 rng(与原版一致)。
- **CaptureItemDelAll**(捕获第 5 步,`World.cpp` 捕获事件写回):`isNeedCaptureItem(src_enemy->pet_id)` → 遍历攻方背包段全删命中道具(清槽 + 释放实体成对,**不 break** = 全删,源码 :4074)。`CHAR_complianceParameter`(:4073)装备加成未移植 ⇒ 无可观察后果、不调,登记划出。
- **注入 seam**:`World::giveItemToPlayer(session, Item)`(同 `spawnEnemyToField`/`loadEncounterTables` 性质:灌入点,写入链路 = 掉落/捡起是后续批次)+ 只读面 `playerItemAt(session, slot)`(同 `playerPetAt`)。

**复验**:`ctest` **16/16** · `ci_verify` 六项 · `world_tick` 78→**85 例**(捕获扣道具 6 组:全删 / 门④拦 / 不在表内不删 / 多个同 id 全删 / 多条件缺一即拦 / seam 三门)· `rules_battle` 76→**77 例**(L3 门④ + 不摇捕获 rng)· 反向验证两处逐条转红(注入 A「全删空操作」⇒ 3 组红;注入 B「去门④」⇒ 拦截类 + L3 门④ 红),还原后跨秒重编复跑 16/16、工作树无残留。

⚠️★ 动 `shared/model/Enemy.h` + 新增 `shared/rules/CaptureItem.h` ⇒ watched 路径变 ⇒ **锁定 ref 须前推 `shared-v0.20.0`** + 客户端换 pin 验发布态(推送窗口待办,随窗口带上 `81075ee` 文档 ahead)。

**登记残缺**(有据划出,非遗漏):① `CHAR_complianceParameter` 删道具后重算(装备加成域);② 道具入背包的**写入链路**(掉落/捡起,道具域后两批)—— 当前只有 `giveItemToPlayer` 注入 seam;③ `needitemeneny.txt` 文件加载器(`_NEED_ITEM_ENEMY` 8.0 关,净核不需要)。

---


### 9.0.51 ★★ 批次 I.3 —— 野怪掉落:spawn 千分率摇 → 结算逐件随机拾取 → 灌背包(DR-DT22,2026-09-10)

道具域第三批。让 I.1 的 `itemCount` 从「只减(捕获扣)」到「也能加(战斗掉落)」。

**★★ 开工亲验勘误(纪律①,对象是任务书/记忆)**:掉落被定位到 `NPC_NPCEnemy_Dying`,回源码
(stoneage85 全宏 + StoneAge 双核)实证那是**明雷专用** `additem`(`npc_npcenemy.c:489`,依赖 NPC
argstr 脚本层 = DR-DT19 划出的 D6);**野怪掉落是另一套三阶段** `BATTLE_AddExpItem`。⇒ 用户拍板
本批只做野怪三阶段。⚠️★ 连带:`grep -a` 坑扩大到源码树(`.c/.h` 含 GBK 注释被判 binary,不加
`-a` 对 `ENEMY_ITEM` 假阴性)。裁定详见 `11` §2.24 / DR-DT22。

**交付(三阶段,行号 = stoneage85 全宏)**:
- ①`shared/model/Enemy.h` 加 `dropped_items[10]`/`drop_count`;`spawnEnemy`(`rollSpawnStats` 后)
  千分率 `RAND(0,999)<prob`(`enemy.c:1210`,`_FIX_ITEMPROB` `version.h:117` ON,prob=0 不摇 rng)
  摇进预掉落槽(紧凑存 `item_id`、保摇号序)。
- ②`world/Api.h EnemyEncounter` 加 `item[10]`/`item_prob[10]`;战果结算段逐件 `RAND(0,allnum-1)`
  选在场单位(含宠折算回主人)入局部 `getitem[≤3]`,满则 50/50(`battle.c:6486`,用 `b.rng`)。
- ③`giveItemIntoPlayer`(抽出与 `giveItemToPlayer` seam 共用,DR-BT5)灌背包,满则丢弃(`battle.c:4471`)。

**三处自主决策(源码/纪律支持)**:紧凑存 `item_id`+延迟 `makeItem`(省池、rng 一致)· 本批不下发
客户端(服务端权威 + 观察面)· 拾取用 `b.rng`(战斗结束用完即弃)。

**复验**:`ctest` **16/16** · `ci_verify` 六项全过(`SA_WERROR` 0 告警)· `world_tick` 85→**88 例
/ 1200 断言** · ★ **反向验证三处逐条精确转红**(A 摇号判定恒假 ⇒ drop_count/进背包红、calls 绿;
B 不灌包 ⇒ 仅进背包红;C prob=0 也摇 ⇒ 仅 calls 断言红),还原后 16/16、`INJECT` 残留 0。

⚠️★ 动 `shared/model/Enemy.h` ⇒ watched 变更 ⇒ **锁定 ref 须前推 `shared-v0.20.0`**(与 I.2 合窗口,
推送待用户确认)。**登记残缺**:明雷 additem(D6)· 掉落展示/满包提示 DR-UX1(客户端下发)·
Item 仅 `item_id`(道具表 D 线)· 组队掉落归属(组队未落地)。

---

### 9.0.53 ★★ 批次 I.4 —— 使用道具:战斗内 HP 恢复药(DR-DT23,2026-09-11)

道具域**第四批 = 收官批**。I.1 建槽、I.2 能减、I.3 能加,本批让道具**能用** —— 背包里的东西第一次
产生玩法后果。

**★★ 开工取证解掉了两个悬了三批的未知(纪律 ①,双源交叉:`StoneAge/gmsv/src/` 8.0 树 + stoneage85 全宏)**:

① ⚠️★★ **「8.0 无 Lua ⇒ 道具 usefunc 怎么分发」当场结案:不走 Lua,走硬编码 C 名字表。**
   §9.0.48 开工前取证记的是「效果走 functable 分发(`mylua/function.c`),functable 本体是 Lua 表
   ⇒ 8.0 无 Lua 下这套怎么落地要单独核」—— 那是 **8.5 的形态**。8.0 链路实证:
   `CHAR_ItemUse`(`char_item.c:663`)取 `usefunc = ITEM_getFunctionPointer(itemindex, ITEM_USEFUNC)`
   → `ITEM_constructFunctable`(`item.c:680`)在建道具时按道具表的**函数名字符串**查
   `getFunctionPointerFromName`(`function.c:774`)→ 而那张表 `correspondStringAndFunctionTable[]`
   (`function.c:165`)是**硬编码 C 数组**,`{"ITEM_useRecovery", ITEM_useRecovery, 0}` 在 `:188`。
   ⇒ **道具使用链在 8.0 净核里是通的**,`mylua` 那套与它无关。⚠️ 记忆/取证里「入口是
   `lssproto_ID_recv`→`CHAR_ItemUse`」这半是对的(§9.0.48 已更正过一次符号名),错的是分发机制。

② ⚠️★★ **恢复量要摇 rng —— 本批开工时工作树里已有的实现把它写成了确定值,是错的。**
   工作树(2026-09-10 22:09 留下、未提交)的注释断言「⚠️ 无 rng(恢复量确定 —— `battle_item.c:289`
   直接 `sscanf` 出 power)⇒ 用道具不摇骰」。回源码跟到底:`sscanf` 出的 `power` **只是基数**,
   `BATTLE_MultiRecovery` 拿到它之后还有一步
   `UpPoint = RAND(power*0.9, power*1.1)`(`battle_magic.c:419`)⇒ **恢复量是 ±10% 区间随机,且消耗
   一次随机数**。★ 这条错的后果不在「恢复多少」,在**用过道具之后的所有 rng 消耗整体平移**,
   而任何「返回值对不对」的断言都抓不到(与 DR-BT23 退化区间同族)。⇒ 已按源码更正:
   字段 `item_heal_hp` → **`item_heal_power`**(名字跟着语义走:投影的是基数不是恢复量)。
   ★★ **教训归档**:纪律 ① 的对象这次是**上一次会话自己写的实现与注释** ——「只跟到取到值的
   那一行就下结论」,少跟了一层调用。同 §9.0.46 W.5「别假设记忆/DR 的表述与实现一致」的镜像形态:
   那次是记忆错、实现对;这次是实现错、注释还替它编了理由(纪律 ⓪)。

**净核判定(8.0,三个宏都回 `StoneAge/gmsv/src/include/version.h` 核过,均为开)**:
- `_MAGIC_REHPAI` **开** ⇒ `battle_magic.c:421-425` 的 `#else` 段**不编译** ⇒ **无** `per` 百分比缩放、
  **无** `GetRecoveryRate(vital)` 体力系数(`char.c:8676`)。⇒ 净核 = 一摇 + clamp,**不引入浮点**。
  ⚠️ 早先按 `#else` 段实现会凭空多出体力修正,而它在 8.0 根本不生效。
- `_MAGICPET_SKILL` 开 ⇒ `power == -1` 的「全满恢复」分支存在,但取决于道具表实际有无该数据
  ⇒ 划出登记(D 线)。`_TYPE_TOXICATION` 开 ⇒ `CHAR_CanCureFlg` 门存在 ⇒ 依赖 L4 状态系统,划出。

**交付**:
- `shared/model/Item.h` 加 **`current_pile`**(运行期堆叠数,原版存 `workint`)—— 兑现 I.1 文末 ④
  登记的「留写入侧批次按各自语义建」;`<=0` 视同空格。掉落路径回填 `=1`。
- `shared/rules/Combatant.h` 加 `mods.item_heal_power`(**基数**,默认 0 ⇒ 不恢复**且不摇 rng**)。
- `shared/rules/Battle.cpp` USE_ITEM 分支:`RAND(0.9p,1.1p)` 摇恢复量 → `min(max_hp, hp+heal)`
  → 产 `SetHp`(写**回合内 `hp[]` 镜像**,与攻击链同一份)。
  ★★ 区间**照原版 double 表达式的取值集合、但用整数算**(避开 FMA/跨平台浮点,§9.0.6 的教训):
  原版 `RAND(x,y)` 展开 `x + (int)((y-x+1)*u)`,x=0.9p / y=1.1p 均为 double ⇒ 取值集合
  `{ floor(0.9p)+k : k=0..ceil(0.2p+1)-1 }` ⇒ **`lo = 9p/10`、`hi = lo + (p+9)/5 - 1`**。
  ⓘ **穷举 p=0..100000 验证两式取值集合逐个相等**(临时 C 程序,0 处不匹配)—— 推导时手算差点
  在 p=15 上出错,所以这条是**实测不是推导**。⚠️ 已知偏差(不修,DR-DT23 ②):原版 `(int)(N*u)`
  在 N 非整数时尾值概率偏低、本实现 `r % span` 均匀 —— 与「xorshift64* ≠ glibc `rand()`」同层次
  的不可比项(§0 第③层),取值集合一致即止。
- `src/world/`:`ItemEffect{item_id, heal_power}` 表 + `loadItemEffects`(**默认空 ⇒ 任何道具无效果**,
  同 `loadEncounterTables` 取向,不解析 GBK 文件)· `projectItemUsePower`(resolveTurn **前**投影,
  同捕获门 ④;⚠️ **只投基数,不在这里摇** —— 在 World 层摇会让取数落到 resolveTurn 之外、
  战斗 rng 序列错位)· `consumeUsedItems`(applyEvents **后**扣一个 pile,归零则清槽 + release
  **成对**)· 只读面 `playerItemPile`。

**复验**:`ctest` **16/16** · `ci_verify` 六项全过(`SA_WERROR` 0 告警)·
`rules_battle` 77→**84 例 / 2493 断言** · `world_tick` 88→**95 例 / 1262 断言**。
★ **反向验证四处逐条转红**:A 恢复量改回确定值 ⇒ `rules_battle` 4 例/13 断言红;
B 上界改朴素 `11p/10` ⇒ **精确红 1 条**(`bounds(7)`,其余 power 值两式相同 ⇒ 印证特意选 p=7);
C 清槽不 release ⇒ `world_tick` 2 例红;D 投影挪到 resolveTurn 后 ⇒ 3 例红。
还原后 16/16、`INJECT` 残留 0。
⚠️★ **一处诚实的「注入不红」**:A 在 `world_tick` 侧**全绿** —— 世界侧断言的是恢复量落在
`[500+90, 500+110]` 区间内,而 `heal_lo` 也在区间内 ⇒ 对「摇不摇」无区分力。这是分层的自然
结果(区间由 L3 侧逐值验),不是缺陷,但如实记下:**同一处缺陷在不同层的用例里区分力不同**
(同 §9.0.48 那条 `playerItemSlotsUsed` 的形态)。

**★ 顺带接上一处两批之间的接缝**:新增用例「掉落进背包的道具可以直接喝」—— I.3 的掉落路径若
忘了回填 `current_pile`,道具进了背包却**用不了**,而 I.3 自己的用例(只数 `itemCount`)会全绿。
⇒ 欠债 20「地基绿而运行时不接」在**两个批次接缝**上的形态,现已有断言覆盖。

**★ 兑现一条早就写下的约定**:`RulesBattleTest` 那条「未接入的指令一律跳过」用例把 `USE_ITEM`
留在表里**没有变红** —— 因为 `makeDuel` 的 `item_heal_power` 默认 0,恰好走「非恢复药」那支。
按该用例自己的注释(CAPTURE 当年同款「巧合命中」)⇒ 已从表中移除,行为改由本批用例系列钉住。

⚠️★ 动 `shared/model/Item.h` + `shared/rules/{Battle.cpp,Combatant.h}` ⇒ watched 变更 ⇒
**锁定 ref 须前推 `shared-v0.21.0`**(推送待用户确认,含 server `6e57229` ahead 1 一起推)。
**登记残缺**(有据划出,非遗漏):① MP 恢复(`BD_KIND_MP`)/ 状态药 / 变身 / 传送 / 解猪 / 属性旋转
等其余 usefunc —— 各依赖未移植子系统 · ② **场景内使用** `ITEM_useRecovery_Field`(非战斗态,
`item_event.c:1073` 按 `WORKBATTLEMODE` 分流;需场景态 HP 与提示链)· ③ **目标已死时原版
「改打随机活人」不复刻**(`BATTLE_MultiList` `battle.c:236` 的 `while((toNo=nLifeArea[rand()%10])==-1)`
消耗不定次 rng;全死时 `return -1` 而 `BATTLE_MultiRecovery` 不检查返回值 = 原版 UB)⇒ 本实现
目标不可用即什么都不发生、不摇 rng,**已知行为差** · ④ `CHAR_ItemUse` 的 `ITEM_TYPE` 非
`ITEM_OTHER`/`ITEM_DISH` ⇒ 走穿装备(`CHAR_moveEquipItem`)那条门(装备域)· ⑤ `power == -1`
全满恢复(需道具表真数据)· ⑥ `CanCureFlg` 不可治疗门(L4 状态系统)· ⑦ 道具效果表真数据
(D 线导入 `itemset6.txt` 的 `usefunc`/`ITEM_ARGUMENT` 两列)· ⑧ 客户端表现(用药动画/飘字)。
