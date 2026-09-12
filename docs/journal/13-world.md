# journal / 13-world — 世界系统:移动、视野、刷怪、遇敌触发

> **后续勘误（2026-09-12）**：本文件保留当时交付记录。与当前审计有关的数值、时序、接线及验收更正见[修复记录](../audits/2026-09-12-remediation-results.md)，不以旧批次完成状态代替本轮验证。

> **本文件收录**:W.1 移动 + 529 格视野、W.4 暗雷遇敌闭环、W.2+W.3 世界敌人(刷怪 + 条数制摊还 + 游荡 AI)、W.5 明雷触发战斗。
>
> **批次编号**:§9.0.42 · §9.0.44 · §9.0.45 · §9.0.46(共 4 节,§9.0.42 – §9.0.46 区间内)
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
