// shared/model/Enemy.h —— Enemy 族实体(L2 领域模型,批次 M.4b)
//
// ★★ 本文件的字段清单**不是自拟的**,取自原版 `ENEMY_createEnemy`
//    (展开视图 `char/enemy.c:994-1180`)实际写入 `CharNew` 的那些槽 ——
//    它是"一只敌人由哪些字段构成"在源码里唯一成文的地方。
//    ⇒ 建 / 不建**逐条记明**(见文末),不静默省略(同 `Pet.h` 的纪律)。
//
// ⚠️ 行号基准 = `stoneage-plan/tools/unifdef_80/` **展开视图**,不是原始 8.5 树
//    (两套差约 340 行,00 §9.0.25 ⑤)。
//
// ── 为什么 Enemy 必须是独立的族(03 §2.1 / M2)────────────────────────
//   `03` §2.1 给 Enemy 的族特征是一句话:「**由模板生成,无存档**」。
//   这两半都有结构后果:
//     · **由模板生成** ⇒ 四维不是存下来的,是 `rollSpawnStats` 当场摇的(DR-DT10);
//       同一模板同一等级的两只敌人四维不同,这是设计而非误差。
//     · **无存档**     ⇒ 没有账号绑定、没有 cdkey、没有 Y 五项(见文末 ⑤)。
//
// ⚠️★★ **一处同槽异义把「敌人有幸运」这件事整个取消了**,而它是本批回源码核出来的:
//     `CHAR_VARIABLEAI == CHAR_LUCK`(`include/char_base.h:637`)——
//     `ENEMY_createEnemy:1076` 写的是 `CHAR_VARIABLEAI = 0`,
//     而 `PET_createPetFromCharaIndex:347` 读的是 `CHAR_getInt(enemyindex, CHAR_LUCK)`。
//     **同一个物理槽,写的时候叫 AI 变量、读的时候叫幸运** ⇒ 捕获出来的宠物幸运恒 0,
//     并且**敌人从头到尾就没有"幸运"这个属性**(没有任何一处写它)。
//   ⇒ 本结构**不建 luck**:它不是"拿不到",是原版里不存在。★ 这正是 M1 那条
//     「合并任意两个别名都是静默错误」的活样本 —— 若把 Enemy 与 Pet 合成一个宽表,
//     `variable_ai` 与 `luck` 会共用一个字段,而"AI 变量 0"会被读成"幸运 0",
//     两者恰好同值 ⇒ **连测试都抓不到**。
//
// ⚠️ shared/ 只依赖标准库 + 项目内 rules/ model/ domain/(01 §4)。

#ifndef __SA_Enemy_H__
#define __SA_Enemy_H__

#include <cstdint>

#include "model/EntityKind.h"
#include "model/Pet.h" // NameStr(DR-TS5 的 31 字节上限)

namespace SA::Model
{

// Enemy 族实体。
//
// ★ POD:三根支柱要求池是 `std::array` 且运行期零分配(15 §9.1)⇒ 本结构不含
//   任何自管内存的成员(NameStr 是定长内联串),同 `Pet` / `Player`。
struct Enemy
{
	// ★ 类型与族的对应在编译期可查 —— 和类型的分派不靠运行期 tag 字段(M2)。
	static constexpr EntityKind kKind = EntityKind::kEnemy;

	// ── 原始四维(源码 :1067-1070 的 `PARAM_CAL`)────────────────────
	//
	// ★★ **这四个就是欠债 23 找的那个"非 0 来源"**:`rollSpawnStats` 的产物
	//    (DR-DT10,`shared/rules/Progression.h`)。捕获时逐个拷进 `Pet`
	//    (源码 :342-345),而在 M.4b 之前敌人**没有 L2 实体** ⇒ 那四项只能填 0。
	// ⚠️ 可为负:模板基数可以是 0,经 ±2 扰动后为负,`PARAM_CAL` 照乘
	//    (见 `SpawnTemplate::base_vital` 的注释)⇒ 本结构不夹取,同原版。
	std::int32_t vital = 0;
	std::int32_t str = 0;
	std::int32_t tough = 0;
	std::int32_t dex = 0;

	// ── 成长率:`CHAR_ALLOCPOINT` 的展开(M6,源码 :1052-1056)──────────
	//
	// ★ 与 `Pet` 的 `growth_*` 同型(4 × 8 bit),捕获时整组拷过去(源码 :375)。
	// ⚠️★ 取的是「±2 扰动后、撒 10 点**之前**」那一刻的基数 —— 顺序即语义,
	//    详见 `Progression.cpp` 的四步说明。挪动它成长率会系统性偏大而无一处报错。
	// ⚠️ **不加 ≤60 / ≤50 夹取**:那两条属进化孵化 / 死亡扣属路径,生成路径源码
	//    一处夹取都没有(同 `Pet` 的 `growth_*`,理由同处记明)。
	std::uint8_t growth_vital = 0;
	std::uint8_t growth_str = 0;
	std::uint8_t growth_tough = 0;
	std::uint8_t growth_dex = 0;

