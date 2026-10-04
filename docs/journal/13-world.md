# journal / 13-world — 世界系统:移动、视野、刷怪、遇敌触发

> **后续勘误（2026-09-12）**：本文件保留当时交付记录。与当前审计有关的数值、时序、接线及验收更正见[修复记录](../audits/2026-09-12-remediation-results.md)，不以旧批次完成状态代替本轮验证。

> **本文件收录**:W.1 移动 + 529 格视野、W.4 暗雷遇敌闭环、W.2+W.3 世界敌人(刷怪 + 条数制摊还 + 游荡 AI)、W.5 明雷触发战斗、W.6 WARP传送点、W.7 NPC实体与Healer恢复员。
>
> **批次编号**:§9.0.42 · §9.0.44 · §9.0.45 · §9.0.46 · §9.0.71 · §9.0.72(共 6 节)
>
> ★ **编号沿用 `00-architecture.md` 原 §9.0.x 体系,搬家未改号** —— 全仓约 600 处 `§9.0.x` 引用因此继续有效。总映射见 [`README.md`](README.md)。
>
> ★ 本目录是**开发流程账**(逐批次的取证 / 交付 / 复验 / 教训),原文逐字迁入、未改写。
> 架构裁定看 [`../00-architecture.md`](../00-architecture.md);决策看 [`../11-decision-register.md`](../11-decision-register.md);
> 还欠什么看 [`../backlog/`](../backlog/);和原版哪里不一样看 [`../deviations/`](../deviations/)。

---

### 9.0.42 ★★ 批次 W.1 —— 移动系统:玩家移动 + 529 格视野广播(2026-09-09)

> 遇敌链 + 战果结算之后,补上「玩家如何在世界里移动」—— tick 的 `kCharLoop`(第 5 步)
> 与 `kOutboundFlush`(第 7 步)从占位变实装。⚠️ 用户裁定验证边界含**视野**(能看到彼此移动),
> 故一个批次里连做两个里程碑:① 玩家移动 + 碰撞 · ② 529 格视野广播。

交付面(一个 commit,~500 行):
- **① 玩家移动 + 碰撞**:`Model::Player` 加位置(`floor/x/y/dir`)· `world/Map`(数据结构 + `mapWalkable`
  —— 1:1 移植 `MAP_walkAbleFromPoint`(展开视图 `map/map_deal.c:24`)的地面三值分支 —— + fixture)·
  `WalkRequest`(0x0301,新 `domain/world_map.proto`)上行 + `Session::handleWalkRequest` + `World::onWalk`
  (移植 `lssproto_W_recv`(`callfromcli.c:503`)净核:防瞬移 + 碰撞预检 + 排走路串)· `kCharLoop` 玩家段
  (`CHAR_Loop:4667` + `CHAR_walk_check:4583` + `walk_move` 的碰撞/坐标更新核心)· `playerPos` 观察面。
- **② 529 格视野广播**:`olink` 格子对象索引 · `CharAppear/CharMove/CharDisappear`(0x0302-0x0304)下行 ·
  出生 / 移动 / 断线三处维护 olink + 扫格 diff 广播(视野对称 ⇒ 进出双向 CA/CD)· `kOutboundFlush` 复用既有 flush。
- 用例 `world_map` **10 例 / 63 断言**(地图 3 · 移动 5 · 视野 2),含 `VisMirror`(仿 §9.0.16 `ClientMirror`,只解 CA/CD/Move)。

#### ① 两步移动机制正是「前置是 kCharLoop」的实证

原版走路是**两步**:`lssproto_W_recv` → `CHAR_walk_init`(char_walk.c:937)把 direction 串排进 work 区
(`CHAR_WORKWALKARRAY`);`CHAR_Loop` 玩家段每 tick 全扫在线玩家 → `CHAR_walk_check` 按 `walksendinterval`
间隔逐字符消费(`CHAR_walkcall`)→ `walk_move` 走一格。⇒ 玩家移动执行确实在 `kCharLoop`,这是「遇敌前置是
移动系统 + kCharLoop」的实证。走路间隔 = `csa8.0/setup.cf` `walkinterval=2500` × 100us(char.c:4590 判据)
= **250ms**。方向字符 `CHAR_ctodirmode`(char_walk.c:1398):小写 `a`-`h` 移动 / 大写 `A`-`H` 转身,
`dir` 0-7 照 `CHAR_dxdy[8]`(char.c:2325,北起顺时针)。⚠️ 走路串是运行时态(work 区)⇒ 放 `World::Impl::Conn`
不放 `Model::Player`(后者只依赖标准库,且是持久态)。

#### ② 碰撞可 fixture,地图放 world/(同 EnemyEncounter)

`MAP_walkAble` = 两个查表(`getTileAndObjData` 取 tile/obj 图元号 + `getImageInt(WALKABLE)` 查图元属性表)
⇒ fixture 地图(小网格 + walkable 属性表)接口不变,真实地图(LS2MAP,1,235 图)走 **D 线内容导入**(终点入库,
非运行时读 txt)。★ 地图数据放 `world/`(Api.h)而非 `shared/`:内容数据形状、服务端权威碰撞、客户端只预测
(10 §4.1),同 `EnemyEncounter`(M.5)的判据。

#### ③ ★ 视野常量 23 是裁定不是观测(落在 §10.2 不可判定项上)

