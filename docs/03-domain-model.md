# 03 — L2 领域模型

> **性质**:本文是 `01-server-architecture.md` §7 的展开,给出**领域模型的形状与逐组字段规格口径**。
> 逐字段语义字典**不在本文**,它是 `11` §9 待补 1(5–8 天),已显式登记为实现期债务。
>
> **前提**:以 `00-architecture.md` 的裁定为准。冲突时以 `00` 为准。
> **证据引用**:`11` = `../../stoneage-plan/docs/11-char-fields.md`,余同。
>
> **定稿日期**:2026-08-30

---

## 0. 一句话与三个数字

> ★★ **「一张宽 `Char` 表」是 8.0 的实现方式,不是它的领域模型。**
> 真实的领域模型是 **47 种 `CHAR_TYPE` 共享一个内存池、靠 `CHAR_TYPE` 分派**;
> `11` 找出的 **65 条 slot 复用别名**就是这个事实在源码里留下的疤痕。

```
物理 slot(8.0 视图)  = 270 data + 15 string + 24 flg + 374 workint + 10 workchar = 693
其中被 ≥2 个名字复用    = 38 个 slot / 65 个别名     ← ★ 新模型必须拆成 65 个独立字段
其中零业务引用          = 80 个(11.5%)              ← ★ 新模型可直接删
⇒ 净有效字段 ≈ 693 − 80 + (65 − 38) = 640
sizeof(Char) = 12,584 B(B80 修正后)                  ← 引自 15 §3,不重算
```

⚠️ **一条口径提醒**:本文按 **8.0 视图**口径谈"47 种 `CHAR_TYPE`"(`11` §2.2)。
`16` §3.3 / §10.2 用的是 **8.5 视图全集**口径(59 成员)与 `SSRC80` 口径(63 成员)。
**两个数都对,不要混用**;详见 §3.4 的差集处理。

---

## 1. 两个必须同时成立的形状

```
对外(shared/model)   CHAR_TYPE 分派的和类型 —— 强类型,不可混淆
对内(world 运行时)   定长池 + int 下标句柄 —— 启动期一次性分配,运行期零分配
```

**为什么必须同时成立**(`00` §1.2 的附带前提):

| 侧 | 依赖它的结论 | 出处 |
|---|---|---|
| **扁平池** | `15` §9.1 三根支柱:运行期零分配 / 活对象数常量且几乎无指针图 / 单 tick 只触及 5.4% 槽位 | `15` §5.4 |
| **和类型** | `11` 的 65 条别名要求对外必须是独立强类型字段,否则合并即静默错误 | `11` §7 C1 |

⚠️ **只做一侧的两种失败**:

- 只做和类型、底下改成"每角色一个堆对象 + `map` 索引" ⇒ 三根支柱全部失效,
  失效形式从 GC 停顿变成 **cache miss 与分配抖动**;
- 只做扁平池、对外仍是 693 列宽表 ⇒ 别名合并的静默错误照旧发生。

⇒ **句柄是 `int` 下标,不是裸指针;实体访问经类型化视图,不直接暴露池。**

---

## 2. 实体族划分

### 2.1 从 47 种 `CHAR_TYPE` 到实体族

47 种类型不是 47 个独立实体 —— 它们的字段集高度重叠。按**字段集 + 生命周期**归族:

| 族 | 含 `CHAR_TYPE`(举例) | 特征 |
|---|---|---|
| **Player** | `PLAYER` | 唯一有账号绑定、有存档、有连接 |
| **Pet** | `PET` | ★ 有主人字段(`ownt`/`ocd`/`ocn`)、有融合码、有技能槽;**与 Player 同槽异义最严重**(§4.2) |
| **Enemy** | `ENEMY` | 由模板生成,无存档 |
| **Npc / Interactive** | `TOWNPEOPLE` `HEALER` `WINDOWMAN` `TRANSERMANS` `SHOP` `DENGON` `SAVEPOINT` `DOOR` `BOX` … | ★ 共用一块"NPC 通用暂存区"`NPCWORKINT1..10`(§4.1) |
| **WorldObject** | 地面道具、地面石币 | 不在 `Char` 池里(见 `10-world-map.md` §4) |