	// ── 四属性(源码 :1071-1074,取自模板 `E_T_{FIRE,WATER,EARTH,WIND}AT`)──
	//
	// ⚠️★★ **顺序陷阱,与 `Pet` 同处同理由**:原版 `CHAR_*AT` 的拷贝顺序是
	//    **火 水 地 风**;`Rules::Element` 是 **地(0) 水(1) 火(2) 风(3)**;
	//    相克表表头又是 **无 火 水 地 风** —— 三套顺序两两不同。
	// ★ 所以用**具名字段**,与 `Combatant::elements[]` 互转时一律按
	//   `elements[static_cast<int>(Element::kFire)]` 具名下标取,**绝不按位置拷**。
	std::int32_t fire = 0;
	std::int32_t water = 0;
	std::int32_t earth = 0;
	std::int32_t wind = 0;

	// 等级(源码 :1077 `CHAR_LV = level`)。
	//
	// ⚠️★ `level` 由**调用方**给定 —— 源码 :1030-1035 是
	//    `baselevel > 0 ? baselevel : RAND(ENEMY_LV_MIN, ENEMY_LV_MAX)`,
	//    而那次摇号读的是**遇敌表**(`enemy.txt` 的 `ENEMY_LV_*`),不是模板表。
	//    ⇒ 遇敌表未移植 ⇒ 等级是入参,不在生成函数里摇(`spawnEnemy` 同处记明)。
	std::int32_t level = 0;

	// ── 生命(★ 三个数各有各的来源,不是同一批)────────────────────────
	//
	// ⚠️★ `hp` 的真源是**推导值**:源码 :1153 先 `CHAR_complianceParameter`,
	//    :1159 再 `CHAR_HP = CHAR_getWorkInt(WORKMAXHP)` ⇒ **敌人满血入场**。
	//    ⇒ 本字段由 `spawnEnemy` 用 `deriveBaseStats(...).max_hp` 填,
	//      而**不建 max_hp 存储字段** —— 那会造出第二个真源(同 `Pet` 不建 max_hp)。
	//
	// ⚠️★★ `mp` / `max_mp` **恒 0,而这是查出来的,不是猜的**:
	//    `ENEMY_createEnemy` 从头到尾**没有一行写 MP / MAXMP** ⇒ 它们只能来自
	//    :1020 的 `CHAR_getDefaultChar(&CharNew, 31010)`,而那张默认表
	//    (`include/defaultPlayer.h` 的 `player`)里 `CHAR_MP = 0` / `CHAR_MAXMP = 0`。
	//    ⇒ 敌人真的没有 MP。★ 与 DR-BT3(战斗指令不耗 MP)一致 ⇒ 无下游后果。
	std::int32_t hp = 0;
	std::int32_t mp = 0;
	std::int32_t max_mp = 0;

	// ── AI(源码 :1075-1076)─────────────────────────────────────────
	//
	// ⚠️★ 同槽异义两条,与 `Pet` 同源:
	//      `mod_ai`      = `CHAR_CHARM`(AI 模式 ← 魅力)
	//      `variable_ai` = `CHAR_LUCK` (AI 变量 ← 幸运)★ 见卷首那条
	//   ⇒ `variable_ai` 建出来只为**明确它是 0**:捕获路径要读它(以"幸运"的名义),
	//     不建就会让下一个人以为"敌人的幸运没拷过来"是遗漏。
	std::int32_t mod_ai = 0;
	std::int32_t variable_ai = 0;

	// 宠物评级(源码 :1096-1097 `ENEMY_getRank`)。捕获时拷给 `Pet::pet_rank`(:365)。
	//
	// ⚠️★ 同槽异义第四对(`Pet.h` 卷首点了三对,这是第四):
	//    `CHAR_PETRANK == CHAR_LASTTIMESETLUCK`(`char_base.h:650`)。
	// ★ 判据是**模板的原始四维基数之和**(未扰动!`ENEMY_getRank:825-828` 读的是
	//   `ENEMYTEMP_enemy[tarray]` 而不是被 ±2 改过的局部 `tp`)⇒ 同模板同 rank,
	//   与本次摇号无关。详见 `spawnEnemy` 的实现注释。
	std::int32_t pet_rank = 0;

