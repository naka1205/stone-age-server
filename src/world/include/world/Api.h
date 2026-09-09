// src/world/include/world/api.h —— L2/L1 世界循环的**唯一**对外面
//
// ── 阶段 1.5 的切面(00 §9.0.4)──────────────────────────────
//   ✅ 要:最小 tick(01 §3.1 的 1/2/4/8 四步)· 一场战斗的生命周期
//   ⬜ 不要:NPC 生成 · 移动 · 视野 · 角色循环
//
// ⚠️★ 明确认下的三条边界,免得"跑通了"被读成"做完了":
//   ① **不落盘、无 Redis、单实例**(1.5 不要 storage / lock);
//   ② **不验证 00 §3.1 的服务边界** —— 1.5 是单模块,那留到阶段 3;
//   ③ **不做 L2 领域模型** —— 世界里没有"角色",只有战斗里的 Rules::Combatant。
//      ⇒ 因此本批次**不下发 BattleSnapshot**:那需要把 Rules::Combatant 映射成
//        Domain::CombatantState,而那是 1.2 L2 实体族的活。
//        1.4 demo 的验收口径是**事件流端到端一致**(客户端 01 §12.1),
//        BattleEvents 就是它要的东西。

#ifndef __SA_WorldApi_H__
#define __SA_WorldApi_H__

#include <cstdint>
#include <memory>
#include <vector>

#include "model/Enemy.h"
#include "model/Pet.h"
#include "net/Api.h"
#include "platform/Api.h"
#include "rules/Battle.h"
#include "rules/Combatant.h"
#include "rules/Config.h"
#include "rules/Progression.h"
#include "rules/RandomSource.h"

namespace SA::World
{

using BattleId = std::uint64_t;

// tick 的阶段。★ 顺序**照抄** 01 §3.1,连未实现的四步也占位 ——
//   原版 mainloop() 的顺序是"整个服务端行为的骨架"(01 §2),
//   骨架的形状现在就要对,否则将来补 NPC 生成时会补在错的位置上。
enum class TickPhase : std::uint8_t
{
	kClock = 1,         // ✅ 时钟推进
	kNetInbound = 2,    // ✅ 网络入站
	kNpcSpawn = 3,      // ⬜ 阶段 2
	kBattle = 4,        // ✅ 战斗推进(★ 受节拍层控制,不等于 tick 频率)
	kCharLoop = 5,      // ⬜ 阶段 2
	kTimedJobs = 6,     // ⬜ 阶段 2
	kOutboundFlush = 7, // ⬜ 阶段 2(CA/CD 视野聚合;1.5 无视野)
	kShutdown = 8,      // ✅ 关闭检查
};

// 一场战斗。★ 生命周期在 world,规则在 L3 —— 两者不混。
struct BattleStats
{
	std::uint32_t turns_resolved = 0;
	std::uint32_t events_emitted = 0;
	bool truncated_once = false; // ResolveTurn 曾返回 false(见 battle.h)
	bool finished = false;
};

// ── 敌人生成与入场(批次 M.4b)────────────────────────────────────────
//
// ★★ 这一组补的是**欠债 23 的下半**:M.3 补了「四维 → 三围」的推导公式、
//    M.4a 补了「模板 + 等级 → 四维」的生成公式,但 **0 是推导的不动点** ——
//    真正让宠物有战力的前提是"四维有一个非 0 的**来源**",而那个来源在原版里
//    是**敌人的 L2 实体**:`PET_createPetFromCharaIndex` 从 `enemyindex` 逐字段拷。
//    ⇒ 本组建起那个来源:敌人不再只是战场上的 `Rules::Combatant`,
//      而是一个 `Model::Enemy` 实体 + 它在战场上的**投影**。

// 敌人生成模板 —— `enemybase1.txt` 的**本批用到的那些列** + 一列来自遇敌表。
//
// ★ 为什么不放 `shared/`:生成模板是**内容数据的形状**,不是双端共享的规则。
//   `shared/rules` 只拿 `SpawnTemplate` 那 6 列(算四维真正要用到的),
//   其余列(四属 / AI / 图号 / 名字 / 捕获难度)属 L4 内容与服务端刷怪 ⇒ 客户端不刷怪。
//   ⚠️ 反过来说也成立:**内容表的形状变化不该波及纯函数层** —— 这正是
//     `Progression.h` 的 `SpawnTemplate` 有意只取 6 列的理由,本结构不去破坏它。
//
// ⚠️★★ **两张表,别当成一张**(源码 `ENEMY_createEnemy(array, baselevel)` 的两个下标):
//      `array`  → **敌人表** `enemy1.txt`     —— `ENEMY_LV_MIN/MAX` · `ENEMY_PETFLG` ·
//                                                掉落 · 战术 · 经验 · `ENEMY_ID`
//      `tarray` → **模板表** `enemybase1.txt` —— `E_T_*` 全部列
//   ⇒ 本结构**只装模板表的列**;敌人表那一半见下方 `EnemyEncounter`。
//     ★ 同一只怪在不同敌人表配置下可捕 / 不可捕,而捕获难度跟着模板走 ——
//     合并两张表会把这个区分抹掉。
//
// ✅ **批次 M.5 已把敌人表建起来**(`EnemyEncounter`)⇒ M.4b 那句「`capturable` 是入参」
//    到此兑现:该字段**已从本结构移走**,它从来不属于模板表(源码 :1165 读的是
//    `*(p + ENEMY_PETFLG)`,而模板表 c38 那个同名的 `E_T_PETFLG` **没有一处读它**)。
//    ⚠️ 别因为模板表里也有个 `PETFLG` 列就把它加回来 —— 那是本项目已栽过四次的
//    「同名不同源」,而这一对的两个值在真数据里**常常相同** ⇒ 加回来测试也未必抓到。
struct EnemyTemplate
{
	// 参与四维生成的 6 列(DR-DT10)。★ 直接复用 L3 的结构,不另立一份 ——
	//   两份会漂移,而漂移的表现是"四维算出来不一样"而没有一处报错。
	SA::Rules::SpawnTemplate stats{};