★ **归族的判据不是名字,是"哪些字段真被读写"**。
`16` §3.5 已给出逐文件的"写/读 `CHAR_*` 字段数"统计(129 文件),
可直接作为**字段热度排序**与归族的输入。

### 2.2 Interactive 族不做成 47 个类

`16` §3.1 实测:66 个 NPC functionset 里
**46 个有 `windowtalkedfunc`(69.7%)、只有 16 个有 `loopfunc`**。

⇒ ★ **绝大多数 NPC 玩法可以做成无状态的 `(actor, window_id, choice) → WindowOpen` 纯函数**,
状态存在实体的少量工作字段里。**16 个带 `loopfunc` 的才是真 tick 驱动状态机**
(赌场轮盘、宠物赛跑、庄园调度、公车飞机…)。

详见 `07-npc-quest.md` §4。

### 2.3 ⚠️ 不复刻字符串→函数指针的运行期绑定

`16` §3.2 的链路:模板的 `functionset=` → `functionSet[66]` 的 16 个**回调名字符串** →
`function.c` 的 331 条名表 `hashpjw + strcmp` → **查不到返回 NULL,不报错不日志**。

实测三处可断且**全部静默**:

```
functionset= 找不到 id           18 例(★ 含 ExChageMan 拼写错,6 个 NPC 是哑的)
回调名不在名表                   16 例(★ 恰好是 5 个 functionset 的完整集合)
实现文件不参与编译                6 例
```

⇒ **新模型用接口 / 函数值直接注册,断链在编译期发现。**
⚠️ 但**迁移时必须先跑一遍这三重检查**,否则会把已经死掉的子系统当活的去实现
(`16` §9.2 的 11 个建议弃用项就是这么来的)。

---

## 3. 字段分组

### 3.1 五组物理 slot 与新模型的对应

| 原枚举 | 物理 slot | 持久化 | 新模型归属 |
|---|---:|:-:|---|
| `CHAR_DATAINT` | 270 | ✅ 有存档键 | 各实体的持久化数值字段 |
| `CHAR_DATACHAR` | 15 | ✅ | 持久化字符串(名字、主人 cdkey…) |
| `CHAR_DATAFLG` | 24 | ✅ 位标志 | ★ 改为**具名布尔集合**,不做位图 |
| `CHAR_WORKDATAINT` | 374 | ❌ 纯运行时 | 运行时状态,不入库 |
| `CHAR_WORKDATACHAR` | 10 | ❌ | 运行时字符串(★ 别名密度最高,22 名压 10 槽) |
| **合计** | **693** | 285 / 384 | |

★ **持久化边界在字段级已经画好了**(`11` §7 C5):**285 个有存档键 / 384 个纯运行时**。
⚠️ **把运行时字段一起持久化会让存档膨胀 135%** —— 直接沿用原版的字段级划分即可。

### 3.2 `Char` 结构体本体的非枚举字段

`11` §10.3 点明的一项**任何包都没认领**的内容:`char_base.h:1786-1824` 的 **24 个非枚举字段**:

```
addressBook[80]        ← ★ 15 §3.3 实测占 sizeof(Char) 的 39%(5,760 B)
unionTable             ← indexOfPet[5] + indexOfPetskill[7]
haveSkill[]            ← 26(开职业技能)/ 5
indexOfHaveTitle[30]
indexOfExistItems / indexOfExistPoolItems / indexOfExistDepotItems / indexOfExistDepotPets
charfunctable[] / functable[]     ← ★ NPC 行为绑定,新模型不复刻(§2.3)
StreetVendor[20]
```

**处置**:

| 字段 | 处置 | 理由 |
|---|---|---|
| `addressBook[80]` | ★ **移出实体,做成独立聚合** | 占 39% 内存却是纯社交数据;`17` §5.5 已证它参与删角 Saga |
| `haveSkill[]` / `indexOfHaveTitle[30]` | 变长集合 | 定长数组的浪费同 §5.2 |
| `functable[]` / `charfunctable[]` | ❌ 不迁移 | §2.3 |
| `StreetVendor[20]` | ⏸ 长尾 | 摆摊,`12` §10 待补 6 |
| `unionTable` | 保留但拆开 | 宠物槽与宠物技能槽是两件事 |

---

## 4. ★★ 65 条别名的展开

> **这是本文最重要的一节。合并任意两个别名都是静默错误 —— 不报错、不崩溃、数据烂掉。**

### 4.1 高危族:`NPCWORKINT1..10` 是一整块被 10 个子系统各自改名占用的暂存区

`11` §4.3 的 14 组 / 41 个别名,最严重的一个槽有 **9 种含义**:

| 物理 slot | 别名(部分) | 推定适用类型 |
|---|---|---|
| **`CHAR_NPCWORKINT1`** | `WORKENCOUNTPROBABILITY_MIN` / `WORKPLAYERINDEX` / `WORK_PETFLG` / `WORKGENERATEINDEX` / `WORKDOORCLOSETIME` / `WORKOLDMANID` / `WORKSHOPCLIENTINDEX` / `WORKDENGONMAXID` | PET,PLAYER / PLAYER / ENEMY,PET / — / DOOR / SAVEPOINT / SHOP / DENGON |
| `CHAR_NPCWORKINT2` | `..PROBABILITY_MAX` / `WORKTACTICS` / `WORKDOORSWITCHCOUNT` | |
| `CHAR_NPCWORKINT3` | `WORK_TOHELOS_CUTRATE` / `WORKPETFOLLOWMODE` / `WORKDOOROPENG` | |
| `CHAR_NPCWORKINT4` | `WORK_TOHELOS_COUNT` / `WORKPETFOLLOWCOUNT` / `WORKDOORCLOSEG` | |
| `CHAR_NPCWORKINT5` | `WORKSHOPRELEVANT` / `WORKRENAMEITEMINDEX` / `WORKDOORSOONFLG` | |
| `CHAR_NPCWORKCHAR1` | `WORKBATTLE_TACTICSOPTION` / `WORKDOORPASSWD` / `WORKDOORMANDOORNAME` | |
| `CHAR_NPCWORKCHAR2` | `WORKBATTLE_ACT_CONDITION` / `WORKDOORNAME` / `CHAR_TIME1` | |
| `CHAR_LOGINCOUNT` | `NPCCREATEINDEX` / `PUTPETTIME` | ★ **这一组跨持久化字段** |
| …(其余 6 组见 `11` §4.3) | | |

★ **它们靠"`CHAR_TYPE` 互不相干"保证不冲突,没有任何编译期保护。**

⇒ **新模型把这 41 个别名拆成 41 个独立字段**,归到各自的实体族里:
`DOOR` 的关门时刻、`SHOP` 的客户 index、`DENGON` 的留言最大 ID …… 各自成为该族的字段。

### 4.2 ★ 更危险的一族:`CHAR_PET` 段的 19 条

`11` §4.5 要点 2:`CHAR_PET` 段(19 条)是**「玩家 vs 宠物」的整体同槽异义**,
**危险度高于** `NPCWORKINT` 那批:

| 差别 | `NPCWORKINT` 族 | ★ `CHAR_PET` 族 |
|---|---|---|
| 冲突可能性 | 不同 NPC 类型不会同时存在 | ★ **同一个 `Char` 实例只要 `CHAR_TYPE` 判错就读到完全不相干的值** |
| 典型例 | 门的关门时刻 vs 商店客户 index | ★ **`CHAR_CHARM`(魅力)在宠物身上叫 `CHAR_MODAI`(AI 模式)** |