	// ── 图号(源码 :1023-1024)──────────────────────────────────────
	//
	// 源码把两个槽设成同一个值:`BASEBASEIMAGENUMBER` = `BASEIMAGENUMBER` = 模板图号。
	// 前者是**原始**图号(变身 / 换装后能还原),后者是**当前**图号。
	// ⚠️★ 这不违反 DR-BT11:那条管的是**判定逻辑**不得依赖图号(雷尔免暴击改标志位),
	//    图号作为**显示数据**是必要的。⇒ 判定读 `Rules::CombatModifiers`,渲染读这里。
	std::int32_t origin_image = 0;
	std::int32_t base_image = 0;

	// 名字(源码 :1108-1110,取自模板 `E_T_NAME`)。上限 31 字节(DR-TS5)。
	//
	// ⚠️★ **它是 `enemybase1.txt` 里唯一的非 ASCII 列** ⇒ 落真数据要过
	//    `04` §7.2 的编码归一(数值列不受此约束,`11` §13 D 线已记明)。
	//    本批不读文件 ⇒ 由调用方给,见 `world/Api.h` 的 `EnemyTemplate`。
	NameStr name{};

	// ── 捕获相关:★ 两个 **WORK** 字段,不是持久化数值 ──────────────────
	//
	// ★★ 这两个把 `Combatant.h` 里那句「1.5 无敌人数值表 ⇒ 调用方按 30 兜底」
	//    第一次变成了真数据:
	//      `capturable`         = `CHAR_WORK_PETFLG`            (源码 :1165)
	//      `capture_difficulty` = `CHAR_WORKMODCAPTUREDEFAULT`  (源码 :1166)
	//
	// ⚠️★ 两者**来源不同的表**,这个区分要紧:
	//      `capturable` ← `*(p + ENEMY_PETFLG)`     = **遇敌表** `enemy.txt`
	//      `capture_difficulty` ← `*(tp + E_T_GET)` = **模板表** `enemybase1.txt`
	//    ⇒ 同一只怪在不同遇敌配置下可捕 / 不可捕,而捕获难度跟着模板走。
	//    ★ `Combatant.h` 的 `capturable` 注释「并非所有敌人都可捕」由此得到源码依据。
	//
	// ⚠️ `capture_difficulty` **不兜底 30**:源码 `battle_event.c:3845` 那个 30 是
	//    局部初值、随即被 `CHAR_getWorkInt` 覆盖 ⇒ 30 是"读不到时的兜底",不是默认值。
	//    模板列为空时载入器保持 `-1`(`06` §3.5 的载入器语义)⇒ 照抄传下去,不替它修。
	bool capturable = false;
	std::int32_t capture_difficulty = 0;