	// 四属性(`E_T_{EARTH,WATER,FIRE,WIND}AT`,源码 :1071-1074)。
	// ⚠️★ 这里按 **地水火风** 具名列出(与 `Rules::Element` 同序),而源码的拷贝顺序是
	//    火水地风 —— **具名字段**就是为了让这个差别无从出错(顺序陷阱,已栽过两次)。
	std::int32_t earth = 0;
	std::int32_t water = 0;
	std::int32_t fire = 0;
	std::int32_t wind = 0;

	// AI 模式(`E_T_MODAI`,源码 :1075)。⚠️ 同槽异义 `CHAR_MODAI == CHAR_CHARM`。
	std::int32_t mod_ai = 0;

	// 图号(`E_T_IMGNUMBER`,源码 :1023-1024)。
	std::int32_t image = 0;

	// 捕获难度(`E_T_GET` → `CHAR_WORKMODCAPTUREDEFAULT`,源码 :1166)。
	// ⚠️ 载入器对空列保持 `-1`(`06` §3.5)⇒ 这里可以是 −1,照传不兜底。
	std::int32_t capture_difficulty = 0;

	// 名字(`E_T_NAME`,源码 :1108-1110)。★ 模板表里唯一的非 ASCII 列(`04` §7.2)。
	//
	// ⚠️★★ **敌人的名字取的是这一列,不是敌人表的 `ENEMY_NAME`** —— 这是 M.5 回源码
	//    核出来的,此前四份文档都没记:`ENEMY_createEnemy:1108-1110` 拷的是
	//    `ENEMYTEMP_enemy[tarray].chardata[E_T_NAME]`,而敌人表那一列 `ENEMY_NAME`
	//    在**整个生成路径上一次都没被读**(它只出现在 GM 制作宠物 / 宠物领取 / 问答
	//    奖励的提示串里)。★ 实测印证:`enemy1.txt` 第 75 行的 `ENEMY_NAME` 是
	//    `sai_w_001_2/3乌力` —— 把等级区间写进了名字,那是**给配表人看的标签**,
	//    不是玩家看到的名字。⇒ `EnemyEncounter` 因此**不建 name**(见那边文末 ⑧)。
	SA::Model::NameStr name{};
};

// 敌人表(`enemy1.txt`)的一行 —— 批次 M.5。
//
// ★★ 这一批补的是 M.4b 明确留下的两个入参:`level` 与 `capturable`。
//    ⇒ 刷怪从「调用方指定等级」变成「据配置摇号」,而这**不是**把旧路径推翻:
//    源码 :1030-1035 是 `baselevel > 0 ? baselevel : RAND(LV_MIN, LV_MAX)`,
//    ★ 两个分支**都是原版**,而分流靠一个**局部变量的初值**:
//    `battle.c:2178` 声明 `baselevel = 0`,**只有 NPC 触发时才被赋值**
//    (`:2225` 取 `CHAR_getInt(npcindex, CHAR_LV)`),最后统一传入(`:2250`)
//    ⇒ 野外遇敌那支(`:2228` 的 `npcindex == -1`)**一路保持 0** ⇒ 走摇号。
//    ⇒ M.4b 的接口对应的是前一支,本批补的是后一支。
//
// ── 文件与列的对应(★ 用错文件会整表错位一列)──────────────────────────
//   `setup.cf` 的 `enemyfile` 指向 **`enemy1.txt`(34 列 = 3 char + 31 int)**,
//   ⚠️ **不是**同目录那个 `enemy.txt`(33 列)—— 后者是宏关闭时代的旧数据。
//   判据:`ENEMY_STARTINTNUM` 在 `_BATTLENPC_WARP_PLAYER` 开启时是 **4**
//   (展开视图 `char/enemy.c:458`,而 `include/version.h:103` 该宏已定义)
//   ⇒ 多一列 `ENEMY_ACT_CONDITION`。`stoneage-plan/docs/05` §3.1 把这一对
//   新旧文件称作「宏状态的天然标尺」。
//   ⇒ 1-based 列号:c1 NAME · c2 TACTICSOPTION · c3 ACT_CONDITION ·
//     c4 ID · c5 TEMPNO · c6 LV_MIN · c7 LV_MAX · c8 CREATEMAXNUM ·
//     c9 CREATEMINNUM · c10 TACTICS · c11 EXP · c12 DUELPOINT · c13 STYLE ·
//     c14 PETFLG · c15-24 ITEM1-10 · c25-34 ITEMPROB1-10。
//
// ★ 为什么和 `EnemyTemplate` 一样放在 `world/` 而不是 `shared/`:同一条判据 ——
//   内容表的形状不是双端共享的规则,客户端不刷怪(见 `EnemyTemplate` 卷首)。
struct EnemyEncounter
{
	// 本行的身份(`ENEMY_ID`,c4)。
	//
	// ★ 它是这张表的主键:全树 44 处引用靠它查行(GM 制作宠物 `chatmagic.c` ·
	//   宠物领取 `callfromac.c:1551` · 问答奖励 `playerquestion.c:83`)。
	// ⚠️ 本批**不把它写进 `Model::Enemy`**(源码 :1090 写 `CHAR_PETENEMYID`,
	//    而捕获 `pet.c:366` 会拷给宠物)—— 理由不是"做不到"而是**没有消费方**:
	//    读它的那三个功能都未移植,而 D 线入库后 ID 的权威在库里。
	//    ⇒ 保持 `Enemy.h` 文末 ② 的裁定不变;届时接经验结算(`ENEMY_getExp` 要行下标)
	//      时一并决定,那时才有真读者。
	std::int32_t enemy_id = 0;