引用最多的三条:`CHAR_PETID`(101 次)、`CHAR_PETFAMILY`(38)、`CHAR_VARIABLEAI`(37)。

其余同槽对(`01` §6.2 列出的一部分):

```
CHAR_MODAI            = CHAR_CHARM              宠物 AI 模式 ← 魅力
CHAR_VARIABLEAI       = CHAR_LUCK               宠物 AI 变量 ← 幸运
CHAR_PETGETLV         = CHAR_CHATVOLUME         捕获等级   ← 音量
CHAR_ALLOCPOINT       = CHAR_LEVELUPPOINT       ★ 位打包成长率 ← 升级点(§6)
CHAR_PETID            = CHAR_DUELMAXSTWINCOUNT
CHAR_PETFAMILY        = CHAR_FMLEADERFLAG
CHAR_PETVALIDITY      = CHAR_VIPTIME
…(共 19 条)
```

⇒ ★ **Player 与 Pet 必须是两个独立类型,不是"同一类型的两个 flag"。**

### 4.3 隐式复用:机械扫描抓不到的一类

`11` §9 待补 2 的实证样本:★★ **`CHAR_ENDEVENT` 在宠物身上被复用为「宠物事件标记」(0/1)**,
与角色身上的**位图**语义完全不同(`09` §3.5)。

这类复用**不是 `=` 别名**,是"同一个名字在不同 `CHAR_TYPE` 下语义不同" ⇒
机械判据抓不到,要抓须做控制流分析(2–3 天)。

⇒ ⚠️ **本文的 65 条是下界,不是全集。** 实现期须逐 `CHAR_TYPE` × 消费文件做读写点分析。

### 4.4 三条死别名

`11` §4.5 要点 3:`CHAR_WORKGENERATEINDEX`(引用 1 次 = 定义处)与
`CHAR_TIME1` / `CHAR_TIME2`(各 1 次)—— 占着 slot 但无人读写,归入 §5 死字段。

---

## 5. 死字段与预留槽位

### 5.1 口径与规模

```
口径:8.0 视图活跃 且 非别名 且 业务引用文件数 == 0
     (剔除三类非业务出现:定义头文件 / char_base.c 通用 get-set 骨架 / mylua 绑定表)

死字段  80 / 693 = 11.5%
分布    CHAR_DATAINT 41 / CHAR_WORKDATAINT 34 / CHAR_WORKDATACHAR 5
★ 其中占存档空间的 41 个 —— 每条角色存档都在写这 41 个恒零字段
```

⚠️ **一个必须记住的口径坑**(`11` §11.1):
只剔除定义头文件时死字段数是 **50**,把 `char_base.c` 一并剔除后是 **80**。
差的 30 条全是"只在通用 get/set 骨架与存档键表里出现过"的字段 ——
**那不构成用途证据**,算成"有引用"会让占比从 11.5% 缩水到 7.2%。

### 5.2 ★ 37.5% 的死字段来自一个模式:批量预留槽位

| 族 | 条数 | 说明 |
|---|---:|---|
| `CHAR_MATERIAL02` … `CHAR_MATERIAL25` | **24** | 全族 25 槽,**只有 `MATERIAL01` 被业务代码用到** |
| `CHAR_LOWRIDEPETS3` … `CHAR_LOWRIDEPETS8` | **6** | 骑宠槽同理:1–2 有用,3–8 零引用 |

⇒ ★ **`11` §7 C4:定长预留槽位数组必须改成变长集合。**
这不是"优化",是**不照搬即不浪费**:照搬就照搬了 30 个恒零的持久化列。

### 5.3 8.0 门控下不存在的字段

两个方向都要处理(`11` §6):