展开视图 unifdef_80 的 `CHAR_DEFAULTSEESIZ` 是 **20**(char_base.h:58 —— 8.5 血统,unifdef 只处理条件编译、
**不改 #define 字面值**),而 §5.1 / 10-world-map §9 决策取 **23**(8.0 血统源码 + 与数据基线一致)。
⚠️ #define 不进符号表 ⇒ **无二进制证据**,正是 §10.2 六项不可判定之一(「视野常量 20 vs 23」)⇒ 取 23 是
**裁定不是观测**,代码注释标明。扫格公式 `(2*(c/2)+1)²`(c/2 整数除,c=23 奇数 ⇒ ≠ (c+1)²)= 23² = **529**。

#### ④ 视野对称 ⇒ CA/CD 双向;扫格 diff(§5.3 决策5:先扫格不订阅)

A 移动后,扫 A **旧**位置与**新**位置周围 529 格的 olink,diff 两个可见集:仍可见 ⇒ B 收 A 的 `CharMove`·
新进入 ⇒ 双向 `CharAppear`(A 见 B 出现、B 见 A 出现,因视野对称)· 离开 ⇒ 双向 `CharDisappear`。
出生 / 断线同理(单向 appear / disappear)。⚠️ `CharAppear` **只带位置**:1.5 的 Player 无选角 ⇒ 无图号、
名字恒空(同 §9.0.16 ③ `menu_flags` 恒 0 那族登记残缺)⇒ 客户端画占位角色。

#### ⑤ 两处守卫 / 自查抓到的问题

- ★ **`module_boundaries` 抓到 `Map.h` 违反「include/ 只暴露 Api.h」**(§3.1)⇒ 把 Map 的公开类型并入
  `Api.h`(同 `EnemyEncounter`),视野广播 helper 做成 **`World::Impl` 成员方法**(要访问 olink/conns/players
  等私有状态,而 `conns` 的 value `Conn` 是 Impl 私有嵌套 ⇒ context struct 装不下,不能像 `applyEvents`
  那样用自由函数)。
- **`next_walk_at_ms` 修一个时序 bug**:初版用「上次走一步的时刻」+ `last==0` 表示「没走过」,而 ManualClock
  从 0 起 ⇒ **t=0 走完后 last 仍是 0**,与「没走过」无法区分、间隔门失效 ⇒ 改用「下次可走的最早时刻」
  (同 `BattleInstance::next_turn_at_ms`)。★ 这个 bug 是**写用例时**发现的(第二 tick 不该走却走了)。

#### ⑥ 复验

`ctest` **16/16** · `ci_verify` 六项全过(`SA_WERROR` 清洁构建 **0 告警**)· `code_format` · `dr_table` ·
`module_boundaries`。★ **反向验证 4 处逐条精确转红后回绿**:① 方向反向(`+dx`→`-dx`,移动系用例)· ② 间隔门
失效(串消费用例的「第二 tick 不走」)· ③ 不发 `CharMove`(视野 move)· ④ 不发 `CharDisappear`(视野 disappear);
每处只让对应用例红、其余全绿(注入精确)。⚠️ 又踩 §9.0.39 的 **make 秒级 mtime 坑**,`sleep` 隔秒 + `touch`
确认重编才采信。

#### ⑦ ⚠️ 未做 / 登记残缺

| 项 | 归属 |
|---|---|
| NPC/敌人 AI 摊还(`CHAR_Loop` 非玩家段:游标滑动窗口 + `getCharLoopTime` 时间片 + `EnemyMoveNum` **8.0=10**)| **W.3**(依赖 `kNpcSpawn` 敌人实体来源)|
| 遇敌触发(`char_walk.c:585` `rand()%(120*getEnemyAction())` + 传送点抑制)→ 接 M.5–M.7 → `spawnEnemyToField` 批量入场 | **W.4** ⇒ **「走动 → 遇敌 → 战斗 → 拿经验」闭环** |
| 脏槽跟踪(`01` §13 欠债 20②)| `kCharLoop` 实装后可对齐 `EntityPool.h` 文末,建议独立小批 |
| `CharAppear` 无名字 / 图号 | Player 无选角来源 ⇒ 客户端画占位(同 `menu_flags` 恒 0 族)|
| 客户端地图场景表现层(消费 CA/CD/Move)| 客户端批次;本批服务端侧 `VisMirror` 断言线上契约自洽(同 1.4 分工)|
| 组队移动(`CHAR_PARTY_CLIENT`)· 重叠事件(`RunCharOverlapEvent`)· nuke 反作弊 | 各依赖组队 / NPC-Lua 系统;nuke 是自由服魔改非 8.0 净核 ⇒ 划外 |
| 真实地图导入(LS2MAP)| D 线内容导入(替换 fixture 数据源,接口不变)|
| ★★ **锁定 ref 前推** | `shared/model/Player.h`(位置字段)+ `idl/generated`(world_map)= watched 路径 ⇒ **必须前推 `shared-v0.16.0`** + 客户端换 pin 复验(推送窗口待办)· 只在 Apple clang 21 跑过,GCC/MSVC 交 CI |

---

### 9.0.44 ★★ 批次 W.4 —— 遇敌触发闭环:走动 → 遇敌 → 战斗 → 拿经验(DR-DT17,2026-09-09)

> 移动系统(W.1)之后,把已造齐的零件串成阶段 2 的关键闭环:玩家走一格 → 遇敌骰子 →
> 遇敌链选怪 → 开战 → 战果。★ 取证确认这在原版是 `char_walk.c:585` 骰子 → `EN_recv`
> → `BATTLE_CreateVsEnemy`,而后者内部**恰好是 server 现有函数的组合** ⇒ 本批以「复用 + 接线」为主。

交付面(全在 `src/world/`):
- **遇敌骰子**(`kCharLoop` 玩家段,走完一格 `moved` 后):`findEncountArea` 命中 ⇒ `cep` 夹在
  `[prob_min, prob_max]` ⇒ 骰子 `world_rng.randMod(120*getEnemyAction()) < cep`(移植 `char_walk.c:585`;
  `temp=cep`,无技能 `p_cep=0`)。命中 ⇒ 清走路串(`EN_recv` 的 `WALKARRAY=""`)+ `cep=prob_min`。
- **开战组装** `World::triggerEncounter`(移植 `EN_recv`(`callfromcli.c:1249`)+ `BATTLE_CreateVsEnemy(_,0,-1)`
  净核(`battle.c:2528`)):`pickEnemyGroup` → `rollEnemyList` 选怪 → `startBattle`(玩家占位入 Side[0]) →
  `joinBattle` → 逐只 `spawnEnemyToField`(Side[1],`baselevel=-1` 野外摇号) → 战斗自动进 tick。
- **遇敌数据源** `World::loadEncounterTables`(注入四表 `EncountArea`/`EnemyGroup`/`EnemyEncounter`/
  `EnemyTemplate`)。★ 默认空 ⇒ 永不遇敌 ⇒ 现有走路用例不受影响;真玩法由 D 线导入灌入(阶段 2)。
- **世界级遇敌 rng** `world_rng`:种子从 `masterSeed` **派生但不调 `nextSeed`** ⇒ 不消耗战斗种子序列
  (否则现有战斗回放整体平移)。· **配置** `enemy_action`(`getEnemyAction` clamp[1,100];原版未配 ⇒ 1)·
  观察面 `battleCount`。

#### ① 玩家进场四维是占位(无选角来源,登记残缺)

`makePlayerCombatant` 用占位四维(同 `makeDemoField` 的 me)—— 1.5 的 `Player` **无四维、无 level**
(`Player.h` 只有位置 + `exp` + 宠物槽),同 §9.0.42 名字留空那族。⇒ 闭环能跑但玩家数值是占位,
阶段 2 接选角后由存档取代。★ 占位量级(attack≈322)让它打得动遇敌链产出的真实弱怪(欠债 25 的 16 倍差),
使「打赢拿经验」有意义。

#### ② rng 分工:遇敌用世界 rng,敌人四维用战斗 rng

原版 `ENEMY_getEnemy` 在建 battle **之前**用全局 `rand()`(战斗还没建)⇒ 遇敌骰子 + 选怪用
`world_rng`;而 `spawnEnemyToField` 内的 `spawnEnemy`(生成四维)用 `b.rng`(战斗种子,可回放,M.4b)
⇒ 两条序列分离,各自可回放。

#### ③ 复验

`ctest` **16/16**(`world_map` 10 → **15 例**,+5:必遇敌 / 不遇敌 / 未注入 / 清串 / 端到端)· `code_format` ·
全套无破坏(现有 `world_tick` / 走路 / 视野全绿)。★ **反向验证:骰子符号 `<`→`>=`** ⇒ 用例 1/2/5/6
精确转红(必遇敌变不遇敌、不遇敌变遇敌、清串失败、端到端无战斗),而用例 3(未注入)保持绿 ⇒ 断言有区分力。
⚠️★★ **`enemyCount` 断言的区分力由一个真实 bug 兑现,比构造反验更强**:初版 `MoveFixture::spawn`
跳过握手 ⇒ `joinBattle` 握手门拒绝 ⇒ `battleCount==1`(开了战)**而 `enemyCount==0`(敌人没入场)**
⇒ 用例 1 当场把这两个断言分开抓到 —— 正是「开战」与「敌人入场」两件事各需一个探针(同欠债 20/25 那族)。
⇒ 修法:遇敌用例改用握手 spawn(遇敌虽是服务端触发,但玩家仍须是握手过的会话)。

#### ④ ⚠️ 未做 / 登记残缺

| 项 | 归属 |
|---|---|
| 传送点抑制(`entflag`,`char_walk.c:578`)· 明雷敌人退回(`:566`)| **依赖未就绪**:1.5 地图无 WARP 对象、无 NPC 敌人实体(属 `kNpcSpawn`/W.3)⇒ 登记非留空(同 M.6 空背包)|
| 自由服魔改:`getEqNoenemy` / `getEqRandenemy` / Ra's amulet(`eqen`)· `getStayEncount` | 非 8.0 净核 ⇒ 恒等划外 |
| `cep` 累积 `cep++`(`char_walk.c:607`)| 在**战斗态**分支,玩家走路恒非战斗态 ⇒ 走不到,照抄源码结构但不硬接(同 M.6/M.7 等价 / 冗余族)|
| `CHAR_ENCOUNT_FIX`(技能固定遇敌率 `p_cep`)· 组队遇敌(`BATTLE_PartyNewEntry`)| 技能 / 组队系统未移植 |
| 玩家进场四维占位 | 无选角来源(见 ①),阶段 2 接选角 |
| 遇敌数据 fixture / 真实导入 | 用例注入 fixture;真数据 D 线入库(同 M.5 的 `enemy1.txt`)|

★ 本批**全在 `src/world/`**(Conn 私有 + world 逻辑 + config),复用现有战斗下行消息(`BattleSelfInfo`/
`BattleTurnBegin`/`BattleResult`)⇒ **watched 路径零改动 ⇒ 锁定 ref 不前推**。只在 Apple clang 21 跑过,GCC/MSVC 交 CI。

---

### 9.0.45 ★★ 批次 W.2+W.3 —— 世界敌人:地图刷怪 + 条数制摊还游荡 + 视野扩到"玩家看敌人"(DR-DT18,2026-09-10)

> 用户拍板**合一批做完**(如 W.1「移动 + 视野」):让敌人成为**地图上的常驻游荡怪**(明雷)——
> spawn 到世界坐标、按节拍游荡、被周围玩家视野看到。区别于 W.4 的**遇敌即时 spawn 到战场**(暗雷)。

交付面:
- **W.2 刷怪**(`kNpcSpawn` tick 第 3 步实装):`loadSpawnPoints` 注入 `SpawnPoint`(floor/中心/enemy_id/count/
  radius/interval/level,默认空)· `spawnWorldEnemies` 据点补齐到 count(`enemy_id → findEnemyEncounter →
  findEnemyTemplate → spawnEnemy` 入 `EnemyPool` + 写世界位置)· 观察面 `worldEnemyCount` / `worldEnemies`。
- **W.3 摊还 + 游荡**(`kCharLoop` 非玩家段 5b):`wanderWorldEnemies(tempo.enemy_move_num)` 条数制摊还
  (`charloop_cursor` 游标续跑)· 游荡 AI(节拍 `Enemy.next_wander_at_ms` → `world_rng.randMod(8)` 方向 →
  `mapWalkable` + 半径门 → 走一格)。
- **视野扩展**:`world_map.proto` 的 `CharAppear/Move/Disappear` 加 `entity_type`、`CharAppear` 加 `image`;
  敌人 spawn/move/despawn **单向**广播给周围玩家(`collectVisiblePlayers`),玩家移动后 `refreshEnemyView` 补发。
- **`Enemy.h` 加世界态位置**(floor/x/y/dir + `next_wander_at_ms`)· `tempo.enemy_move_num`(默认 20,可配)。

#### ① ⚠️★★ 一次二度反转:摊还是条数制不是时间预算制(本批最该被读的一节)

`CHAR_Loop`(`char.c:4655`)非玩家段在原版是**三套宏互斥**:`_CHAR_LOOP_TIME`(时间预算 `while` + `getCharLoopTime()`
微秒预算)/ `_FIX_CHAR_LOOP`(pet 段 `EnemyMoveNum` + other 段 50)/ 默认(`petnum/2` + `EnemyMoveNum`)。
★★ **`_CHAR_LOOP_TIME` 在 8.0 三证实测关**(`15 §5.2 C18`:getter `getCharLoopTime` 不在 B80 符号 + 配置键
`charlooptime` 不在 B80 配置表 + `csa8.0/setup.cf` 未赋值,三证一致;实现仓本文件 §9 的 `TempoConfig` 注释早已采信)
⇒ 走 `#else` **条数制**:每 tick 处理够 `EnemyMoveNum` 只即停、`static charcnt` 游标记位下 tick 续。

⚠️★★ **`unifdef_80` 展开视图 `char.c:4714` 把 `_CHAR_LOOP_TIME` 当"开"、展开成时间预算 `while`,是错的** ——
根因是 `_CHAR_LOOP_TIME` 是**编译期 `-D` 宏**(stoneage85 全树无 `#define`),`macros_80.json` 把它列入 ⇒ unifdef 当定义。
★ **动手时先照展开视图记成"时间预算制、EnemyMoveNum 死变量",直到撞见本文件 `TempoConfig` 注释与 `15 §5.2` 才二度反转**
—— 这是取证纪律「展开视图会误导」的**最强样本:不是丢被宏关的分支,是把关的宏当开、选错分支**(记忆 evidence-workflow 已补)。
⇒ 反转后实现**更简单**:条数制确定可测(去掉墙钟 / 预算的非确定性),`EnemyMoveNum` 移植为 `tempo.enemy_move_num`。
⚠️ `_FIX_CHAR_LOOP` 开关未核(other 段 50 vs 默认 `EnemyMoveNum`),fixture 规模无可观察差异 ⇒ 登记。
⚠️ 分池后游标只在 `world_enemies` 上绕(原版绕回 `playernum` 跳过玩家段;我们玩家在玩家段每 tick 全扫)⇒ 语义等价、更简单。

#### ② 刷怪点是不阻塞 D6 的注入式替身,不是原版某表

原版地图敌人由 **NPC/Lua 脚本**调 `ENEMY_createEnemy(enemy_id, level)` 刷(`mylua/npcbase.c:472`),`CHAR_callLoop:4598`
的 AI 也靠角色身上的**函数指针 `CHAR_LOOPFUNC`** + `RunCharLoopEvent`(Lua)。而**脚本层 D6 未落地**、函数指针在 POD 架构不可照搬。
⇒ `SpawnPoint` 是**不阻塞 D6 的最小切法**(同 W.4 `loadEncounterTables` 默认空的注入式取向):真玩法接 D6 后由脚本产出刷怪参数。
★ 敌人来源仍走单一真源(`enemy_id` 查敌人表 → 模板表 → `spawnEnemy`,复用 M.4b/M.7);游荡 AI 改成数据驱动(随机游走)替代函数指针。

#### ③ 视野从"玩家↔玩家"扩到"玩家看敌人"(两处 watched 变更)

W.1 视野对称(`olink` 挂会话)。敌人无会话 ⇒ `entity_type` 区分玩家 id 空间与敌人 `EntityHandle` 空间(数值会撞,
客户端按 `(type,id)` 二元组跟踪);`image` 带敌人真图号(玩家 0,残缺同 §9.0.42/DR-DT16 ⑤)。⇒ **单向**:敌人不接收下行。
⚠️ 改 `world_map.proto`(重跑 `saidl_gen.py`,`idl_verify` 一致)+ `Enemy.h` 加位置 ⇒ **两处 watched 变更,须前推 `shared`**
(与 W.1「零改动不前推」相反)。

#### ④ 复验

`ctest` **16/16**(`world_map` 15 → **26 例 / 213 断言**,+11:刷怪补齐 / 落位带图号 / 幂等 / 查不到不刷 / 默认无刷 /
游荡位置变 + 半径 / radius=0 不动 / 条数制上限 / 视野 appear(entity_type+image) / move / disappear)· `ci_verify` 六项全过
(`SA_WERROR` 清洁构建 0 告警,新加 `LogEvent` 两条补 `Log.cpp` switch)· `code_format` · `idl_verify` · `shared_purity`
(`Enemy` 加位置不违纯度)· `dr_table`(§2 加 DT18 + §2.20)。★ **反向验证三处逐条精确转红**:条数制上限(`moved<max`→不限
⇒ 3 只全动 `3==1`)· 视野 `entity_type`(ENEMY→PLAYER ⇒ `0==1`)· 半径门(→`false` ⇒ radius=0 也走 `33==32`),恢复后全绿。

#### ⑤ ⚠️ 未做 / 登记残缺

| 项 | 归属 |
|---|---|
| **明雷触发战斗**(玩家撞上世界敌人开战,把世界敌人拉进战斗)| 解 W.4「明雷退回」的那一层,留下批;当前世界敌人可被看到、会游荡,但撞上不开战 |
| `CHAR_LOOPFUNC` 完整 AI(追击 / 攻击玩家)· `RunCharLoopEvent`(Lua)| 依赖脚本层 D6 + 函数指针数据驱动化 |
| 时间预算截断路径(`_CHAR_LOOP_TIME` 那支)| 8.0 关 ⇒ 不实现;墙钟不可确定性复现 ⇒ 结构留 `charloop_cursor` 但不做确定性用例 |
| `_FIX_CHAR_LOOP` 的 other 段上限 50 | 未核(fixture 规模无差异);敌人规模大且需精确摊还时回 B80 核 |
| 多 floor(fixture 单张)· 真实刷怪点 | D 线内容导入 / D6 脚本层 |
| 世界态敌人四维观察面 | `worldEnemies` 只暴露位置 / 图号;四维已由 M.4b `spawnEnemy` 的用例覆盖 |

★ 只在 Apple clang 21 跑过,GCC/MSVC 交 CI。

### 9.0.46 ★★ 批次 W.5 —— 明雷触发战斗:EV 事件开战 + 撞明雷退回(DR-DT19,2026-09-10)

> 补全明雷交互闭环:世界游荡怪(明雷,W.2+W.3)此前**撞上无反应** —— `walkStep` 只判地形、敌人不占格、不进 `olink`
> ⇒ 玩家能直接走上明雷格,既不退回也不开战。本批从零做**退回 + EV 事件开战**两者。裁定详见 `11` §2.21 DR-DT19。

⚠️★★ **勘误(承 §9.0.45 ⑤ 未做表首行)**:那条写「明雷触发战斗…解 W.4 明雷退回」,隐含"退回已实现、W.5 改成开战"。
勘察实证**退回从未实现** ⇒ 明雷交互(退回 + 开战)整个是缺口,W.5 做两者,不是"改退回为开战"。

交付面:
- **IDL**(前推面):`world_map.proto` 加 `EventRequest`(0x0305:x/y/dir/event_type/seqno)/ `EventResult`(0x0306:seqno/ok);
  重跑 `saidl_gen.py`(`ids.h` + `world_map.sa.h` 生成 + `msg_ids.json` 注册)。
- **net**:`SessionHost::onEvent`(照 W.1 `onWalk` 族)· `Session::handleEventRequest`(0x0305 分发,状态门 kOnline,照 `handleWalkRequest`)。
- **world(核心)**:`World::onEvent` 移植 `EVENT_main` 净核 · `triggerNpcEnemyBattle` 明雷开战 · `walkStep` 后加退回门。

#### ① EV 事件驱动:开战是客户端主动发,不是服务端自动(取证纠正)

原版 `EVENT_main`(`event.c:37`)**全仓唯一调用点在 `callfromcli.c:1405`**(即 `lssproto_EV_recv`)⇒ 明雷开战由**客户端发 EV**驱动
(玩家面向明雷格 → 扫面前格事件对象 → 命中 `CHAR_EVENT_ENEMY` → `NPC_NPCEnemy_BattleIn` → `BATTLE_CreateVsEnemy(player,_,enemy)`),
**不是**服务端走路后自动开战。⇒ W.5 建最小 EV 事件通道:`onEvent` 用**权威玩家坐标 + `dir`** 算面前格(不信 `req.x/y`,同 `onWalk` 防瞬移)。
★ 只接 `ENTITY_ENEMY`;`functbl[event]` 通用派发(传送点 `_MAP_WARPPOINT` 等)骨架预留、本批不接。

#### ② 明雷开战用已存在实体,转移所有权(与暗雷 triggerEncounter 的关键区别)

暗雷当场 `spawnEnemyToField`(allocate + `spawnEnemy` 生成)。明雷那只**早在地图上生成好**(W.2)⇒ `triggerNpcEnemyBattle`:
`startBattle` + `joinBattle`(玩家 Side[0])+ `enterEnemyToField(kSideOffset, *已有 Enemy)`,★★ **把 `EntityHandle` 从 `world_enemies`
转移给 `enemy_of_slot`**(不 allocate/不 spawnEnemy/**不耗战斗 rng**)⇒ `enemyCount` 守恒。开战即从 `world_enemies` 移除 + 单向广播消失。
★ 复活 = count 补齐:进战斗腾出名额 ⇒ 下 tick `spawnWorldEnemies` 补(⚠️ 立即补、精确 `REVIVALTIME` 划出)。

#### ③ 撞明雷退回,与开战解耦(移植 char_walk.c:585)

`kCharLoop` 玩家段 `walkStep` 成功后,`worldEnemyAt(新格)` 命中 ⇒ 弹回原格 + `moved=false`(复用撞墙处理,不广播 move)。
★ **退回≠开战**:原版两条独立机制——退回是位置保护(走不进敌人格),开战靠玩家主动发 EV。⚠️ 客户端坐标纠正(`XYD_send:588`)划出(W.1 未建 XYD)。

#### ④ 复验

`ctest` **16/16**(`world_map` 26 → **30 例 / 270 断言**,+4:撞明雷退回 / 面向发 EV 开战+池守恒 / 打空格 ok=false / 战后 count 补齐;
`net_framing` +1:EV 0x0305 的 kOnline 门 + 路由 + 字段解码)· `ci_verify` 六项全过(`SA_WERROR` 清洁构建 0 告警)· `idl_verify`
(schema 改 + 重跑一致)· `dr_table`(§2 加 DT19 + §2.21)· `code_format`。★ **反向验证两处逐条精确转红**:禁退回门 ⇒ **仅**「撞明雷退回」红
(开战用例仍绿 ⇒ 坐实两机制解耦)· 禁 `world_enemies` 移除 ⇒ 开战 + 复活红(退回仍绿),恢复后全绿。★ `enemyCount` 守恒断言正常跑通过即证「转移非新建」。

#### ⑤ ⚠️ 未做 / 登记残缺

| 项 | 归属 |
|---|---|
| NPC `argstr` 脚本门(`gym`/`item`/`startmsg`/`steal`/`deniedmsg`,`NPC_NPCEnemy_BattleIn` 全靠它)| D6 脚本层未落地 |
| 胜利掉落(`NPC_NPCEnemy_Dying`)| 道具域 |
| `gym`→`BATTLE_CreateVsEnemy` mode 2 决斗点场 | PvP / saac 域 |
| 通用事件表(传送点 `_MAP_WARPPOINT` 等)| `EVENT_main` 骨架已留,只接明雷一路 |
| 精确复活时机(`DIETIME`/`REVIVALTIME`)| 状态系统;当前用 count 立即补齐替代 |
| 客户端 EV 实装 + 坐标纠正 XYD | 客户端仓 / W.1 走路同步残缺 |

⚠️★★ **`world_map.proto` 加 EV 两消息 ⇒ `idl/generated` watched 变更 ⇒ 锁定 ref 须前推 `shared-v0.18.0`**(推送窗口待办)。
★ 只在 Apple clang 21 跑过,GCC/MSVC 交 CI。

---

### 9.0.71 批次 W.6 —— 静态事件格与 WARP 传送点(2026-09-25)

2026-09-25 交付。世界系统中引入 WARP 传送点(全游戏实例数最多 NPC 类别，原版 4,297 个实例，`npc_warp.c`)。

#### 1. 源码事实与裁定

1. **触发时机与机制**(原版 `npc_warp.c:112`、`char_walk.c:348-367`):
   - 传送点设置 `CHAR_ISOVERED = 1`，玩家走入格子(walkStep 成功)时触发 `POSTOFUNC` (`NPC_WarpPostOver` → `NPC_WarpWarpCharacter`)。
   - 参数 `arg`: `floor|x|y`。
   - 目标合法性检查(`MAP_IsValidCoordinate`): 越界或不可走格忽略传送。
2. **传送过程(`CHAR_warpToSpecificPoint`, `char.c:4594-4675`)**:
   - 清空剩余路径串(`CHAR_WORKWALKARRAY`)，立即停止当前巡航。
   - 旧位置周围广播 `CharDisappear`，玩家自身接收旧位置实体的 `CharDisappear`。
   - 更新角色坐标到目标点，迁移 `olink` 索引。
   - 新位置周围广播 `CharAppear`，双向刷新新视野内玩家与世界怪物。
   - 给玩家自身下发坐标同步 `CharMove`。
   - 传送落地后不触发该格暗雷遇敌。

#### 2. 验证与指标

- `world_map` 用例数从 30 增至 34，断言数从 368 增至 412。
- **反向验证**: 注入条件恒 false ⇒ 2 条用例精准转红; 恢复后回绿。

---

### 9.0.72 批次 W.7 —— NPC 实体框架与 Healer 恢复员(2026-09-25)

2026-09-25 交付。世界系统中落地基础 NPC 实体框架与首个服务型交互 NPC —— Healer 恢复员(`npc_healer.c` / `npc_windowhealer.c`)。

#### 1. 源码事实与裁定

1. **实体与碰撞属性**:
   - NPC 实体作为不可穿透对象(`CHAR_ISOVERED = 0`)，玩家走路撞上时弹回原格(`moved = false`)，防止与 NPC 格重叠。
   - NPC 视野单向广播: 玩家进视野收到 `CharAppear(entity_type = ENTITY_NPC, image = npc.image)`，出视野收到 `CharDisappear`。
2. **交互与恢复(`NPC_HealerAllHeal`, `npc_healer.c:109-141`)**:
   - 玩家面向 NPC 发起 EV 协议请求(`ENTITY_NPC`)。
   - 费用扣除: 严格通过 `GoldLedger::delGold` 扣除石币(`GoldReason::kHealerFee`)，成为经济系统汇的第一个真实调用者; 余额不足时拒绝，不扣钱不回复。
   - 满状态恢复: 玩家自身及其所有随行宠物(`p->pets`)的 HP 与 MP 全部回满(HP 依据 `deriveBaseStats` 推导的 `max_hp`)。

#### 2. 验证与指标

- `world_map` 用例数从 34 增至 38，断言数从 412 增至 466。
- **反向验证**: 注入错误 HP 增量 ⇒ 2 条用例精准转红; 恢复后回绿。
- **架构守卫**: `check_gold_writes.py` 100% 绿灯，无任何旁路直接修改石币。

---

### 9.0.73 批次 W.8 —— 城镇居民 NPC 对话 (TownPeople) 与 对白/窗口骨架 (2026-09-25)

2026-09-25 交付。世界系统中落地高频城镇居民 NPC 对话机制（全游戏 533 个实例，复用率极高，`npc_townpeople.c`）以及服务端驱动 UI 的窗口消息与回执闭环（`WindowOpen` / `WindowReply`，`lssproto_WN_send` / `WN_recv`，DR-PR3 / DR-PR8）。

#### 1. 源码事实与裁定

1. **面对交互与多文案随机选择 (`npc_townpeople.c:28-52`)**:
   - 玩家面向 `NpcType::kTownPeople` 发起 EV 事件请求（`event_type = ENTITY_NPC`）。
   - NPC 配置支持逗号分隔多条候选文案（例如 `msg1,msg2,msg3`），忠实对应原版 `getStringFromIndexWithDelim(arg, ",", rand()%tokennum+1, token)`。
   - 使用确定性世界随机源 `world_rng.randMod(candidates.size())` 随机选出一句下发。
2. **服务端驱动 UI 窗口协议闭环 (`domain/window.proto`, DR-PR3 / DR-PR8)**:
   - 下行发送 `WindowOpen`（0x0601）：`kind = WINDOW_KIND_MESSAGE`，`buttons = BUTTON_FLAG_OK`，`source = {ENTITY_SOURCE_ENTITY, npc.id}`，消息体 `MessageBody` 支持按 `\n` 切分为多行。
   - 窗口会话状态机：服务端在会话连接上记录 `active_window_id` 与 `active_window_npc_id`，window_id 保证会话内单调递增。
   - 上行处理 `WindowReply`（0x0602）：客户端确认后校验 `reply.window_id == active_window_id`，匹配时闭环销毁活动窗口；不匹配时保持激活，抵御越界与陈旧回执。
3. **碰撞与阻挡**:
   - TownPeople 作为常规 NPC 实体，同样具备 `CHAR_ISOVERED = 0` 不可穿透碰撞阻挡（继承自 W.7 NPC 框架），玩家无法走进其所在格子。

#### 2. 验证与指标

- `world_map` 用例数从 38 增至 **42**（+4 用例），断言数从 466 增至 **639**（+173 断言）。
- **反向验证 (RV-1)**: 注入篡改窗口类型（MESSAGE → LINE_INPUT）⇒ 用例精准转红（1 失败 / 41 通过）；恢复后回绿。
- **反向验证 (RV-2)**: 注入禁用 `WindowReply` 窗口关闭逻辑 ⇒ 窗口闭环用例精准转红（1 失败 / 41 通过）；恢复后回绿。
- **全套静态守卫**: `check_format.py`、`check_shared_purity.py`、`check_module_boundaries.py`、`check_gold_writes.py`、`check_dr_table.py`、`check_docs_index.py` 100% 绿灯。

---

### 9.0.74 批次 W.9 —— 任务旗标空间与 ExChangeMan 基础事件块解析骨架 (2026-09-25)

2026-09-25 交付。世界系统中落地 8.0 规范权威任务旗标位图（`NOWEV` / `ENDEV`，原 `CHAR_NOWEVENT` / `CHAR_ENDEVENT`，`npcutil.c:1277-1377`）与任务核心引擎 ExChangeMan 基础事件块解析与交互闭环（全游戏 771 个文件，EventNo 方言唯一消费者，`npc_exchangeman.c`，`09-npc-event-dsl.md`）。

#### 1. 源码事实与裁定

1. **权威任务旗标空间 (09 §7.1-§7.4, C28-C31)**:
   - 8.0 权威规格：`NOWEV`（进行中）与 `ENDEV`（已完成）各由 8 槽 × 32 位 = 256 位无符号位图构成（编号范围 0..255），投产数据实测最大用到 226。
   - 严格边界防御 (C30)：约定 `-1` 旗标号代表无旗标/不占旗标（可无限触发），读写安全返回 `false`；显式判定 `shiftbit < 0 || shiftbit >= 256` 严格防御越界，根除原版无上界检查覆写相邻数据字段的历史漏洞。
   - 清除语义修复 (C31)：清除采用确定的位操作 `&= ~(1U << shift)`，修复原版清除路径使用 XOR 导致 0 位误被置 1 的缺陷。
2. **ExChangeMan 脚本解析与前置门 (09 §2.3 C5, §3.1 C9, §4 C20-C21)**:
   - 块划分与字段提取：按 `EventEnd` 分块，支持 `EventNo`、`TYPE`、`EVENT`、`AcceptMsg`、`ThanksMsg`、`EndSetFlg`、`CleanFlg`、`NomalMsg`、`NomalWindowMsg` 等 key 的解析。
   - 前置门求值顺序：
     1. 若 `event_no != -1` 且已完成（`hasEndEvent(event_no)` 为 true），跳过该块；
     2. 条件表达式求值：支持 `LV`（<, >, !=, =）、`NOWEV`（=, !=）、`ENDEV`（=, !=），短路与 `&`，逗号 `,` 分支选择器（命中分支返回 1-based 序号）；
     3. 若所有分支均不满足，跳过该块。
3. **TYPE 分派与窗口会话确认闭环**:
   - `kMessage`：立即执行旗标副作用（`EndSetFlg` 置完成且清当前，`CleanFlg` 彻底清除，无 EndSetFlg 时标记为当前任务 `setNowEvent`），下发 `WindowOpen`（`BUTTON_FLAG_OK`）；
   - `kAccept`：弹出接取/确认窗（`BUTTON_FLAG_YES | BUTTON_FLAG_NO`），在连接上记录待决 ExChange 上下文；收到客户端 `WindowReply` 且按键为 YES 时才结算旗标副作用，并下发 `ThanksMsg` 窗口；若按 NO 则取消且不改变任何旗标；
   - 兜底对白：若所有事件块都不满足，从 `NomalMainMsg` 的逗号候选列表中随机摇选一句下发。

#### 2. 验证与指标

- `world_map` 用例数从 42 增至 **47**（+5 用例），断言数从 639 增至 **1336**（+697 断言）。
- **反向验证 (RV-1)**: 篡改任务旗标上界检查（`>= 256` → `> 256`）⇒ 边界保护测试精准转红（1 失败 / 46 通过）；恢复后回绿。
- **反向验证 (RV-2)**: 篡改条件求值器中的 `NOWEV` 比较逻辑 ⇒ 条件表达式求值测试与全链测试精准转红（2 失败 / 45 通过）；恢复后回绿。
- **反向验证 (RV-3)**: 篡改 `WindowReply` 处理中 ACCEPT 确认后的 `setNowEvent` 副作用 ⇒ 任务全周期测试精准转红（1 失败 / 46 通过）；恢复后回绿。
- **全套静态守卫**: `check_format.py`、`check_shared_purity.py`、`check_module_boundaries.py`、`check_gold_writes.py`、`check_dr_table.py`、`check_docs_index.py` 100% 绿灯。

---

### 9.0.75 批次 W.10 —— ExChangeMan 道具/宠物交付与奖励结算 (2026-09-25)

2026-09-25 交付。世界系统中落地 ExChangeMan 道具与宠物交付、石币手续费扣除与奖励结算全流程（`npc_exchangeman.c`，`09-npc-event-dsl.md`），实现任务系统与玩家背包（Inventory）、随行宠物槽（PetSlots）、经济账本（GoldLedger）的全面联动。

#### 1. 源码事实与裁定

1. **实体模型扩展 (`shared/model/Pet.h`)**:
   - `Pet` 结构引入 `std::int32_t pet_id = 0;`（对应原版 `CHAR_PETID`，同时与 `Enemy::pet_id` 对齐），供任务交付与奖励条件判断；保持 POD、`kKind` 编译期常量与零分配契约。
2. **经济账本唯一入口守卫 (`GoldReason::kQuestReward`, `kQuestFee`)**:
   - 随身石币写点严格通过 `GoldLedger` 唯一入口（`delGold` 扣除 `DelStone`，`addGold` 给予 `GetStone`）；
   - 守卫脚本 `check_gold_writes.py`（ctest `gold_writes`）100% 保持通过，杜绝任何裸写与静默旁路。
3. **DSL 表达式与前置门判断扩展 (`ExChangeMan.cpp`, `09-npc-event-dsl.md` §3-§4)**:
   - 解析器支持 `GetItem`, `DelItem`, `GetPet`, `DelPet`, `GetStone`, `DelStone`, `ItemFullMsg`, `PetFullMsg`, `StoneLessMsg`, `StoneFullMsg`；
   - 表达式求值扩展：`PET`（按 petid 与等级/数量判定）、`ITEM`（按 item_id 与数量堆叠判定）、`reITEM`/`rePET`（剩余空位判定）、`GOLD`（石币比较）；
   - 前置门容量保护：
     - 石币不足门：`p.gold < del_stone` 时阻断并下发 `StoneLessMsg`；
     - 石币超限门：`p.gold + get_stone > maxHaveGold(0)` 时阻断并下发 `StoneFullMsg`；
     - 背包容量门：`free_item_slots + del_item_slots < get_item_slots` 时阻断并下发 `ItemFullMsg`；因考虑 `del_item_slots` 释放格，原生支持“以物易物”的满包置换；
     - 宠物容量门：`free_pet_slots + del_pet_slots < get_pet_slots` 时阻断并下发 `PetFullMsg`；同样支持满槽换宠。
4. **副作用结算与 EVDEL 动态解析 (`World.cpp`)**:
   - 支持 `DelItem: EVDEL` 与 `DelPet: EVDEL` 从命中分支的 `EVENT` 表达式中动态反解需扣除的道具和宠物；
   - `applyExChangeEffects` 原子执行：扣除/增加石币、扣除/给予道具（落背包与道具池）、扣除/给予宠物（落宠物槽与宠物池）、置位 `EndSetFlg`、清除 `CleanFlg`；
   - 在 `kMessage` 即时交互与 `kAccept` 确认回执（YES/NO 状态机）双路径中统一闭环。

#### 2. 验证与指标

- `world_map` 用例数从 47 增至 **52**（+5 专项用例），断言数从 1336 增至 **1531**（+195 断言）。
- **反向验证 (RV-1)**: 篡改背包满检查逻辑（绕过检查）⇒ `W.10: ExChangeMan 背包满拦截` 测试精准报红（1 失败 / 51 通过）；恢复后回绿。
- **反向验证 (RV-2)**: 篡改石币不足检查逻辑（绕过检查）⇒ `W.10: ExChangeMan 石币不足与超限拦截` 测试精准报红（1 失败 / 51 通过）；恢复后回绿。
- **全套静态守卫**: `check_format.py`、`check_shared_purity.py`、`check_module_boundaries.py`、`check_gold_writes.py`、`check_dr_table.py`、`check_docs_index.py`、22 项 ctest 全量绿灯。

---

### 9.0.76 批次 W.11 —— 世界 NPC 巡逻与随机移动漫游 (2026-09-25)

2026-09-25 交付。世界系统中落地石器 8.0 规范的 NPC 巡逻（Route Patrol）与自由游荡漫游（Random Wanderer）引擎（`char.c:5910-5935`、`npcutil.c:283-302` `NPC_Util_getDirFromTwoPoint`、`npctemplate.c`），实现了静态城镇与野外 NPC 到动态世界实体的转变。

#### 1. 源码事实与裁定

1. **实体模型扩展 (`src/world/include/world/Api.h`)**:
   - 私有扩展 `NpcPoint` 坐标点与 `NpcEntity` 巡逻/漫游参数：`wander_radius`（游荡半径）、`wander_interval_ms`（步进间隔节拍）、`born_x, born_y`（出生锚点）、`next_wander_at_ms`（下一次移动调度时间戳）、`route`（固定路点序列）与 `route_index`（当前寻路路点索引）；
   - 裁定保持在 `world/` 内部，纯属服务端私有世界实体，不污染 `shared/`，无需升级共享库 tag。
2. **循序寻径与 8 方向离散化 (`World.cpp`, `npcutil.c:283-302`)**:
   - 固定路点循环巡逻：1:1 移植原版 `NPC_Util_getDirFromTwoPoint` 的 `dirtable[3][3]` 离散化映射表 `{ {7,0,1}, {6,-1,2}, {5,4,3} }`，精确将 $\Delta x, \Delta y$ 差分映射为 8 向枚举；到达当前路点时循环切换至下一路点（闭环巡逻）；
   - 自由游荡漫游：受限于以 `(born_x, born_y)` 为中心的 `wander_radius` 切比雪夫范围，周期性随机选取 8 方向步进。
3. **双向不可穿透性与阻挡转向 (CHAR_ISOVERED=0)**:
   - 与 W.7 玩家撞 NPC 阻挡对称，NPC 移动受四重守卫拦截：地图通行门（含斜向墙角保护）、撞其他 NPC 阻挡、撞在线玩家实体阻挡、撞世界明雷阻挡；
   - 遇阻挡时不产生位移，但依据原版行为转向目标方向并向视野广播转向，保持生动的世界表现。
4. **视野广播与对话打断锁定**:
   - 单向广播契约：沿用视野对称差分，向视野重叠区玩家广播 `CharMove`（`ENTITY_NPC`），新进入视野玩家广播 `CharAppear`，离开发 `CharDisappear`；
   - 对话打断锁（`isNpcEngagedInDialog`）：若有玩家正在与该 NPC 交互（窗口处于打开态），NPC 暂停走动并延后调度节拍，防止交互期间 NPC 擅自离去；窗口关闭后自然恢复巡逻。
5. **条数制摊还调度纪律 (`_CHAR_LOOP_TIME` 关)**:
   - 严守石器 8.0 规范，在 `World::tick` 第 5c 步按 `tempo.enemy_move_num` 条数制游标切片摊还，杜绝主循环单 tick 卡顿。

#### 2. 验证与指标

- `world_map` 用例数从 52 增至 **57**（+5 专项用例），断言数从 1531 增至 **1651**（+120 断言）。
- **反向验证 (RV-1)**: 篡改阻挡检查逻辑（绕过玩家实体阻挡检查）⇒ `W.11: 实体与地形阻挡不可穿透` 测试精准报红（2 失败 / 55 通过）；恢复后回绿。
- **反向验证 (RV-2)**: 篡改对话打断逻辑（绕过 `isNpcEngagedInDialog`）⇒ `W.11: 对话打断锁定` 测试精准报红（2 失败 / 56 通过）；恢复后回绿。
- **全套静态守卫**: `check_format.py`、`check_shared_purity.py`、`check_module_boundaries.py`、`check_gold_writes.py`、`check_dr_table.py`、`check_docs_index.py`、22 项 ctest 全量绿灯。

---

### 9.0.77 批次 W.12 —— NPC 商店与道具交易系统 (2026-09-25)

2026-09-25 交付。世界系统中落地石器 8.0 规范的 NPC 商店与道具交易系统（`npc_itemshop.c`、`07-npc-quest.md` §0.1、`08-economy.md` §7.2），实现了从玩家触发商店对话、查看结构化货物列表（`WindowOpen::ShopBody`）、买入结算到向商店出售回收（`sellItemToShop`）的全流程闭环。

#### 1. 源码事实与裁定

1. **实体与协议契约 (`src/world/include/world/Api.h`, `idl/schema/domain/window.proto`)**:
   - `NpcType::kShop` 确立为一等 NPC 类型（原版 `CHAR_TYPESHOP`，308 个投产实例）；
   - 商店数据定义：`ShopProduct`（`item_id`, `cost`, `image_id`, `level`, `name`）、`shop_products` 货物列表、`buy_rate`（买入倍率，默认 1.0）、`sell_rate`（回收倍率，默认 0.2）、`shop_name`、`main_msg`、`stone_less_msg`、`item_full_msg` 与 `stone_full_msg`；
   - 对齐 IDL `ShopBody` 与 `ShopHeader`：使用显式 `entry_id`（1-based 序号，非位置耦合），下发 `WINDOW_KIND_ITEM_SHOP`（11）。
2. **购买交易与原子状态机 (`World.cpp`, `npc_itemshop.c:488-680`)**:
   - 会话状态机：记录 `pending_shop.npc_id`，在收到 `WindowReply`（`ENTRY_ID`）时进入结算；
   - 门 1（石币门）：`p->gold < price` 时阻断并下发 `stone_less_msg`；
   - 门 2（容量门）：`p->findFreeItemSlot() < 0` 时阻断并下发 `item_full_msg`；
   - 事务结算：扣除石币严格经唯一入口 `delGold(*p, GoldReason::kShopBuy, price, ...)`；背包分配新道具（`Item` 实例落池，`giveItemIntoPlayer`）并提示购买成功。
3. **出售回收与上限保护 (`World::sellItemToShop`, `npc_itemshop.c:1037-1100`, DR-EC3)**:
   - 距离守卫：检查玩家与 NPC 距离 $\le 3$ 格；
   - 回收定价：以道具 `cost`（或商店同名道具基准价）$\times sell\_rate$ 计价（保底 1 石币），支持堆叠数倍增；
   - 溢出拦截门：若 `p->gold + total_price > maxHaveGold(0)`，下发 `stone_full_msg` 并拒绝交易，**严守 DR-EC3 拒绝原则：不移除道具、不改余额、零静默销毁**；
   - 事务结算：背包清槽并释放道具池（`items.release`），增加石币严格经唯一入口 `addGold(*p, GoldReason::kShopSell, total_price, ...)`。
4. **经济账本纪律 (`check_gold_writes.py`)**:
   - `GoldReason` 追加 `kShopBuy`（汇）与 `kShopSell`（源），`gold_writes` 门禁 100% 保持通过，杜绝任何裸写。

#### 2. 验证与指标

- `world_map` 用例数从 57 增至 **62**（+5 专项用例），断言数从 1651 增至 **1689**（+38 断言）。
- **反向验证 (RV-1)**: 篡改购买石币检查逻辑（绕过余额检查）⇒ `W.12: 商店购买石币不足拦截` 测试精准报红（2 失败 / 61 通过）；恢复后回绿。
- **反向验证 (RV-2)**: 篡改出售石币上限检查逻辑（绕过 `maxHaveGold` 检查）⇒ `W.12: 商店回收出售石币超上限拦截` 测试精准报红（4 失败 / 61 通过）；恢复后回绿。
- **全套静态守卫**: `check_format.py`、`check_shared_purity.py`、`check_module_boundaries.py`、`check_gold_writes.py`、`check_dr_table.py`、`check_docs_index.py`、22 项 ctest 全量绿灯。

---

### 9.0.78 批次 W.13 —— 宠物商店与宠物技能商人 (2026-09-25)

2026-09-25 交付。世界系统中落地石器 8.0 规范的宠物商店与宠物技能商人系统（`npc_petshop.c`、`npc_petskillshop.c`、`07-npc-quest.md` §0.1、`08-economy.md` §7.2），实现了从玩家触发宠物商店/技能导师对话、在售宠物列表与技能导师下发（`WindowOpen::ShopBody`）、买入宠物（`buyPetFromShop`）、出售宠物回收（`sellPetToShop`）到向导师学习宠物技能（`learnPetSkill`）的全流程闭环。

#### 1. 源码事实与裁定

1. **实体与协议契约 (`src/world/include/world/Api.h`, `idl/schema/domain/window.proto`)**:
   - `NpcType::kPetShop`（4）与 `NpcType::kPetSkillShop`（5）确立为一等 NPC 类型（原版 `CHAR_TYPESTONESHOP` 中细分子类，对应原版 `npc_petshop.c` 与 `npc_petskillshop.c`）；
   - 数据结构定义：
     - `PetProduct`（在售宠物：`pet_id`, `name`, `level`, `cost`, `image`, `hp`, `mp`, `vital`, `str`, `tough`, `dex`）；
     - `PetSkillProduct`（教授技能：`skill_id`, `name`, `cost`, `level` 需求等级）；
     - `NpcEntity` 扩展对应配置列表及提示语（`pet_full_msg`, `level_low_msg`, `skill_full_msg` 等）；
   - 协议复用：利用已生成的 `WindowOpen::ShopBody` 结构化下发，宠物商店下发 `WINDOW_KIND_ITEM_SHOP`，技能导师下发 `WINDOW_KIND_PET_SKILL_SHOP`（13）。
2. **宠物商店购买与出售状态机 (`World::buyPetFromShop`, `World::sellPetToShop`, `npc_petshop.c:210-900`)**:
   - 购买宠物门槛：石币是否充足（门 1）、宠物栏是否有空位（门 2，`findFreePetSlot() >= 0`）、宠物池分配（门 3，`pets.allocate()`）；
   - 购买结算：经 `GoldLedger` 扣除 `kPetShopBuy` 费用，填充宠物完整四维、名字与外观图号，反向挂接主人句柄并插入玩家宠物槽；
   - 回收出售门槛：距离检查 $\le 3$ 格，有效宠物槽与有效实体；
   - 回收定价与上限：以在售基准价（或 `level * 100`）$\times sell\_rate$ 计价；若 `p->gold + price > maxHaveGold(0)`，下发 `stone_full_msg` 并拒绝交易（DR-EC3 拒绝原则：宠物不释放、金币零改动）；
   - 出售结算：释放宠物实体（`clearPetSlot` + `pets.release`），经 `GoldLedger` 增加 `kPetShopSell` 石币。
3. **宠物技能学习与导师机制 (`World::learnPetSkill`, `npc_petskillshop.c:83-168`)**:
   - 门 1（等级门）：`pet->level >= skill.level`，等级不足下发 `level_low_msg` 阻断；
   - 门 2（重复门）：检查宠物现有 7 个技能槽，已习得该技能不可重复学习；
   - 门 3（槽位门）：支持指定槽位或自动寻址首个空槽位（`pet_skills[i] <= 0`），槽满下发 `skill_full_msg` 阻断；
   - 门 4（学费门）：石币检查与 `delGold(kPetSkillFee)` 严格记账；
   - 学习结算：技能 ID 原子写入 `pet->pet_skills[target_slot]`，下发成功提示。
4. **经济账本纪律 (`check_gold_writes.py`)**:
   - `GoldReason` 追加 `kPetShopBuy`（汇）、`kPetShopSell`（源）与 `kPetSkillFee`（汇），`gold_writes` 门禁 100% 保持通过。

#### 2. 验证与指标

- `world_map` 用例数从 62 增至 **67**（+5 专项用例），断言数从 1689 增至 **1766**（+77 断言）。
- **反向验证 (RV-1)**: 篡改学技能等级判定逻辑（绕过 `pet->level < target_prod->level`）⇒ `W.13: 宠物学习技能等级不足与学满拦截` 测试精准报红（7 失败 / 9 通过）；恢复后回绿。
- **反向验证 (RV-2)**: 篡改买宠宠物槽满判定逻辑（绕过 `pet_slot < 0`）⇒ `W.13: 宠物商店购买宠物栏已满拦截` 测试精准报红（2 失败 / 10 通过）；恢复后回绿。
- **全套静态守卫**: `check_format.py`、`check_shared_purity.py`、`check_module_boundaries.py`、`check_gold_writes.py`、`check_dr_table.py`、`check_docs_index.py`、22 项 ctest 全量绿灯。

---

### 9.0.79 批次 W.14 —— 告示牌与传送员 NPC (2026-09-25)

2026-09-25 交付。世界系统中落地石器 8.0 规范的告示牌与传送员 NPC（`npc_signboard.c`、`npc_warpman.c`、`07-npc-quest.md` §0.1、`13-d8-coverage.md` 行 50、94），实现了从玩家触发告示牌交互展示公告标题与告示对白（`WINDOW_KIND_MESSAGE` + `BUTTON_FLAG_OK`）、传送员单目的地确认弹窗（Yes/No）、多目的地列表选项选择（`WINDOW_KIND_SELECT`，含路费标注与等级/石币置灰）、路费扣除（`kWarpFee`）、等级与通行门禁到双向视野切换与自身瞬移同步的全流程闭环。

#### 1. 源码事实与裁定

1. **实体与协议契约 (`src/world/include/world/Api.h`, `idl/schema/domain/window.proto`)**:
   - `NpcType::kSignBoard`（6）与 `NpcType::kWarpMan`（7）确立为一等 NPC 类型（对应原版 `npc_signboard.c` 与 `npc_warpman.c`，投产 230 实例与 324 实例）；
   - 数据结构定义：
     - `WarpDestination`（传送目的地：`floor`, `x`, `y`, `name`, `cost`, `level`）；
     - `NpcEntity` 扩展对应配置字段：`sign_title`（告示牌标题，默认 `"＜　看板　＞"`）、`warp_destinations` 目的地列表与 `warp_msg`；
   - 协议复用：利用已生成的 `WindowOpen` 结构化下发，告示牌下发 `WINDOW_KIND_MESSAGE`（OK 按钮）；传送员单目的地弹出确认 `WINDOW_KIND_MESSAGE`（Yes/No 按钮），多目的地弹出 `WINDOW_KIND_SELECT`（携带目的地名称、路费与可用状态）。
2. **告示牌对话与交互 (`World.cpp`, `npc_signboard.c:45-80`)**:
   - 触发时自动拼接 `sign_title` 与告示正文 `message`（以换行符 `\n` 切分为多行），下发 `WINDOW_KIND_MESSAGE`；
   - 客户端回执 OK 后直接闭环窗口状态机。
3. **传送员确认、选择与门禁拦截 (`World::warpPlayerByNpc`, `npc_warpman.c:120-450`)**:
   - 门 1（距离门）：检查玩家与 NPC 距离 $\le 3$ 格；
   - 门 2（等级门）：`p->level >= dest.level`，等级不足下发 `level_low_msg` 阻断；
   - 门 3（路费门）：`p->gold >= dest.cost`，石币不足下发 `stone_less_msg` 阻断；
   - 门 4（通行门）：`map.inBounds(dest.x, dest.y) && mapWalkable(...)`，不可通行阻断；
   - 传送结算：扣除路费严格经唯一入口 `delGold(*p, GoldReason::kWarpFee, dest.cost, ...)`；
   - 瞬移执行（`warpPlayer`）：旧格 `olink` 摘除、旧视野双向 `CharDisappear` 广播、坐标更新、新格 `olink` 挂接、新视野 `broadcastSpawn` / 敌人与 NPC 刷新、向自身下发 `CharMove` 坐标同步。
4. **经济账本纪律 (`check_gold_writes.py`)**:
   - `GoldReason` 追加 `kWarpFee`（汇），`gold_writes` 门禁 100% 保持通过，绝不旁路裸写石币。

#### 2. 验证与指标

- `world_map` 用例数从 67 增至 **72**（+5 专项用例），断言数从 1766 增至 **1843**（+77 断言）。
- **反向验证 (RV-1)**: 篡改传送路费门禁检查逻辑（绕过 `p->gold < dest.cost`）⇒ `W.14: 传送员路费不足拦截` 测试精准报红（1 失败 / 10 通过）；恢复后回绿。
- **反向验证 (RV-2)**: 篡改传送等级门禁检查逻辑（绕过 `p->level < dest.level`）⇒ `W.14: 传送员等级不足拦截` 测试精准报红（7 失败 / 4 通过）；恢复后回绿。
- **全套静态守卫**: `check_format.py`、`check_shared_purity.py`、`check_module_boundaries.py`、`check_gold_writes.py`、`check_dr_table.py`、`check_docs_index.py`、22 项 ctest 全量绿灯。

---

### 9.0.80 批次 D.1 —— 真实地图 LS2MAP 原生解析与萨伊那斯 NPC 数据接入 (2026-09-25)

2026-09-25 交付。开启 D 线（数据与内容管线）进阶工程，实现了石器时代原始地图二进制文件（`LS2MAP`）的原生零拷贝/安全大端解析器，并在离线构建管线 `tools/build_playable_content.py` 中实现了对石器 8.0 真实 NPC 数据（`csa8.0/gmsv/data/npc/`）的完整扫描与实例化抽取。成功将 100 号地图（萨伊那斯大地图）全部 59 个真实 Warp 传送点与 48 个世界 NPC 实体（含 19 个告示牌/留言板、10 个 ExChangeMan 任务事件、3 个 WarpMan 传送员、3 个客运向导/村民、2 个医院恢复员、1 个采石权利书商店等）导入版本化内容资产 `content/p2-v1/world.json`，在服务端入口 `src/main.cpp` 中完成原子化装载，并通过了端到端实景交互验证。

#### 1. 源码事实与裁定

1. **原生 LS2MAP 解析器 (`src/world/include/world/Api.h`, `src/world/Map.cpp`)**:
   - 依据 `10-world-map.md` §3.1 二进制规约：
     - `+0` char[6] = `"LS2MAP"` 魔数；
     - `+6` u16 BE `id`（地图 Floor 编号）；
     - `+8` char[32] `showstring`（地图显示名，GBK 编码或字面，截取至 `\0`）；
     - `+40` u16 BE `xsiz`、`+42` u16 BE `ysiz`（宽度与高度）；
     - `+44` u16 BE `tile[xsiz * ysiz]`（地表层图元）；
     - `+44 + 2 * count` u16 BE `obj[xsiz * ysiz]`（物件层图元）；
   - 提供 `parseLs2Map(std::span<const uint8_t>)` 与 `loadLs2MapFile(const std::string &)`；
   - 包含尺寸下限、魔数、尺寸越界、缓冲区截断等严格防御性检查；针对大端字节序按 `(b[0] << 8) | b[1]` 规范解码。
2. **离线构建管线数据抽取 (`tools/build_playable_content.py`)**:
   - 遵循 D 线纪律（`04-storage-schema.md` §7.1、`backlog/02-deferred-scope.md` §3）：“终点是构建管线 bundle，不直接在运行时动态读几千个原始 txt”；
   - 实现 `extract_floor_100_npcs`：全量扫描 `csa8.0/gmsv/data/npc` 下 71 个 create 文件，解析并抽取 Floor 100 对应的 107 个配置块：
     - 59 个 `npcgen_warp` 传送点（含萨姆吉尔、玛丽那丝、柯奥、达那、迷宫与副本入口）；
     - 48 个 NPC 实体：SignBoard（告示牌）、Dengon（留言板）、WarpMan（传送员）、Shop（道具商店）、WindowHealer（恢复员）、ExChangeMan（任务使者）、TownPeople（长毛象客运向导）；
     - 解析引用的 `.arg` 脚本，抽取对白、标题、目的地、买卖倍率、`itemset6.txt` 关联商品（采石权利书 2475-2477）以及任务条件；
   - 生成具有确定性排序与哈希可重现性的 `world.json` 与 `manifest.json`，同步产出双端 assets。
3. **运行时原子装载对接 (`src/main.cpp`)**:
   - 在 `configureContent` 中解析 `bundle.world` 的 `"warp_points"` 并调用 `world.loadWarpPoints(...)`；
   - 解析 `"npcs"` 实体列表，将各类 NPC 映射至 `NpcEntity` 并调用 `world.loadNpcEntities(...)`；
   - 对 `ExChangeMan` 的任务脚本直接复用已验证的 `parseExChangeBlocks` 原生解析器。
4. **跨图传送与单场景解耦 (`src/world/World.cpp`)**:
   - 重构 `kCharLoop` 中 Warp 传送触发逻辑，复用统一的 `warpPlayer` 实现；
   - 修复跨图传送判定：同图（`dst_floor == p->floor`）校验当前图元通行性，跨图（`dst_floor != p->floor`）由目标图管辖，解除对异图坐标在本地图上的越界阻断。

#### 2. 验证与指标

- `world_map` 用例数从 72 增至 **74**（+2 组大型实战用例），断言数从 1843 增至 **1896**（+53 断言）。
- **原生 LS2MAP 二进制解析用例**:
  - 防御性异常覆盖：空缓冲、短头截断（43 字节）、错误魔数（"LS1MAP"）、宽高为 0、数据段截断（少 1 字节）；
  - 内存缓冲区大端解码正确性验证；
  - 本地真实 `sainasu` 地图文件全量加载验证（id=100, 800x800, 640,000 tiles, 640,000 objs, `(643, 459)` 界内判定）。
- **萨伊那斯 Floor 100 传送点与 NPC 端到端验证**:
  - 装载 59 个传送点与 48 个 NPC 实体；
  - 玩家向东跨步触发 (638, 491) 传送点，成功瞬移至 (1000, 50, 116)；
  - 玩家面对 (728, 501) 告示牌交互，成功获取并弹窗《第1检查点》告示正文；
  - 玩家面对 (343, 464) 医院恢复员交互，受损 HP/MP 成功全额回复。
- **反向验证 (RV-1)**: 反转 `parseLs2Map` 魔数校验逻辑（`memcmp == 0` 返回 nullopt）⇒ `原生 LS2MAP 地图解析` 报红致命错误；恢复后回绿。
- **反向验证 (RV-2)**: 故意跳过 `world.loadWarpPoints` 注入 ⇒ 传送触发断言报红（未瞬移，停留在 (100, 638, 491)）；恢复后回绿。
- **全套静态守卫**: 22 项 ctest 全量绿灯，`ci_verify.py` 全部 6 项门禁通过。

### 9.0.81 批次 D.2: 四大村庄全量地图与 NPC/Warp 批量导入与多地图管理 (2026-09-25)

2026-09-25 交付。在批次 D.1 基础上推进多地图运行时架构（`Multi-Floor World Map`），实现了石器时代四大村庄（1000 萨姆吉尔村、2000 玛丽娜丝渔村、3000 加加村、4000 卡鲁它那村）可走阻挡图的离线构建与运行时独立管理。离线管线 `tools/build_playable_content.py` 完成了 5 大核心区域全量 164 个 Warp 传送点与 254 个世界 NPC 实体的全量扫描与属性提取，解决了官方历史脚本中 `borncenter` 矩形中心半径解析、`ItemList` 逗号与连字符混排、字段错位容错等多项数据缺陷。服务端不仅支持跨地图坐标系解耦与视野分层严格隔离，还实现了萨伊那斯与萨姆吉尔村之间真实双向传送闭环以及四大村庄 NPC 真实服务交互。

#### 1. 源码事实与裁定

1. **多地图运行时管理体系 (`src/world/include/world/Api.h`, `src/world/World.cpp`)**:
   - `World::Impl` 引入 `FloorState`（内含独立 `GridMap` 与专属 `olink` 空间二维表）；
   - 暴露 `loadFloorMap(int32_t floor_id, GridMap map)`、`findFloorMap(int32_t floor_id)`、`floorMapCount()` C++ API；
   - ⚠️ 默认单图向前兼容：未显式注册的 Floor 自动回退至 `s.map` 与 `s.olink`，既有单图用例与测试无需任何适配；
   - 实现零依赖、防越界、无异常逃逸的标准 Base64 解码器 `decodeBase64`。
2. **跨地图视野完全隔离与双向传送重构 (`src/world/World.cpp`)**:
   - `collectVisible`、`collectVisiblePlayers`、`broadcastDespawn`、`broadcastEnemyDespawn` 全面挂接目标 `floor` 参数，严格通过对应图层的 `olink` 索引扫格，彻底阻断跨 Floor 坐标重叠带来的视野泄漏；
   - `warpPlayer` 完善旧图 `olink` 摘除、旧图视野双向 `CharDisappear` 广播、目标图坐标校验与新图 `olink` 挂接；跨图传送时将参考坐标置为 `-1000, -1000` 全量重置目标图的 NPC 与世界怪物视野；
   - 扩展 `kCharLoop` 玩家移动碰撞与传送门逻辑：玩家在当前 `p->floor` 的专属地图上漫步，踩中 Warp 传送点时自动校验目标 Floor 是否在目标图界内且可通行；`warpPlayerByNpc` 同步支持目标图层通行校验。
3. **离线构建管线全量数据抽取 (`tools/build_playable_content.py`)**:
   - `extract_world_content` 抽取 4 大村庄二进制 LS2MAP 地图（1000 萨姆吉尔村 160x160、2000 玛丽娜丝渔村 150x150、3000 加加村 150x150、4000 卡鲁它那村 150x150），结合 `mapset.txt` 图元属性表计算可走阻挡图，紧凑 Base64 编码存入 `world.json` 的 `floors` 列表；
   - 全量扫描 5 大区域（100, 1000, 2000, 3000, 4000）全部 NPC create 脚本，产出 **164** 个 Warp 传送点与 **254** 个世界 NPC（71 other、66 exchangeman、55 townpeople、41 signboard、11 warpman、8 shop、2 healer）；
   - 防御性解析裁定：
     - `borncenter` 语义对齐原版 C 源码（`cx, cy, w, h`），消除以前误当作 corner 导致的 8,800+ 虚拟格子膨胀问题；
     - `ItemList` 支持逗号与连字符混排（如 `13053,13088-13092,20176`）；
     - `shop_m2.create` 中 `dir=多多的传言板` 非数值容错恢复为 NPC 名字。
4. **运行时原子装载 (`src/main.cpp`)**:
   - 在 `configureContent` 中解析 `bundle.world` 的 `"floors"` 列表并调用 `world.loadFloorMap(...)`，完成服务端启动时多地图全量装载。

#### 2. 验证与指标

- `world_map` 用例数从 74 增至 **75**（+1 组大型多地图实测用例），断言数从 1896 增至 **1952**（+56 断言）。
- **Base64 编解码器与多地图管理验证**:
  - RFC 4648 向量（空串、"TWFu"->"Man"、Padding、换行忽略）测试全部通过；
  - 四大村庄地图尺寸（1000: 160x160, 2000/3000/4000: 150x150）与 `floorMapCount() == 4` 校验通过。
- **跨地图视野完全隔离验证**:
  - 玩家 A 位于 Floor 100 (20, 20)，玩家 B 位于 Floor 1000 (20, 20)；
  - 验证 B 传送离开时双方收到对端的 `CharDisappear`；
  - 验证 A 在 Floor 100 移动时，B 未收到任何 `CharMove` 或 `CharAppear`；B 在 Floor 1000 移动时，A 同样零广播泄漏。
- **双向跨图传送与村庄真实 NPC 交互验证**:
  - 玩家在萨伊那斯 (100, 638, 491) 踩传送点瞬移至萨姆吉尔村 (1000, 50, 116)；
  - 玩家在萨姆吉尔村向西移动踩入 (1000, 49, 116)，成功反向瞬移回萨伊那斯 (100, 637, 491)，双向传送闭环达成；
  - 加加村「多多的传言板」(SignBoard at 50, 62) 与卡鲁它那村「特产品贩卖员」(Shop at 36, 70) 交互成功下发对话与商店窗口。
- **反向验证 (RV-1)**: 篡改 `getFloor` 恒返回 `nullptr`（所有 Floor 错误共享主地图）⇒ `findFloorMap` 断言与跨图移动碰撞检测立即失败；恢复后回绿。
- **反向验证 (RV-2)**: 篡改 `kCharLoop` 的 Warp 触发条件（强制为 false）⇒ 传送瞬移断言（未瞬移，停留在 (100, 638, 491)）立即失败；恢复后回绿。
- **全套静态守卫**: 22 项 CTest 全量通过，`python3 tools/ci_verify.py` 全部 6 项门禁通过。

---

### 9.0.88 批次 D.3: 加鲁卡南岛、附属村庄与深渊地下城多地图深化与客户端窗口/战斗表现补齐 (2026-09-26)

2026-09-26 交付。在批次 D.2 基础上推进世界地图全景深化与客户端交互演进（`World Map Depth & Client Scene Enhancement`），实现了石器时代核心世界版图南岛加鲁卡（Floor 200，800×1200 巨幅主岛）、南岛四大附属村庄（3100 塔姆塔姆村、3200 多多村、3300 乌鲁力村、3400 奇喀喀村）以及多层深渊地下城（20801..20807 琉璃地下城 / 龙的巢穴 1F..7F、21201 & 21215 碧青的洞窟地下 1F 与 15F）共 18 张地图的离线构建与运行时统一纳管。离线管线 `tools/build_playable_content.py` 完成了全量 311 个 Warp 传送点与 399 个世界 NPC 实体的全量扫描与属性提取，解决了官方历史脚本中 `enemy=npcgen_warp|60341|28|3d` 格式污染容错与非数值过滤。在客户端侧，新增零引擎依赖的 `WindowDispatcher` 统一派发 NPC 文本对话、传送选项选择与商店买卖，并在 `BattlePresenter` 中补齐自爆、畏惧、落马、偷盗、换宠、求援、属性反转等宠物特殊技能表现。

#### 1. 源码事实与裁定

1. **多地图大容量资产与 Bundle 读取扩展 (`runtime/content/Bundle.cpp`)**:
   - 随 18 张地图扩展（包含 800×1200 巨幅地图与地下城），`world.json` 体积自 320KB 增长至 1.9MB；
   - 将 `Bundle::load` 中 `world.json` 静态上限安全提升至 16MB，严格防范溢出并满足全量地图扩展诉求。
2. **离线管线全量 18 图层深度抽取 (`tools/build_playable_content.py`)**:
   - `extract_world_content` 扩展目标图集合至 18 个核心 Floor：`{200, 1000, 2000, 3000, 3100, 3200, 3300, 3400, 4000, 20801..20807, 21201, 21215}`；
   - 抽取 311 个 Warp 传送点与 399 个世界 NPC（包含 TownPeople、Shop、Healer、SignBoard、WarpMan 等）；
   - 防御性解析修复：增加 `.isdigit()` 严格校验，滤除官方脚本中偶现的脏字段（如 `3d` 坐标）。
3. **客户端场景窗口分发器 (`stone-age-client/src/scenes/WindowDispatcher.{h,cpp}`)**:
   - 贯彻零引擎依赖与 D2 纯粹性（仅依赖 IDL 与标准库，可在无图形 CI 环境下直接运行）；
   - 统一承接 `WindowOpen`（`MESSAGE`、`SHOP`、`SELECT`），结构化拆解行文本、商店条目与选择项；
   - 封装 `createButtonReply`、`createChoiceReply`、`createShopBuyReply` 构建标准 `WindowReply` 并闭环会话。
4. **客户端战斗宠物特殊技能表现 (`stone-age-client/src/battle/BattlePresenter.cpp`)**:
   - 映射 `BattleStatus` 异常状态文本（FEAR、POISON、PARALYSIS、SLEEP、STONE、DRUNK、CONFUSION、WEAKEN、DEEPPOISON、BARRIER、NOCAST）；
   - 完善 `DAMAGE_FLAG_EXPLODE`（自爆）、`KNOCKBACK_STATE`（击退/落马）、`SUMMON`（召唤）、`CALL_COMPANIONS`（求援）、`STEAL`（偷盗）、`PET_SWITCH`（换宠）、`STATUS_TICK`（状态结算）、`REVERSE`（属性反转）的日志格式化与表现层更新。
5. **客户端会话与场景接线 (`ClientSession.cpp`, `GameScene.{h,cpp}`)**:
   - `ClientSession` 接入 `WindowOpen` 解码派发至 `ClientSessionHost::onWindowOpen`，并提供 `sendWindowReply` 发送通道；
   - `GameScene` 在画面正中渲染互动弹窗面板，支持点击选项传送、点击商品购买、点击确认/关闭。

#### 2. 验证与指标

- `world_map` 用例数从 75 增至 **78**（+3 组大型南岛与深渊地下城实测用例），断言数从 1952 增至 **2094**（+142 断言）。
- **南岛加鲁卡与多多村多地图管理实证**:
   - 验证加鲁卡主岛（Floor 200，800×1200 尺寸）与 18 张地图独立加载（`floorMapCount() == 18`）；
   - 验证加鲁卡南岛与多多村（Floor 3200）之间跨图双向 Warp 传送闭环；
   - 验证琉璃地下城龙的巢穴跨层连环传送（200 -> 20801 -> 20802）；
   - 验证多多村真实村民 NPC 对话触发。
- **反向验证 (RV-1)**: 篡改 `world.floorMapCount()` 预期断言为 99 ⇒ `加鲁卡南岛、附属村庄与深渊地下城` 立即报红失败；恢复后回绿。
- **反向验证 (RV-2)**: 篡改反向 Warp 目标 Floor 校验（将 200 篡改为 999）⇒ 双向跨图传送测试立即报红失败；恢复后回绿。
- **客户端测试闭环**: `sa_client_net_test` 增补宠物技能与 NPC 窗口交互用例，CTest 7/7 100% 绿灯。
- **全套静态守卫**: 22 项服务端 CTest + 7 项客户端 CTest 全量通过，双端代码格式校验 100% 绿灯。

---

### 9.0.89 阶段 2: 队伍系统与组队协同 (Party System) (2026-09-26)

2026-09-26 交付。在批次 D.3 真实世界大地图深化与 NPC 交互闭环基础上，实现石器时代原汁原味的队伍系统与组队全域协同（`Party System & World Coordination`）。严格对齐官方 GMSV C 源码（`char_party.c`、`char_walk.c`、`battle.c`），建立了包含队长（Leader）与至多 4 名队员（Member，整队上限 5 人 `kPartyMaxMembers = 5`）的队伍数据结构与生命周期状态机（建队、入队、离队、踢出、队长解散、掉线级联清理）。实现了经典的贪吃蛇足迹跟随算法（`char_walk.c:689-705`）与位移门禁（队员禁止自主移动与独立踩点），完善了队长触发传送点及 WarpMan NPC 传送时全队同步拉取（`map_warppoint.c:270-278`），以及队长触发暗雷与 NPC 明雷时全队无缝切入战斗、5..9 号位宠物默认参战出击、回合指令协同提交与经验结算全员分配的完整闭环。

#### 1. 源码事实与裁定

1. **队伍核心生命周期与数据结构 (`src/world/include/world/Api.h`, `src/world/World.cpp`)**:
   - 对齐 `char_party.c:88-91` 与 `char_party.c:534-545`：
     - 定义 `PartyMode` 枚举（`kNone = 0`, `kLeader = 1`, `kMember = 2`）与队伍容量上限常量 `kPartyMaxMembers = 5`；
     - 引入 `Party` 运行时聚合（`party_id`、`leader`、`members` 变长容器）及 `party_of_session` 双向哈希索引；
     - `joinParty(requester, target)`：若目标为游离玩家，目标自动升格为队长（Leader），发起者成为队员（Member）；若目标已为队长，校验 `members.size() < kPartyMaxMembers` 门禁，满员拒绝；
     - `leaveParty(session)` / `kickPartyMember(leader, member)`：队员主动离队或被踢，若队伍仅剩队长一人则自动解散，队长降格为 `kNone`；若队长离队或掉线（`removeSession` / `onSessionClosed`），整队安全解散，所有队员降格为 `kNone`，杜绝悬挂指针与孤儿队员状态。
2. **贪吃蛇足迹跟随算法与队员自主位移门禁 (`src/world/World.cpp`)**:
   - 对齐 `char_walk.c:924`：在 `onWalk` 与 `kCharLoop` 玩家移动入口处，校验 `partyModeOf(session) == PartyMode::kMember`，严格拦截队员自主位移与转向指令；
   - 对齐 `char_walk.c:689-705` 与 `npcutil.c:280`：
     - 移植 `getDirFromTwoPoints(sx, sy, ex, ey)` 八方向向量推导纯函数；
     - 队长移动时，按入队顺序级联推进队员：前驱者旧坐标作为后继者的移动目标点（`end` 链条传递），各队员依次从当前坐标朝目标点前进一步并更新朝向；
     - 队员移动严格维护地图 `olink` 空间拓扑索引与增量九宫格视野广播（`broadcastMove`、`collectVisible`、`collectVisiblePlayers`），视野外与视野内进出平滑同步。
3. **组队协同传送 (`src/world/World.cpp`)**:
   - 对齐 `map_warppoint.c:270-278`：
     - 重构 `warpPlayer` 为单人原子底层 `warpSinglePlayer` 与组队协调器 `warpPlayer`；
     - 队长踩踏 Warp 传送点或通过 WarpMan NPC 对话传送时，原子校验目标 Floor 与坐标合法性，随后同步将全队队员传送至队长目标点（同一 Floor 及坐标），完成旧图 `olink` 摘除、新图 `olink` 挂接及跨图视野完全重置。
4. **组队战斗无缝切入与全员出战闭环 (`src/world/World.cpp`)**:
   - 对齐 `battle.c:1759-1772`（`BATTLE_PartyNewEntry`）：
     - 队长在野外漫步踩中暗雷（`triggerEncounter`）或与明雷战斗 NPC 交互（`triggerNpcEnemyBattle`）时，自动拉取队伍内全部成员；
     - 队长占据己方 0 号位，队员依次进入 1..4 号位；同时检索各成员当前出战宠物（`findActiveCombatPet`），按官方规则部署至 `slot + 5`（即 5..9 号位）；
     - 将全队成员会话绑定至该战斗实例（`joinBattle` 并广播 `BattleInit`）；战斗中各成员协同提交回合指令，战胜后结算经验值（`playerExp` 递增）并携宠完好返回大世界，队伍关系保持不变。

#### 2. 验证与指标

- `world_map` 用例数从 78 增至 **82**（+4 组完整组队体系实测用例），断言数从 2094 增至 **2236**（+142 断言）。
- **组队生命周期全流程实证**:
  - 验证游离玩家组队、队长升格、队员入队（2 人队）；
  - 验证队伍逐人扩充至 5 人上限；
  - 验证第 6 人申请加入被绝对拒绝（`joinParty == false`，RV-1 容量硬防线）；
  - 验证队长踢人、队员主动离队与解散全链路；
  - 验证队长离线/断开连接时，队伍级联解散且余下队员状态安全恢复为 `kNone`。
- **贪吃蛇足迹跟随移动与位移门禁实证**:
  - 验证队员自主调用 `onWalk` 时被安全拒止，坐标原封不动；
  - 验证 3 人队伍在地图上漫步时，队长向东走一步，队员 1 进驻队长原位，队员 2 进驻队员 1 原位，贪吃蛇足迹传递与朝向推导完全准确；
  - 验证队员视野广播与 `olink` 坐标严密同步。
- **组队协同传送验证**:
  - 验证队长踩萨伊那斯村口传送点传送至萨姆吉尔村时，全队队员同步瞬移至目标图与目标坐标，队员旧图视野注销并接入新图。
- **组队战斗全流程验证**:
  - 验证队长遇敌切入战斗后，两名玩家分别位于战场 0、1 号位，双方宠物位于 5、6 号位；
  - 验证双方协同下达战斗指令，击倒敌人完成战斗结算，全员经验值累加并安然返回大世界。
- **反向验证 (RV-1)**: 篡改队伍最大人数上限（如 `kPartyMaxMembers = 6`）或允许第 6 人入队 ⇒ 满员门禁与边界断言立即变红失败；恢复后回绿。
- **反向验证 (RV-2)**: 篡改组队传送逻辑（仅瞬移队长、不协同瞬移队员）⇒ 队员坐标停留在旧图的断言立即变红失败；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest + 7 项客户端 CTest 全量通过，双端代码格式校验 100% 绿灯。

---

### 9.0.90 阶段 2: 玩家间安全交易系统 (Trade System) 与 决斗切磋 (Duel / PVP System) (2026-09-26)

2026-09-26 交付。在组队系统与世界协同（`Party System`）基础上，依据石器时代官方 GMSV 核心源码（`char/trade.c`、`battle.c:3008 BATTLE_CreateVsPlayer`），实现大世界玩家交互两大核心体系：**玩家间安全交易系统 (Trade System)** 与 **决斗切磋系统 (Duel / PVP System)**。

#### 1. 源码事实与裁定

1. **决斗切磋系统 (Duel / PVP System) (`battle.c:3008 BATTLE_CreateVsPlayer`)**:
   - **严格门禁校验**:
     - 发起者与目标必须在线且存活（`hp > 0`），并在同一地图层内切比雪夫距离 $\le 2$ 格；
     - 双方及所属队伍中任何成员当前处于战斗中均拒绝；
     - 同一队伍内的队员/队长之间绝对禁止决斗互打（对齐官方 `BATTLE_ERR_SAMEPARTY` [RV-1]）；
     - 发起方与目标方若处于组队状态，必须为队长（Leader），队员发起或向非队长队员发起均拒绝（`BATTLE_ERR_NOTLEADER`）。
   - **组队无缝拉取与阵营站位**:
     - Side 0 部署发起方全队（队长 slot 0，队员 slots 1..4），各成员默认出战宠自动入驻 slots 5..9；
     - Side 1 部署目标方全队（队长 slot 10，队员 slots 11..14），各成员默认出战宠自动入驻 slots 15..19；
     - 战场标记为 `is_pvp = true` 与 `dp_battle = true`。
   - **双端玩家协同推进与宠物托管**:
     - 战斗每回合等待双方所有存活人类玩家提交指令（`b.members` 等待判定），未指定指令的出战宠物通过 `autoFillPetCommands` 自动普攻敌方首个存活目标；
     - 战斗结束时，跳过野怪收益交付（`deliverPlayerProfit`），无怪物经验产生，无石币通胀产出；战败方玩家 HP 钳位保护为 $\ge 1$ 点（免于死亡惩罚、掉气与回城复活），双方全员队伍状态完好保留返回大世界。
2. **玩家间安全交易系统 (Trade System) (`char/trade.c`)**:
   - **双向确认防诈状态机**:
     - 交易经历 `kNone` $\to$ `kTrading` $\to$ `kLocked` $\to$ `kConfirmed` $\to$ 原子置换 $\to$ `kNone` 严格状态机；
     - 任一方撤回抵押物或解锁（`unlockTrade`），双方锁定态与确认态全部重置复位，彻底杜绝换货诈骗；
     - 任何一方主动取消、断开连接（`onSessionClosed` / `removeSession`）或切入决斗，交易会话立即安全回滚并清理。
   - **支持多资产类型抵押**:
     - 背包道具（`kStartItemArray..kMaxItemHave - 1`，排他防重复质押与有效性校验）；
     - 随身宠物（slots 0..4，排他防重复质押与有效性校验）；
     - 石币（校验非负且不超过当前持有石币）。
   - **防刷容量三层前置预检 [RV-2]**:
     - 在双方确认最终执行置换前，原子校验：
       1. 背包容量：双方各自 `现有道具 - 质押道具 + 收入道具 <= 45`；
       2. 宠物栏容量：双方各自 `现有宠物 - 质押宠物 + 收入宠物 <= 5`；
       3. 石币上限：双方各自 `现有石币 - 质押石币 + 收入石币 <= maxHaveGold(0)`；
     - 任何一项超限立即阻断并拒绝确认，杜绝爆包吞物与石币溢出损失。
   - **原子互换与默认出战宠重置**:
     - 道具句柄交换、宠物所有权句柄转移、石币通过 `GoldLedger` 双向记账（`kTradeGive` / `kTradeReceive`）；
     - 若玩家质押并交易出去的宠物恰为当前出战宠（`p->default_pet`），自动将其安全重置为 `-1`，杜绝悬挂指针与非法出战。

#### 2. 验证与指标

- `world_map` 用例数从 82 增至 **89**（+7 组决斗与交易全生命周期实测用例），断言数从 2236 增至 **2452**（+216 断言）。
- **实测用例矩阵**:
  1. `PVP 决斗发起门禁: 距离过远/阵亡/跨图/同队互打拦截 [RV-1]`；
  2. `PVP 组队决斗切入: 双方队长发起拉取全队队员(0..4 vs 10..14)与出战宠物(5..9 vs 15..19)`；
  3. `PVP 决斗战斗闭环: 双方玩家指令协同、战败方 HP 钳位为 1 且队伍关系完整返回大世界`；
  4. `交易生命周期: 发起、距离过远拦截、接受、取消与断线回滚`；
  5. `交易抵押物操作: 道具、宠物、石币抵押与锁定/解锁状态机`；
  6. `交易容量防刷门禁 [RV-2]: 接收方背包满/宠物栏满/石币溢出阻断`；
  7. `交易原子互换: 道具、宠物、石币无损原子置换与默认出战宠安全重置`。
- **反向验证 (RV-1)**: 篡改同队不可决斗门禁（允许同队决斗）⇒ RV-1 拦截断言立即变红失败；恢复后回绿。
- **反向验证 (RV-2)**: 篡改背包/宠物/石币容量前置检查（绕过容量校验）⇒ 满背包/满宠栏/石币溢出阻断断言立即变红失败；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest + 7 项客户端 CTest 全量通过，双端代码格式校验 100% 绿灯。

---

### 9.0.91 阶段 2: 核心社交与通信系统: 名片夹与好友 (AddressBook) · 邮件/离线信件 (Mail) · 分级聊天频道 (Chat & Channel) (2026-09-26)

2026-09-26 交付。在组队系统、玩家交易与决斗切磋基础上，依据石器时代官方 GMSV 核心源码（`char/addressbook.c`、`char/petmail.c`、`char/mail.c`、`char/char_talk.c`），实现大世界核心社交网络与通信基础设施三大子系统：**名片夹与好友系统 (AddressBook System)**、**邮件与离线信件系统 (Mail System)** 以及 **分级聊天频道与广播 (Chat & Channel System)**。

#### 1. 源码事实与裁定

1. **名片夹与好友系统 (`addressbook.c`)**:
   - **容量与生命周期**:
     - 每位玩家上限持有 80 张名片（`kMaxAddressBook = 80`，对齐官方 `ADDRESSBOOK_MAX = 80` [RV-1]）；
     - 名片结构包含目标玩家 SessionId/玩家编号、名字、等级、头像造型 ID 以及 `online` 在线状态与 `blocked` 黑名单屏蔽标志。
   - **交换门禁与双向原子互存**:
     - 发起者与目标必须在线且存活（`hp > 0`），非战斗态，同地图切比雪夫距离 $\le 2$ 格；
     - 双向容量校验：任何一方名片数 $\ge 80$ 即刻阻断（[RV-1]）；
     - 防重复校验：已存在对方名片则不可重复发起；
     - 黑名单拦截：若目标已将发起者加入黑名单（`blocked == true`），拒绝名片请求；
     - 接受请求后，双方原子交换名片并立即写入各自的名片夹列表，双向状态同步。
   - **全生命周期上下线在线感知与删除/拉黑**:
     - 玩家登录就绪（`onSessionReady`）时，自动扫描大世界所有已互换名片的好友，广播通知上线状态；
     - 玩家断开连接或退出世界（`removeSession` / `onSessionClosed`）时，向好友列表广播通知下线状态；
     - 名片支持主动删除与黑名单屏蔽（`setAddressCardBlock`）。
2. **邮件与离线信件系统 (`petmail.c` / `mail.c`)**:
   - **邮箱容量与寄信门禁**:
     - 个人信箱上限容量 20 封信件（`kMaxMailBoxSize = 20`，对齐官方设定 [RV-2]）；
     - 发送邮件支持在线收件与离线信件投递（目标玩家不在线时亦可投递至邮箱）；
     - 标题不可为空（最大 64 字节），正文非空约束。
   - **多资产附件无损流转与实体解耦**:
     - 附件支持道具（`SA::Model::Item`）、随身宠物（`SA::Model::Pet`）与石币（`gold`）；
     - 发送时严格剥离扣除：道具从背包卸下并销毁源句柄、宠物从随身栏卸下（若寄出的为当前出战宠自动将 `default_pet` 安全重置为 `-1`），石币通过 `GoldLedger` 统一记账扣除（`GoldReason::kMailSend`）；
     - `MailEntry` 直接存储资产实体值快照，彻底与运行时动态句柄解耦，杜绝邮件滞留占用有限 EntityPool 句柄造成泄露；
   - **防刷容量三层前置预检与防误删保护 [RV-2]**:
     - 提取附件（`takeMailAttachment`）执行严格原子预检：
       1. 背包容量：带道具附件时，玩家背包未满（`< 45`）；
       2. 宠物栏容量：带宠物附件时，随身宠物未满（`< 5`）；
       3. 石币上限：带石币附件时，玩家当前石币 + 附件石币 $\le$ 个人石币持有上限（`maxHaveGold`）；
     - 任何一项超限立即阻断并拒绝提取；提取时重新生成合法实体句柄入包并经 `GoldLedger` 入账（`GoldReason::kMailReceive`）；
     - 防丢保护：信件仍包含未提取的附件时，禁止删除（`deleteMail` 拦截返回 `false`）。
3. **分级聊天频道与广播 (`char_talk.c`)**:
   - **四大核心频道划分**:
     - `kTalkNormal` (0): 普通附近说话，基于 9 格切比雪夫视野半径广播；
     - `kTalkParty` (1): 队伍频道，全队队员跨距离、跨地图实时协同通信（非队内人员不可见）；
     - `kTalkShout` (2): 世界/全服大喊广播，全服所有在线玩家均可接收；
     - `kTalkTell` (3): 点对点私聊密信，支持定向传递，附带离线拦截与黑名单屏蔽阻断（若被拉黑则静默拒收）。
   - **轮询消费机制**:
     - 玩家会话缓存未读聊天消息，通过 `pollChatMessages` 轮询消费并自动清空队列。

#### 2. 验证与指标

- `world_map` 用例数从 89 增至 **96**（+7 组名片、邮件、聊天全场景实测用例），断言数从 2452 增至 **2960**（+508 断言）。
- **实测用例矩阵**:
  1. `名片夹容量上限(80张)与满员拒绝 [RV-1]`；
  2. `名片交换发起、距离门禁、原子双向互存与防重复添加`；
  3. `名片系统上下线在线状态感知与黑名单屏蔽`；
  4. `邮件系统发送与邮箱上限(20封)拦截 [RV-2]`；
  5. `邮件附件资产流转: 道具/宠物/石币寄送剥离与默认出战宠重置`；
  6. `邮件附件提取三层前置校验(背包满/宠栏满/石币超限)与防误删保护`；
  7. `分级聊天频道广播: 视野说话(<=9格)、组队频道跨图同步、世界广播与黑名单私聊拦截`。
- **反向验证 (RV-1)**: 篡改名片夹上限校验（绕过 80 张上限检查）⇒ RV-1 满员拦截断言立即变红失败；恢复后回绿。
- **反向验证 (RV-2)**: 篡改邮箱容量上限校验（绕过 20 封上限检查）⇒ RV-2 满箱拦截断言立即变红失败；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，双端代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

---

### 9.0.92 阶段 2: 家族管理与庄园系统: 家族创建与解散 · 职位任免与50人上限 · 家族金库与贡献 · 四大庄园据点占领 · 家族专属频道 (Family System) (2026-09-26)

2026-09-26 交付。在组队系统、玩家交易、决斗切磋与核心社交系统（名片夹/邮件/聊天）基础上，依据石器时代官方 GMSV 核心源码（`char/family.c`、`include/family.h`、`include/char_base.h:1960`），实现大世界社交体系最高层级组织系统：**家族管理与庄园系统 (Family System)**。

#### 1. 源码事实与裁定

1. **家族创建与解散 (`family.c:346 FAMILY_Add` / `include/family.h`)**:
   - **严格门禁与资费**:
     - 创办者必须在线且存活（`hp > 0`），非战斗态；
     - 等级门禁：创办者等级须达到 30 级（对齐官方 `FMLEADERLV = 30`）；
     - 已有家族检查：当前未加入任何家族；
     - 家族命名合规：长度 1..32 字节，不可含空格，且全服唯一（不可重名）；
     - 创建资费：严格通过 `GoldLedger` 扣除创办人 10,000 随身石币（`GoldReason::kFamilyCreate`）；
   - **创建产物与生命周期**:
     - 分配全服自增唯一 `family_id`；创办人自动成为首任族长（`FamilyRole::kLeader`），初始成员 1 人，家族声望设为 100；
     - 解散（`disbandFamily`）：仅现任族长有权解散；若该家族占领庄园，自动释放庄园占领权，并解除全体成员家族归属与金库数据。
2. **职位权限与 50 人容量上限 (`char_base.h:1960` / `FAMILY_MAXMEMBER = 50`) [RV-1]**:
   - **五层角色体系**:
     - `kNone = -1` (无家族), `kMember = 1` (普通成员), `kApply = 2` (申请入族中), `kLeader = 3` (族长), `kElder = 4` (长老/副族长)；
   - **申请与审批**:
     - 玩家申请入族进入 `applicants` 待审队列（防重复申请拦截）；
     - 族长与长老拥有审批权（`acceptFamilyMember`）；批准入族赋予 `FamilyRole::kMember` 并同步在线/离线会话映射；
     - **满员门禁 [RV-1]**：家族成员达到 50 人上限（`kMaxFamilyMembers = 50`）时，阻断任何新申请与审批入族。
   - **请离与退出**:
     - 族长可请离长老与成员；长老仅可请离普通成员（不可请离同级长老与族长；族长不可被任何人开除）；
     - 主动退出（`leaveFamily`）：普通成员与长老可主动脱离；族长禁止直接退出，必须转让族长或解散家族。
   - **族长转让与职位任免**:
     - 族长可任命普通成员为长老（`kElder`）或降为成员；
     - 族长任命目标为族长（`kLeader`）时触发转让：原族长自动转为长老，目标成为新族长。
3. **家族金库与贡献体系 (`family.c:1973 FAMILY_Bank`) [RV-2]**:
   - **金库存储与上限 [RV-2]**:
     - 任何家族成员均可向家族银行金库注资；
     - 严格通过 `GoldLedger` 扣除随身石币（`GoldReason::kFamilyDeposit`）；
     - 金库持有上限为 100,000,000 石币（1 亿）；存款超出上限即刻阻断拒绝；
     - 每存入 1000 石币，存入者获得 1 点个人贡献，家族获得 1 点声望。
   - **金库取款与随身上限前置预检 [RV-2]**:
     - 仅族长与长老拥有取款权限；
     - 校验金库余额充足；
     - 预检取款人随身石币上限容量（`player->gold + amount <= maxHaveGold(0)`），防止因溢出造成资产损失；
     - 严格通过 `GoldLedger` 存入随身石币（`GoldReason::kFamilyWithdraw`）。
4. **四大庄园据点占领 (`include/family.h:30 MANORNUM = 4`)**:
   - 支持萨姆吉尔庄园（`kSamo`）、玛丽娜斯庄园（`kMarina`）、加加庄园（`kJaja`）、卡鲁它那庄园（`kKarutana`）四大庄园；
   - 互斥占领机制：一个家族占领新庄园自动释放旧庄园；庄园被新家族占领则原占领家族据点自动剥夺转为 `kNone`。
5. **家族专属通信频道 (`ChatChannel::kTalkFamily`)**:
   - 扩展聊天系统支持家族专属频道（`kTalkFamily`）；
   - 仅本家族成员可发言与接收；跨地图、跨距离全服广播；非家族成员或其它家族成员完全隔离不可见。
6. **全生命周期上下线在线状态感知**:
   - 家族成员掉线（`removeSession` / `onSessionClosed`）自动标记为离线态；
   - 成员重新上线登录（`onSessionReady`）自动根据角色名检索家族归属，恢复在线状态并更新最新等级与造型。

#### 2. 验证与指标

- `world_map` 用例数从 96 增至 **103**（+7 组家族全系统全流程实测用例），断言数从 2960 增至 **3147**（+187 断言）。
- **实测用例矩阵**:
  1. `家族创建门禁: 30级门限、10000石币扣除、重名与名称合法性校验`；
  2. `家族申请与审批及满员门禁 [RV-1]`；
  3. `家族职位任免、族长转让与请离成员`；
  4. `家族主动退出门禁: 族长不可退与普通成员解绑`；
  5. `家族金库存储、提取与容量双向校验 [RV-2]`；
  6. `家族四大庄园据点占领与争夺`；
  7. `家族跨图专属聊天频道 (kTalkFamily) 与上下线感知`。
- **反向验证 (RV-1)**: 篡改家族 50 人满员判定（允许无限制扩招）⇒ RV-1 满员拦截断言立即变红失败；恢复后回绿。
- **反向验证 (RV-2)**: 篡改金库 1 亿上限校验（绕过金库上限检查）⇒ RV-2 满库拦截断言立即变红失败；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，双端代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

---

### 9.0.93 阶段 2: 玩家摆摊与拍卖市场系统: 原地摆摊与状态锁定 · 寄售上架与100挂牌费 · 5%交易税与资产解耦 · 离线与溢出系统邮件到账保全 (Street Stall & Consignment Market System) (2026-09-26)

2026-09-26 交付。在组队系统、玩家安全交易、决斗切磋、核心社交系统与家族庄园系统基础上，依据石器时代官方 GMSV 核心源码（`char/char.c:9057 CHAR_sendStreetVendor`、`char/char_talk.c`、`include/char.h`），实现大世界去中心化与中心化结合的双轨交易流通体系：**玩家摆摊系统 (Street Stall System)** 与 **寄售拍卖市场系统 (Consignment & Auction Market System)**。

#### 1. 源码事实与裁定

1. **玩家摆摊系统 (`char.c:9057 CHAR_sendStreetVendor` / `STREET_VENDOR`)**:
   - **原地开摊与货架配置 (`openStall`, `setStallItem`, `setStallPet`)**:
     - 摆摊门禁：摊主必须在线且存活（`hp > 0`），非战斗态，无队伍（`PartyMode::kNone`），非交易态；
     - 货架容量限制：最多支持 10 件道具（`kMaxStallItemSlots = 10`）与 3 只宠物（`kMaxStallPetSlots = 3`）；
     - 支持标价石币与招牌标题设定；出战宠上架时，自动安全重置 `default_pet = -1`，杜绝参战悬空引用。
   - **正式出摊与营业态门禁 (`startStallVending`, `isPlayerVending`)**:
     - 必须至少有一件在售道具或宠物；重新强校验在售商品依然存在于背包/随身宠栏中；
     - 正式进入营业态后（`isPlayerVending`），摊主原地锁定：`onWalk` 立即清空走路队列并拦截移动，禁止发起或接受组队邀请（`joinParty` 拦截），禁止发起或接受交易请求（`requestTrade` 拦截）。
   - **大世界摊位检视与购买 (`nearbyStalls`, `buyFromStall`)**:
     - 附近玩家检视摊位（切比雪夫九宫格距离扫描）；
     - 购买切比雪夫距离 $\le 3$ 格严格门禁；
     - **买家容量前置阻断 [RV-1]**：购买道具前预检买家背包有空位（`findFreeItemSlot() >= 0`），购买宠物前预检买家宠物栏有空位（`findFreePetSlot() >= 0`），满包/满宠栏直接阻断，买家石币分文未扣；
     - 随身石币严格经 `GoldLedger` 扣除与结算（`kMarketBuy` / `kMarketSellEarn`）；
     - **卖家溢出补偿投递 [RV-2]**：若卖家随身石币达到上限（1,000,000 石币）发生截断（`GoldDisposition::kClamped`），超额部分石币自动转存为系统补偿邮件附件投递至卖家邮箱；
     - 货架全部售空后摊位自动安全收摊（`closeStall`）。
2. **寄售与拍卖市场系统 (Consignment & Auction Market System)**:
   - **跨会话中心化离线流通**:
     - 全服中心化寄售行，支持离线挂售与全服资产流转。
   - **上架挂牌与资产严格解耦 (`listMarketItem`, `listMarketPet`)**:
     - 严格收取 100 石币挂牌费（`kMarketListingFee = 100`，`GoldReason::kMarketListFee`）；
     - 卖家在售上限控制：每名玩家最多同时在售 10 件挂牌（`kMaxMarketListingsPerPlayer = 10`），超限拒绝；
     - 出战宠挂牌安全重置 `default_pet = -1`；
     - **资产解耦与快照化**：上架成功后生成只读资产快照（`Item` / `Pet`），原 `EntityPool` 槽位立即释放（`items.release` / `pets.release`），原背包/宠栏清空，彻底杜绝句柄悬空与池泄漏。
   - **全服检索与跨玩家购买 (`searchMarket`, `buyMarketListing`)**:
     - 支持关键字模糊检索与资产类别（道具/宠物）类型过滤；
     - 自买自卖严格阻断；
     - **买家容量前置阻断 [RV-1]**：买家背包满或宠物栏满直接阻断交易；
     - **5% 成交税与多渠道结算 [RV-2]**：买家扣除标价石币（`kMarketBuy`），系统扣除 5% 成交税（`kMarketTaxFee` 并写入审计流），净收益（95%）结算给卖家：
       - 若卖家在线且随身容量充足，直接通过 `GoldLedger` 入账（`kMarketSellEarn`）；
       - 若卖家随身石币溢出，溢出部分自动转存为系统邮件附件；
       - 若卖家离线，全部净收益直接打包为系统邮件（发件人 "拍卖市场"）投递至卖家邮箱。
   - **下架撤回与资产返还 (`cancelMarketListing`)**:
     - 仅卖家本人可下架撤回；
     - 若卖家背包/宠栏有空位，原样还原实体句柄入包/入栏；
     - 若卖家背包/宠栏已满或处于受限状态，物品/宠物自动打包为系统邮件附件退回至邮箱。

#### 2. 验证与指标

- `world_map` 用例数从 103 增至 **110**（+7 组摆摊与拍卖市场全流程实测用例），断言数从 3147 增至 **3350**（+203 断言）。
- **实测用例矩阵**:
  1. `玩家摆摊生命周期与移动/组队/交易拦截门禁`；
  2. `玩家摆摊购买与出战宠重置`；
  3. `摆摊购买买家容量前置阻断 [RV-1] 与距离门禁`；
  4. `拍卖市场挂牌、100石币挂牌费扣除与资产严格解耦`；
  5. `拍卖市场模糊检索与跨玩家购买结算 (5% 成交税)`；
  6. `拍卖市场离线卖家与随身溢出转存系统邮件保全 [RV-2]`；
  7. `拍卖市场商品撤回下架与退还 (满包退回系统邮件)`。
- **反向验证 (RV-1)**: 篡改买家容量前置判定（允许满包继续购买）⇒ RV-1 满包拦截断言立即变红失败（`CHECK_FALSE(true)` 与 `CHECK(9800 == 10000)`）；恢复后回绿。
- **反向验证 (RV-2)**: 篡改溢出石币邮件补投逻辑（禁用溢出邮件投递）⇒ RV-2 邮件到账断言立即变红失败（`REQUIRE(1 >= 2)`）；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，客户端 7 项 CTest 全量通过，双端代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

---

### 9.0.94 阶段 2: 骑乘系统: 资质门限与濒死拦截 · 复合外观切换与下马还原 · 出战宠互斥 · 战斗生命分摊与战后血量回写 · 资产流转脱钩与庄园特权联动 (Ride System) (2026-09-26)

2026-09-26 交付。依据石器时代官方 GMSV 核心源码（`char/char.c:3990-4075`、`char/char_base.c:50`、`include/char_base.h:1567 tagRidePetTable`、`char/family.c:2440`、`npc_riderman.c`、`battle.c:5492`），在服务端核心大世界循环与战斗系统之间落地实现**骑乘系统 (Ride System)**。

#### 1. 源码事实与裁定

1. **骑乘资质与庄园特权门限 (`canPlayerRide`, `grantRidePermit`, `hasRidePermit`, `revokeRidePermit`)**:
   - **普通资质**: 玩家持有通用骑乘学习证（`hasRidePermit(session, "骑乘学习证")` / `"骑乘许可"`）或特约骑宠证（匹配宠物名称或特定品系）；
   - **庄园特权**: 四大庄园占领家族成员享有对应特约骑宠骑乘特权，无需学习证即可骑乘：
     - 萨姆吉尔庄园 (`FamilyManor::kSamo`): 暴龙系（暴龙、巴朵兰恩、左迪洛斯、奥卡洛斯、帖拉所伊朵、红暴、机暴等）；
     - 玛丽娜斯庄园 (`FamilyManor::kMarina`): 虎系（虎、佩露夏、贝鲁卡、格鲁西斯、贝鲁伊卡等）；
     - 加加庄园 (`FamilyManor::kJaja`): 飞龙 / 加美系（飞龙、加美、朵拉比斯、飞飞、布伊、加宝格、扑扑等）；
     - 卡鲁它那庄园 (`FamilyManor::kKarutana`): 雷龙系（雷龙、布拉奇多斯、布鲁顿、斯天多斯、邦恩多斯等）；
   - **[RV-1] 濒死门禁**: 骑宠生命值 `hp <= 0` 时，严格阻断上马，无论是否持有许可证或庄园特权均不可骑乘。

2. **骑乘生命周期与状态管理 (`mountPet`, `dismountPet`, `isPlayerRiding`, `playerRidePetSlot`, `getPlayerRideInfo`)**:
   - **外观切换与精准还原**: 上马记录玩家原外观 `original_image`，依据源码公式 `100700 + ((player_image >= 100000 ? player_image - 100000 : player_image) % 500) + (pet_image % 100)` 计算复合骑乘外观 `ride_image` 并切换角色图号；下马时精准还原 `original_image`；
   - **出战宠互斥防护**: 上马的宠物若是当前出战宠（`default_pet == pet_slot`），自动重置 `default_pet = -1`，杜绝既作为座骑又作为独立战斗单位出战的逻辑冲突；
   - **战斗与营业态保护**: 战斗中或摆摊营业中禁止手动上马。

3. **大世界资产流转安全脱钩**:
   - 寄售上架（`listMarketPet`）、摆摊上架（`setStallPet`）、交易放置（`offerTradePet`）、邮件寄送（`sendMail`）、宠物商店出售（`sellPetToShop`）等资产转移操作触发时，若目标宠物为当前骑乘宠，自动触发下马（`dismountPet`）并恢复人物初始外观；
   - 玩家下线或连接关闭（`removeSession` / `onSessionClosed`）时，自动安全解骑并清理会话临时资质状态。

4. **战斗生命分摊、战后血量回写与酒醉欠债清偿 [RV-2]**:
   - **进战投影**: `makePlayerCombatant` 接收骑宠实体，填充 `c.has_ride = true` 与骑宠四维/三围/血量（`ride_hp`, `ride_max_hp`, `ride_attack`, `ride_defense`, `ride_vital`, `ride_str`, `ride_tough`, `ride_dex`）；
   - **战斗实例绑定**: `BattleInstance` 扩充 `ride_pet_of_slot` 稳定句柄数组，在 `joinBattle` 时精准绑定主人骑宠；
   - **生命分摊与落马**: 战斗中受到攻击时经 `splitRideDamage` 自动分摊伤害；受重击落马（`rollFallGround`）或骑宠死亡时，`c.has_ride` 变为 false；
   - **[RV-2] 战后血量同步**: 战后 `syncPetState` 遍历 `ride_pet_of_slot`，将战斗剩余血量 `ride_hp` 严格同步回玩家真实 `Pet` 实体；若战后 `pet->hp <= 0` 或已落马，战斗结束时自动在世界态下马恢复人身外观；
   - **酒醉欠债清偿**: 修复 `World.cpp:1858-1869` 历史欠债，对齐原版 `battle.c:5492`，骑乘状态解除酒醉时敏捷恢复由 `c.quick *= 2` 调整为 `c.quick += c.ride_dex`。

#### 2. 验证与指标

- `world_map` 用例数从 110 增至 **117**（+7 组骑乘系统全流程实测用例），断言数从 3350 增至 **3512**（+162 断言）。
- **实测用例矩阵**:
  1. `骑乘资质门限与 [RV-1] 濒死拦截`；
  2. `骑乘复合外观切换与下马精准还原`；
  3. `出战宠与骑宠互斥防护`；
  4. `大世界资产流转安全脱钩 (寄售/摆摊/交易/邮件/离线)`；
  5. `战斗人宠生命分摊与 [RV-2] 战后血量回写`；
  6. `战中骑宠濒死战后自动下马恢复人身外观`；
  7. `庄园骑宠特权与庄园易主联动`。
- **反向验证 (RV-1)**: 篡改濒死门禁（禁用 `pet->hp <= 0` 拦截）⇒ 用例 1 中 5 处断言全部变红失败；恢复后回绿。
- **反向验证 (RV-2)**: 篡改战后状态回写（绕过 `syncPetState` 中 `ride_pet->hp` 写回）⇒ 用例 5 与用例 6 断言立即变红失败（`CHECK(500 < 500)` 与 `CHECK(1 == 0)`）；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，客户端 8 项 CTest 全量通过（锁定 `shared-v0.42.0` 零漂移），双端代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

---

### 9.0.95 阶段 2: 选角流程与多角色槽位系统: 2槽位查询与选角 · 槽位超限拦截 · 12种原型与48种外观头像映射 · 初始四维与属性分配校验 · 四大新手村出生地 · 跨图恢复登入 (Multi-character Slots & Creation Flow) (2026-09-26)

- **日期**: 2026-09-26
- **模块**: world / session_storage / net
- **源码依据**:
  - `char/char.c:118-195` (`CHAR_makeCharFromOptionAtCreate`)
  - `char/char.c:254-370` (`CHAR_createNewChar`)
  - `char/char_data.c:228-268` (`CHAR_playerImageNumber`, `CHAR_checkPlayerImageNumber`)
  - `char/char_data.c:285-310` (`CHAR_checkFaceImageNumber`)
  - `char/char_data.c:932-965` (`CHAR_getInitElderPosition`, `elders`)
  - `callfromcli.c:186-296` (`lssproto_CreateNewChar_recv`)
  - `include/anim_tbl.h:7-55` (`SPR_001em..SPR_114em`, `CG_CHR_MAKE_FACE`)

#### 1. 业务逻辑与规则对齐

1. **账号多角色槽位管理与查询/进入 (`LoginResult`, `SelectCharacterRequest`, `CharacterSummary`)**:
   - 契约 `FixedVec<CharacterSummary, 2>` 原生支持单账号至多 2 个角色槽位；
   - 登录成功返回已创建角色列表（`char_id`, `name`, `level`, `image`）；
   - 客户端展示多角色并按 `char_id` 选择进入大世界，支持重登与角色切换。

2. **[RV-1] 槽位已满新建角色拦截**:
   - 对齐 MySQL 存储层 `slot >= 2` 阻断规则，当账号已拥有 2 个角色时，客户端再次发起 `CreateCharacterRequest` 时由存储层返回 `ACCOUNT_CONFLICT`，并在 `World::processStorage` 中精确转送给客户端，保持会话停留在 `kSelectingChar` 选角态，大世界玩家数保持为 0。

3. **12 种角色原型与 48 种配色外观校验与头像映射 (`isValidPlayerImage`, `computeFaceImage`)**:
   - 12 种人物原型（小男孩、少年1/2/3、青年、壮汉、小女孩、少女1/2/3、御姐、熟女），每种 4 种颜色变种（绿/黄/蓝/红等），共计 48 种合法基础形象：`100000 + k * 5`（`0 <= k < 48`，范围 `100000..100235`）；
   - 动态头像映射公式：`30000 + (k / 4) * 100 + (k % 4) * 25`，生成对应 48 种人物头像（`30000..31175`）；
   - 门禁校验：提交非 48 种合法形象且非配置默认图号时，服务端本地直接拦截返回 `ACCOUNT_INVALID`，不向存储层提交。

4. **[RV-2] 初始四维与地水火风属性点分配校验**:
   - 初始四维自由分配：体力 `vital`、腕力 `str`、耐力 `tough`、敏捷 `dex`；
   - 门禁规则：单项属性范围 `[0, 20]`，且四项点数总和必须严格等于 20（`points == 20`）；换算系数 `* 100`，初始生命值严格按 `Rules::deriveBaseStats` 推导计算；
   - 元素属性自由分配：地水火风四属性单项范围 `[0, 10]`，四项总和严格等于 10（`elements == 10`），互克属性不可同时存在（`!(earth > 0 && fire > 0) && !(water > 0 && wind > 0)`），最多选择 2 项属性；换算系数 `* 10`；
   - 违规分配（点数超标、点数不足、负数、超过 20、相克共存、超过 2 种属性）服务端本地立即拦截返回 `ACCOUNT_INVALID`。

5. **四大新手村出生地分配与兜底 (`setHometownSpawn`, `hometownSpawn`, `setSessionHometown`, `sessionHometown`)**:
   - 支持设置与查询四大新手村初始出生点：玛丽娜斯村 (0: Floor 1000/1006)、萨姆吉尔村 (1: Floor 2000/2006)、加加村 (2: Floor 3000/3006)、卡鲁它那村 (3: Floor 4000/4006)；
   - 创建角色时若设置新手村且目标楼层可通行，角色初始落点自动设定至对应村庄；未设置或越界时平滑兜底至 `character_defaults`。

6. **在线中禁止选角与建角防护**:
   - 针对在线角色状态（`conn.char_id != 0` 或 `kOnline`），若收到选角或建角请求，直接拦截返回 `ACCOUNT_CONFLICT`，且协议层状态机对越权报文进行拦截与保护。

7. **多楼层大世界跨图角色恢复登入 (`World::Impl::install`)**:
   - 消除单一楼层硬编码限制，支持在已注册的多楼层大世界地图（如 Floor 1000、Floor 2000 等）恢复登录并载入视野索引。

#### 2. 验证与指标

- `world_persistence` 用例数从 8 组增至 **15 组**（+7 组选角流程实测用例），断言数从 194 条增至 **377 条**（+183 条断言）。
- **实测用例矩阵**:
  1. `选角流程: 账号多角色槽位查询与按 ID 选角进入`
  2. `选角流程: [RV-1] 槽位已满新建角色被阻断 (ACCOUNT_CONFLICT)`
  3. `角色创建: 12 种原型与 48 种配色外观合法性与头像映射`
  4. `角色创建: [RV-2] 初始四维与地水火风属性点分配合法性校验`
  5. `选角流程: 四大新手村出生地分配与兜底`
  6. `选角流程: 在线中收到选角/建角请求被冲突阻断 (ACCOUNT_CONFLICT)`
  7. `选角流程: 多楼层大世界跨图角色恢复登入`
- **反向验证 (RV-1)**: 篡改模拟槽位满时错误放行（返回 `ACCOUNT_OK`）⇒ 用例 2 中 `CHECK(res->code == Code::ACCOUNT_CONFLICT)` 与会话状态断言立即变红失败（`CHECK(1 == 5)` 与 `CHECK(5 == 3)`）；恢复后回绿。
- **反向验证 (RV-2)**: 篡改四维总和校验门禁（如放行 `points == 22`）⇒ 用例 4 中 15 处断言立即集体变红失败；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，客户端 8 项 CTest 全量通过（锁定 `shared-v0.42.0` 零漂移），双端代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

---

### 9.0.96 阶段 2: 宠物融合与转生系统 (Pet Fusion & Rebirth) (2026-09-26)

2026-09-26 交付。对齐官方石器时代源码（`char_base.c:509-567`、`char_data.c:1550-1618`、`char_data.c:1701-1804`、`enemy.c:1851-1960`、`npc_petfusion.c`、`docs/06-progression.md` §4 与 §6、`docs/11-decision-register.md` DR-DT4），落地实现了经典石器时代的**宠物融合 (Pet Fusion)** 与 **宠物转生 (Pet Rebirth / Transmigration)** 两大成长进阶系统，涵盖三表投影算法、历史 off-by-one 属性决策、四维成长继承、等级惩罚、技能遗传过滤、转生五次方公式与 Fx 档位衰减算力、全状态互斥拦截门禁与 NPC 交互闭环。

#### 1. 源码事实与裁定

1. **三表串联投影目标宠物 (`getPetFusionBase2`, `getPetFusionBase1`, `lookupPetFusionTarget`, `resolvePetFusionResultId`)**:
   - `PetTable[29][29]`（`char_base.c:509`）：以副宠 1 与主宠的融合码 `fusion_code` 投影出 `base2`（取值域 $0..10$）；
   - `PropertyTable[4][4]`（`char_base.c:547`）：以副宠 1 与主宠的属性（地水火风）投影出 `base1`（取值域 $0..15$）；遵守 DR-DT4 历史 off-by-one 决策（`k1 <= 1 ? Property[0] : Property[rand() % (k1-1)]`），双属性宠第 2 属性不参与融合判定；
   - `FusionTable[11][16]`（`char_base.c:555`，`csa8.0/gmsv/data/oldfusion.txt` 11×16 逐元素副本）：由 `[base2][base1]` 索引查出产物融合宠模板 ID（$989..1033$）。
2. **四维成长继承算法与技能遗传过滤 (`calculateFusionGrowth`, `calculateFusionSkills`)**:
   - **等级惩罚**：参与融合的宠物等级若 $< 80$ 级，其基础四维成长系数乘以 $0.8$（原版 `base[i] = base[i] * 0.8`）；
   - **权重融合**：单副宠贡献为 $40\%$（`sub1 * 0.4`），双副宠贡献为平均值的 $40\%$（`((sub1 + sub2)/2) * 0.4`），主宠贡献为 $60\%$（`main * 0.6`），合成最终成长并 clamp 在 $[5, 60]$；
   - **技能遗传**：主宠 7 槽技能优先遗传，副宠技能去重填补剩余空槽；过滤不可遗传非法技能（`illegalpetskill[15]`：41, 52, 600..604, 614, 617, 628, 630, 631, 635, 638, 641）以及 0 与空槽。
3. **[RV-1] 融合/转生资格与状态互斥防御拦截 (`fusePets`, `reincarnatePet`)**:
   - **已融合宠拦截**：已是融合宠（`is_fusion == true`）禁止再次作为主宠或副宠参与融合，返回 `kAlreadyFused`；
   - **融合码非法拦截**：融合码为 $-1$ 的宠物禁止融合，返回 `kIneligibleFusionCode`；
   - **出战/骑乘互斥拦截**：当前出战宠（`default_pet`）或正在骑乘中的宠物禁止参与融合或转生，返回 `kPetInBattleOrRide`；
   - **摆摊货架互斥拦截**：摆摊货架上的宠物禁止参与融合或转生，返回 `kPetInTradeOrStall`；
   - **转生等级门禁**：转生宠物等级必须 $\ge 100$ 级，未达标返回 `kInsufficientLevel`；
   - **转生上限门禁**：最多允许 2 转（0 转升 1 转，1 转升 2 转），已达 2 转再次转生返回 `kMaxTransReached`。
4. **[RV-2] 转生五次方公式与 Fx 档位算力精确还原 (`calculatePetTransAns`, `calculatePetTransStats`)**:
   - 原版 `NPC_PetTransManGetAns` 1:1 算力：
     $$\text{total} = \left(\frac{\text{total1}}{100}\right)^5 \times 1.3$$
     $$\text{Fx} = \lfloor(5 - \text{pet\_rank}) \times 1.2\rfloor + 5$$
     $$\text{ans} = \lfloor\text{total}\rfloor + \text{total2} + \frac{\min(130, \text{level}) - 100}{\text{Fx}}$$
   - 0 转上限 150，1 转上限 200；
   - 原版 `PETTRANS_PetTransManStatus` 1:1 加权四维重构：
     $$\text{growth}[i] = \frac{\text{ans} \times (\text{base}[i] + \text{work}[i] \times 4)}{\text{total1} + \text{work\_total} \times 4}$$
   - 转生后等级重置为 1 级，经验清零，生命值按 `deriveBaseStats` 重新推导生成，转生次数 $+1$；若有辅助宠（如玛蕾菲雅），辅助宠被原子消耗扣除。
5. **NPC 对白与交互闭环**:
   - 增加 NPC 类型 `kPetFusionMan = 9`（宠物融合师）与 `kPetTransMan = 10`（宠物转生师），支持大世界对话与操作引导。

#### 2. 验证与指标

- `world_map` 用例数从 117 组增至 **125 组**（+8 组宠物融合与转生全流程实测用例），断言数从 3512 条增至 **3650 条**（+138 条断言，100% 成功）。
- **实测用例矩阵**:
  1. `宠物融合与转生: 三表投影矩阵与目标宠物模板匹配`
  2. `宠物融合与转生: 资质继承算法与等级削弱惩罚`
  3. `宠物融合与转生: [RV-2] 转生五次方公式与 Fx 档位算力精确验证`
  4. `宠物融合与转生: 大世界两宠与三宠融合全流程闭环`
  5. `宠物融合与转生: [RV-1] 融合资格门限与状态互斥防御拦截`
  6. `宠物融合与转生: [RV-1] 转生等级门禁、最大转生次数与状态互斥`
  7. `宠物融合与转生: 转生消耗辅助宠与资质大幅重构飞跃`
  8. `宠物融合与转生: 融合师与转生师 NPC 对白与交互`
- **反向验证 (RV-1)**:
  - 篡改放行已融合宠再次融合与放行未满 100 级转生 ⇒ 用例 5 与 6 中 7 处防御断言立即全部报红失败；恢复后回绿。
- **反向验证 (RV-2)**:
  - 篡改 `FusionTable` 查表结果与 `calculatePetTransAns` 五次方倍率 ⇒ 用例 3 与相关断言立即精准报红失败；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，客户端 8 项 CTest 全量通过（锁定 `shared-v0.42.0` 零漂移），双端代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

### 9.0.97 ExChangeMan Script Expansion (阶段 2 任务引擎脚本全景扩展)

> **对应架构设计**: `stoneage-plan/docs/09-npc-event-dsl.md` §2-§5，`10-world-map.md` 阶段 2 扩展  
> **核心交付**: 任务引擎复合条件解析器 (Recursive Descent Parser)、全量条件变量求值上下文 (`EventCheckContext`)、全动作集执行闭环 (`applyExChangeEffects`)、多步对话树推进 (`next_block_index`) 与委托/清除状态机 (`kRequest` / `kClean`)。

#### 1. 核心设计与落地产出

1. **递归下降复合条件解析器 (`ExprParser`)**:
   - 彻底超越扁平原子合取，实现完整的递归下降语法分析器，支持 `&`（合取）、`|`（析取）、`!`（非）与任意嵌套深度的圆括号优先级 `(...)`；
   - 顶层逗号分支扫描（`splitTopLevelBranches`）保护圆括号内逗号，严防参数切分误伤；
   - 关系运算符全量支持：`=`、`!=`、`<`、`>`、`<=`、`>=`（优先匹配双字符运算符再匹配单字符，彻底消除前缀贪婪匹配错位）。
2. **变量表全面扩展 (`evaluateAtom`)**:
   - **角色基础与成长**: 等级（`LV`）、转生（`TRANS`/`TRANS7`）、声望（`FAME`）、家族（`FM`/`FAMILY`）、职业（`PROF`/`CLASS`/`PROFESSION`）、石币（`GOLD`/`gold`）、生命法力点数（`HP`/`MP`/`SP`/`SKCP`）；
   - **背包与宠物容量**: 道具空槽（`reITEM`）、宠物空槽（`rePET`）；
   - **道具数量语法**: `ITEM=id*count` (持有 $\ge count$)、`ITEM=id^count` (持有 $== count$)，并兼容 `ITEM*count=id` 与 `ITEM^count=id`；
   - **宠物持有语法**: 官方三段式 `PET<op><level>-<petid>[*<count>]` 与 `PET=id*level*count`，以及数量关系 `PET*count=id`、`PET^count=id`，并严格排除 `rePET` 前缀误判；
   - **任务旗标 256 位空间**: `NOWEV=bit`、`!NOWEV=bit`、`ENDEV=bit`、`!ENDEV=bit`，以及冒号语法 `NOWEV:bit=val`、`ENDEV:bit=val`，并带越界安全防护。
3. **全动作集（Action / Effect）原子执行闭环 (`applyExChangeEffects`)**:
   - 经验奖励（`AddExps`/`AddExp`）、技能点奖励（`AddSkillPoint`/`AddPFSkillPoint`）；
   - 生命与法力恢复（`Heal`/`HealHp`/`HealMp`，基于 `deriveBaseStats` 约束且防御 0 上限）；
   - 声望奖扣（`AddFame`/`DelFame`）；
   - 空间传送（`NpcWarp`/`Warp`，调用 `warpSinglePlayer`）；
   - 旗标增删（`SetNowEvent`/`EvNow`、`ClearNowEvent`、`ClearEndEvent`、`EndSetFlg`、`CleanFlg`）。
4. **多步对话树推进与委托/清除状态机 (`onEvent` & `onWindowReply`)**:
   - **`kRequest` (委托型)**: 未接取时弹出 `request_msg` (YES/NO)，点击 YES 接取任务并置位 NOWEV；进行中时弹出 `nomal_window_msg` (OK)，友好提示进度并阻断重复接取；
   - **`kClean` (清除型)**: 弹出放弃确认窗 (YES/NO)，确认后原子清除 NOWEV 与 ENDEV，重置任务状态；
   - **`next_block_index` (多步推进)**: 窗口回复后无缝自动触发推进链条，支持连续对话树与多阶段交互跳转。
5. **防御机制与反向变异验证**:
   - **[RV-1] 复合条件门禁防御拦截**: 验证 `(LV>=80 & TRANS>=1) & (FAME>=100 & reITEM>=2)` 门禁体系，转生不足或空位不足严格阻断弹窗与执行；
   - **[RV-2] 动作原子执行与资产事务一致性**: 复杂多资产置换（扣石币、道具、宠物，给道具、宠物），在条件或资产不足时前置阻断，绝对保持 0 副作用；成功时原子完成置换。

#### 2. 验证与指标

- `world_map` 用例数从 125 组增至 **130 组**（+5 组全量条件、全动作执行、多步对话树及 RV-1/RV-2 变异用例），断言数从 3650 条增至 **3807 条**（+157 条断言，100% 成功）。
- **实测用例矩阵**:
  1. `§9.0.97: ExChangeMan 全量条件表达式求值 (复合条件/括号优先级/关系运算符/变量全集)`
  2. `§9.0.97: ExChangeMan 脚本解析与全动作集执行闭环 (经验/点数/血蓝/声望/传送/旗标)`
  3. `§9.0.97: ExChangeMan 多步对话树与委托/清除状态机闭环 (REQUEST / CLEAN / NextBlock)`
  4. `§9.0.97: [RV-1] 复合条件门禁防御拦截与反向变异验证`
  5. `§9.0.97: [RV-2] 动作原子执行与资产事务一致性反向变异验证`
- **反向验证 (RV-1)**:
  - 篡改 `parseAnd` 将合取 `&` 降级为 `||` ⇒ 用例 4 中转生未达标的拦截断言立即报红失败；恢复后回绿。
- **反向验证 (RV-2)**:
  - 篡改 `checkExChangePreconditions` 绕过道具持有充足性检查 ⇒ 用例 5 中石币扣减一致性断言（500 != 1000）立即精准报红失败；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，客户端 8 项 CTest 全量通过（锁定 `shared-v0.42.0` 零漂移），双端代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

### 9.0.98 Title & Fame Shop (阶段 2 称号系统与声望商城体系)

> **对应架构设计**: 原版 `title.c` (`TITLE_TitleCheck`, `TITLE_TitleCheck_Npc`, `indexOfHaveTitle[30]`)，`npc_fameshop.c` (`FAMESHOP_WindowOpen`, `FAMESHOP_WindowReply`)，`10-world-map.md` 阶段 2 扩展  
> **核心交付**: 称号元数据注册（`TitleDefinition` 包含 ID、名称、说明、所需声望 `req_fame` 与四维加成 `TitleStatsBonus`）、称号授予（`grantTitle`）、称号移除（`revokeTitle`）、拥有检查（`hasTitle`）、称号佩戴（`equipTitle`）与卸下（`unequipTitle`）、称号容量上限（30 个，对齐原版 `indexOfHaveTitle[30]`）、声望商城体系（`NpcType::kFameShop`，在售条目 `FameShopItem` 包含称号 `kTitle`、道具 `kItem`、宠物 `kPet`，交互弹窗 `WindowOpen` 与购买响应 `buyFromFameShop`）、[RV-1] 称号佩戴门限与防重购/超限防御拦截、[RV-2] 声望商城兑换原子事务一致性（声望不足/背包满/宠物栏满严格 0 消耗拦截）。

#### 1. 核心设计与落地产出

1. **称号元数据与四维加成 (`TitleDefinition` & `TitleStatsBonus`)**:
   - `TitleStatsBonus`: 支持生命加成（`bonus_hp`）、攻击加成（`bonus_attack`）、防御加成（`bonus_defense`）、敏捷加成（`bonus_dex`）；
   - `TitleDefinition`: 包含 `title_id`、`name`、`desc`、所需声望门槛（`req_fame`）以及四维属性加成 `bonus`；
   - 注册机制：`World::registerTitle` 校验 `title_id > 0` 且不为空名，成功后收录于全局称号注册表 `registered_titles`。
2. **称号授予、移除、佩戴与容量治理**:
   - **容量与去重**: 严格对齐原版 30 称号位上限（`owned_titles.size() >= 30` 阻断），`grantTitle` 具备天然幂等保护（已有则直接返回 `true`，防止重复占用名额）；
   - **佩戴门槛与活跃称号**: `equipTitle` 实施双重前置校验（1. 必须属于已拥有称号列表；2. 玩家当前声望必须 $\ge req\_fame$ 称号门槛，防刷降声望佩戴高阶荣誉），佩戴后激活 `active_title_id`；
   - **卸下与吊销收口**: `unequipTitle` 将 `active_title_id` 重置为 0；`revokeTitle` 在移除拥有权时，若该称号正处于佩戴态，自动触发下马/卸下逻辑收口为 0，防止残留幽灵属性加成。
3. **声望商城体系 (Fame Shop System)**:
   - **NPC 类型扩充**: 扩展 `NpcType::kFameShop = 11`，大世界面对交互（`onEvent`）触发 `kFameShop` 专属欢迎与商品引导弹窗；
   - **在售条目模型 (`FameShopItem`)**: 支持三种条目类型 `FameShopItemType::kTitle`（荣誉称号）、`kItem`（珍稀道具）、`kPet`（强力战宠），配置兑换所需声望消耗 `fame_cost` 与目标 ID 及数量/初始等级；
   - **兑换执行 (`buyFromFameShop`)**:
     - 距离与地图校验：必须处于同张地图且曼哈顿距离 $\le 3$；
     - 声望充足性前置校验：玩家声望未达到标价严格拦截并弹窗 `fame_less_msg`；
     - 称号专属门禁：已拥有该称号（`kAlreadyHaveTitle`）或 30 称号位已满（`kTitleSlotsFull`）严格阻断；
     - 背包与宠物空间预检：道具类型检查背包空槽（`kInventoryFull`），宠物类型检查宠物空槽（`kPetSlotsFull`）；
     - 扣费与发放原子闭环：前置校验全过之后原子扣减声望（调用 `setPlayerFame`，绝对不触碰 `p.gold`，零非法金币写），并原子发放称号/道具/宠物。
4. **防御机制与反向变异验证**:
   - **[RV-1] 称号佩戴门限与防重购/超限防御拦截**: 验证玩家声望低于门限时拒绝佩戴；验证声望商城中已拥有称号防重复购买；验证称号槽满 30 个拒绝购买；
   - **[RV-2] 声望商城兑换原子事务一致性**: 验证在声望不足、背包满（45/45）、宠物栏满（5/5）等异常工况下，声望严格 0 扣减，背包与宠物栏严格无副作用变动；在空间释放后兑换成功且声望扣减精确对账。

#### 2. 验证与指标

- `world_map` 用例数从 130 组增至 **135 组**（+5 组称号全生命周期管理、佩戴/卸下/吊销收口、声望商城多资产兑换及 RV-1/RV-2 变异用例），断言数从 3807 条增至 **4013 条**（+206 条断言，100% 成功）。
- **实测用例矩阵**:
  1. `§9.0.98: 称号元数据注册、玩家称号授予、容量上限与拥有判定`
  2. `§9.0.98: 称号佩戴、卸下、声望门槛校验、属性加成与吊销收口`
  3. `§9.0.98: 声望商城 (Fame Shop) 兑换称号、道具与宠物全流程闭环`
  4. `§9.0.98: [RV-1] 称号佩戴门限与防重购/超限防御拦截与反向变异验证`
  5. `§9.0.98: [RV-2] 声望商城兑换原子事务一致性反向变异验证`
- **反向验证 (RV-1)**:
  - 篡改 `equipTitle` 注释掉声望门限校验 `if (playerFame(session) < tit->second.req_fame) return false;` ⇒ 用例 4 中未达声望门槛佩戴断言（`CHECK_FALSE` 与 `CHECK(playerActiveTitle == 0)`）立即精确报红失败；恢复后回绿。
- **反向验证 (RV-2)**:
  - 篡改 `buyFromFameShop` 注释掉背包空间空槽检查 `if (s.countFreeItemSlots(*p) < 1) ...` ⇒ 用例 5 中背包满时断言立即捕获非预期返回（`0 != 7`）与声望被错误扣减（`400 != 500`）；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，客户端 8 项 CTest 全量通过（锁定 `shared-v0.42.0` 零漂移），双端代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

### 9.0.99 Cooking & Crafting System (阶段 2 道具制造与生活技能：料理/合成系统与素材加工)

> **对应架构设计**: 原版 `item_gen.c` (`ITEM_mergeItem`, `ITEM_mergeItem_merge`, `ITEM_DISH`, `ITEM_MERGEFLG`)，`10-world-map.md` 阶段 2 扩展  
> **核心交付**: 生活技能配方注册（`CraftingRecipe` 包含配方 ID、制造类别 `CraftingType`、原材料需求列表 `IngredientRequirement`、成功产物、失败碎料、等级门槛、成功率、声望奖励与手续费）、料理烹饪与合成精炼执行闭环（`craftItem`）、堆叠材料扣减与空槽保全、存活宠物协助加成、工匠导师 NPC 实体扩展（`NpcType::kCraftsman`）与引导弹窗、[RV-1] 状态互斥（濒死/战斗/摆摊）、材料混杂（食材与非食材）与槽位作弊防御拦截、[RV-2] 制造资产事务一致性反向变异验证（材料不足/手续费不足/背包满严格 0 扣减）。

#### 1. 核心设计与落地产出

1. **配方元数据与制造类别 (`CraftingRecipe` & `CraftingType`)**:
   - `CraftingType`: 严格区分 `kCooking`（料理烹饪系统，对应原版 `ITEM_DISH` 食物类别）与 `kSynthesis`（合成精炼系统，对应原版 `ITEM_MERGEFLG` 装备与素材合成）；
   - `IngredientRequirement`: 支持配置原材料 ID（`item_id`）、消耗数量（`count`）与材料名称，支持多材料复合投入；
   - `CraftingRecipe`: 包含配方 ID、名称、类别、原材料清单、产出模板 ID 与堆叠数、失败碎料 ID（如焦黑炭块/破碎骨片）、玩家等级门限、基础成功率（`success_rate` 0~100%）、声望奖励（原版 `fooddp`/`syndp` 机制映射）及石币手续费（`cost_gold`）。
2. **生活技能执行闭环 (`craftItem`)**:
   - **状态门禁**: 严格校验玩家存活状态（`hp > 0`，濒死拦截 `kPlayerDead`）、战斗状态（`inBattle`，战斗拦截 `kInBattle`）与摆摊状态（`isPlayerVending`，开摊拦截 `kInVending`）；
   - **输入安全与防作弊**: 遍历 `input_slots` 校验槽位边界合法性（$\in [kStartItemArray, kMaxItemHave)$），使用去重集合阻断重复槽位提交（`collision` 防刷作弊），并拦截摆摊货架上的锁定物品（`kSlotLocked`）；
   - **类型互斥拦截 (原版 `item_type == 20` 互斥)**: 料理配方投入非食材素材或合成配方投入食材，前置阻断返回 `kTypeMismatch`；
   - **容量与材料预检**: 统计各材料数量，任一材料不足前置阻断返回 `kMissingIngredient`；模拟计算扣减释放的槽位数加上当前空槽，空间不足前置阻断返回 `kInventoryFull`；
   - **辅助宠物加成**: 若携带存活宠物协助，成功率获得特化加成（+10% 成功率）；
   - **原子事务与资金审计**: 扣减手续费严格调用 `delGold` 挂载 `GoldReason::kCraftingFee`（零非法直接写）；原子扣减输入槽位材料堆叠数（归零自动释放对象池并清空句柄）；成功生成产物并发放声望，失败生成碎料。
3. **NPC 工匠/导师类型支持**:
   - 扩充 `NpcType::kCraftsman = 12`，大世界面对交互（`onEvent`）触发工坊欢迎语与生活技能引导窗口。

#### 2. 验证与指标

- `world_map` 用例数从 135 组增至 **140 组**（+5 组配方注册/料理与合成闭环、堆叠材料与宠物辅助、失败碎料与工匠 NPC、RV-1 互斥拦截与 RV-2 资产事务一致性），断言数从 4013 条增至 **4105 条**（+92 条断言，100% 成功）。
- **实测用例矩阵**:
  1. `§9.0.99: 道具制造与生活技能配方注册、料理烹饪与合成精炼全流程闭环`
  2. `§9.0.99: 堆叠材料原子扣减与辅助宠物协助制造加成`
  3. `§9.0.99: 制作失败碎料/副产物生成与工匠大师 NPC 交互引导`
  4. `§9.0.99: [RV-1] 状态互斥（濒死/战斗/摆摊）、材料混杂与槽位作弊防御拦截与反向变异验证`
  5. `§9.0.99: [RV-2] 制造资产事务一致性反向变异验证（材料不足/手续费不足/背包满严格0扣减）`
- **反向验证 (RV-1)**:
  - 篡改 `craftItem` 绕过料理食材类型互斥校验 `if (false && recipe->type == CraftingType::kCooking)` ⇒ 用例 4 中材料混杂拦截断言立即报红失败（`11 != 12` 与 `12 != 0`）；恢复后回绿。
- **反向验证 (RV-2)**:
  - 篡改 `craftItem` 注释掉手续费充足性前置检查 `if (recipe->cost_gold > 0 && p->gold < recipe->cost_gold)` ⇒ 用例 5 中资金不足时的材料保全与阻断断言立即精准报红失败（`0 != 8`, `1 != 3`, `9 != 0`, `1000 != 700`）；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，客户端 8 项 CTest 全量通过（锁定 `shared-v0.42.0` 零漂移），双端代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

### 9.0.100 Pet Advanced Growth & Ride Certification (阶段 2 宠物进阶成长与骑乘认证体系)

> **对应架构设计**: 原版 `npc_riderman.c` (`NPC_Riderman`, `w.takegold / 5` 庄园分成), `char/char.c:3990-4075`, `10-world-map.md` 阶段 2 扩展  
> **核心交付**: 庄园骑乘认证体系（`RideCertType` 包含基础、萨姆吉尔庄园暴龙、玛丽娜斯庄园绿暴、加加庄园飞龙、卡鲁它那庄园雷龙及宗师全能认证）、骑乘认证考核全流程闭环（`takeRideExam` 涵盖等级/声望/石币门槛校验）、考核学费 20% 原子注资庄园家族金库（原版 `npc_riderman.c:234` `takegold / 5` 一致性对齐）、骑乘门禁扩展（未拥有庄园特权的非家族成员凭借考取的认证合法骑乘专属宠）、骑宠契合度相性与属性共鸣推导（`calculateRideAffinity` 包含忠诚度、等级差、专精认证加成与攻防敏折算）、骑乘考官 NPC 实体扩展（`NpcType::kRideMaster = 13`）与交互弹窗、[RV-1] 庄园专属骑宠认证门禁与未授权拦截反向变异验证、[RV-2] 认证考核原子事务与庄园金库 20% 分成一致性反向变异验证。

#### 1. 核心设计与落地产出

1. **庄园骑乘认证类型 (`RideCertType`) 与考核门槛 (`RideExamRequirement`)**:
   - `RideCertType`: 严格定义 `kBasic`（基础坐骑认证）、`kManorSamo`（萨姆吉尔庄园暴龙系专属认证）、`kManorMarina`（玛丽娜斯庄园绿暴/虎系认证）、`kJaja`（加加庄园飞龙/加美系认证）、`kKarutana`（卡鲁它那庄园雷龙系认证）与 `kMaster`（宗师全能认证，通骑全系专属宠）；
   - `RideExamRequirement`: 为每种认证注册准入资质（等级需求 40/80/120 级、声望需求 50/200/1000 声望、考核学费 5,000/20,000/100,000 石币）及关联庄园（`associated_manor`）。
2. **考核流程闭环与庄园金库 20% 原子分成 (`takeRideExam`)**:
   - **状态与门槛前置检查**: 校验玩家存活（`hp > 0`）、战斗中互斥（`inBattle`）、摆摊中互斥（`isPlayerVending`）、重复考核防刷防扣费拦截（`kAlreadyCertified`），以及等级、声望与石币充足性检查；
   - **原子扣费与庄园分成**:
     - 严格通过 `delGold` 挂载 `GoldReason::kRideExamFee` 扣除学费；
     - 对齐原版 `npc_riderman.c:234, 311, 388, 464`（`sprintf(buf2, "%d", w.takegold / 5)`），当所考认证关联有占领家族的庄园时，将实扣学费的 $20\%$（`share = tx.applied / 5`）原子注入占领家族金库（`family_gold += share`，严格受 1 亿上限保护）；其余资金由系统回收销毁；
     - 考核成功激活认证，记录于玩家认证集 `player_ride_certs`。
3. **骑乘门禁与进阶相性共鸣 (`canPlayerRide` & `calculateRideAffinity`)**:
   - **门禁无缝接驳**: 在 `canPlayerRide` 中接驳认证查询，非占领家族成员凭借考取的专属认证亦可合法骑乘红暴、绿暴、飞龙与雷龙；宗师认证者享有一证通骑特权；
   - **安全吊销解骑**: `revokeRideCert` 吊销认证时，若玩家当前处于骑乘状态且不再满足骑乘条件，自动调用 `dismountPet` 触发安全下马脱钩；
   - **相性契合度与共鸣属性**:
     - 基准契合度基于宠物忠诚度（`petLoyalty` 0~100）；
     - 等级差驾驭修正：宠物等级高于角色时产生驾驭惩罚（每高 1 级 -2%），角色高等级提供驾驭增益（最高 +10%）；
     - 专精认证加成：持有对应庄园认证 +15% 契合度，宗师全能认证 +20% 契合度（夹取于 10%~100%）；
     - 属性共鸣折算：血量加成 $HP_{bonus} = (HP_{pet} \times rate)/100$，攻击折算 $Atk_{bonus} = (Atk_{pet} \times rate)/200$，防御折算 $Def_{bonus} = (Def_{pet} \times rate)/200$，综合敏捷 $Dex = (Dex_{player} \times 60 + Dex_{pet} \times 40 \times rate / 100) / 100$。
4. **NPC 骑乘考官实体扩展**:
   - 扩充 `NpcType::kRideMaster = 13`，支持服务端解析与大世界面对交互（`onEvent`）对话引导。

#### 2. 验证与指标

- `world_map` 用例数从 140 组增至 **145 组**（+5 组庄园专属考官考核流程、宗师全能认证、契合度相性与属性共鸣推导、RV-1 门禁拦截与 RV-2 庄园分成变异实测），断言数从 4105 条增至 **4201 条**（+96 条断言，100% 成功）。
- **实测用例矩阵**:
  1. `§9.0.100: 庄园专属骑乘考官认证考核全流程 (门槛校验、资质发放与非家族成员骑乘解锁)`
  2. `§9.0.100: 宗师全能认证 (Master Ride Cert) 与全系特权验证`
  3. `§9.0.100: 骑宠契合度相性与属性共鸣加成 (calculateRideAffinity) 验证`
  4. `§9.0.100: [RV-1] 庄园专属骑宠认证门禁与未授权拦截反向变异验证`
  5. `§9.0.100: [RV-2] 认证考核原子事务与庄园金库 20% 分成一致性反向变异验证`
- **反向验证 (RV-1)**:
  - 篡改 `canPlayerRide` 中暴龙系认证校验，绕过萨姆吉尔专属认证门限 ⇒ 用例 4 中未获萨姆吉尔认证时的跨庄园拦截断言（`CHECK_FALSE(f.world.canPlayerRide(id, s_samo))`）立即精准报红失败（`CHECK_FALSE(true)`）；恢复后回绿。
- **反向验证 (RV-2)**:
  - 篡改 `takeRideExam` 庄园分成比例（将 `share = tx.applied / 5` 篡改为 `/ 2` 即 50% 错误分成）⇒ 用例 5 中金库 20% 增量核对断言（`CHECK(fam_info_after->family_gold == init_gold + 4000)`）立即精确报红失败（`CHECK(10000 == 4000)`）；恢复后回绿。

### 9.0.101 Pet Loyalty & Advanced Skills (阶段 2 宠物进阶技能与忠诚度交互体系)

> **对应架构设计**: 原版 `char.c` / `pet.c` / `battle.c` 宠物忠诚度与服从度计算规则，`10-world-map.md` 阶段 2 扩展  
> **核心交付**: 宠物喂食交互全流程（`feedPet` 涵盖食物/料理类别门禁 `item_type == 20`、角色与宠物等级压制上限封顶、堆叠食材原子消耗、生命恢复与忠诚提升）、宠物主动技能遗忘与技能栏释放（`forgetPetSkill`）、宠物顺服度阶梯判定（`checkPetObedience` 包含 60 顺服/20 失控/极度叛逆状态机）、战后战斗结算宠物忠诚度生命周期演化（出战宠与骑乘宠战损落马 -5 忠诚度，胜利存活 +1 忠诚度）、[RV-1] 食物类型门禁与等级压制封顶拦截反向变异实证、[RV-2] 摆摊互斥与资产原子扣减一致性反向变异实证。

#### 1. 核心设计与落地产出

1. **宠物喂食交互体系 (`feedPet`)**:
   - **状态门禁**: 严格校验玩家存活（`hp > 0`，濒死拦截 `kPlayerDead`）、战斗中互斥（`inBattle`，战斗拦截 `kPlayerInBattle`）、摆摊中互斥（`isPlayerVending`，开摊拦截 `kPlayerVending`）；
   - **道具与宠物有效性校验**: 校验宠物槽位合法性、宠物存活（`pet->hp > 0`，阵亡拦截 `kPetDead`）、道具槽位有效性及摆摊货架锁定拦截（`kItemLocked`）；
   - **食材类别门禁**: 严格限定原版食物/肉类/料理类别（`item->type == 20`），非食材道具喂食拒绝并返回 `kNotFoodItem`（[RV-1] 严防喂食武器/防具导致资产损毁）；
   - **等级压制与忠诚度上限封顶**: 对齐原版 `char.c` / `pet.c` 规则，当宠物等级大于主人等级时（`pet->level > p->level`），忠诚度上限被压制为 $\max(20, 100 - (pet\_level - player\_level) \times 3)$，达到压制上限后拦截继续喂食并返回 `kLoyaltyCapped`；
   - **生命恢复与忠诚度原子提升**: 食物恢复基准 50 点生命（以 `std::max(base_stats.max_hp, pet->hp)` 为上限保护，杜绝逆向掉血）；忠诚度提升 5 点（等级压制时减半），原子写入 `setPetLoyalty`；
   - **堆叠材料原子扣减**: 堆叠数 $>1$ 时递减堆叠数；堆叠数归零时从玩家背包清除槽位并安全释放道具对象池。
2. **宠物技能主动遗忘与技能栏管理 (`forgetPetSkill`)**:
   - 校验玩家存活、非战斗与非摆摊状态；
   - 校验宠物槽与技能槽合法性（`0 <= skill_slot < kPetSkillSlots`）；
   - 校验槽位是否存在技能（`pet_skills[slot] > 0`）；
   - 原子置 0 释放技能槽位，支持后续重新向技能导师学习进阶技能。
3. **宠物服从与顺服状态判定 (`checkPetObedience`)**:
   - 对齐石器经典顺服度阶梯：
     - `kObedient`（顺服态）：忠诚度 $\ge 60$，完全听从指令并全力协同作战；
     - `kConfused`（偶发失控态）：$20 \le$ 忠诚度 $< 60$，战斗中存在指令失效或发呆、攻击自己人可能；
     - `kBetray`（极度叛逆态）：忠诚度 $< 20$，战斗中极高概率叛逃、离场或转攻己方。
4. **战后忠诚度生命周期演化与战损回写 (`World.cpp` 战斗结算)**:
   - 在战斗胜利与战败结算（4613 行）处，遍历结算出战宠与骑乘宠忠诚度：
     - 出战宠阵亡（`pet->hp <= 0` 或 `b.field.dead`）：忠诚度扣减 5 点（保底 0）；
     - 骑乘宠被打落马/阵亡（`ride_hp <= 0` 或 `rpet->hp <= 0`）：忠诚度扣减 5 点（保底 0）；
     - 玩家获胜且宠物存活：忠诚度增加 1 点（封顶 100）。

#### 2. 验证与指标

- `world_map` 用例数从 145 组增至 **150 组**（+5 组喂食全流程、战后胜负与战损演化、技能遗忘与管理、RV-1 食物门禁与压制封顶、RV-2 摆摊互斥与资产一致性），断言数从 4201 条增至 **4276 条**（+75 条断言，100% 成功）。
- **实测用例矩阵**:
  1. `§9.0.101: 宠物喂食交互全流程 (食物类型门禁、生命恢复、忠诚度提升与材料堆叠原子扣减)`
  2. `§9.0.101: 战斗胜负与生死战损对忠诚度影响全景验证`
  3. `§9.0.101: 宠物技能学习、遗忘 (forgetPetSkill) 与技能栏管理闭环`
  4. `§9.0.101: [RV-1] 宠物喂食食材类型门禁与等级压制忠诚度封顶拦截反向变异验证`
  5. `§9.0.101: [RV-2] 宠物喂食资产原子消耗与宠物状态同步一致性反向变异验证`
- **反向验证 (RV-1)**:
  - 篡改 `feedPet` 中食材门禁校验 `if (false && item->type != 20)` ⇒ 用例 4 中武器喂食拦截与道具保全断言立即报红失败（捕获 `0 != 8`、武器被错误扣除、忠诚度被错误修改）；恢复后回绿。
- **反向验证 (RV-2)**:
  - 篡改 `feedPet` 中摆摊拦截校验 `if (false && isPlayerVending(session))` ⇒ 用例 5 中摆摊状态互斥拦截断言立即精准报红失败（`CHECK(9 == 4)`）；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，客户端 8 项 CTest 全量通过（锁定 `shared-v0.42.0` 零漂移），双端代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

### 9.0.102 Manor War & Ride Battle Preparation (阶段 2 庄园家族战体系与骑乘战备闭环)

> **对应架构设计**: 原版 `family.c` / `manorsman.c` / `fmpkcallman.c` 庄园争夺与约战规则，`04-storage-schema.md` §5 T8/T10  
> **核心交付**: 四大庄园据点争夺与决斗调度（`challengeManor` 涵盖族长门禁 `role == kLeader`、无主庄园进驻、已有庄园互斥、10 万石币挑战押金原子扣除 `kManorChallengeFee`、已预约状态互斥拦截）、庄园战状态机与决斗计分闭环（`startManorWar` 推进 `kInWar`、`recordManorDuelScore` 双方战绩积分累加）、庄园战结果原子交割与特权过户（`concludeManorWar` 挑战方胜出庄园易主与奖金发放、守方卫冕成功押金 100% 注资守方家族金库 `addFamilyGold` 与战后 24 小时休战保护期 `kCooldown`）、[RV-1] 族长门禁与已有庄园互斥拦截反向变异实证、[RV-2] 资金不足零扣减与结算交割 100% 注资一致性反向变异实证。

#### 1. 核心设计与落地产出

1. **庄园挑战与战备预约全流程 (`challengeManor`)**:
   - **状态门禁**: 严格校验玩家存活（`hp > 0`，濒死拦截 `kPlayerDead`）、战斗中互斥（`inBattle`，战斗拦截 `kPlayerInBattle`）、摆摊中互斥（`isPlayerVending`，开摊拦截 `kVending`）；
   - **族长权限与庄园互斥**: 严格限定仅家族族长（`role == FamilyRole::kLeader`）可发起庄园挑战（[RV-1] 普通成员与长老拦截 `kNotLeader`）；自身家族若已占领其他庄园，互斥拦截 `kAlreadyOwnManor`；
   - **最低押金与资金预检**: 设定 100,000 石币最低押金门槛（`kDepositInsufficient`）；通过 `GoldLedger` 原子扣减发起族长随身石币（`kManorChallengeFee`），资金不足严格拦截并保持 0 扣减（[RV-2] `kGoldInsufficient`）；
   - **无主庄园进驻与约战状态推进**: 若目标庄园当前无守方家族，自动进驻占领并免收押金（`kNoDefender`）；已有守方时，状态转为 `kScheduled`，记录守方与挑战方家族 ID 及押金快照，拦截第三方并发约战（`kManorNotIdle`）。
2. **庄园战开战与决斗积分调度 (`startManorWar` / `recordManorDuelScore`)**:
   - **开战推进**: 校验 `state == kScheduled` 后推进至 `kInWar`，初始化 1 小时对决倒计时；
   - **决斗积分**: 在交战期间接收团队与单挑决斗战报，严格累加对战双方家族胜场积分，屏蔽外部无关家族伪造积分。
3. **庄园归属过户与资金原子交割 (`concludeManorWar`)**:
   - **挑战方攻方胜出**: 调用 `occupyManor` 实现庄园归属原子过户，原守方失去庄园；挑战押金全额返还并作为战利金注资挑战方家族金库（上限保全），挑战家族声望 +500；
   - **守方卫冕成功**: 守方保有庄园特权与分成收益；挑战方没收之押金 100% 原子注资守方家族金库（上限保全），守方家族声望 +200；
   - **战后保护期**: 庄园状态转入 `kCooldown` 休战期，设置 24 小时冷却保护倒计时，清空挑战方挂载。

#### 2. 验证与指标

- `world_map` 用例数从 150 组增至 **155 组**（+5 组庄园挑战与排期全流程、开战与积分累加、攻守胜负所有权与资金交割、RV-1 门禁互斥变异、RV-2 资金原子扣减与一致性变异），断言数从 4276 条增至 **4373 条**（+97 条断言，100% 成功）。
- **实测用例矩阵**:
  1. `§9.0.102: 庄园挑战发起、无主进驻与战备排期全流程`
  2. `§9.0.102: 庄园战开战状态机推进与决斗比分累加闭环`
  3. `§9.0.102: 庄园战胜负结算与攻守庄园所有权与奖金原子交割闭环`
  4. `§9.0.102: [RV-1] 庄园挑战发起族长门禁与已有庄园互斥拦截反向变异验证`
  5. `§9.0.102: [RV-2] 庄园战押金原子扣除与结算交割一致性反向变异验证`
- **反向验证 (RV-1)**:
  - 篡改 `challengeManor` 中族长身份门禁 `if (false && role != FamilyRole::kLeader)` ⇒ 用例 4 中普通成员挑战拦截断言立即精准报红失败；恢复后回绿。
- **反向验证 (RV-2)**:
  - 篡改 `challengeManor` 中押金充足性校验 `if (false && tx.disposition == GoldDisposition::kRejected)` ⇒ 用例 5 中资金不足拦截断言立即精准报红失败；恢复后回绿。
- **全套静态守卫**: 22 项服务端 CTest 全量通过，客户端 7 项 CTest 全量通过，代码格式校验 100% 绿灯，`check_gold_writes.py` 严格零非法直接赋值。

### 9.0.103 阶段 2.6 —— 称号与名片持久化闭环贯通及 World.cpp 单体解耦重构 (World Persistence & Domain Decomposition)

**日期**: 2026-10-02  
**依据**: `deploy/sql/002-world-features.sql`、`00-architecture.md` §3.1 模块边界硬约束、`01-server-architecture.md` §3/§4 分层架构。  
**目标**:
1. 将 `002-world-features.sql` 的 v2 schema 正式贯通进 `session_storage` 与 `World` 角色登入登出存档生命周期（titles 与 address_book）；
2. 彻底拆解膨胀至 14,265 行的 `World.cpp` 巨石单体，抽取私有内部上下文头文件 `src/world/WorldImpl.h` 与 7 大领域编译单元，在不破坏任何模块边界守卫与黄金账本审计前提下达成架构模块化解耦。

#### 1. 核心设计与落地产出

1. **称号与名片夹存储贯通 (Storage Schema v2 & World Persistence)**:
   - **`session_storage` 演进**:
     - 在 `SA::SessionStorage::AddressBookRecord` 增加名片持久化字段（`charname`, `title`, `level`, `image`, `online`）；
     - 在 `SA::SessionStorage::CharacterRecord` 增加 `titles`（`std::vector<int>`）与 `address_book`（`std::vector<AddressBookRecord>`）；
     - `MySqlService` 升级 Schema 版本判定：兼容校验 `schema_version`（版本 1 自动向前兼容，版本 2 启用全量字段读写）；在 `kCreate`、`kSelect`、`kSave` 中完整落实 `writeTitles`/`readTitles` 与 `writeAddressBook`/`readAddressBook`；
   - **`World` 角色生命周期闭环**:
     - 在 `saveCharacter` 中，将内存态玩家佩戴/拥有称号列表 (`player_titles`) 及名片夹列表 (`address_books`) 序列化至 `SA::Domain::CharacterRecord` 提交至持久化服务；
     - 在 `processStorage` / `install` 中，反序列化还原玩家所拥有的全部称号元数据与名片卡片条目，无缝恢复名片在线感知与状态追踪。
   - **单元回归与持久化测试**:
     - `tests/WorldPersistenceTest.cpp` 新增 `titles_and_address_book_persist_across_save_reload` 验证用例，覆盖拥有多个称号、佩戴专属称号、拥有多张好友名片并在重新登录后精确还原称号加成与名片条目。测试全绿（16 用例 / 417 断言）。

2. **`World.cpp` 单体解耦重构 (Monolith Decomposition)**:
   - **内部上下文头文件 (`src/world/WorldImpl.h`)**:
     - 严格约束于 `src/world/` 私有目录，不泄露至公共头文件 `include/world/Api.h`（保持 `tools/check_module_boundaries.py` 100% 守卫有效）；
     - 集中承载 `kMaxPlayers`、`kMaxPets`、`PlayerPool`、`WorldWriteContext`、`ChargeState`、`MagicStatusState`、`BattleInstance` 等实体池与战场状态结构；
     - 内联实现 `makeBattleSnapshot`、`syncPetState`、`makePlayerCombatant`、`retireBattle`、`pushBattleSnapshot` 以及 `sendTo<M>` 下发模板；
     - 定义完备的 `struct World::Impl : GoldAuditSink`，作为所有世界领域编译单元共享的内部实现基石。
   - **8 大领域独立编译单元**:
     1. `WorldPartyTrade.cpp`: 组队邀请与踢出、决斗发起与切磋判定、双向确认安全交易全状态机；
     2. `WorldSocial.cpp`: 好友名片索取与屏蔽、邮件寄送/查阅/附件提取与删除、世界/公屏/队伍/私聊分级频道；
     3. `WorldFamily.cpp`: 家族创建/解散/成员管理、金库充提、四大庄园占领与庄园对决争夺战；
     4. `WorldEconomy.cpp`: 原地摆摊/标价/上下架/购买、寄售拍卖市场挂牌/下架/选购与搜索；
     5. `WorldRide.cpp`: 宠物骑乘上下马、庄园骑乘考试考核、骑宠契合度相性计算；
     6. `WorldPetFeatures.cpp`: 宠物忠诚度、料理喂食、技能遗忘、顺服度判定、宠物融合与转生流程；
     7. `WorldLifestyle.cpp`: 外观与头像计算、新手村出生点、称号系统、声望商城、料理烹饪与合成精炼；
     8. `WorldEncounter.cpp`: 大世界暗雷/明雷遇敌链、编组筛选与抽签、敌人四维与经验生成、入场投影与大世界明雷开战。
   - **构建与边界合规**:
     - `src/world/CMakeLists.txt` 完整纳入 8 个新编译单元，严格维持 `-ffp-contract=off`；
     - `World.cpp` 由原先臃肿的 14,265 行大幅缩减至 **7,703 行**（累计削减 6,562 行业务逻辑代码，降幅达 46%），职责纯化为世界主循环 tick、地图与地板管理、战斗生命周期推进及角色进出场生命周期调度。

#### 2. 验证与指标

- **双端 CTest 100% 绿灯**:
  - 服务端 22 项 CTest 全量通过（`world_persistence` 16/16 用例、`world_map` 155/155 用例、`rules_battle` 170/170 用例全绿）；
  - 客户端 7 项 CTest 全量通过（启用 GameStudio 引擎宿主时 8/8 全绿，含 `boot` 场景启动测试）；
- **CI 门禁与守卫验证通过**:
  - `tools/ci_verify.py` 全部 6 项门禁通过（编码检查、WERROR 构建、CTest 22/22、断言反向探针验证通过）；
  - `tools/check_module_boundaries.py` 严格校验 4 模块 30 源文件依赖纯净无泄漏；
  - `tools/check_gold_writes.py` 守卫确认所有石币改动全部由 `GoldLedger` 统一审计；
  - `tools/check_shared_purity.py` 确认 28 个 shared 头文件纯度无污染。

---

### 9.0.104 批次 D.5 —— 遇敌与战斗子系统深度解耦 (WorldEncounter.cpp & WorldBattle.cpp) (2026-10-03)

2026-10-03 交付。为彻底解决 `World.cpp` 历史遗留单体过于庞大（原 14,265 行）所带来的维护与编译瓶颈，继前序 7 大领域单元拆分后，本批次完成最后两大重度模块的深度剥离——**大世界遇敌子系统 (`WorldEncounter.cpp`)** 与 **战斗生命周期及推进子系统 (`WorldBattle.cpp`)**。

#### 1. 拆分架构与工程落地

1. **遇敌子系统 (`src/world/WorldEncounter.cpp`)**:
   - **大世界暗雷遇敌链**: 完整抽离 `findEncountArea`、`findEnemyGroup`、`pickEnemyGroup`、`findEnemyEncounter`、`findEnemyTemplate`、`rollEnemyList`；
   - **数值与属性生成**: 移植 `rollEncounterLevel`、`kEnemyBaseExpTbl`、`enemyExp`、`spawnEnemy`；
   - **入场与生命周期挂接**: 封装 `enterEnemyToField`、`World::spawnEnemyToField`、`World::loadEncounterTables`、`World::triggerEncounter`、`World::triggerNpcEnemyBattle`；
   - **内部接口对齐**: `encodeHandle` 与 `makeEnemyAppear` 内联至 `WorldImpl.h`，对外保持零暴露。

2. **战斗子系统 (`src/world/WorldBattle.cpp`)**:
   - **战斗生命周期与指令调度**: 统一收拢 `World::startBattle`、`World::joinBattle`、`World::battleCount`、`World::detachBattles`、`World::onBattleCommand`、`World::stats`、`World::battleEnemyAt`、`World::battleField`；
   - **战斗推进主循环**: 将 `World::tick()` 内长达 443 行的第 4 步（战斗推进、回合就绪、行动排序、状态判定、快照广播、战损与经验交付、忠诚度结算与超时清理）完整封装为 `World::advanceBattles()` 私有方法；
   - **战斗辅助纯函数族**: 完整转移 25 个战场内部计算与状态投影函数（`readyMask`、`sideWipedOut`、`fillEnemyCommands`、`autoFillPetCommands`、`createPetFromCapture`、`captureItemDelAll`、`deliverPlayerProfit`、`expForKill`、`settleDeaths`、`hasCaptureItems`、`projectCaptureItemGate`、`findItemHealPower`、`projectItemUsePower`、`findPetSkillEffect`、`projectPetSkill`、`projectProfSkill`、`projectChargeState`、`injectChargeCommands`、`applyChargeEffects`、`projectMagicStatus`、`tickMagicStatus`、`applyMagicStatusPetSkill`、`applyGuardianPetSkill`、`consumeUsedItem`、`applyEvents`）；
   - **共享实体交互辅助**: `giveItemIntoPlayer`、`makeDemoField` 在 `WorldImpl.h` 声明并在此实现；`enterPetToField`、`exitPetFromField` 保持 `Api.h` 既有声明并在此落地。

3. **规模与纯度指标**:
   - `World.cpp` 行数由 7,703 行一举降至 **4,994 行**（较最初 14,265 行累计缩减 **65%**）；
   - `WorldBattle.cpp` 2,716 行，`WorldEncounter.cpp` 1,070 行，各自职责边界极度清晰；
   - 保持 `include/world/Api.h` 作为唯一公共头文件的架构守卫 100% 有效；
   - 严格延续 `-ffp-contract=off` 确保浮点运算跨平台逐位一致。

#### 2. 验证与门禁

- **全量 CTest 22/22 100% 绿灯**:
  - `world_tick`、`rules_battle`、`world_map`、`world_persistence` 等 22 项测试全通；
  - `tools/check_format.py` 82 文件无缝合规；
  - `tools/check_module_boundaries.py` 4 模块 31 源文件 0 越界；
  - `tools/check_gold_writes.py` 确认石币写操作 100% 经由 `GoldLedger`；
  - `tools/check_docs_index.py` 文档一致性校验全绿。

---

### 9.0.105 批次 D.6 —— 大世界移动、多楼层与传送子系统解耦 (WorldMovement.cpp) (2026-10-03)

2026-10-03 交付。本批次完成大世界移动、碰撞阻挡、多楼层地图管理与传送点瞬移子系统的实体解耦（`src/world/WorldMovement.cpp`），继续推进 `World.cpp` 的单体瘦身。

#### 1. 拆分架构与工程落地

1. **移动子系统 (`src/world/WorldMovement.cpp`)**:
   - **大世界移动推进主循环**: 将 `World::tick()` 内第 5 步（玩家移动推进、路径串消费、明雷/NPC 实体碰撞阻挡退回、WarpPoint 踩踏瞬移、olink 格挂接转移、同屏视野广播、组队跟随贪吃蛇算法、暗雷遇敌判定与 CEP 掷骰）封装为 `World::advanceMovement()` 私有方法；
   - **移动指令与门禁**: 转移 `World::onWalk`（含防瞬移、非本队队员禁止自主移动、交易/战斗状态拦截、(0,0) 历史调试门、目标格通行性校验、路径串入队）；
   - **步进物理与墙角阻挡**: 移植 `walkStep`，支持 8 方向直线与斜向分量判定（防穿墙角）；
   - **瞬移与传送门处理**: 抽离 `World::Impl::warpSinglePlayer` 与 `World::Impl::warpPlayer`（支持队长传送时全队队员同步瞬移、跨图视野重置与实体 Appear/Disappear 刷新）；
   - **传送员 NPC 交互**: 抽离 `World::warpPlayerByNpc`（等级门禁、石币路费审计经由 `GoldLedger`、目标地点通行性校验）；
   - **多楼层地图与传送点管理**: 转移 `World::loadFloorMap`、`World::findFloorMap`、`World::floorMapCount`、`World::loadWarpPoints`、`World::warpPointCount`、`World::warpPlayerForTest`、`World::playerPos`；
   - **方向与步频常数共享**: `kWalkIntervalMs`、`DirDelta`、`kDirDelta`、`decodeDirChar`、`getDirFromTwoPoints` 统一内联收拢于 `WorldImpl.h`，供各子系统零依赖复用。

2. **规模与纯度指标**:
   - `World.cpp` 行数从 4,994 行进一步缩减至 **4,381 行**（相较最初 14,265 行累计缩减 **69.3%**）；
   - `WorldMovement.cpp` 574 行，专职负责移动与楼层传送；
   - 保持 `include/world/Api.h` 作为唯一公共头文件的架构硬纪律 100% 遵守；
   - 严格遵守 `GoldLedger` 经济审计守卫；
   - 浮点与平台编译选项无缝对齐。

#### 2. 验证与门禁

- **全量 CTest 22/22 100% 绿灯**:
  - `world_tick`、`world_map`、`world_persistence`、`rules_battle` 等全量测试用例全数通过；
  - 静态架构检查工具 `tools/check_module_boundaries.py` 4 模块 32 源文件全通；
  - 石币安全守卫 `tools/check_gold_writes.py` 100% 通过；
  - 源码格式守卫 `tools/check_format.py` 全绿。

---

### 9.0.106 批次 D.7 —— NPC 对话事件、任务剧情、商店买卖与技能导师子系统解耦 (WorldNpcDialog.cpp) (2026-10-03)

2026-10-03 交付。本批次完成 NPC 交互派发、对白与窗口状态机、ExChangeMan 任务剧情与前置条件、NPC 道具与宠物商店买卖、以及宠物技能导师传授子系统的实体解耦（`src/world/WorldNpcDialog.cpp`）。至此，`World.cpp` 行数大幅削减至 **2,300 行**，核心业务模块完成全景拆分解耦。

#### 1. 拆分架构与工程落地

1. **NPC 对话与剧情子系统 (`src/world/WorldNpcDialog.cpp`)**:
   - **大世界事件派发入口 (`onEvent`)**:
     - 明雷切入触发门禁（面前格计算、队长特权与队员行动限制）；
     - 恢复员 Healer 交互与路费原子结算（经由 `GoldLedger`）；
     - 城镇居民 TownPeople 候选文案切分与随机摇选；
     - ExChangeMan 复合前置条件求值（`evaluateEventCondition`、`checkExChangePreconditions`）与对话分支跳转；
     - 道具商店 ItemShop、宠物商店 PetShop、宠物技能导师 PetSkillShop 货品目录与买卖倍率装配；
     - 告示牌 SignBoard、传送员 WarpMan、融合师、转生师、声望商城、工匠、骑乘导师对话派发；
     - 事件响应与执行状态回执（`EventResult`）。
   - **客户端窗口应答状态机 (`onWindowReply`)**:
     - 活动窗口 `active_window_id` 校验与 NPC 关联匹配；
     - ExChange 任务接取、确认交付与多步剧情跳转树推进（`pending_exchange`、`applyExChangeEffects`）；
     - 商店购买道具入包事务与石币安全扣除；
     - 宠物商店购买与宠物栏落池；
     - 宠物技能导师学习传授；
     - 传送员多目的地 SELECT 选项与单目的地 Yes/No 传送核验。
   - **商店与技能导师业务实现**:
     - `World::sellItemToShop`: 道具出售回购、所有权清除与石币上限安全入账（走 `GoldLedger`）；
     - `World::buyPetFromShop`: 宠物在售购买、资质与属性生成、石币扣除与宠物槽位绑定；
     - `World::sellPetToShop`: 宠物回购出售、随行骑乘状态自动卸载与石币安全入账；
     - `World::learnPetSkill`: 技能等级门禁、防重学校验、技能空槽查找与学费扣除。
   - **ExChange 任务辅助纯逻辑与实体管理**:
     - 交付道具/宠物解析（`parseExchangeItems`、`parseExchangePets`、`resolveDelItems`、`resolveDelPets`）；
     - 背包/宠物持有量统计与空槽位安全预检；
     - Side-effects 执行流（石币/声望/经验/属性点/血蓝/道具进出/宠物进出/任务旗标/NPC传送）；
     - NPC 实体加载与快速查询（`loadNpcEntities`、`npcCount`、`findNpc`、`playerHasActiveWindow`、`playerHasNowEvent`、`playerHasEndEvent`）。

2. **规模与纯度指标**:
   - `World.cpp` 行数从 4,381 行锐降至 **2,300 行**（相较最初 14,265 行累计缩减 **83.9%**，彻底摆脱单体臃肿）；
   - `WorldNpcDialog.cpp` 2,115 行，专职负责全部 NPC 交互、剧情事件与窗口应答；
   - 保持 `include/world/Api.h` 作为唯一公共头文件的架构硬纪律 100% 遵守；
   - 严格遵守 `GoldLedger` 经济审计守卫；
   - 保证编译无任何 Warning，测试全量通过。

#### 2. 验证与门禁

- **全量 CTest 22/22 100% 绿灯**:
  - `world_tick`、`world_map`、`world_persistence`、`rules_battle`、`playable_content` 等 22 项测试全数通过；
  - 静态架构检查工具 `tools/check_module_boundaries.py` 4 模块 33 源文件 0 违规；
  - 石币安全守卫 `tools/check_gold_writes.py` 100% 通过；
  - 源码格式守卫 `tools/check_format.py` 全绿。

### 9.0.107 服务端战斗观战系统落地与广播链路全覆盖 (阶段 2.10)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 依据《05 战斗系统架构规范》§6.3 救援与观战规范，实现服务端战斗观战（Spectating）核心系统，建立观战者入场/离场、同屏搜索、多播流同步及生命周期管理。

#### 1. 核心架构与功能落地

1. **观战席位与状态抽象 (`BattleInstance` / `WorldImpl.h`)**:
   - 在 `BattleInstance` 引入 `std::vector<SA::Net::SessionId> spectators`，独立于参战者 `members`；
   - 观战者状态接入 `WorldImpl::isSpectating` 与 `WorldImpl::inBattle`，保证观战期间行为隔离（防瞬移、禁大世界移动、禁重复触发战斗与交易）；
   - 观战槽位分配标准化为 `SA::Rules::kSlotCount` (槽位 20)，客户端通过 `BattleSelfInfo (slot=20, mp=0, cannot_act=CANNOT_ACT_NONE)` 明确自身为观战身份。

2. **全周期多播事件流覆盖 (`WorldBattle.cpp`)**:
   - **观战加入与快照注入**:
     - `World::spectateBattle(battle, session)`：严格门禁（自身在线未参战、目标战斗有效未结束、单场观战人数上限 20）；
     - 入场即刻下发三件套：`BattleSelfInfo(slot 20)` + `BattleSnapshot` + `BattleTurnBegin`，确保客户端视角与当前战局瞬时对齐；
   - **回合事件流广播 (`flush`)**:
     - `advanceBattles` 的逐行动 `flush()` 闭包将 `BattleEvents` 统一多播至 `members` 与 `spectators`；
   - **战场动态快照广播 (`pushBattleSnapshot`)**:
     - 捕获、离场、换宠等引起的战场阵容变更快照一并推送给全部观战者；
   - **下回合就绪通知 (`BattleTurnBegin`)**:
     - 每回合推进时向全部观战者发送更新的 `BattleTurnBegin`；
   - **战斗结算与退出 (`BattleResult` / `BattleLeave`)**:
     - 战斗决胜或中止时向观战者下发 `BattleResult`（无经验与掉落结算），并自动清除观战状态；
     - 观战者发送 `ESCAPE` 指令或主动调用 `leaveSpectate` 时，下发 `BattleLeave` 退出并恢复自由大世界状态；
     - `detachBattles` 离线保护移除观战者，杜绝悬挂连接。

3. **玩家同屏交互观战 (`World::spectatePlayer` / `World::onEvent`)**:
   - 依据《05 战斗系统架构规范》§6.3：取面前一格（或 $\le 2$ 格请求格），检查目标玩家是否正处于战斗；
   - 接入大世界事件交互流 (`World::onEvent` 处理 `ENTITY_PLAYER`)，客户端可直接通过 `EventRequest` 面向/点击目标玩家触发观战；
   - 校验同层地图门禁与距离门禁（切比雪夫距离 $\le 5$ 格）；
   - 观战者支持随时发送 `ESCAPE` 指令即时撤出观战，脱离回合对齐等待，并干净清理；
   - 成功即无缝转入 `spectateBattle`。

#### 2. 接口扩展与架构合规

- `src/world/include/world/Api.h`:
  - `spectateBattle(BattleId, SessionId)`
  - `leaveSpectate(SessionId)`
  - `spectatePlayer(SessionId spectator, SessionId target_player)`
  - `inBattle(SessionId)`
  - `isSpectating(SessionId)`
  - `spectatorCount(BattleId)`
- 架构守卫验证：
  - `include/world/Api.h` 仍为 `world` 模块唯一公开头文件（`check_module_boundaries.py` 100% 保持）；
  - 无任何 `GoldLedger` 绕行（`check_gold_writes.py` 100% 保持）；
  - 全工程 `-Werror` 零告警。

#### 3. 验证与门禁

- **新增单元测试 (`tests/WorldTickTest.cpp`)**:
  - `Battle spectating: spectateBattle, leaveSpectate, and spectator limits`: 验证观战加入、重复拦截、主动离场与状态复位；
  - `Battle spectating: spectatePlayer distance and floor gating`: 验证近距离观战成功、跨层拦截与超距拦截；
  - `Battle spectating: spectator lifecycle on battle resolution`: 验证战斗决胜后观战者自动结算与清空；
  - `Battle spectating: onEvent ENTITY_PLAYER triggers spectatePlayer and clean leave`: 验证大世界协议级 `EventRequest(ENTITY_PLAYER)` 触发面向观战及随时 `ESCAPE` 指令安全脱离。
- **全量 CTest 22/22 100% 绿灯**。

---

### 9.0.108 阶段 2.11 —— 战斗救援与乱入系统落地 (Battle Rescue & Join-in-Progress System)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 依据《05 战斗系统架构规范》§6.3 救援与观战规范，实现服务端战斗救援（Rescue / Join-in-Progress / 乱入）核心系统，建立乱入参战门禁、槽位动态分配、出战宠自动协同带出及全场快照多播流。

#### 1. 核心架构与功能落地

1. **严格入场门禁体系 (`WorldBattle.cpp:rescuePlayer`)**:
   - 自身状态防御校验：自身存活且 HP > 0，不在战斗中，不在摆摊中，不在队伍中（`partyModeOf == kNone`）；
   - 空间门限校验：与目标处于同一地图楼层（`floor` 一致），空间切比雪夫距离 $\le 5$ 格；
   - 目标战局门限：目标正在进行的战斗有效且未结束（`!finished`）；
   - 战斗类型与防作弊门禁：严格禁止乱入 PVP 决斗/庄园战（`!is_pvp` 且敌方 Side 1 无玩家），仅允许 P_vs_E 野外战斗乱入；
   - 队伍容量门禁：检查己方 Side 0（槽位 0..4），存在空闲槽位方可加入（最多 5 人满员）。

2. **动态槽位分配与战斗装配**:
   - 命中首个空闲槽位后，通过 `makePlayerCombatant` 注入参战者角色、穿戴装备修正与骑乘宠物；
   - 调用 `joinBattle` 建立映射，同步带出角色默认出战宠至对应宠位（`slot + 5`）；
   - 触发全场快照广播 `pushBattleSnapshot`，瞬时同步至战斗全员及所有在席观战者。

3. **大世界协议级事件流接入 (`WorldNpcDialog.cpp:onEvent`)**:
   - `World::onEvent` 新增识别 `event_type == 3`（救援请求）；
   - 支持面前一格判定与近身点击格探测，成功执行后回执 `EventResult(ok=true)`。

#### 2. 接口扩展与架构合规

- `src/world/include/world/Api.h`:
  - `bool rescuePlayer(SA::Net::SessionId rescuer, SA::Net::SessionId target_player);`
- 架构守卫验证：
  - `include/world/Api.h` 仍为 `world` 模块唯一公开头文件（`check_module_boundaries.py` 100% 保持）；
  - 无任何 `GoldLedger` 绕行（`check_gold_writes.py` 100% 保持）；
  - 全工程 `-Werror` 零告警。

#### 3. 验证与门禁

- **新增单元测试 (`tests/WorldTickTest.cpp`)**:
  - `Battle rescue: rescuePlayer into PVE battle allocates slot and joins combat`: 验证乱入成功落座 Side 0 空闲槽、参战状态建立与重复拦截；
  - `Battle rescue: gate validations and onEvent event_type=3`: 验证跨距离拦截、大世界 `EventRequest(event_type=3)` 协议级面向救援。
- **全量 CTest 22/22 100% 绿灯**。

---

### 9.0.109 阶段 3.1 —— 大世界全量资产批处理管线与地图/NPC/传送门编目落地 (Batch Content Pipeline & World Catalogs)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 依据实施计划 Phase 3.1 核心资产批处理要求，构建全自动化无头大世界资产解析器与编目体系，打通原版 1,235 张 LS2MAP 地图、9,011 处传送门及 3,450 个具名功能 NPC 实体。

#### 1. 核心架构与功能落地

1. **全量资产批处理工具链 (`tools/import_all_world_content.py`)**:
   - **地图解析引擎**: 遍历 `csa8.0/gmsv/data/map`，逐文件校验 `LS2MAP` 二进制头部、解算宽高尺寸、UTF-8 字符集安全转译地图名称，全量编目 1,185 张有效地图（覆盖四大主村、各大洞窟、外岛与深渊地下城）；
   - **双轨传送网络整合**: 读取 `mapwarp.txt` 静态传送配置（4,742 条），并穿透解析 `npcgen_warp` 动态传送门 NPC（4,269 条），去重归并生成包含 9,011 条路由的统一传送字典 `warps_catalog.json`；
   - **NPC 实体与模板关联画像**: 解析 7,719 个 `.create` 实例块与 178 个 `.template` 模板块，提取出生坐标（`borncenter`/`borncorner`）、朝向、图形编号与外部 `file:*.arg` 脚本，精准分类映射到服务端核心领域类型（ExChangeMan 861 个、Enemy 533 个、TownPeople 531 个、Shop 360 个、WarpMan 334 个、SignBoard 290 个、Healer 47 个），生成 `npcs_catalog.json`。

2. **编目产物持久化与回归测试 (`tools/test_content_catalog.py`)**:
   - 输出结构化数据至 `content/catalog/` (`maps_catalog.json`, `warps_catalog.json`, `npcs_catalog.json`, `summary.json`)；
   - 新增专用自动化测试套件 `test_content_catalog.py`，断言地图数量 > 1000、传送点 > 5000、NPC > 3000 及分类完备性。

#### 2. 验证与门禁

- **全量回归与门禁**:
  - `python3 tools/test_content_catalog.py` 4 项测试通过；
  - 全量 CTest 22/22 保持 100% 绿灯；
  - 架构守卫工具 `check_module_boundaries.py`, `check_gold_writes.py`, `check_shared_purity.py`, `check_docs_index.py`, `check_dr_table.py` 全数绿灯。

---

### 9.0.110 阶段 3.2 —— 战中掉线断网保护与重连接管系统落地 (Battle Disconnect Grace & Re-attach System)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 依据 Phase 3.2 网络弹性规范，建立网络异常断开保护、离线防御托管与重连接管状态机，避免弱网抖动或掉线导致的参战槽位丢失与战斗溃散。

#### 1. 核心架构与功能落地

1. **断线槽位保全与离线托管 (`WorldBattle.cpp`)**:
   - `World::disconnectBattleMember(id)`: 参战玩家断开连接时，将其从活跃会话映射及广播流脱钩，但保全 `field.at(slot)` 战斗槽位与 `player_of_slot` 实体句柄，记录入 `disconnected_slots`；
   - `advanceBattles`: 在等待玩家输入前，检查 `b.disconnected_slots` 中占位存活但无活跃会话绑定的单位，自动填入 `kDefense` 防御指令并置标志 `present[slot] = true`，使战场回合不卡顿、平滑推进。

2. **重登接管与多播状态恢复 (`WorldBattle.cpp`)**:
   - `World::reattachBattle(battle_id, old_session, new_session)`: 允许重登上线的新会话无缝接管原有角色战斗槽位，重置映射 `slot_of[new_session] = slot` 并加入 `members`；
   - 入场三件套推送：瞬时下发 `BattleSelfInfo`、`BattleSnapshot` 与 `BattleTurnBegin`，客户端场景自动复原实时战况；
   - 辅助查询：提供 `World::battleOfSession` 与 `World::battleOfPlayer` 支持精准战局定位。

#### 2. 接口扩展与架构合规

- `src/world/include/world/Api.h`:
  - `void disconnectBattleMember(SA::Net::SessionId session);`
  - `bool reattachBattle(BattleId battle, SA::Net::SessionId old_session, SA::Net::SessionId new_session);`
  - `BattleId battleOfSession(SA::Net::SessionId session) const noexcept;`
  - `BattleId battleOfPlayer(SA::Model::EntityHandle player_handle) const noexcept;`
- 架构守卫验证：
  - `include/world/Api.h` 仍为唯一公开头文件（`check_module_boundaries.py` 100% 保持）；
  - 无任何 `GoldLedger` 绕行（`check_gold_writes.py` 100% 保持）；
  - 全工程零告警。

#### 3. 验证与门禁

- **新增单元测试 (`tests/WorldTickTest.cpp`)**:
  - `Battle disconnect: disconnectBattleMember preserves slot and auto-defends in tick`: 验证玩家掉线后战斗槽位保全、回合离线托管防御与 turn 自动推进；
  - `Battle reattach: reattachBattle restores battle control to newly connected session`: 验证新会话接管战斗、发送攻击指令与战后正常推进。
- **全量 CTest 22/22 100% 绿灯**。

---

### 9.0.111 批次 D.4 —— 四大新手村全域场景室内外贯通、全功能NPC接入与组队合击协同机制落地 (Four Villages World & Party Combo Synergy)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 依据实施计划 Phase 3.3 核心大世界与社交战斗协同规范，打通四大新手村（萨姆吉尔、玛丽娜斯、加加、卡鲁它那）全域 73 张室内外地图与 818 个功能 NPC 的运行时装载，并基于石器时代 8.0 官方源码还原组队与人宠合击（Combo）战斗协同机制。

#### 1. 核心架构与功能落地

1. **地图资产管线升级与四大新手村室内外全域装载 (`tools/build_playable_content.py` / `tools/import_all_world_content.py`)**:
   - 修复 LS2MAP 原生头部偏移 6（2 字节大端 uint16）floor ID 解析，根除无数字文件名导致的村落主图 ID 丢失问题，使 1,214 张大世界地图全部具备权威编号；
   - 扩展构建管线将四大主村（萨姆吉尔 1000、玛丽娜斯 2000、加加 3000、卡鲁它那 4000）及其所有的室内功能房（1001-1022、2001-2030、3001-3030、4001-4030，含医院、道具店、武器店、肉店、村长家、便利店、竞技场、道场等）共 73 张地图的可走阻挡图（Base64）全量装载入 `world.json`；
   - 提取并激活四大新手村区域的 815 处 Warp 传送路由与 818 个具名实体 NPC（含 8 个医院 Healer、67 个商店 Shop、43 个传送员 WarpMan、122 个任务使者 ExChangeMan、73 个告示看板、115 个城镇向导/村民）。

2. **石器时代 8.0 原版组队与战斗合击机制 (`WorldBattle.cpp`)**:
   - 依据石器 8.0 官方源码（`battle.c:4410-4450` 及 `battle.c:8605-8648`），在战斗回合推进主循环 `advanceBattles` 中建立合击判定与结算机制：
     - **候选门禁**: 仅针对己方（Side 0，同队玩家与出战宠物），连续行动的近战物理普攻（`CommandKind::ATTACK`），锁定同一存活目标，且非远程/投掷武器（弓、回旋镖等投掷武器严格不可合击），排除多段连击专属技能；
     - **概率判定**: 原版基础合击率 50%（`rng.rand(1, 100) <= 50`）；
     - **伤害结算与协同**: 命中时调用纯函数 `SA::Rules::computeComboDamage` 累积全员伤害并一次性削减防御方生命，为每个合击单位派发冲刺 `HIT` 动作事件，并对目标下发总伤害 `DAMAGE` 事件；
     - **协同去重**: 将后续参与者标记为 `combo_acted`，本回合不再重复单独出手，目标死亡时战果归属于主力发起者。

#### 2. 验证与门禁

- **新增与更新单元测试**:
  - `tests/WorldMapTest.cpp`:
    - 更新全域地图规模断言：73 张楼层地图、815 处传送门、818 个功能 NPC；
    - 新增 `四大新手村全域场景室内外贯通、医院恢复与全量功能NPC交互 (Phase 3.3)`：验证四大村庄与核心室内功能房全部就绪，验证玩家在萨姆吉尔医院 (1005) 接受护士 Healer 治疗恢复满血满蓝，以及跨村庄传送网络连通。
  - `tests/WorldTickTest.cpp`:
    - 新增 `Battle combo: consecutive melee attacks on same target trigger combo damage`：验证同队多近战单位普攻同一目标时触发合击、伤害累加及协同单位不重复出手；
    - 新增 `Battle combo: ranged weapon does not trigger combo`：验证远程武器（弓）严格不触发合击，各自独立出手。
- **全量回归与门禁**:
  - 全量 CTest 22/22 100% 绿灯；
  - 架构守卫工具 `check_module_boundaries.py`, `check_gold_writes.py`, `check_shared_purity.py`, `check_docs_index.py`, `check_dr_table.py` 全数绿灯。

---

### 9.0.112 批次 D.5 —— 客户端原生石刻 UI 质感换装、四大新手村全域交互与双端闭环验证 (Client Stone Aesthetic UI & Four Villages E2E Verification)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 依据实施计划 Phase 4.0 规划，完成客户端原生石刻 UI 质感换装升级（三维双层石刻框线、按下凹陷视差按钮、石刻血槽与气力槽、功能快捷坞及战斗指令轮盘），并在双端实现四大新手村全域室内外贯通、医院恢复、商店货架与跨村传送完整交互闭环。

#### 1. 核心架构与功能落地

1. **客户端原生石刻 UI 质感换装 (`stone-age-client/src/scenes/GameScene.cpp`)**:
   - **石刻材质色阶体系**:
     - 引入浅黄米高光外边 `kStoneHighlight` (`#D6C8AC`)、深褐沉降槽 `kStoneGroove` (`#4A3C28`)、石板古朴底色 `kStoneBg` (`#2A2621`)、沉降阴影 `kStoneShadow` (`#1C1814`)、仿古石雕铆钉 `kStoneRivet` (`#E1D7BE`)；
     - 按钮色系采用苔石原色 `kBtnFaceNormal` (`#4E5C3D`)、灰蚀岩禁用色 `kBtnFaceDisabled` (`#3A3C34`)、铭文金米印文 `kBtnTextInk` (`#FAF0D8`) 与内雕刻印阴影 `kBtnTextShadow` (`#16120E`)；
   - **`StoneButton` 按下凹陷视差反馈**:
     - 继承 `SG::ui::Button` 实现 `StoneButton`，重写 `onPressed()` / `onReleased()` / `onCancelled()`；
     - 按下时将内容挂接根节点位移 `(+1, -1)` 并激活暗调压光层，松开或取消时复位至 `(0, 0)`，完美还原石雕按键按压受力凹陷与弹回手感；
     - 文本渲染升级为底层刻印阴影与顶层亮米金印文的双层雕刻字形；
   - **`stonePanel` 与 `sunkenSlot` 辅助渲染**:
     - `stonePanel`: 自动渲染外框浅黄米高光双边线、外框深褐阴影双边线、内层暗刻沉降凹槽，并在尺寸充足时自动生成四角仿古凸起石钉；
     - `sunkenSlot`: 专用于输入框、状态槽与货架展台，具备上左深陷阴影与下右微亮反光边缘；
   - **主界面与战斗状态排版重构**:
     - 底部状态栏规范化分块：左侧为石器 8.0 徽标与地图地名，中间为角色名与等级、骑乘/组队徽标、石刻生命槽（绿条）与气力槽（蓝条）、随身石币；右侧为清晰规整的 7 项石刻功能快捷坞（设置、地标雷达、任务、聊天、宠物/背包、保存、退出）；
     - 商店货架条目后置沉降石刻展台槽位，NPC 对话窗体根据庄园、家族、摆摊、声望定制特色石板材质；
     - 战斗指令轮盘统一石雕按键质感，子菜单拉出呈现深色石刻抽屉抽拉视觉。

2. **四大新手村全域交互与双端跑通闭环 (`WorldMapTest.cpp` & `ClientNetTest.cpp`)**:
   - **服务端全域验证 (`stone-age-server/tests/WorldMapTest.cpp`)**:
     - 验证四大新手村（萨姆吉尔 1000、玛丽娜丝 2000、加加 3000、卡鲁它那 4000）医院护士（Healer）在 1005、2005、3005、4005 的坐标与朝向，发起 EventRequest 均实现伤势完全治愈与满血满蓝；
     - 验证四大主村道具店（Shop）在 1002、2002、3002、4002 的 NPC 交互与商品窗口下发；
     - 验证四大村落室内外核心出入口双向 Warp 传送网络连通。
   - **客户端协议与消费端到端验证 (`stone-age-client/tests/ClientNetTest.cpp`)**:
     - 新增 `四大新手村全域场景室内外贯通、医院恢复、商店货架与跨村传送完整跑通 (Phase 4.0)` 测试用例：
       - Healer 恢复对话拉起与回执打包送出；
       - Shop 道具店商品展示、条目选购与购买回执打包送出；
       - WarpMan 跨村传送选项展示、目的地选择与回执打包送出；
       - MiniMapRadar 对四大主村室内外 Warp 门点距离排序与指引验证。

#### 2. 验证与门禁

- **服务端**:
  - 全量 CTest 22/22 100% 绿灯；
  - 全部 7 项架构与质量守卫脚本全绿。
- **客户端**:
  - CI 预设测试 7/7 100% 绿灯；
  - Engine 预设测试（含无头 SDL3 引擎驱动与 boot 测试）8/8 100% 绿灯。

---

### 9.0.113 批次 D.6 —— 四大庄园守护战决胜排期、胜负过户与全系骑乘特权及野外暗雷战果掉落双端闭环验证 (Manor War, Ride System & Wild Loot E2E)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 依据实施计划 Phase 4.1 核心大世界竞争与骑乘特权闭环规范，在双端完整落地并验证四大庄园使者（萨姆吉尔暴龙、玛丽娜丝绿暴、加加飞龙、卡鲁它那雷龙）守护战预约约战、决斗比分累加与胜负交割过户，打通庄园家族神兽免考骑乘特权、骑乘考官考核与 20% 金库注资、骑宠契合度相性共鸣、战中人宠生命分摊与濒死落马下马，并在大世界野外实现暗雷步进概率累加遇敌、野生宠物捕获进池与战斗胜利战利品/魔石原子并入背包的双端端到端可玩闭环。

#### 1. 核心架构与功能落地

1. **四大庄园守护战决胜排期与胜负过户交割 (`WorldFamily.cpp` / `WorldMapTest.cpp`)**:
   - 四大庄园使者（萨姆吉尔 `kSamo`、玛丽娜丝 `kMarina`、加加 `kJaja`、卡鲁它那 `kKarutana`）约战排期与胜负交割全流程跑通；
   - 挑战方族长缴纳 100,000+ 押金发起约战，通过 `GoldLedger` 原子冻结并转移庄园状态为 `kScheduled`，拦截第三方重复挑战与已有庄园家族互斥；
   - 决斗阶段累加交战双方积分（`recordManorDuelScore`），推进决胜交割（`concludeManorWar`）：
     - 挑战方胜出：庄园所有权原子过户至挑战方家族，挑战押金全额返还挑战家族金库，并赋予 500 点家族声望；
     - 守方卫冕：庄园保有不变，挑战方没收之押金 100% 注资守方金库，并赋予 200 点家族声望；
     - 战后庄园自动转入保护冷却期（`kCooldown`）。

2. **全系骑乘特权、考官考核与战中生命分摊闭环 (`WorldRide.cpp` / `BattlePresenter.cpp` / `ClientNetTest.cpp`)**:
   - **庄园成员专属特权**: 攻占庄园的家族成员享有免考核骑乘本庄园神兽的特权（萨姆吉尔暴龙、玛丽娜丝绿暴/虎、加加飞龙、卡鲁它那雷龙），`canPlayerRide` 立即生效；
   - **骑乘考官考核与 20% 家族注资**: 非庄园成员通过 `takeRideExam` 参与庄园骑乘考核，扣除 20,000 考核学费，严格对齐原版官方规则（`npc_riderman.c:234` `w.takegold / 5`），其中 20%（4,000 石币）原子注资入当前占领该庄园的家族金库；
   - **外观映射与解构渲染**: 双端严格对齐计算公式 `computeRideImage`（100700 + p_rem + pet_rem），客户端 `isRideImage` 与 `decomposeRideImage` 正确解构骑手与坐骑纹理；
   - **契合度相性共鸣**: `calculateRideAffinity` 依据骑宠等级、忠诚度与基础四维赋予攻防敏属性共鸣；
   - **战中生命分摊与濒死下马**: 客户端 `BattlePresenter` 强化战场快照 (`BattleSnapshot`) 与事件流 (`BattleEvents`) 对骑乘状态 (`RIDE_STATE_RIDING`) 及骑宠生命槽 (`pet_hp`) 的跟踪；受到物理攻击时，骑宠分摊损失 `ride_pet_hp_loss = c.ride_hp / 10`，若骑宠生命归零则自动解除骑乘并落马还原人身外观。

3. **野外暗雷步数计算、野生宠物捕获与战利品拾取链路 (`WorldEncounter.cpp` / `WorldBattle.cpp`)**:
   - **步进概率累加遇敌**: 玩家在大世界移动步进，步数累加步进遇敌概率 `cep`，当随机数命中时清空移动路径串并触发野外暗雷遇敌（`triggerEncounter`）；
   - **野生宠物捕获全流程**: 战中对野生怪物下达 `CAPTURE` 捕获指令，依据等级差、魅力值与敌方残血二次式判定，捕获成功后由 `createPetFromCapture` 将敌人转换为属于玩家的宠物实体，分配到玩家的空闲宠物槽位，玩家 `capture_count` 递增，且目标离场；
   - **战胜战利品与魔石入包**: 战斗击败敌人后，`settleDeaths` 在 `b.getitem` 登记战利品掉落（如大块肉、魔石），战后 `deliverPlayerProfit` 将其原子并入玩家背包，玩家持有道具槽位递增，双端数据同步到达。

#### 2. 验证与门禁

- **服务端单元与端到端测试 (`tests/WorldMapTest.cpp`)**:
  - 新增 `四大庄园守护战决胜排期、胜负交割过户与全系骑乘特权双端端到端跑通 (Phase 4.1)`：验证四大庄园约战排期、比分记录、易主交割、金库注资、免考特权、考官分成、相性共鸣与战中生命分摊；
  - 新增 `大世界野外暗雷步数计算、野生宠物捕获与战果战利品入包端到端跑通 (Phase 4.1)`：验证野外移动步数遇敌、野生乌力捕获入槽、战胜掉落肉与魔石入包。
  - 全量 CTest 22/22 100% 保持全绿。
- **客户端单元与协议测试 (`stone-age-client/tests/ClientNetTest.cpp`)**:
  - 新增 `四大庄园决胜、全系骑乘外观解构与战斗生命分摊同步跑通 (Phase 4.1)`：验证四大庄园专属骑宠外观映射解构、庄园约战窗口与多频道广播过滤、战中骑宠生命分摊快照同步与濒死下马；
  - 新增 `大世界野外暗雷遇敌、野生宠物捕获与战果战利品网络协议端到端跑通 (Phase 4.1)`：验证野外行走请求编解码、野生宠物捕获指令下发与成帧往返。
  - CI 预设 7/7 100% 绿灯，Engine 预设 8/8 100% 绿灯。
- **全仓守卫**:
  - `check_module_boundaries.py`, `check_gold_writes.py`, `check_shared_purity.py`, `check_docs_index.py`, `check_dr_table.py`, `check_format.py` 全部一次性绿灯。

---

### 9.0.114 批次 D.7 —— 玩家多线摆摊行商网络、拍卖行寄售全景流通与系统邮箱到账双端闭环验证 (Multi-Stall, Auction Market & Mail E2E)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 依据实施计划 Phase 4.2 核心大世界行商网络与全景经济生态闭环规范，在双端落地并验证多城镇集市多玩家并发开摊设点、货架配置与招牌文案广播、移动/组队/切磋决斗/安全交易全方位状态机封锁，打通近身切比雪夫距离采购、随身石币上限溢出防爆仓与系统邮件保全（`attached_gold`）、售空自动收摊恢复行走自由；打通跨图全域拍卖行/寄售市场（Consignment Market）跨村异地寄售、关键词与品类过滤检索、5% 交易税清算、离线卖家成交净收益系统邮件送达；打通系统邮箱查阅与附件安全提取防刷状态机。

#### 1. 核心架构与功能落地

1. **大世界集市多摊位并发、状态机封锁与近身采购闭环 (`WorldEconomy.cpp` / `WorldMovement.cpp` / `WorldPartyTrade.cpp`)**:
   - **摆摊货架与招牌配置**: 玩家开辟摊位（`openStall`），自由上架道具（`setStallItem`）与宠物（`setStallPet`），设置摊位招牌文案；启动摆摊（`startStallVending`）广播视野；
   - **状态机全方位安全封锁**: 摆摊中玩家物理移动被阻断（`onWalk` 拦截并清空 `walk_seq`）、禁止发起或接受组队邀请（`joinParty` 互斥）、禁止发起或接受切磋决斗（`requestDuel` 互斥）、禁止发起或接受玩家间交易（`requestTrade` 互斥）；
   - **近身采购门限与超距拦截**: `nearbyStalls(viewer, max_distance)` 精准检索视野内有效摊位；`buyFromStall` 实施严格切比雪夫距离门限（$\le 3$ 允许采购，$\ge 4$ 超距严格拦截阻断）；
   - **随身石币上限溢出防爆仓与邮件保全**: 依据官方原版未转生 1,000,000 随身石币上限，当售出收益导致卖家随身金币达到上限时（`GoldDisposition::kClamped`），超额部分（`overflow`）原子生成系统邮件投递至卖家邮箱（“摆摊收入超额补发”），杜绝吞金与爆仓损耗；
   - **买家满包防御与售空自动解封**: 买家宠物栏满时购买被阻断；当货架全部售空或摊主主动调用 `closeStall`，摊位关闭并自动解除摆摊状态，摊主恢复移动与交互自由。

2. **跨地图全域拍卖行/寄售市场跨村网络流通 (`WorldEconomy.cpp`)**:
   - **异地寄售与 100 挂牌费**: 玩家在任意地图（如萨伊那斯村庄）将高价值装备或灵兽宠物挂牌上架（`listMarketItem`, `listMarketPet`），扣除 100 石币挂牌手续费，上架骑乘宠物时自动解除骑乘；
   - **跨图全局检索与过滤**: 异地买家通过 `searchMarket` 进行跨服/跨地图全局检索，支持关键字匹配与品类过滤（道具/宠物）；
   - **一口价成交与 5% 交易税率清算**: 买家支付一口价石币，道具/宠物原子划拨入包；系统扣除 5% 交易税（`kMarketTaxRatePercent = 5`，通过 `GoldTx` 记账 `kMarketTaxFee` 并触发审计）；
   - **离线卖家成交与系统邮件到账**: 当卖家处于断线/离线托管状态时（`detached = true`），扣除税费后的净收益（95%）全额以系统邮件（“拍卖行成交到账”）安全寄送至卖家邮箱。

3. **系统邮箱与附件提取防刷状态机 (`WorldSocial.cpp`)**:
   - 卖家上线后通过 `playerMails(session)` 查阅信件列表与附件状态；
   - 提取附件石币（`takeMailAttachment`），石币原子充入随身钱包；
   - 提取成功后信件自动标记为附件已取走（`has_attachment = false`），二次提取严格返回 false 阻断刷金。

4. **客户端表现层增强与交互状态机 (`PlayerInteraction.h/.cpp` / `ClientNetTest.cpp`)**:
   - 客户端扩展 `PlayerInteraction`：
     - 摆摊货架管理（自身摊位标题、货架商品/宠物增删、开摊/收摊）；
     - 他人摊位浏览与距离计算（`canBrowseStall`、`StallView`、`StallItemView`）；
     - 拍卖行市场检索与过滤（`MarketListingView`、`filterMarketListings`）；
     - 邮箱与附件管理（`MailView`、`unreadMailCount`、`takeMailAttachment`）。

#### 2. 验证与门禁

- **服务端单元与端到端测试 (`tests/WorldMapTest.cpp`)**:
  - 新增 `大世界多线摆摊行商网络、拍卖行寄售全景流通与系统邮箱到账闭环 (Phase 4.2)`：
    - 验证萨村集市多玩家开摊、货架上架、招牌文案呈现；
    - 验证摆摊状态下移动、组队、决斗、交易全方位封锁；
    - 验证切比雪夫距离 $\le 3$ 采购与超距拦截、满包拦截；
    - 验证 1,000,000 石币上限溢出防爆仓与邮件保全（30,000 石币附件）；
    - 验证售空自动解除封锁并恢复走动；
    - 验证跨图拍卖行寄售、关键字检索、宠物过滤、一口价购买；
    - 验证 5% 交易税清算与离线卖家系统邮件附件送达（475,000 石币）；
    - 验证系统邮箱查阅、附件提取与防重复提取刷金。
  - 全量 CTest 22/22 100% 保持全绿。
- **客户端单元与协议测试 (`stone-age-client/tests/ClientNetTest.cpp`)**:
  - 新增 `大世界多线摆摊、拍卖行寄售检索与系统邮箱附件提取双端协议跑通 (Phase 4.2)`：
    - 验证摆摊货架管理、招牌广播与近身浏览距离判定；
    - 验证全域拍卖行寄售检索、类别过滤与 5% 交易税算力核算；
    - 验证系统离线邮件通知与附件提取防刷状态机。
  - CI 预设 7/7 100% 绿灯。
- **全仓守卫**:
  - `check_module_boundaries.py`, `check_gold_writes.py`, `check_shared_purity.py`, `check_docs_index.py`, `check_dr_table.py`, `check_format.py` 全部一次性绿灯。

---

### 9.0.115 批次 D.8 —— 四大新手村经典成人礼全流程、救父草药支线与生活技能料理/合成制造生态双端闭环验证 (Adult Ceremony, Quests & Lifestyle Crafting E2E)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 依据实施计划 Phase 4.3 核心大世界剧情任务引擎与生活技能制造生态闭环规范，在双端落地并验证石器时代 8.0 标志性剧情与生活系统：
  1. 四大新手村经典成人礼（EventNo: 4）全生命周期闭环（接取委托、差使领玉、满包防爆仓容量门前置防御、交付 15 勾玉换取成人礼首饰、NOWEV/ENDEV 旗标原子交割与永久成人见证）；
  2. 经典救父草药支线任务（EventNo: 1）全流程闭环（村庄小姑娘委托、咍罗山医生赠药、交付草药、护身符发放、经验与声望奖励原子结算）；
  3. 生活技能料理烹饪（Cooking）与装备精炼合成（Synthesis）全景闭环（配方注册、状态机互斥：濒死/战斗/摆摊、等级不足/金币不足/材料不足/槽位作弊/食材混杂严格 0 扣减防御、投入材料消耗与产物首空槽复用、声望奖励增加）；
  4. 客户端表现层增强：`QuestTracker` 扩展成人礼与经典任务元数据及目标动态更新，`PlayerInteraction` 扩展生活技能配方过滤与前置制作门槛校验；
  5. 双端全量测试 100% 绿灯与静态架构守卫验证。

#### 1. 核心架构与功能落地

1. **四大新手村经典成人礼全生命周期 (`ExChangeMan.cpp` / `WorldNpcDialog.cpp`)**:
   - **审判官考验发起 (`kRequest`, EventNo 4)**: 等级 $\ge 1$ 玩家与成人礼审判官交互，弹出确认窗口，确认后原子写入 `NOWEV: 4` 进行中状态，下发寻访石像守护差使引导；进行中再次对话展示进度文案；
   - **差使容量门与仪玉发放 (`kAccept`, NOWEV=4&ITEM!=2417)**: 玩家寻得守护使者；系统在前置容量门（`checkExChangePreconditions`）严格核算背包可用空槽，若空槽不足 15 格（如占用 31 格，仅剩 14 空槽）直接拦截并下发 `item_full_msg`（“如果想要全部的仪玉，就要空出15个空间才可”），杜绝部分发放与吞玉爆仓；背包清理后正向发放 15 枚仪玉（item 2417）；已有仪玉再次对话命中 ITEM!=2417 拦截展示兜底文案，彻底封堵重复领玉漏洞；
   - **交付仪玉与成人礼见证 (`kAccept`, ITEM=2417*15)**: 玩家返回审判官处，系统原子扣除 15 枚仪玉并核发 1 枚成人礼首饰凭证（item 2418）；原子清除 `NOWEV: 4` 并永久写入 `ENDEV: 4`；
   - **永久成人特权与敬畏文案 (`kMessage`, ENDEV=4)**: 成人后再次对话命中 Block 2，展示精进自我的成人见证文案。

2. **加鲁卡村庄小姑娘救父草药支线任务闭环 (`WorldNpcDialog.cpp`)**:
   - **任务接取**: 村庄小姑娘委托前往哈罗山顶取药，接取后写入 `NOWEV: 1`；
   - **医生赠药**: 咍罗山的医生识别求药委托，赠予救命草药（item 2401）；
   - **交付与结算**: 返回小姑娘处交付草药，扣除 2401 并赠予护身符（item 2427），原子结算 500 经验与 25 点家族/个人声望奖励，写入 `ENDEV: 1` 永久完成。

3. **生活技能料理烹饪与装备精炼合成全景生态 (`WorldLifestyle.cpp`)**:
   - **配方注册与多品类管理 (`registerCraftingRecipe`)**: 支持料理（`kCooking`）与精炼合成（`kSynthesis`）独立分类，支持多材料需求、等级要求、手续费要求、产物规格与声望奖励；
   - **严格状态机互斥与防御拦截**:
     - 濒死状态（`hp <= 0`）拦截 `kPlayerDead`；
     - 摆摊状态（`isPlayerVending`）互斥拦截 `kInVending`；
     - 等级不足拦截 `kLevelTooLow`；
     - 制作费不足拦截 `kInsufficientGold`（严格 0 扣减）；
     - 重复槽位防御拦截 `kInvalidSlots`（防刷漏洞防御）；
     - 材料不足拦截 `kMissingIngredient`（严格 0 扣减）；
     - 食材与非食材混杂防御拦截 `kTypeMismatch`（合成武器禁止混入料理食材）；
   - **事务原子性扣减与产物生成**:
     - 手续费精准扣减（通过 `GoldLedger` 记账 `kCraftFee` 并触发审计）；
     - 投入材料槽位全额清空并回收，产物道具精准复用背包中释放的首个空槽；
     - 玩家声望原子累加。

4. **客户端表现层增强与交互状态机 (`QuestTracker.h/.cpp` / `PlayerInteraction.h/.cpp` / `ClientNetTest.cpp`)**:
   - **`QuestTracker` 扩展**:
     - 注册成人仪式（`event_no = 4`）与加鲁卡草药（`event_no = 1`）任务元数据；
     - 支持 `getQuest(event_no)` 获取任务当前生命周期状态（`kNotStarted`、`kInProgress`、`kCompleted`）；
     - 新增 `updateQuestObjective(event_no, text)` 动态更新进行中目标引导文案；
   - **`PlayerInteraction` 扩展生活技能**:
     - 新增 `CraftingIngredientView` 与 `CraftingRecipeView` 配方视图；
     - 新增 `setAvailableRecipes` 载入配方库，`filterRecipes(type)` 按料理/合成筛选；
     - 新增 `canCraft(recipe_id, player_level, player_gold, available_materials)` 提供完备的前置红点与条件校验。

#### 2. 验证与门禁

- **服务端单元与端到端测试 (`tests/WorldMapTest.cpp`)**:
  - 新增 `四大新手村经典成人礼全流程与生活技能制造生态闭环 (Phase 4.3)`：
    - 验证四大村庄成人礼审判官接取、差使领玉、15 空槽满包前置防御、正向领取 15 仪玉、防刷重复领取拦截、交付换取成人礼首饰、NOWEV/ENDEV 旗标原子交割与成人见证；
    - 验证加鲁卡救父草药接取、医生赠药、交付获得护身符、500 经验与 25 声望奖励结算闭环；
    - 验证料理烹饪与装备精炼合成配方注册、濒死/摆摊互斥、等级/金币不足拦截、材料不足 0 扣减、食材混杂拦截、手续费与材料原子扣减、产物入包与声望累加。
  - 用例数从 159 增至 **160 组**，断言数从 4867 增至 **4918 条**（+51 条断言，100% 成功）。
  - 全量 CTest 22/22 100% 保持全绿。
- **客户端单元与协议测试 (`stone-age-client/tests/ClientNetTest.cpp`)**:
  - 新增 `大世界全域剧情任务引擎与生活技能制造生态闭环 (Phase 4.3)`：
    - 验证四大村庄成人礼元数据登记、未开始/进行中/已完成状态感知与动态目标文案刷新；
    - 验证料理与合成配方类型过滤、制作门槛与材料充足性校验。
  - CI 预设 7/7 100% 绿灯。
- **全仓守卫**:
  - `check_module_boundaries.py`, `check_gold_writes.py`, `check_shared_purity.py`, `check_docs_index.py`, `check_dr_table.py`, `check_format.py` 全部一次性绿灯。

---

### 9.0.116 批次 D.9 —— 宠物转生与融合进阶生态、声望商城与称号加成大世界双端全流程贯通验证 (Pet Fusion/Rebirth, Titles & Fame Shop E2E)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 依据实施计划 Phase 4.4 宠物进阶生态与称号/声望商城规范，在大世界端到端链路中落地并验证：
  1. 宠物三宠与两宠融合全流程（主副宠相性与技能遗传、四维成长资质继承、出战/骑乘/摆摊/同槽多重状态互斥、全新融合宠生成）；
  2. 100 级宠物转生全生命周期闭环（100 级等级门槛、最大 2 转上限、出战与骑乘状态互斥、辅助牺牲宠原子销毁、五次方成长算力与资质重算、等级重置为 1）；
  3. 声望商城与荣誉称号系统全景闭环（商城 NPC 货架配置、声望不足/防重购/背包满/宠物栏满严格 0 扣减防御、原子兑换到账）；
  4. 称号佩戴与四维属性加成即时联动（未拥有拦截、声望不足门槛拦截、佩戴称号生命/攻击/防御/敏捷即时生效与卸下归零）；
  5. 客户端交互状态机扩展（`PlayerInteraction` 称号管理、声望商城浏览与门槛预检、转生融合候选过滤）；
  6. 双端测试 100% 绿灯与反向变异实证（RV）。

#### 1. 核心架构与功能落地

1. **大世界宠物融合与状态机防御 (`WorldPetFeatures.cpp` / `WorldMapTest.cpp`)**:
   - 验证主副宠合法性与槽位互斥（相同槽位 `kInvalidSlot`）；
   - 摆摊出摊与交易中锁定互斥（`kPetInTradeOrStall`）；
   - 正向三宠融合：原子释放副宠 1 与副宠 2，生成全新融合宠（`isPetFusion == true`），继承主宠属性倾向与技能；
2. **大世界宠物转生全生命周期 (`WorldPetFeatures.cpp` / `WorldMapTest.cpp`)**:
   - 等级门禁（`< 100` 级拦截 `kInsufficientLevel`）；
   - 出战与骑乘状态互斥拦截（`kPetInBattleOrRide`）；
   - 辅助牺牲宠出战互斥与同槽拦截（`kInvalidSacrifice`）；
   - 正向 1 转：辅助宠原子销毁，目标宠等级重置为 1，四维资质重算，`petTransCount` 累加至 1；
3. **声望商城与荣誉称号大世界全景联动 (`WorldLifestyle.cpp` / `WorldMapTest.cpp`)**:
   - 注册「萨姆吉尔勇者」（HP+100, ATK+20, DEF+10, DEX+10）与「玛丽娜斯精灵学者」；
   - 部署声望商城 NPC 9001，配置称号、稀有道具、珍稀骑宠条目；
   - 初始声望不足拦截 `kInsufficientFame`（严格 0 扣减）；
   - 正向购买称号精准扣除声望，重复购买已拥有称号拦截 `kAlreadyHaveTitle`；
   - 背包满购买道具拦截 `kInventoryFull`（严格 0 扣减）；
   - 称号佩戴门槛拦截（声望 `< req_fame` 拦截 `equipTitle` 失败）；
   - 佩戴后属性加成即时生效，卸下后属性加成归零；
4. **客户端表现层增强 (`PlayerInteraction.h/.cpp` / `ClientNetTest.cpp`)**:
   - `TitleView` 与 `FameShopItemView` 视图结构；
   - `setOwnedTitles`、`setActiveTitleId`、`canEquipTitle`；
   - `canBuyFromFameShop`（声望、背包、宠物栏与已拥有称号多层预检）；
   - `canRebirthPet` 与 `canFusePets` 状态互斥与门槛候选校验。

#### 2. 验证与门禁

- **服务端单元与端到端测试 (`tests/WorldMapTest.cpp`)**:
  - 新增 `大世界宠物转生与融合进阶生态、声望商城与称号加成双端全流程贯通 (Phase 4.4)` 用例（3 大 SUBCASE）；
  - 用例数从 160 增至 **161 组**，断言数从 4918 增至 **4983 条**（+65 条断言，100% 成功）；
  - 执行反向变异实证（RV-1：变异转生次数期望值 1 为 2，测试精确转红）；
  - 全量 CTest 22/22 100% 保持全绿。
- **客户端单元与协议测试 (`stone-age-client/tests/ClientNetTest.cpp`)**:
  - 新增 `大世界宠物转生与融合进阶生态、声望商城与称号加成双端全流程贯通 (Phase 4.4)` 用例；
  - CI 预设 d2-only 7/7 与 engine 8/8 100% 绿灯。
- **全仓守卫**:
  - `check_module_boundaries.py`, `check_gold_writes.py`, `check_shared_purity.py`, `check_docs_index.py`, `check_dr_table.py`, `check_format.py` 全部一次性绿灯。

---

### 9.0.121 阶段 6.0 —— S11 精灵/天使系统架构落地与双向瞬移契约管线 (Angel & Spirit System Pipeline)

> **本批聚焦**: 落实 `docs/13-d8-coverage.md` 第 11 行 S11 精灵/天使系统（Angel/Spirit System）核心功能。基于 `shared/rules/Angel.h/cpp` 零堆分配纯函数与 `src/world/WorldAngel.cpp` 契约管线，实现使命发布、契约签订、双向瞬移召唤、使者信物遇敌抑制（`isAngelModeActive` 1:1 对齐原版 `CHAR_WORKANGELMODE`）、神佑防御/减伤守护（15% 防御增益 / 10% 最终伤害减免）以及双向独立交付领奖闭环。

#### 1. 架构设计与实现

1. **L3 规则层纯函数与数据模型 (`shared/rules/Angel.h` / `Angel.cpp`)**:
   - `AngelRole`（None, Messenger/使者, Hero/勇者）；
   - `AngelMissionStage`（None, Summoned, Accepted, Completed）；
   - `AngelWarpResult` 传送门禁诊断枚举；
   - 基础常量：`kAngelTokenItemId = 2884`（使者信物）、`kHeroTokenItemId = 2885`（勇者信物）、`kAngelDamageReductionBps = 1000`（10% 减伤）、`kAngelDefenseBonusBps = 1500`（15% 防御增益）；
   - 纯函数矩阵：
     - `isEligibleForAngelMission`：资格与等级门限判定；
     - `hasAngelMissionExpired`：基于绝对时间戳的使命超时判定；
     - `canUseAngelTokenToWarp`：瞬移传唤前置条件门禁校验（等级、存活、非战斗、角色与契约阶段校验）；
     - `calculateSpiritBlessingDamageReduction` 与 `calculateSpiritBlessingDefenseBonus`：神佑减伤与防御力增益纯函数计算；
     - `isWildEncounterSuppressed`：遇敌抑制状态机。
2. **战中神佑守护接入 (`shared/rules/Combatant.h` / `Battle.cpp`)**:
   - `CombatModifiers` 扩充 `bool spirit_blessing`；
   - `Battle.cpp` 伤害结算中注入 `calculateSpiritBlessingDamageReduction` 与防御力加成。
3. **大世界契约管线与运行时 (`src/world/WorldAngel.cpp` / `WorldImpl.h`)**:
   - `WorldImpl` 维护使命注册表 `angel_missions_` 与活跃契约字典 `angel_contracts_`；
   - `registerAngelMission`：支持神使 NPC 登记天使使命，配置所需道具、目标怪物击杀数与时限；
   - `createAngelContract` / `acceptAngelContract`：使者接取契约并指定勇者伙伴，生成唯一契约记录并向使者分发使者信物（2884）；勇者确认接取后分发勇者信物（2885）；
   - `useAngelToken`：持有信物使用瞬移拉取伙伴，双向同步跨地图传送与视野广播；
   - `isAngelTokenEquipped` 与 `isAngelModeActive`：使者穿戴信物（首饰槽位 4）激活天使模式；
   - `WorldMovement.cpp`：移动步进暗雷检测中若 `isAngelModeActive` 为真，`eff_cep` 遭遇概率归零，实现 100% 暗雷遇敌抑制；
   - `WorldBattle.cpp`：开战投影向战斗者实体注入 `s.hasSpiritBlessing(mid)`；
   - `claimAngelReward`：使者与勇者各自独立前往神使交付领奖，支持独立 `angel_claimed` 与 `hero_claimed` 状态追踪，双方均交付后契约标记 `kCompleted`。

#### 2. 验证与反向变异双证据

- **单元与规则测试 (`tests/RulesBattleTest.cpp`)**:
  - 新增 `Phase 6.0: S11 精灵/天使系统纯函数规则与神佑防护测试`；
  - 用例数增至 **178 组**，断言数增至 **3,408 条**（全绿）；
  - **RV-Angel-1 反向变异**：破坏神佑减伤公式（基准减伤改为 0），实证精确捕获 2 处测试断言失败。
- **大世界集成测试 (`tests/WorldMapTest.cpp`)**:
  - 新增 3 组集成用例：`使命注册与契约建立/信物发放`、`使者模式暗雷抑制与神佑战斗防护`、`双向瞬移传唤与独立交付领奖`；
  - 用例数增至 **153 组**，断言数增至 **2,731 条**（全绿）；
  - **RV-Angel-2 反向变异**：破坏瞬移坐标对齐精度，实证精确捕获坐标断言失败。
- **纯度与质量守卫**:
  - `check_shared_purity.py` 30/30 纯净（零堆分配、零外部依赖）；
  - 全套 CTest 22/22 100% 绿灯通过；
  - `check_format.py` 代码格式 100% 合规。

---

### 9.0.122 阶段 6.1 —— 客户端天使契约与神佑状态表现层对接 (Client Angel System & Spirit Blessing Presentation)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 落实 S11 精灵/天使系统客户端表现与交互状态机对接（阶段 6.1）。实现大世界场景交互与角色状态管理（`AngelContractView` / `AngelRoleKind` / `canUseAngelToken`）、天使模式暗雷遇敌抑制状态感知（`isAngelMode` / `isEncounterSuppressed`）、神佑守护状态（`hasSpiritBlessing`），并在战斗表现层（`BattlePresenter`）对接战中神佑守护状态标识与 `DAMAGE_FLAG_GUARDIAN` 伤害事件流结构化文本解析，完成双端确定性闭环。

#### 1. 核心架构与功能落地

1. **客户端玩家交互与状态层 (`PlayerInteraction.h` / `PlayerInteraction.cpp`)**:
   - 定义 `AngelRoleKind`（kNone, kAngel, kHero）；
   - 定义 `AngelContractView` 契约视图模型（契约 ID、使命 ID、角色、阶段状态、伙伴昵称/CDKEY、使命详情、超时标记等）；
   - 实现契约视图生命周期管理：`setAngelContract`、`clearAngelContract`、`hasAngelContract`、`angelContract()`；
   - 实现信物使用门禁与角色权限互斥校验：`canUseAngelToken(item_id)`：
     - 使者信物 2884：仅使者角色（`kAngel`）且契约处于执行中（`stage == 2`）且未超时可使用；
     - 勇者信物 2885：仅勇者角色（`kHero`）且契约处于执行中（`stage == 2`）且未超时可使用；
     - 契约未接取、已超时或非对应角色直接阻断拦截；
   - 实现神佑守护与天使模式状态管理：`setSpiritBlessing`/`hasSpiritBlessing` 与 `setAngelMode`/`isAngelMode`，打通暗雷遇敌抑制感知（`isEncounterSuppressed()`）。

2. **客户端战斗表现层 (`BattlePresenter.h` / `BattlePresenter.cpp`)**:
   - `UnitView` 与 `SlotState` 扩充 `bool has_spirit_blessing` 神佑守护状态字段；
   - 增加公共接口 `setUnitSpiritBlessing(slot, active)` 与 `unitSpiritBlessing(slot)`；
   - `applyEvent` 中捕获含有 `DAMAGE_FLAG_GUARDIAN`（512）的伤害事件，自动标记受击目标槽位处于神佑加持状态；
   - `describeEvent` 中解析伤害事件标志位，当命中 `DAMAGE_FLAG_GUARDIAN` 时增加 `"神佑守护 "` 前缀，实现战中神佑守护减伤与防护效果的精准结构化广播呈现。

#### 2. 验证与门禁

- **客户端单元与协议测试 (`tests/ClientNetTest.cpp`)**:
  - 新增 `大世界天使契约与神佑守护状态表现端到端跑通 (Phase 6.1)` 测试用例（4 大 SUBCASE）；
  - 覆盖契约视图生命周期、信物门禁互斥校验、神佑/使者模式暗雷抑制、战斗神佑呈现与事件流解析全链路；
  - **RV-Angel-Client-1 反向变异实证**：篡改 `canUseAngelToken` 中使者信物 ID 为 9999，精确触发 2 处测试断言失败（红灯），还原后一次性恢复全绿；
  - 客户端 CTest 7/7 100% 绿灯。
- **服务端与文档守卫**:
  - 服务端 CTest 22/22 保持 100% 绿灯；
  - `check_docs_index.py`、`check_format.py` 守卫全绿。

---

### 9.0.123 阶段 7 —— S22 saac 客户端通信管线与账号中心服务架构落地 (Account & Central Server Protocol Pipeline)

- **日期**: 2026-10-03
- **分支**: `master`
- **目标**: 落实 `docs/13-d8-coverage.md` 第 22 行 S22 saac 客户端 / 中心账号服务通信管线（阶段 7）。依据 `00-architecture.md` §3/§4/§7 架构裁定与 `17-saac-boundary.md` 取证成果，在服务端新增独立模块 `src/saac/`（静态库 `sa_saac`），建立带三元组信封（`instance_id`, `generation`, `request_id`, `deadline_ms`）的 RPC 在途生命周期管理与超时扫描机制，彻底消弭原版 `fdid` 归零引发跨实例串包的致命缺陷（C33/C35）；实现账号登录鉴权、角色档案加载/保存、排他锁与租约状态机（消弭原版移民锁误解缺陷 C45），并打通 8.0 独有跨线路全服广播/聊天室分发通道（00 §7）。

#### 1. 核心架构与功能落地

1. **统一公开接口与信封模型 (`src/saac/include/saac/Api.h`)**:
   - 严格遵循 `check_module_boundaries.py` 守卫规范，模块仅暴露单一 `Api.h` 头；
   - 定义 `RequestEnvelope` 与 `ResponseEnvelope` 三元组信封模型；
   - 定义 `SaacAccountStatus` 诊断状态码与 `AccountLockType` 排他锁类型（普通锁与移民锁显式隔离）；
   - 定义 `ISaacClient` 抽象接口与 `createSaacClient` 工厂入口。
2. **协议帧成帧封包与解包 (`src/saac/SaacProtocol.cpp`)**:
   - 实现长度前缀成帧打包 `encodeRequest` 与响应帧校验解析 `decodeResponse`；
   - 覆盖核心协议类型：`kLoginReq/Resp`、`kCharLoadReq/Resp`、`kCharSaveReq/Resp`、`kLockReq/Resp`、`kBroadcastReq/Ack`。
3. **在途生命周期与跨实例世代隔离 (`src/saac/SaacClient.cpp`)**:
   - `SaacClient` 维护原子递增 `request_id` 与并发安全在途表 `_inFlight`；
   - `feedResponse` 强校验回包 `instance_id` 与 `generation`：旧实例世代回包自动拦截丢弃并累计 `staleDropCount()`，杜绝跨实例会话串包；
   - `tick(now_ms)` 扫描超时请求，自动触发超时回调并归还资源，累计 `timeoutDropCount()`；
   - `broadcastWorldMessage` 与 `registerBroadcastListener` 实现跨线路世界广播与分发监听。

#### 2. 验证与反向变异双证据

- **单元与协议测试 (`tests/SaacClientTest.cpp`)**:
  - 新增 `saac_client` 独立测试套件（5 大 TEST_CASE）；
  - 覆盖协议帧封包解包、跨实例世代号防御丢弃、超时扫描与 In-Flight 清理、排他锁申请/释放、跨线路全服广播；
  - **RV-Saac-1 反向变异实证**：在 `feedResponse` 中故意移除世代号比对逻辑，测试立即精确捕获 5 处断言失败（红灯），还原后一次性恢复全绿；
  - CTest 用例数增至 **23/23**，100% 保持全绿。
- **全平台 CI 远端实证 (GitHub Actions Run 37135872029)**:
  - Commit `652f582` 在 Linux GCC、macOS Apple Clang、Windows MSVC 以及 MySQL 8.4.8 + Redis 8.6.2 集成测试中 **100% SUCCESS**；
  - `ci_verify.py` EXPECTED_TESTS 完整登记 23 项测试（含 `saac_client`），清洁构建零告警、断言防线反向探针验证通过。
- **模块边界与格式守卫**:
  - `check_module_boundaries.py` 严格校验 5 个模块依赖与单一暴露头，100% 通过；
  - `check_docs_index.py`、`check_format.py`、`check_gold_writes.py` 守卫全绿。

---

### 9.0.124 阶段 8 —— 跨线路社交体系与全服家族战系统落地 (Cross-Server Social & Manor War)

- **日期**: 2026-10-04
- **分支**: `master`
- **目标**: 落实 `docs/13-d8-coverage.md` 第 7 行 S07 社交-聊天室与四大庄园全服家族战系统（阶段 8）。依据 `00-architecture.md` §3/§4.3/§7（D8 核心子系统 S06/S07 跨线路聊天室 506 行 & 全服四大庄园战）架构裁定，在 `src/world/` 新增 `WorldCrossServer.cpp`，打通与 S22 `sa_saac` 广播总线的数据协同管道，严格恪守 `check_module_boundaries.py` 单一暴露头守卫（仅暴露 `src/world/include/world/Api.h`）；实现跨线路世界喊话多播 (`kTalkShout`)、跨线路私聊路由 (`kTalkTell`) 与黑名单拦截过滤、全服系统公告 (`kTalkSystem`)；实现 8.0 独有跨线路独立聊天室体系（房间创建、加入、退出、密码保护、容量门禁、房内发言多播与房间列表查询）；实现跨线路好友上下线在线状态感知 (`kCrossChannelFriendPresence`)；实现全服四大庄园跨服唯一权威所有权同步、跨服约战排期与状态锁定 (`kScheduled`)、跨服决斗比分累加汇聚与决胜过户 (`kCooldown` 与押金注资)。

#### 1. 核心架构与功能落地

1. **跨线路通信接口扩展与解耦设计 (`src/world/include/world/Api.h` / `src/saac/include/saac/Api.h`)**:
   - `ChatChannel` 扩充：新增 `kTalkRoom = 5`（跨线路独立聊天室）与 `kTalkSystem = 6`（全服系统公告）；
   - 定义 `ChatRoomInfo` 数据结构（房间 ID、名称、房主、密码保护标记、最大人数、当前在线人数）；
   - `World` 增加跨服通信接入与聊天室管理接口：`setSaacClient`、`saacClient`、`createChatRoom`、`joinChatRoom`、`leaveChatRoom`、`playerChatRoom`、`listChatRooms`、`sendChatRoomMessage`、`broadcastSystemAnnouncement`；
   - `World` 增加庄园跨服同步接口：`syncManorStateToCrossServer`、`syncManorWarScheduleToCrossServer`、`syncManorDuelScoreToCrossServer`、`syncManorWarConclusionToCrossServer`；
   - `ISaacClient` 接口解耦：引入 `setBroadcastSender` 与 `feedBroadcastMessage`，彻底消弭多节点在广播 Hub 间转发时的无限递归死循环（Stack Overflow 防御）。
2. **跨线路消息路由与聊天室生命周期 (`src/world/WorldCrossServer.cpp`)**:
   - 划分跨服协议通道：
     - `100` (`kCrossChannelShout`): 世界大喊跨线路广播；
     - `101` (`kCrossChannelTell`): 跨线路私聊路由，目标节点自动匹配在线角色并施加黑名单阻断；
     - `102` (`kCrossChannelRoomChat`): 跨服聊天室消息定向分发至本地房间成员；
     - `103` (`kCrossChannelRoomSync`): 跨服聊天室元数据同步（创建、加入、退出全服字典同步）；
     - `104` (`kCrossChannelFriendPresence`): 跨服好友上下线状态感知；
     - `105` (`kCrossChannelManorSync`): 全服四大庄园状态、排期、比分与归属全量权威同步；
     - `106` (`kCrossChannelAnnouncement`): 全服系统公告广播；
   - 聊天室管理：支持最大 2-50 人容量限制、密码校验、多服成员列表聚合；退出时若房间为空自动销毁全服资源。
3. **大世界社交与庄园战跨服管线贯通 (`src/world/WorldSocial.cpp` / `src/world/WorldFamily.cpp`)**:
   - `sendChat` 中当频道为 `kTalkShout`、`kTalkTell`（本地未找到目标时尝试跨服路由）、`kTalkRoom` 时，自动装配跨服网络帧并派发；
   - `notifyAddressBookStatus` 在角色上线/下线时向全服广播 `kCrossChannelFriendPresence`；
   - `occupyManor`、`challengeManor`、`recordManorDuelScore`、`concludeManorWar` 在本地权威校验通过后，自动驱动跨服庄园状态同步，确保全线全服庄园所有权与比分唯一一致。

#### 2. 验证与反向变异双证据

- **单元与集成测试 (`tests/WorldCrossServerTest.cpp`)**:
   - 新增 `world_cross_server` 独立测试套件（6 大 TEST_CASE / 155 个断言）：
     1. `跨线路全服世界喊话与系统公告多播`（双节点大喊互通与系统公告多播）；
     2. `跨线路私聊路由与黑名单过滤`（本地未命中时跨服路由，接收方黑名单精准拦截）；
     3. `跨线路独立聊天室全生命周期`（跨服建房、入房、退房、跨服发言广播、全服房间列表查询）；
     4. `跨线路好友上下线状态感知`（异服上线自动感知并刷新名片簿在线状态）；
     5. `全服四大庄园跨服排期、比分汇聚与胜负交割`（跨服占领同步、跨服约战锁定 `kScheduled`、跨服比分累加汇聚、决胜过户至挑战家族并转入休战期 `kCooldown`）；
     6. **阶段 8.6 RV-CrossServer-1 反向变异实证**：
        - 聊天室密码保护与满员容量防御：错误密码加入被精准阻断；2/2 满员后异服新玩家加入被精准阻断；
        - 庄园战越权交割防御：未约战未排期庄园尝试强制交割被拦截，庄园归属不受污染；
        - 畸形网络包防御：注入未知 channel_id 跨服帧，节点安全丢弃而不崩溃。
   - CTest 用例数增至 **24/24**，100% 保持全绿。
- **质量与工程守卫**:
   - `tools/ci_verify.py` 6 项守卫全部绿灯（测试注册清单 24 项完整、WERROR 清洁构建零告警、断言防线反向探针验证通过）；
   - `check_module_boundaries.py` 模块边界严格校验 100% 通过（world 仅依赖 net, platform, saac, session_storage，单一暴露头）；
   - `check_docs_index.py`、`check_format.py` 守卫全绿。
- **全平台 CI 远端实证 (GitHub Actions Run 37139697765)**:
   - Commit `ca604f6` 在 Linux GCC、macOS Apple Clang、Windows MSVC 以及 MySQL 8.4.8 + Redis 8.6.2 集成测试中 **100% SUCCESS**；
   - `ci_verify.py` EXPECTED_TESTS 完整登记 24 项测试（含 `world_cross_server`），清洁构建零告警、断言防线反向探针验证通过。

---

### 9.0.125 阶段 9 —— 客户端跨服社交表现层与全服庄园战决斗演练对接 (Client Cross-Server Social UI & Manor Duel Presentation)

- **日期**: 2026-10-04
- **分支**: `master`
- **目标**: 落实 S07 跨线路聊天室与全服家族庄园战在客户端的表现层状态机对接（阶段 9）。依据 `docs/09-social-ui.md` §1/§5/§6 规格裁定，在客户端 `ChatManager` 与 `PlayerInteraction` 实现跨线路独立聊天室生命周期管理（创建/加退/密码验证/满员容量/房内发言多播/列表过滤）、名片簿跨服好友在线状态感知（`updateFriendPresence`）与私聊黑名单阻断门禁（`canSendTell`），以及全服四大庄园守护战约战排期告示、决斗实时比分战况看板与决胜过户休战保护呈现（`ManorWarView` / `ManorWarState`）；完善确定性单元测试与反向变异实证。

#### 1. 核心架构与功能落地

1. **跨线路独立聊天室模型与生命周期 (`src/scenes/ChatManager.h` / `ChatManager.cpp`)**:
   - `ChatChannelType` 扩充：增加 `kTalkRoom = 6`（跨线路独立聊天室）以及标签 `"[聊天室]"`；
   - `ChatFilter` 扩充：增加 `kRoom = 5` 专属过滤器；
   - 增加 `ChatRoomView` 视图模型（房间 ID、名称、房主、是否加密、密码、最大人数、当前人数、成员列表）；
   - 实现房间创建门禁 `canCreateRoom`（2..50 人容量限制、名称非空）；
   - 实现房间加入门禁 `canJoinRoom`（密码匹配校验、满员安全阻断）；
   - 实现房间列表检索过滤 `filterAvailableRooms`、房主判定 `isRoomOwner`、成员增删 `addRoomMember`/`removeRoomMember`；
   - 实现房间消息格式化呈现 `addRoomMessage`。
2. **名片簿跨服好友在线感知与私聊黑名单 (`src/scenes/PlayerInteraction.h` / `PlayerInteraction.cpp`)**:
   - 定义 `AddressCardView` 数据结构（卡片 ID、角色名、等级、形象、是否在线、是否拉黑、跨服标识）；
   - 实现跨服上下线广播动态感知刷新 `updateFriendPresence`；
   - 实现私聊黑名单阻断拦截门禁 `canSendTell`。
3. **全服四大庄园约战排期与决斗看板 (`src/scenes/PlayerInteraction.h` / `PlayerInteraction.cpp`)**:
   - 定义 `ManorWarState`（kPeace, kScheduled, kInProgress, kCooldown）与 `ManorWarView` 视图模型；
   - 实现守护战排期告示描述 `describeManorSchedule`；
   - 实现决斗实时比分汇聚与战况看板 `updateManorDuelScore` / `describeManorDuelScore`；
   - 实现决胜过户与全服休战保护期呈现 `concludeManorWar` / `describeManorConclusion`。

#### 2. 验证与反向变异双证据

- **客户端单元测试 (`tests/ClientNetTest.cpp`)**:
   - 新增 `阶段 9: 客户端跨服社交表现层与全服庄园战决斗演练 (Phase 9)` 测试套件（4 大 SUBCASE）；
   - 覆盖聊天室创建门禁/加退/密码验证/多播过滤、名片簿跨服感知/私聊黑名单、庄园约战排期/比分战报/决胜过户全景链路；
   - **RV-CrossServer-Client-1 反向变异实证**：错误密码阻断、满员加入阻断、拉黑用户私聊阻断、非法容量创建阻断，断言精准转红并安全还原；
   - 客户端 7/7 CTest 全绿，8/8 守卫 100% 通过。
- **全平台 CI 远端实证 (GitHub Actions Run 37170889271)**:
   - Commit `424ac58` 在 Linux GCC、macOS Apple Clang、Windows MSVC 全平台 **100% SUCCESS**。

---

### 9.0.126 阶段 10 —— 大世界昼夜交替与动态气候环境系统落地 (Day/Night & Dynamic Weather System)

- **日期**: 2026-10-04
- **分支**: `master`
- **目标**: 依据石器时代 8.0 原版历法规则与地图特效规范，落地大世界时间流动引擎、昼夜四时段状态机、调色板映射以及动态气候调度与 EF 协议管线。

#### 1. 核心架构与功能落地

1. **L3 石器历法与昼夜时段流动纯函数 (`shared/rules/LSTime.h` & `LSTime.cpp`)**:
   - 严守 01 §4 / 05 §1.5 纯函数约束，禁止包含 `<chrono>` / `<ctime>`，时间戳以入参形式注入，保证全平台 100% 确定性与可回放；
   - 石器时代历法常量（1:1 复刻原版 `handletime.c`）：
     - `kEraSeconds = 912766409 + 5400`（纪元起点）
     - `kSecondsPerLSDay = 5400`（现实 5400 秒 = 90 分钟 = 1.5 小时为 1 石器天）
     - `kHoursPerLSDay = 1024`（1 石器天划分为 1024 个刻度，0..1023）
     - `kDaysPerLSYear = 100`（1 石器年 = 100 石器天）
   - 四时段状态机 `LSTimeSection`（1:1 对齐 `LSTIME_SECTION`）：
     - `kNight = 0`（黑夜）：`300 < hour <= 700`，对应经典夜景调色板 `PALET_3`
     - `kMorning = 1`（清晨）：`700 < hour <= 930`，对应晨曦微光调色板 `PALET_1`
     - `kNoon = 2`（正午/白天）：`930 < hour <= 1023 || 0 <= hour <= 200`，对应明朗白昼调色板 `PALET_2`
     - `kEvening = 3`（黄昏/傍晚）：`200 < hour <= 300`，对应夕阳暮霭调色板 `PALET_4`
   - 实现时间正逆向高精度转换函数：`computeLSTime` 与 `computeRealTime`。

2. **L2 大世界动态气候环境调度器 (`src/world/WorldWeather.h` & `WorldWeather.cpp`)**:
   - 天气类型掩码 `WeatherKind`：`kNone` (0), `kRain` (1), `kSnow` (2), `kCherryBlossom` (4), `kStarFall` (8)；
   - 单地图气候状态机 `MapWeather`：包含地图 floor_id、天气类型、强度 level [1..5]、到期时间戳与附加参数；
   - 天气生命周期管理：支持设置场景天气 `setWeather`、清除 `clearWeather`、超时检测 `tick` 与恢复默认晴朗回调；
   - 原版 8.0 `EF` 协议成帧与解析：`formatWeatherPacket` / `parseWeatherPacket` 编解码。

3. **大世界世界循环集成与跨图同步 (`src/world/World.cpp` / `WorldMovement.cpp` / `WorldImpl.h`)**:
   - `World::tick` 定时业务阶段驱动气候时钟，到期自动多播重置特效；
   - 玩家跨图传送（`warpSinglePlayer`）及上线登录时自动拉取并下发当前楼层最新天气环境；
   - 暴露公共观察面：`setMapWeather`、`clearMapWeather`、`mapWeather`、`isMapWeatherClear`、`currentLSTime`、`currentLSTimeSection`、`pollWeatherEvents`、`playerWeather`。

#### 2. 验证与反向变异双证据

- **新增测试目标 (`tests/WorldWeatherTest.cpp` / `world_weather`)**:
   - 涵盖历法时间纪元推进与往返等价性、四时段状态机判定与调色板映射、EF 协议编解码、场景天气设置与强度钳位、超时到期恢复晴朗以及跨图传送环境同步闭环；
   - **RV-Weather-1 反向变异实证**：实证时段分界线防篡改、非法天气强度严格钳位至 5 级以内防止客户端粒子系统溢出崩溃。
- **全量 CTest 25/25 100% 绿灯**，6 项守卫通过，断言防线反向验证通过。
- **全平台 CI 远端实证 (GitHub Actions Run 37172669412)**:
   - Commit `749a408` 在 Linux GCC、macOS Apple Clang、Windows MSVC、MySQL+Redis 集成全平台 **100% SUCCESS**。

---

### 9.0.127 阶段 11 —— 客户端昼夜调色板渐变渲染与全景动态天气粒子系统对接 (Client Weather Particle & Day/Night Palette Presentation)

- **日期**: 2026-10-04
- **分支**: `master`
- **目标**: 依据 8.0 经典调色板映射与大世界天气特效协议，在客户端落地昼夜调色板渐变渲染引擎 (`PaletteManager`) 与全景动态天气粒子系统 (`WeatherEffect`)，完成双端历法与天气规则的闭环贯通。

#### 1. 核心架构与功能落地

1. **客户端昼夜调色板状态机 (`stone-age-client/src/scenes/PaletteManager.h` & `PaletteManager.cpp`)**:
   - 支持经典 16 级调色板映射（`PALET_0` 室内基础、`PALET_1` 晨曦朝霞、`PALET_2` 正午亮昼、`PALET_3` 沉夜浓墨、`PALET_4` 夕阳暮霭、`PALET_5` 地底洞窟等）；
   - 环境光 RGB 增益乘子模型与线性插值（LERP），默认 2000ms 平滑渐变过渡，消除场景突兀切换；
   - 对接 L3 `SA::Rules::LSTime`，支持由 Unix 现实时间戳直接驱动石器时段与调色板；
   - 室内/洞窟独立覆盖（`setOverridePalette`）与离开恢复机制（`clearOverridePalette`）。
2. **全景动态天气粒子系统 (`stone-age-client/src/scenes/WeatherEffect.h` & `WeatherEffect.cpp`)**:
   - 完整消费 8.0 `EF` 协议报文（`"EF <type> <level> [option]"`）；
   - 粒子物理模拟：
     - 雨（垂直高速降落、微风偏角）；
     - 雪（轻盈下落、正弦摆动）；
     - 落樱（自由翻滚旋转、随风起伏飘散）；
     - 流星（向左下极速划过夜空、生命期快速淡入淡出）；
   - 强度等级控制（1..5 级动态容量控制，40..200 粒子）；
   - 淡出消散态（FadeOut）：收到停止天气命令后停止发射新粒子，现有粒子透明度按衰减曲线自然消逝；
   - 纯函数伪随机引擎（XorShift32），保证全平台零 I/O、确定性与跨平台行为一致。
3. **主场景与测试装配**:
   - `GameScene` 集成 `palette_manager` 与 `weather_effect` 步进时钟；
   - `sa_client_net_test` 链接 `sa_shared`，实现双端规则零引擎依赖验证。

#### 2. 验证与反向变异双证据

- **客户端单元测试 (`stone-age-client/tests/ClientNetTest.cpp`)**:
   - 新增 `阶段 11: 昼夜调色板渐变与全景动态天气粒子系统对接 (Phase 11)` 测试用例（4 大 SUBCASE）；
   - 覆盖调色板基准时段映射、Unix 秒数驱动、50%/100% 进度色彩线性插值、洞窟覆盖隔离与恢复；
   - 覆盖 EF 协议解析（雨、雪、落樱、流星雨）、1..5 级容量控制、淡出与完全消散；
   - **RV-Weather-Client-1 反向变异实证**：调色板编号越界夹紧防御（负数与超大值）、畸形 EF 报文防御、非法天气类型防御、零步长/负数步长物理步进防御；
   - 客户端 CTest 7/7 100% 绿灯，守卫 8/8 项全部通过。