	// 模板号(`ENEMY_TEMPNO`,c5)⇒ 指向 `enemybase1.txt` 的 `E_T_TEMPNO`。
	//
	// ⚠️★ **原版在载入期把它解析成行下标并缓存**(`:469-478`:遍历模板表找相等的
	//    `E_T_TEMPNO`,存进 `ENEMY_enemy[i].enemytemparray`),★ 而且**找不到就整行丢弃**
	//    (`:474-477` 打「文件语法错误」+ `continue`)⇒ **运行期不存在"配了个不存在的
	//    模板"的敌人行**。⇒ 那道门属 D 线导入器(入库时校验外键),不是运行期检查。
	// ⇒ 本字段留着是为了让这条外键在代码里看得见:`spawnEnemyToField` 收 tmpl + enc
	//   两个参数,而**谁保证它们是配对的**这个问题必须有个明确的回答(答案:导入期)。
	std::int32_t temp_no = 0;

	// 等级区间(`ENEMY_LV_MIN` / `ENEMY_LV_MAX`,c6 / c7)。
	//
	// ⚠️★★ **载入期有两条归一,而它们在 8.0 投产数据上一次都不触发**(源码 :479-486):
	//      ① `if (lv_min == 0) lv_min = lv_max;`   ⇒ 0 是「固定为 max」,不是「从 0 级起」
	//      ② `LV_MIN = min(a,b)` · `LV_MAX = max(a,b)` ⇒ 写反了自动纠正
	//    实测 `enemy1.txt` 2154 行:`lv_min == 0` **0 行** · `lv_min > lv_max` **0 行**
	//    ⇒ 两条都是防御性代码。★ **但仍然移植**,判据是第三条而不是"照抄源码":
	//      `Rules::Random::rand` 的契约写明「lo > hi 的行为由实现定义,调用方须自行
	//      保证 lo <= hi」⇒ 不归一就是把一个**原版永不出现**的状态引入我们的运行期。
	//    ⇒ 归一落在 `rollEncounterLevel` 里(位置与原版不同,等价性判据见那里)。
	//
	// ★ 实测分布(2154 行):`lv_min == lv_max` **1142 行(53%)** 是固定等级,
	//   其余 1012 行是真区间(宽度 top:2 → 291 行 · 10 → 173 · 1 → 162 · 3 → 120)。
	std::int32_t lv_min = 0;
	std::int32_t lv_max = 0;