| 方向 | 条数 | 处置 |
|---|---:|---|
| S1 视图关、8.0 实际有 | **4** | ★ `CHAR_GMFUNCTION` / `CHAR_GMTIME`(四方一致:`SSRC80` 有 + B80 有 + **真实存档 `itemgold` 里有 `gmfunction` 键**)、`CHAR_WORKICECRACK` / `CHAR_WORKMODICECRACK` ⇒ **要做** |
| S1 视图开、B80 修正后关 | **30** | 整批不做。含 **6 条 `CHAR_LUASAVE_*`**(与"8.0 无 Lua"互相印证)、`CHAR_TITLE1..3` + `TITLE_DEFAULT`、`CHAR_NEWNAME`、`CHAR_WORK_MAC` / `SERVID` / `OFFLINE`(私服运营字段) |

```
8.5 全集              761 条目
S1(macros_80)活跃    739
S2(B80 修正)活跃     713     ⇒ 8.0 投产口径约 713,比 8.5 少 48 条(6.3%)
```

⚠️ **713 是估算不是实测**:S2 只覆盖 641 个宏中可裁定的 352 个,
其余 289 个宏无独占函数样本 ⇒ 落在这些宏里的字段仍按 S1 计,标【8.5 源码推定】。

⚠️ **唯一的三方分歧**:`CHAR_WORKSPETRELIFE`(`SSRC80` 有、B80 判 `_LOSE_FINCH_` 为关)
按优先级 `B80 > SSRC80 > macros_80` 判为 **8.0 无**,但列入待补。

---

## 6. 位打包字段必须标为复合

`11` §7 C6 + `08` §3.1:

### 6.1 `CHAR_ALLOCPOINT` —— 4 × 8 bit 宠物成长率

```c
LevelUpPoint = CHAR_getInt(petindex, CHAR_ALLOCPOINT);
vital = (LevelUpPoint >> 24) & 0xFF;
str   = (LevelUpPoint >> 16) & 0xFF;
tgh   = (LevelUpPoint >>  8) & 0xFF;
dex   = (LevelUpPoint >>  0) & 0xFF;
```

★ **语义不是"玩家可分配的属性点",是宠物的四维成长率**(GM/Lua 暴露名「对像_成长%」)。

⚠️ **同一字段有三个并存上限**:字段容量 0..255,
宠物创建时夹到 **≤ 60**(`PET_getEvolutionAns`)、宠物死亡扣属性时夹到 **≤ 50**。
⇒ **拆成 4 个独立字段时必须逐调用点保留各自的夹取**,不能统一成一个上限。

### 6.2 `CHAR_WORKBATTLECOM1..4` —— 指令 + 参数四槽复用

`07` §12.3:`COM3` 还做 `HIGH16` / `LOW16` **位打包**(`battle_command.c:390-391`)。
⇒ 战斗指令的参数传递不复刻这套打包,改结构化(见 `02-protocol.md` §6.3)。

### 6.3 ★ 事件旗标是两族各自连续的槽,且原版无上界检查

`11` §7 C7(**三源交叉**,本文档置信度最高的结论之一):

| 血统 | 槽位数 | 编号空间 | 证据 |
|---|---:|---:|---|
| **8.0** | **8** | **0..255** | B80 里 `evt`..`evt8` / `nev`..`nev8` 可检索、`evt9` 不存在;`SSRC80` 枚举同型;8.5 枚举门控 |
| 8.5 | 32 | 0..1,023 | |

⚠️ **实测投产数据已用到 226** ⇒ **8.0 下只剩 29 个空位**。
⚠️ 且 `NPC_EventSetFlg` **没有上界检查** ⇒ 旗标号 ≥ 256 会写进相邻的 `CHAR_NOWEVENT` 段,
更大的号写到 `CHAR_TRANSMIGRATION`。

⇒ **新模型若改成变长集合,须自己补上界检查**;若保留定长,须显式定容量。
详见 `07-npc-quest.md` §6。

---

## 7. 石币不是一个字段

`12` §2.1:石币在 8.0 是 **5 个并存载体 + 4 个独立上限**:

| 载体 | 语义 | 上限(8.0) |
|---|---|---:|
| `CHAR_GOLD` | 随身 | `1,000,000 + 转生 × 1,800,000` |
| `CHAR_BANKGOLD` | 家族银行·个人帐户 | 10,000,000 |
| `CHAR_PERSONAGOLD` | 赌场个人银行(宝箱) | 50,000,000 |
| `CHAR_FMBANKGOLD` | 家族银行·家族公款 | 100,000,000 |
| `CHAR_AUCGOLD` | 拍卖所得待领 | ★ **8.0 有 0 个写点 ⇒ 死字段** |

⇒ ★★ **「玩家有多少钱」在 8.0 里没有单一真值。**
⚠️ **合并成一个 `balance` 前必须先决定 4 个上限怎么合并** —— 直接相加得 1.6 亿,
远超 8.0 任何单一上限。

领域操作侧的形状见 `08-economy.md` §3(`GoldLedger` 单入口)。

---

## 8. 句柄与索引

### 8.1 句柄

```
EntityHandle = { uint32 index; uint32 generation }
```

★ **必须带 `generation`**。理由不是理论洁癖,而是两处实测故障:

| 故障 | 出处 |
|---|---|
| gmsv 重启后 `fdid` 归零 ⇒ 迟到应答命中新连接 | `17` §7.2 |
| 池槽位复用后旧引用仍指向该槽 | 定长池的固有问题 |

### 8.2 ⚠️ 索引不得线性扫描

`01`(服务端)§5.2 已裁定。此处补量化依据:
`17` §7.3 实测三个 `fdid` 查找函数全部是 `for(i=0; i<ConnectLen; i++)` 全表扫描
+ 每格加解锁,**每条 saac 应答至少扫一次**(部分协议扫两次)。

`csa8.0` 的 `fdnum=100` 下可忽略,但 `stoneage85` 的 `fdnum=1000` 就是每条应答 1,000 次比较。

⇒ 新实现全部走哈希索引:`account → session`、`char_name → entity`、`entity → connection`。

---

## 9. 硬约束总表

| # | 约束 | 违反后果 | 出处 |
|---|---|---|---|
| **M1** | ★★ **65 条别名展开为 65 个独立字段**,不得按物理 slot 合并 | **静默错误** —— 门的"关门时刻"写进商店的"客户 index" | `11` §7 C1 |
| **M2** | ★★ 拆分的判别键是 `CHAR_TYPE` ⇒ **和类型,不是 693 列宽表** | 宽表会把 47 种实体的字段全塞进一行,90% 恒为默认值 | `11` §7 C2 |
| **M3** | **80 个死字段直接删**,其中 41 个占持久化空间 | 新存档继续写 41 个恒零列 | `11` §7 C3 |
| **M4** | ★ **定长预留槽位数组改变长集合** | 37.5% 的死字段来自这一个模式 | `11` §7 C4 |
| **M5** | **持久化边界沿用字段级划分**(285 / 384) | ★ 存档膨胀 **135%** | `11` §7 C5 |
| **M6** | ★ 位打包字段标为复合字段,**逐调用点保留各自的夹取上限** | `CHAR_ALLOCPOINT` 有 50/60/255 三个上限 | `11` §7 C6 / `08` §3.1 |
| **M7** | ★ **事件旗标显式定容量 + 上界检查** | 旗标号 ≥256 写进相邻字段 | `11` §7 C7 |
| **M8** | ★ 存档须有**结构级校验**,不能只判开头两字符 | 原版唯一坏档判据是"开头两字符为 `\|\|`",**半截档识别不出来** | `11` §7 C8 / `17` §5.2 |
| **M9** | ★ **拼写异常的原名不得"顺手修正"** | `CHAR_WORKERSIST_F_I_T`(应为 `RESIST`)是存档/协议对齐的一部分 | `11` §8 W1 行 |
| **M10** | ★ 句柄带 `generation` | 槽位复用后的悬空引用 | §8.1 |