	// ── 战果:经验值与决斗点(源码 :1028 初始化 + :1101-1107 判定树,战果结算批次)─
	//
	// ★★ 这两个是**一棵判定树**的产物,别分开理解(源码 `char/enemy.c:1101-1107`):
	//      `duelpoint` = 敌人表 `ENEMY_DUELPOINT`,**无条件写**;
	//      仅当 `duelpoint <= 0` 才给 `exp` ⇒ `duelpoint > 0` 的怪是「决斗点怪」、不给经验;
	//      `exp`:敌人表 `ENEMY_EXP != -1` 用表值,`== -1` 是哨兵 ⇒ 走 `enemyExp()` 公式算。
	// ⚠️★ `duelpoint > 0` 还有第二个后果:`battle.c:2267` 据它把整场置成 `dpbattle=1`
	//    (决斗点战斗)⇒ 结算走决斗点、不走经验。本批建**初值** +「决斗点怪不给经验」的
	//    判定;决斗点的**累积 / 下发**属 PvP / saac 域(plan 06/17),推迟(登记残缺 ——
	//    决斗点怪打赢暂不给点)。
	// ⚠️ 这是「敌人身上值多少」,不是玩家实拿:分配时还要过等级差衰减
	//    (`EXPGET_MAXLEVEL=5` / `DIV=15`,见 `World.cpp` finished 段)。
	std::int32_t exp = 0;
	std::int32_t duelpoint = 0;
};

// ── 文末:源码写了、本批**有意不建**的字段(逐条记明,均非遗漏)─────────────
//
// ① **六种状态异常**(源码 :1080-1085:POISON / PARALYSIS / SLEEP / STONE / DRUNK /
//    CONFUSION)⇒ L4 状态系统未移植。★ 与 `Pet.h` 文末 ① 同批来:反击(DR-BT17 余项)
//    也排在 L4 之后、依赖同一个 `BATTLE_GetDamageReact` 状态面。
//
// ② `CHAR_RARE` / `CHAR_PETID`(= `E_T_TEMPNO`)/ `CHAR_PETENEMYID`(= `ENEMY_ID`)
//    (源码 :1086-1087, :1090)⇒ L4 内容导入(D 线)。★ 其中 `PETENEMYID` 还依赖
//    **遇敌表**(见 `level` 的注释),不只是模板表。
//
// ③ `CHAR_CRITIAL` / `CHAR_COUNTER`(源码 :1088-1089)⇒ 暴击已在 A.3 走
//    `Rules::CombatModifiers.equip_critical`;反击排在 L4 之后。
//    ⚠️ 别看到"暴击已实现"就把 critial 建上 —— 战斗输入走 `Combatant`,不走 L2 实体。
//
// ④ **宠技槽** `unionTable.indexOfPetskill[7]`(源码 :1092-1094)⇒ 宠技批次。
//    ★ 03 §3.2 已裁定 `unionTable` **保留但拆开**,届时按那条落地。
//
// ⑤ ★★ **Y 五项**(源码 :1154-1158)—— 敌人侧**不建**,而 `Pet` 侧本批**建了**。
//    这个不对称是有理由的,不是漏:Y 五项是「初值快照」,消费方是**升级 / 成长**
//    (`06` §3.1)。敌人**无存档、不升级** ⇒ 存下来没有任何一处会读它;
//    宠物会,所以它归 `Pet`(见 `Pet.h` 文末 ②)。
//    ⚠️ 源码两边都设了 —— 那是因为原版两边是**同一个 `Char` 结构**,
//      设不设由那段代码顺手决定,不构成"敌人需要它"的证据。
//
// ⑥ ✅ `CHAR_EXP` / `CHAR_DUELPOINT`(源码 :1028, :1101-1107)⇒ **战果结算批次已建**
//    为上方 `exp` / `duelpoint`(判定树见那里)。★ 校正此前一处转述:`ENEMY_getExp`
//    的生效行(`:796`)**只读模板 `tp` + 入参 level / rank**,不读敌人表行 `p`
//    (`p` 只出现在 :795 那条被注释掉的旧式里)⇒ 公式**不依赖遇敌表**,与 ② 的前置无关。
//
// ⑦ `CHAR_SLOT`(源码 :1079)⇒ ★ **含义未定,不猜**(同 `Pet.h` 文末 ⑦):
//    属 03 §11 欠债 1(693 字段逐条含义)。建一个不知道含义的字段等于把猜测固化。
//
// ⑧ `CHAR_WORKTACTICS` / `WORKBATTLE_TACTICSOPTION` / `WORKBATTLE_ACT_CONDITION`
//    (源码 :1160-1164)⇒ **敌人 AI 战术**,来自遇敌表。当前 `fillEnemyCommands`
//    只填"普攻 + 打对面第一个活着的"(有意不猜,`World.cpp` 同处记明)⇒ 一并推迟。
//
// ⑨ `charm`(魅力)⇒ **不建**,两条理由叠加:① 同槽异义 `CHAR_CHARM == CHAR_MODAI`,
//    敌人那个槽存的是 AI 模式;② 捕获公式(§6.2 `× charm / 50`)读的是**攻方**魅力,
//    攻方是玩家 ⇒ 敌人侧根本不参与。
//
// ⑩ 装备与掉落(源码 :1120-1151:`ENEMY_ITEM*` 概率掉落 + `ENEMY_STYLE` → 武器)
//    ⇒ 道具系统(与 `USE_ITEM` 同批,见 `01` §13 欠债 1 的余项)。
//    ⚠️★ 这一条有战斗后果、不只是掉落:`ENEMY_STYLE` 给敌人**发一把武器**
//      ⇒ 影响 `CombatModifiers.unarmed` / `weapon`(§3.9 空手可达 10 段连击)。
//      本批敌人一律空手 ⇒ **比接了武器的原版更容易触发多段**,是登记在案的偏差。
//
// ⑪ `ENEMY_RandomChange`(源码 :910-987,在 :1152 被调)⇒ 道场 / 宠物族的随机变异
//    (按 `tempno` 落在三段区间才生效)⇒ 属 L4 内容 + 遇敌表。
//    ⚠️★ 回源码核实了它到底改什么,免得将来接错位置:它改**图号 + 四属性 + 两个宠技槽**
//      (:945-973),**不改四维** ⇒ 接它的位置必须在四属性拷贝**之后**;
//      与属性推导无关(`deriveBaseStats` 不读四属性)。
//    ⚠️ 它还**消耗 rng**(`RAND` 至少 4 次)⇒ 接上会改变同种子下的后续序列。

} // namespace SA::Model

#endif // __SA_Enemy_H__