	// 可否被捕(`ENEMY_PETFLG`,c14 → `CHAR_WORK_PETFLG`,源码 :1165)。
	//
	// ★★ **这一列是本结构存在的第二个理由**(第一个是等级区间):它从 M.4b 的
	//    `EnemyTemplate` 移到这里,因为它从来属于敌人表 —— 见 `EnemyTemplate`
	//    卷首那条「别因为模板表里也有个 PETFLG 就加回去」。
	// ★ 实测分布:**1277/2154 行可捕(59%)**,877 行不可捕 ⇒ `Combatant.h` 那句
	//   「并非所有敌人都可捕(BOSS / 事件怪不带此标记)」第一次有了量。
	bool capturable = false;
};

// ── 敌人表里源码写了、本批**有意不建**的列(逐条记明,均非遗漏)───────────────
//
// ① `ENEMY_CREATEMAXNUM`(c8)⇒ **敌人编组**批次:它是"这一行最多刷几只"，
//    消费点在 `ENEMY_getEnemy` 的编组摇号里(`:1394` 累加上限 · `:1426` 同种计数),
//    而那条路要连 `group1.txt` + `encount.txt` 一起做(见 `spawnEnemyToField` 声明处)。
//
// ② ⚠️★ `ENEMY_CREATEMINNUM`(c9)—— **死列,全树 0 处引用**(2026-09-09 实测:
//    展开视图全树 grep 只命中 `include/enemy.h` 的枚举声明本身)。
//    ★ 这不是"我们暂不用",是**原版从来不用** ⇒ 它属 `03` §11 那族死字段,
//    D 线入库时**不该为它建列**。⚠️ 别看名字对称就以为 MAX 有 MIN 也有。
//
// ③ `ENEMY_TACTICS`(c10)· `ENEMY_TACTICSOPTION`(c2)· `ENEMY_ACT_CONDITION`(c3)
//    ⇒ **敌人 AI 战术**(源码 :1160-1164 写三个 WORK 字段),L4。
//    ★ 实测一条能缩小这个缺口的事实:`ENEMY_TACTICS` 在 2154 行里**全是 1**
//    (单一值)⇒ 战术**号**无变化,有变化的是 `TACTICSOPTION` 那个字符串
//    (`at:10;1;1|gu:1|es:1|wa:0;...`)⇒ 将来接 AI 时要解析的是它,不是那个号。
//
// ④ `ENEMY_EXP`(c11)· `ENEMY_DUELPOINT`(c12)⇒ **战果结算**。
//    ⚠️★ 两者有**依赖顺序**,别分开移植:源码 :1101-1107 是
//    `DUELPOINT` 先无条件写,然后**只有 `DUELPOINT <= 0` 时才给 EXP**,
//    且 `EXP == -1` 是"不覆盖"哨兵 ⇒ 走 `ENEMY_getExp(array, tarray, level, rank)`。
//    ⇒ 三个值构成一棵判定树,拆开移植会得到一个"看起来对"的错经验值。
//
// ⑤ `ENEMY_STYLE`(c13)⇒ 敌人武器(源码 :1132-1150 的 switch → `ITEM_makeItemAndRegist`)。
//    ⚠️★★ **实测把 M.4b 登记的那条偏差缩小了一个数量级,值得改口径**:
//    `ENEMY_STYLE` 在 2154 行里 **2112 行是 0**(98%),而 `switch(0)` 落 `default`
//    ⇒ `wepon` 保持 −1 ⇒ **不发武器** ⇒ **原版 98% 的敌人本来就是空手的**。
//    ⇒ 「我们的敌人一律空手 ⇒ 比原版更容易触发空手多段」这条偏差**仍然成立,
//      但只影响 42 行(2%)**,不是全部敌人。★ M.4b 写它时没有数据,现在有了。
//
// ⑥ `ENEMY_ITEM1-10`(c15-24)· `ENEMY_ITEMPROB1-10`(c25-34)⇒ 掉落,道具系统。
//    ★ 实测 **948/2154 行(44%)配了掉落**;概率是千分数(源码 :1121 `RAND(0,999) <  prob`)。
//    ⚠️ 源码的循环上界写成 `(ENEMY_ITEMPROB10 - ENEMY_ITEM1 + 1) / 2`(:1119)——
//      即"两段列宽的一半" ⇒ 它**要求两段等长且相邻**,这个隐含约束在改表结构时会断。
//
// ⑦ `ENEMY_ID` 写进 `Model::Enemy`(`CHAR_PETENEMYID`)⇒ 见 `enemy_id` 字段那条。
//
// ⑧ `ENEMY_NAME`(c1)⇒ **不建,而这不是推迟,是它在生成路径上根本不被读** ——
//    见 `EnemyTemplate::name` 那条。★ 建了它就会有人拿它当敌人名字用,
//    而真名在模板表 ⇒ 那会是一个"显示正确了 99% 的行"的静默错误。

class World final : public SA::Net::TransportEvents,
                    public SA::Net::SessionHost
{
  public:
	World(const SA::Platform::ServerConfig &config,
	      SA::Platform::Clock &clock,
	      SA::Platform::Logger &logger,
	      SA::Platform::RandomSource &random,
	      SA::Net::Transport &transport);
	~World() override;

	World(const World &) = delete;
	World &operator=(const World &) = delete;

	// 推进一个 tick。⚠️ 01 §2:主线程绝不允许阻塞 ⇒ 本函数不等待任何 I/O。
	void tick();

	// 开一场战斗。★ 种子由 Platform::RandomSource 派发**并落日志** ——
	//   01 §10「战斗事件流 + 注入式随机源 = 可回放」,而可回放的前提是种子留得下来。
	BattleId startBattle(const SA::Rules::BattleField &field);

	// 把一条会话接进某场战斗的某个槽。1.5 没有选角,槽位由调用方指定。
	bool joinBattle(BattleId battle, SA::Net::SessionId session,
	                std::uint8_t slot);

	// 据模板生成一只敌人,建 L2 实体并投影进某场战斗的某个槽(批次 M.4b)。
	//
	// ★★ **这是 `rollSpawnStats` 的第一个真实调用方** —— 欠债 25 立案的原话是
	//    「地基绿而运行时不接,`ctest` 一样全过」,而消掉它的唯一办法是有一条
	//    **真的经过 World、真的建实体、真的能被观察**的路径。
	//
	// ★ 与 `joinBattle` 对称:那个把**会话**接进槽(玩家侧),这个把**模板**接进槽(敌人侧)。
	//   ⚠️ 两者都不是"真玩法入口" —— 真入口是 tick 第 3 步 `kNpcSpawn`(遇敌 / 刷怪,
	//     阶段 2)。⇒ 本函数是那一步落地前的**显式入口**,不是脚手架:
	//     刷怪逻辑将来只需决定"何时、在哪、用哪几行",生成与入场就是这里。
	//   ✅ **M.5 把"什么等级"从待答变成已答**:等级不再由调用方硬给,而是据敌人表
	//     的区间摇号(`baselevel <= 0` 那支)⇒ 上面那句原本写的是"用哪个模板、什么等级"。
	//   ⬜ 仍未答的是"**用哪几行**",而那需要两张表接力(都不在本批):
	//       `encount.txt`(1050 行,坐标 → 编组;源码 `ENEMY_getEnemy(charaindex, x, y)`)
	//       → `group1.txt`(1220 行,编组 → 最多 10 个 `ENEMY_ID` + 各自 `CREATEPROB`)
	//       → 逐行本函数。★ 切分点就在本函数的入参:**行以内**是本批,**选哪几行**是下一批。
	//
	// 三道门,失败即**世界一个字节都没动**(顺序 = 预留→提交,同 `createPetFromCombatant`):
	//   ① 战斗不存在 / 槽号越界;
	//   ② 敌人池满(`EntityPool::allocate` 返 kNullHandle ⇒ 落 `kEntityPoolExhausted`);
	//   ③ `enterEnemyToField` 的两道门(槽必须在玩家段 / 槽未被占)——
	//      ★ 这一步失败会把刚 allocate 的实体**释放掉**再返回,不留孤儿。
	//
	// ⚠️ 随机源取的是**该场战斗的 rng**(`b.rng`),不是 `Platform::RandomSource` ——
	//    可回放的凭据是"战斗种子 + 事件流"(`01` §10),敌人四维是那场战斗状态的一部分。
	//    ★ 后果:同一颗战斗种子下,敌人生成会先把若干个数摇掉 ⇒ **入场顺序影响
	//      后续所有取值**。这不是缺陷(原版同理:刷怪也在同一个全局 rng 上),
	//      但它意味着"改变刷怪时机"会改变回放 ⇒ 回放必须连刷怪调用序一起重现。
	//    ⚠️★ **摇掉几个数取决于走哪个分支**(M.5):`baselevel > 0` ⇒ 14 次
	//      (只有 `rollSpawnStats`);`baselevel <= 0` ⇒ **15 次**,多的那一次是等级摇号,
	//      而且它在 14 次**之前** —— 顺序即语义,详见 `rollEncounterLevel`。
	bool spawnEnemyToField(BattleId battle, std::uint8_t slot,
	                       const EnemyTemplate &tmpl, const EnemyEncounter &enc,
	                       std::int32_t baselevel);

	// ⚠️ 这两个不能写成内联 —— 状态在 pimpl 的 Impl 里,头文件看不见它。
	void requestShutdown() noexcept;
	bool stopped() const noexcept;

	// ── TransportEvents ──
	void onConnected(SA::Net::ConnectionId id) override;
	void onBytes(SA::Net::ConnectionId id, const std::uint8_t *data,
	             std::size_t n) override;
	void onDisconnected(SA::Net::ConnectionId id) override;

	// ── SessionHost ──
	void onSessionReady(SA::Net::SessionId id) override;
	void onBattleCommand(SA::Net::SessionId id,
	                     const SA::Domain::BattleCommand &cmd) override;
	void onSessionClosed(SA::Net::SessionId id) override;

	// ── 观察面(测试与运维)──
	std::uint64_t ticks() const noexcept;
	std::size_t sessionCount() const noexcept;
	const BattleStats *stats(BattleId id) const;
	SA::Net::SessionState sessionState(SA::Net::SessionId id) const;

	// ── L2 实体池的观察面(批次 M.1)──────────────────────────────
	//
	// ★ 池在 pimpl 的 Impl 里 ⇒ 用例与运维只能经这里看见它。加这四个是因为
	//   欠债 20 的要害正是「地基绿而运行时不接,ctest 一样全过」——
	//   **没有观察面,就没有任何东西能断言接上了。**
	std::size_t playerCount() const noexcept;
	std::size_t petCount() const noexcept;

	// 敌人池的活跃数(批次 M.4b)。
	//
	// ★★ 加它的理由与 `petCount` 完全一样,而且这次更要紧:欠债 25 的判据就是
	//    「必须有观察面」—— 没有它,`spawnEnemyToField` 建了实体还是没建、
	//    战斗结束有没有回池,**没有任何东西能断言**。
	// ⚠️ 它同时是**泄漏的探针**:M.1 的教训是"主人走了宠物没释放,池只增不减,
	//    跑够久才表现为捕获突然失败"。敌人池同族 ⇒ 用例断言"战斗结束后回落到 0"。
	std::size_t enemyCount() const noexcept;

	// 某场战斗某个槽背后的 L2 `Enemy` 实体(只读)。不存在 / 无实体返回 nullptr。
	//
	// ★ 这是捕获链路的观察面:捕获拷的四维来自这里,用例要能对着源头比。
	const SA::Model::Enemy *battleEnemyAt(BattleId id, std::uint8_t slot) const;

	// 某会话背后 Player 的第 pet_slot 只宠物(只读)。空槽 / 无实体返回 nullptr。
	//
	// ⚠️★ **这条推翻了 M.1 时写下的一句话**:那时 `world_tick` 里记着
	//    「池内实体没有对外观察面(有意如此:L2 字段不该经 world 的公开 API 逐个漏出去)」,
	//    于是"捕获出的宠物字段"那条用例只能断言 `petCount()`。
	//    ★ 现在看,那句话把两件事混了:反对的是**逐字段 getter**(每个字段一个方法),
	//      而不是"返回整个实体的 const 视图" —— 后者与 `battleEnemyAt` 同形,
	//      调用面只有一个方法、且是只读。
	//    ⇒ M.4b 必须有它:欠债 23 关闭的判据就是"捕获出的宠物四维非 0",
	//      没有这个面,那句话**无法被断言**,而不可断言的关闭等于没关闭。
	const SA::Model::Pet *playerPetAt(SA::Net::SessionId session, int pet_slot) const;

	// 某会话背后 Player 的捕获计数 / 已占宠物槽数。
	// ⚠️ 会话不存在或没有 L2 实体 ⇒ **返回 −1**,不返 0 ——
	//    0 与"真的是 0"分不开,而这两种情况在排查时要问的是完全不同的问题
	//    (同 EntityIndex::find 未命中给明确空值那一条)。
	int playerCaptureCount(SA::Net::SessionId session) const;
	int playerPetSlotsUsed(SA::Net::SessionId session) const;
	// 当前出战宠在 `pets[]` 的槽号(原 `CHAR_DEFAULTPET`)。-1 = 无实体 / 无出战宠。
	// ★ DR-BT21 的测试观察面:换宠(PET_OUT/PET_IN)是否写对 `default_pet`。
	int playerDefaultPet(SA::Net::SessionId session) const;

	// 某场战斗的战场快照(只读)。不存在返回 nullptr。
	//
	// ★ 加它的理由有两条,都不是"为了测试方便":
	//   ① 运维侧「现在战场什么样」是排查战斗问题的第一手信息(与 stats() 同族);
	//   ② ★ 世界写的效果有一半落在 `field` 上(HP / 骑宠 HP / 离场 / capture_bonus 清零),
	//      而 applyEvents 是 world 内部函数 ⇒ **没有这个面,那半边世界写没有任何东西
	//      能断言它真的发生了** —— 而"看起来做了、其实没写"正是 §9.0.16 那族静默。
	// ⚠️ 返回 const 引用语义:调用方不得改战场。要改只能经事件(ApplyEvents 卷首那条分工)。
	const SA::Rules::BattleField *battleField(BattleId id) const;

  private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};

// ── 战场态宠物入场 / 离场(批次 M.2)────────────────────────────────
//
// ★★ 把一只 L2 `Model::Pet` 投影成战场态 `Rules::Combatant`,放进
//    `slots[owner_field_slot + kBattlePlayerMax]`。原版宠物是站在 `Entry[主人位次+5]`
//    的**独立完整战斗单位**(展开视图 `battle.c:932` 的 `BATTLE_NewEntry` `CHAR_TYPEPET`
//    分支),**不是** `Combatant::ride_hp` 那只骑乘宠 —— 两套东西。
//
// ★ 自由函数而非 `World` 成员:它是**纯投影 + 站位判定**,不碰 World 的运行时所有权
//   (会话 / 池)⇒ 可被单元测直接喂 `field` + `Pet` 验证,不必跑一整场战斗。
//
// ⚠️★ **本批(M.2)只交付这个机制**。"选哪只宠"(读 `Player::default_pet`)与在
//    `joinBattle` 里自动带出,留给换宠指令批次 —— 那时 `default_pet` 才有写者(PET_OUT);
//    现在就接进 `joinBattle` 会是一条**永不触发的死路径**(捕获不设 `default_pet`,
//    而换宠指令尚未做)⇒ 不写永假分支(同 `createPetFromCombatant` 对源码门 ② 的处置)。
//
// 返回是否入场成功。三道门(照 `BATTLE_PetDefaultEntry:1402` + `NewEntry` 的 kPet 分支):
//   ① `owner_field_slot` 必须在玩家段(每 side 前 `kBattlePlayerMax` 槽;宠位不能再带宠);
//   ② 宠物存活 `pet.hp > 0`。⚠️ 源码是"有效 && !CHAR_ISDIE && HP>0",`ISDIE` 依赖未移植的
//      状态系统 ⇒ 本批以 `hp>0` 为准并记明,`ISDIE` 单独成立的情形待状态系统补;
//   ③ 目标宠位 `slots[owner+5]` 未被占(源码 `NewEntry:975` 的 `ENTRYMAX`)。
//
// ⚠️★★ 源码 `BATTLE_PetDefaultEntry` **恒返回 0**(`battle.c:1432`,`NewEntry` 成败都不改
//    `ret`),`BATTLE_PetOut` 只能靠"入场后 `DEFAULTPET` 是否 <0"反推成败 —— 一处隐性缺陷。
//    ★ 本函数**返回真实入场成败**,不复刻它;换宠指令批次的 PET_OUT 用这个返回值。
//
// ✅ **战斗三围 `attack`/`defense`/`quick`/`max_hp` 由 `deriveBaseStats` 从四维推出**
//    (DR-DT9,批次 M.3)。⚠️★ 那条旧注释(「留 0,`complianceParameter` 未移植」)
//    在 M.3 就已作废,这里连同它的**第二半**一起更新:M.4b 让捕获宠的四维有了非 0 来源
//    (从 `Model::Enemy` 拷)⇒ 经捕获来的宠物**现在真的有战力**。
//    ⚠️ 但**一般宠创建**(`PET_createPet`,从 `enemyindex` 拷四维给非战斗获得的宠物)
//      仍未移植 ⇒ 经那条路来的宠物四维照旧 0、三围照旧 0。★ 别把"捕获宠能打了"
//      读成"所有宠物都能打了";`world_tick` 有一条用例专门钉住 0 四维那一支仍是 0。
bool enterPetToField(SA::Rules::BattleField &field, int owner_field_slot,
                     const SA::Model::Pet &pet);

// 把宠物从战场撤下(占位清空)。仿 `BATTLE_PetDefaultExit:1377`。
// ★ 只置 `occupied=false` —— 撤下不是战死,**不置 `dead`**(同逃跑成功 / 捕获离场:
//   记成阵亡会污染战果 / 经验结算,阶段 2)。owner_field_slot 越界或非玩家段则无操作。
void exitPetFromField(SA::Rules::BattleField &field, int owner_field_slot);

// ── 敌人生成与入场:两个自由函数(接上文 `EnemyTemplate`,批次 M.4b)──────────
//
// ★ 模板结构声明在 `World` 类**之前**(类里的 `spawnEnemyToField` 要用它),
//   而这两个函数放在这里 —— 与 `enterPetToField` / `exitPetFromField` 并排,
//   因为它们是同一类东西:**纯投影 + 站位判定**,不碰 World 的运行时所有权。

// 据敌人表的等级区间摇一个等级 —— 1:1 移植 `ENEMY_createEnemy:1034` 的
// `RAND(ENEMY_LV_MIN, ENEMY_LV_MAX)`,**外加载入期的两条归一**(`:479-486`)。批次 M.5。
//
// ⚠️★★ **归一的位置与原版不同,这是有意的,等价性判据在此**:
//    原版在**载入期**归一并**写回表**(`ENEMY_setInt`)—— 一次归一、多次摇号;
//    我们没有载入期(本批不做文件加载器,`04` §7.1 的终点是入库)⇒ 归一落在这里,
//    每次摇号前对**局部副本**做一遍。三条判据保证等价:
//      ① 归一**幂等** ⇒ 做一次和做 N 次结果相同;
//      ② 归一**不消耗 rng** ⇒ 不影响可回放序列;
//      ③ 归一**只碰这两列** ⇒ 不影响其他列的取值。
//    ⚠️ 代价说清:归一结果**不写回** `enc` ⇒ 若将来有别处读 `enc.lv_min`(例如
//      运维显示"这只怪 3-5 级"),读到的是**未归一**值。本批无第二个读者;
//      ★ D 线导入器落地时若选择在入库时归一,本函数的归一因幂等而不必删。
//
// ★ 为什么不省掉归一(实测它一次都不触发):见 `EnemyEncounter::lv_min` 那条 ——
//   判据不是"照抄源码",是 `Random::rand` 的 `lo <= hi` 契约由**调用方**负责。
std::int32_t rollEncounterLevel(const EnemyEncounter &enc, SA::Rules::Random &rng);

// 据模板 + 敌人表行生成一只敌人 —— 1:1 移植 `ENEMY_createEnemy`
// (`char/enemy.c:994-1180`)里**做得到**的那一段。建 / 不建逐条见
// `shared/model/Enemy.h` 文末(模板侧)与 `EnemyEncounter` 文末(敌人表侧)。
//
// ★ 纯函数(不碰池、不碰战场)⇒ 可被单元测直接喂模板验证,同 `enterPetToField` 的取向。
//
// ⚠️★★ **`baselevel` 的语义照抄源码 :1030-1035,两个分支都是原版**:
//      `baselevel > 0`  ⇒ 用它(NPC 触发的战斗,`battle.c:2250` 传 NPC 的等级);
//      `baselevel <= 0` ⇒ `rollEncounterLevel(enc, rng)`(野外遇敌:`battle.c:2178`
//                         的初值 0 一路没被赋值,见 `EnemyEncounter` 卷首)。
//    ⇒ M.4b 的"等级是入参"**不是权宜**,它就是前一支;M.5 补的是后一支。
//    ⚠️ 因此**不要**把参数改成"必须 > 0"再另开一个函数 —— 那会把原版的一个
//      `if/else` 拆成两个入口,而调用方(将来的刷怪)本来就是按这个条件分流的。
//
// ★ rng 消耗:`baselevel > 0` ⇒ **14 次**(全在 `rollSpawnStats` 里);
//   `baselevel <= 0` ⇒ **15 次**,★ 多的那次在**最前面**(源码 :1034 的摇号在
//   :1045 的 ±2 扰动之前)⇒ 同种子下两条分支的四维**不同**,而这不是缺陷:
//   顺序即语义(同 M.4b 成长率取"扰动后、撒点前"那一刻的理由)。
//   ⚠️ 原版在此之后还有 `ENEMY_RandomChange`(会摇)与掉落 / 武器(会摇)——
//     均未移植 ⇒ 同种子下我们的序列与原版不同,而原版不可运行(P1)、无可比对序列。
//
// ⚠️★ **满血入场是推导的产物,不是模板列**:源码 :1153 先 `CHAR_complianceParameter`,
//    :1159 再 `CHAR_HP = CHAR_getWorkInt(WORKMAXHP)` ⇒ 本函数用
//    `deriveBaseStats(四维).max_hp` 填 `hp`,而**不存 max_hp**(不造第二真源)。
SA::Model::Enemy spawnEnemy(const EnemyTemplate &tmpl, const EnemyEncounter &enc,
                            std::int32_t baselevel, SA::Rules::Random &rng,
                            const SA::Rules::RulesConfig &cfg);

// 把一只 L2 `Model::Enemy` 投影成战场态 `Rules::Combatant`,放进 `slots[field_slot]`。
//
// ★ 与 `enterPetToField` 对称:那个把宠物投到 `slots[主人+5]`,这个投到调用方指定的槽。
//   ⇒ 站位不由本函数猜:原版 `BATTLE_NewEntry` 按 **side 参数**落位,而 side 是遇敌 /
//     组队逻辑的结果(未移植)。⚠️ 因此本函数**不强制必须在敌方半场** ——
//     PvE 惯例是 10..14,但把这条写死会让将来的 PvP / 混合阵营撞在一个假约束上。
//
// 返回是否入场成功。两道门:
//   ① `field_slot` 必须在**任一 side 的玩家段**(每 side 前 `kBattlePlayerMax` 槽)——
//      宠位(`slot % kSideOffset >= kBattlePlayerMax`)留给 `enterPetToField`,
//      敌人占了宠位会让"主人+5"的映射失效;
//   ② 目标槽未被占(源码 `NewEntry:975` 的 `ENTRYMAX`)。
//
// ⚠️★ **没有"敌人存活"那道门**(对比 `enterPetToField` 的门②):敌人是**当场生成**的,
//    `spawnEnemy` 已用推导出的 max_hp 把它填成满血 ⇒ 不存在"叫出一只死敌人"这回事。
//    ★ 但仍**照实拷 `enemy.hp`** 而不是重算一遍:若调用方给了一只被改过血的敌人
//      (将来的续场 / 存档),那才是它当前的真血量。
//
// ★★ 战斗三围由 `deriveBaseStats` 从四维推出(DR-DT9),与 `enterPetToField` 同一处理。
// ⚠️★ **HP 夹取(`min(HP, WORKMAXHP)`,原版 `char.c:3555`)在这里也不做**,
//    但理由与 `enterPetToField` **不同**,两条都要留着:
//      · 那边:宠物四维当时无源 ⇒ max_hp 恒 0 ⇒ 夹取 = 叫出即死(残缺 × 夹取的新失真);
//      · 这边:★ **夹取根本不属于"入场投影"这一步** —— 它在 `CHAR_complianceParameter`
//        里,而那个函数在原版是**每回合准备阶段**逐角色重算三围时调的(`05` §2.3 第 4 件事,
//        对应 `BATTLE_TurnParam`,**未移植**)。⇒ 把它塞进入场是把回合准备的动作
//        挪到了错的位置;等回合准备那一批落地时,夹取跟着它一起来。
//      ⚠️ 现在不做也没有可观察后果:`spawnEnemy` 保证 `hp == max_hp`。
bool enterEnemyToField(SA::Rules::BattleField &field, int field_slot,
                       const SA::Model::Enemy &enemy);

} // namespace SA::World

#endif // __SA_WorldApi_H__