---

## 10. 存档串:为什么不沿用

`01`(plan)§6.3 的实测缺陷,逐条对应新做法:

| 旧 | 缺陷 | 新 |
|---|---|---|
| 纯文本 `key=value\n` 串联,一角色一整串 | 无版本号 ⇒ 字段增删即破坏兼容 | ★ **JSON 列 + 生成列索引**(`00` §5),schema 版本显式 |
| 溢出处理是 `fprint("err chardata buffer over"); goto RETURN` | ★ **静默截断,玩家丢数据** | 长度约束在写入前校验,超限拒绝并告警 |
| `CHARDATASIZE = 1 MB` 硬上限,`loadCharOne` **不检查** | 超长档直接溢出 | 显式上限 + 校验 |
| 三个宏(`_SIMPLIFY_ITEMSTRING` 等)改变编码格式 | ★ **同一份存档在不同编译产物间不兼容** | schema 唯一,无编译期分支 |
| 一族键**由代码拼出**、不在键表里(`PETSKILLSERVERSTRING "psk"` + 下标) | 迁移脚本按键表扫会漏 | ★ 显式建模宠物技能槽 |

★ **决策 ②(生产数据不迁移)已把最硬的要求去掉** —— 不需要读懂旧存档。
⚠️ 但 `11` §9.2 的一条数字要记住:8 条真实宠物存档串只能锚定存档键的 **18.6%**,
玩家专属字段(道具栏/仓库/称号/名片簿/家族)**一条真实取值都没有**。
⇒ **任何"已对齐存档格式"的说法,上限就是这 18.6%。**

---

## 11. 已知欠债

| # | 欠债 | 估时 | 说明 |
|---|---|---:|---|
| 1 | ★ **693 个字段逐条的「含义 / 取值域 / 单位」** | **5–8 天** | `11` §9 待补 1。★ **无验证手段且永远不会有**;骨架已备好(名 / 存档键 / 中文名 / 引用文件),逐条填「含义」列即可 |
| 2 | ★★ **隐式 slot 复用**(同名不同 `CHAR_TYPE` 语义不同) | 2–3 天 | `11` §9 待补 2;机械判据抓不到,须控制流分析。已知样本:`CHAR_ENDEVENT` |
| 3 | 从 `SSRC80` 补齐 **8 个 8.0 独有的 `CHAR_TYPE`** | 2–3 天 | `16` §11 待补 2:`CHAR_TYPEAUCTIONEER` / `CHAR_MAPTRADEMAN` / `CHAR_SELLSTHMAN` / `CHAR_TYPEVERYWELFARE` / `WELFARE2` …**8.5 源码里根本没有,须切换源码树**;且须先由 D8 裁定是否要 |
| 4 | `CHAR_WORKSPETRELIFE` 的 8.0 归属 | 0.5 h | 三方分歧,须人工看独占函数样本 |
| 5 | **289 个无独占样本的宏**所门控的字段,8.0 归属 | 不可解 | ★ 无 oracle 可挂,恒【8.5 源码推定】 |
| 6 | ★ 中文名映射表的权威性边界 | — | `mylua/charbase.c` 的 363 条中文名整体包在 `_ALLBLUES_LUA` 内,而 **8.0 无 Lua** ⇒ 它是**8.5 私服作者的理解**,不是官方语义。**最强的可读线索,不是权威定义**;且实测**不是一一对应** |

---

## 12. 变更记录

| 日期 | 变更 |
|---|---|
| 2026-08-30 | 初稿:三个数字;两个形状必须同时成立;实体族划分(Player/Pet/Enemy/Interactive);65 条别名展开(高危 14 组 + `CHAR_PET` 19 条 + 隐式复用);80 死字段 + 预留槽位;位打包三处;石币 5 载体;句柄带 generation;M1–M10 硬约束 |
