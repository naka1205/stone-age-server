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

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "model/Enemy.h"
#include "model/Item.h"
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

// ══ 地图与通行性(批次 W.1)═══════════════════════════════════════════════
//
// ★ 地图是**内容数据的形状**,不是双端共享规则 —— 通行性由服务端权威判定,客户端只预测
//   (10 §4.1)。同 EnemyEncounter(M.5)放 world/。真实地图(LS2MAP,1,235 图)走 D 线导入
//   (终点入库,非运行时读 txt);本批用 fixture,两个查表的接口不变、数据源将来可换。
// ⚠️ 本批只做地面通行(isfly=false)与 WALKABLE 一项;飞行 / 角色 flag 短路(透明/公交,
//   MAP_walkAble:73)随 onWalk 接;olink(每格对象链)留视野批次。

using TileId = std::int32_t;

// 图元通行性(原图元属性表 MAP_WALKABLE 列,三值)。值与源码 switch 分支一一对应(map_deal.c:40-57)。
enum class WalkKind : std::uint8_t
{
	kBlocked = 0,  // obj 层此值 ⇒ 不可走
	kNeedBoth = 1, // obj 层此值 ⇒ 需 tile 层也是 kNeedBoth 才可走
	kFree = 2,     // obj 层此值 ⇒ 可走
};

// 一张地图的运行时形状(10 §3.1)。★ 每格只存图元号,属性另查(§3.3)。
struct GridMap
{
	std::int32_t width = 0;
	std::int32_t height = 0;
	std::vector<TileId> tile; // 地表层图元号,size == width*height(行主序)
	std::vector<TileId> obj;  // 物件层图元号,同上

	bool inBounds(std::int32_t x, std::int32_t y) const noexcept
	{
		return x >= 0 && x < width && y >= 0 && y < height;
	}
	// ★ 调用前须 inBounds:越界索引是调用方的错,不在此静默兜底(与 EntityPool 同取向)。
	std::size_t index(std::int32_t x, std::int32_t y) const noexcept
	{
		return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
		       static_cast<std::size_t>(x);
	}
	TileId tileAt(std::int32_t x, std::int32_t y) const noexcept { return tile[index(x, y)]; }
	TileId objAt(std::int32_t x, std::int32_t y) const noexcept { return obj[index(x, y)]; }
};

// 图元属性表(原图元属性文件,10 §3.2 共 17 项;本批只建 WALKABLE 一列)。按图元号索引。
// ⚠️ 越界图元号 ⇒ kBlocked(原版 getTileAndObjData 取不到即 FALSE,map_deal.c:28)。
struct TileAttrTable
{
	std::vector<WalkKind> walkable; // walkable[tileId]

	WalkKind kindOf(TileId id) const noexcept
	{
		if (id < 0 || static_cast<std::size_t>(id) >= walkable.size())
			return WalkKind::kBlocked;
		return walkable[static_cast<std::size_t>(id)];
	}
};

// 地面通行性 —— 1:1 移植 MAP_walkAbleFromPoint(map_deal.c:24)的 isfly==FALSE 分支。越界即不可走。
bool mapWalkable(const GridMap &map, const TileAttrTable &attr, std::int32_t x,
                 std::int32_t y) noexcept;

// fixture(供联调与用例,真实地图走 D 线)。图元号:0 墙 / 1 需双 / 2 地面;全填地面。
GridMap makeFixtureMap(std::int32_t width, std::int32_t height);
TileAttrTable makeFixtureAttr();

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

// 敌人体型(`E_T_SIZE` 的取值,源码枚举 `include/enemy.h:4-8`)。批次 M.7 用于大怪布阵。
inline constexpr std::int32_t kEnemySizeNormal = 0;
inline constexpr std::int32_t kEnemySizeBig = 1;

struct EnemyTemplate
{
	// 模板号(`E_T_TEMPNO`,c7 = int 列第 1 个,源码枚举 `include/enemy.h:12`)。批次 M.7。
	//
	// ⚠️★ **M.7 之前本结构没有它**:M.4b/M.5 的 `spawnEnemy` 由调用方直接传配对好的
	//    (tmpl, enc),不必按号解析。M.7 的第三跳要**运行时**把敌人表行的 `temp_no`
	//    解析成模板行(`findEnemyTemplate`)⇒ 这一列是那条外键的被引用端。
	// ★ 原版在载入期靠它把 `ENEMY_TEMPNO` 解析成模板行下标并缓存(`enemy.c:469-478`,
	//   ★ 找不到就丢弃整行)⇒ 我们没有载入期,改为运行时线性扫(判据同 M.6 的 `findEnemyGroup`)。
	std::int32_t temp_no = 0;

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

	// 体型(`E_T_SIZE`,c39;源码枚举 `include/enemy.h:6-8` = 0:NORMAL / 1:BIG)。批次 M.7。
	//
	// ⚠️★★ **它决定大怪布阵**(`ENEMY_getEnemy:1430-1464`):大怪(BIG)只能站前 5 位,
	//    第 6 只起放不下就减少出场数;要放后排时去前 5 位换一只 NORMAL 顶到后排。
	// ★★ **实测 enemybase1.txt 1053 行:NORMAL 520 / BIG 533 ⇒ BIG 占 50.6%** ——
	//    ⇒ 与 M.6 的 `zorder`(1050 行全 > 0、那半个语义一次都不触发)**恰好相反**:
	//    大怪布阵是**真实主路径**而非防御代码 ⇒ `rollEnemyList` 的用例必须重度覆盖它。
	std::int32_t size = kEnemySizeNormal;

	// 捕获难度(`E_T_GET` → `CHAR_WORKMODCAPTUREDEFAULT`,源码 :1166)。
	// ⚠️ 载入器对空列保持 `-1`(`06` §3.5)⇒ 这里可以是 −1,照传不兜底。
	// ★★ 战果结算批次:它同时是 `enemyExp()` 的 alpha 里的 `E_T_GET` 项(源码 :789)——
	//    **一列两用**,不重建。
	std::int32_t capture_difficulty = 0;

	// ── 经验公式 `enemyExp()` 的 alpha 项(源码 `ENEMY_getExp:789-793`,战果结算批次)──
	//
	// ★ `alpha = (critical + counter + [E_T_GET=capture_difficulty] + poison + paralysis
	//   + sleep + stone + drunk + confusion) / 100.0 + rare` ⇒ 敌人越难缠 / 越稀有,
	//   给的经验越多。列名照 `include/enemy.h` 的 `E_T_*`。
	// ⚠️ 只为经验公式建 —— 状态抗性本身(战斗里的免疫)属 L4 状态系统,那时另建;
	//   这里是同一批模板数据的**经验用途**,不是把 L4 提前做了。
	std::int32_t critical = 0;  // E_T_CRITICAL
	std::int32_t counter = 0;   // E_T_COUNTER
	std::int32_t poison = 0;    // E_T_POISON
	std::int32_t paralysis = 0; // E_T_PARALYSIS
	std::int32_t sleep = 0;     // E_T_SLEEP
	std::int32_t stone = 0;     // E_T_STONE
	std::int32_t drunk = 0;     // E_T_DRUNK
	std::int32_t confusion = 0; // E_T_CONFUSION
	std::int32_t rare = 0;      // E_T_RARE

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
	//    ⇒ 两条都是防御性代码。★ **但仍然移植**,而判据在 2026-09-09 **换过一次**:
	//      ⚠️ 原判据是「`Rules::Random::rand` 的契约把 `lo <= hi` 交给调用方 ⇒ 不归一
	//        就是把原版永不出现的状态引入运行期」—— **DR-BT23 之后那条理由失效了**:
	//        退化区间现在**有定义**(返回 `lo` 且照常消耗一次),不再是 UB 入口。
	//      ✅ 现判据更硬,因为它是**源码事实**而不是我方约定:
	//        **原版在载入期就做了这两条归一**(`char/enemy.c:479-486`)
	//        ⇒ 位置可换(载入期 → 摇号前),**行为须等价**。
	//    ★★ 教训值一句:「因为我们的接口要求 X,所以移植 Y」这种论证**会随接口改口径
	//      而蒸发**;先问「原版是不是也做了 Y」才站得住(`00` §9.0.36 ④)。
	//    ⇒ 归一落在 `rollEncounterLevel` 里(位置与原版不同,等价性判据见那里)。
	//
	// ★ 实测分布(2154 行):`lv_min == lv_max` **1142 行(53%)** 是固定等级,
	//   其余 1012 行是真区间(宽度 top:2 → 291 行 · 10 → 173 · 1 → 162 · 3 → 120)。
	std::int32_t lv_min = 0;
	std::int32_t lv_max = 0;

	// 本行一次最多刷几只(`ENEMY_CREATEMAXNUM`,c8)。批次 M.7。
	//
	// ★ 两个消费点都在第三跳 `ENEMY_getEnemy`:
	//   ① `createenemynum += CREATEMAXNUM`(:1394)⇒ 出场数上界的一半
	//      (`entrymax = RAND(1, min(enemymaxnum, Σ CREATEMAXNUM))`);
	//   ② 同族上限门 `cnt >= CREATEMAXNUM * samecount`(:1426):同一敌人行放够这么多就不再放。
	// ★★ **实测 enemy1.txt 2154 行:min=1 / max=63,无 0 无负** ⇒ `createenemynum ≥ 1`
	//    ⇒ `entrymax = RAND(1, ≥1)` **不触发** DR-BT23 的退化区间,同族门阈 ≥ 1 不会恒真。
	//    ⚠️ 默认值取 **1** 而非 0:0 会让门 `cnt >= 0` 恒真 ⇒ 漏填一行就整组刷不出;
	//      1 是真数据的下界 —— 同 M.6 的 `enemy_max_num` 默认 4 的取向(给安全默认)。
	std::int32_t create_max_num = 1;

	// 可否被捕(`ENEMY_PETFLG`,c14 → `CHAR_WORK_PETFLG`,源码 :1165)。
	//
	// ★★ **这一列是本结构存在的第二个理由**(第一个是等级区间):它从 M.4b 的
	//    `EnemyTemplate` 移到这里,因为它从来属于敌人表 —— 见 `EnemyTemplate`
	//    卷首那条「别因为模板表里也有个 PETFLG 就加回去」。
	// ★ 实测分布:**1277/2154 行可捕(59%)**,877 行不可捕 ⇒ `Combatant.h` 那句
	//   「并非所有敌人都可捕(BOSS / 事件怪不带此标记)」第一次有了量。
	bool capturable = false;

	// ── 战果:经验值与决斗点(`ENEMY_EXP` c11 / `ENEMY_DUELPOINT` c12,战果结算批次)──
	//
	// ★★ 判定树(源码 `char/enemy.c:1101-1107`,落地在 `spawnEnemy`):
	//      `Enemy.duelpoint = enc.duelpoint`(无条件);
	//      仅当 `enc.duelpoint <= 0` 才给 exp;`enc.exp == -1` 是哨兵 ⇒ 走 `enemyExp()`。
	// ⚠️★★ **哨兵是 -1,所以 `exp` 默认值取 -1 而非 0** —— 0 会被判定树读成
	//    「配表写了 0 经验」(用表值)而不是「没配 ⇒ 走公式」,是 M.5「默认值即语义」
	//    那族坑(同 `create_max_num` 默认 1 的取向)。`duelpoint` 默认 0 = 经验怪。
	// ⚠️ `enemyExp()` 只读**模板**列 + level + rank,不读本表(校正见 `Enemy.h` 文末 ⑥)。
	std::int32_t exp = -1;
	std::int32_t duelpoint = 0;

	// ── 掉落表(`ENEMY_ITEM1-10` c15-24 / `ENEMY_ITEMPROB1-10` c25-34,批次 I.3)──────
	//
	// ★ 实测 948/2154 行(44%)配了掉落;概率是**千分数**(源码全宏 `enemy.c:1213`
	//   `RAND(0,999) < prob`,`_FIX_ITEMPROB` ON)。`spawnEnemy` 逐槽摇 → `Enemy.dropped_items`。
	// ⚠️★ **两段等长且相邻**:源码循环上界 `(ENEMY_ITEMPROB10 - ENEMY_ITEM1 + 1) / 2`
	//   要求 item 段与 prob 段各 `kMaxDrops` 个、位置配对(`item[i]` 配 `item_prob[i]`)。
	// ★ 判据是 `item_prob[i] != 0`(不是 item 列):prob=0 ⇒ 该槽不摇、不耗 rng ⇒
	//   默认全 0 = 无掉落 ⇒ 未配掉落的敌人行为与本批之前逐位一致(同 I.1「默认恒 0」)。
	// ⚠️ 内容表放 `world/`(不前推 shared),同 `capturable` / 等级区间:客户端不刷怪。
	std::int32_t item[SA::Model::Enemy::kMaxDrops] = {};
	std::int32_t item_prob[SA::Model::Enemy::kMaxDrops] = {};
};

// ── 敌人表里源码写了、本批**有意不建**的列(逐条记明,均非遗漏)───────────────
//
// ① ✅ `ENEMY_CREATEMAXNUM`(c8)—— **批次 M.7 已建为上方 `create_max_num`**:
//    它是"这一行最多刷几只",两个消费点(`:1394` 累加出场上限 · `:1426` 同族上限门)
//    都在第三跳 `ENEMY_getEnemy` 里,M.7 的 `rollEnemyList` 兑现了 M.5/M.6 的这处预告。
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
// ④ ✅ `ENEMY_EXP`(c11)· `ENEMY_DUELPOINT`(c12)⇒ **战果结算批次已建**为上方
//    `exp` / `duelpoint`(判定树见那里)。★ 三值一棵树的裁定兑现在 `spawnEnemy`、没拆开:
//    `DUELPOINT` 无条件写 → `DUELPOINT <= 0` 才给 EXP → `EXP == -1` 哨兵走 `enemyExp()`。
//    ⚠️ `enemyExp()` 只读模板 + level / rank(不读本表行 `p`,校正见 `Enemy.h` 文末 ⑥)。
//
// ⑤ `ENEMY_STYLE`(c13)⇒ 敌人武器(源码 :1132-1150 的 switch → `ITEM_makeItemAndRegist`)。
//    ⚠️★★ **实测把 M.4b 登记的那条偏差缩小了一个数量级,值得改口径**:
//    `ENEMY_STYLE` 在 2154 行里 **2112 行是 0**(98%),而 `switch(0)` 落 `default`
//    ⇒ `wepon` 保持 −1 ⇒ **不发武器** ⇒ **原版 98% 的敌人本来就是空手的**。
//    ⇒ 「我们的敌人一律空手 ⇒ 比原版更容易触发空手多段」这条偏差**仍然成立,
//      但只影响 42 行(2%)**,不是全部敌人。★ M.4b 写它时没有数据,现在有了。
//
// ⑥ ✅ `ENEMY_ITEM1-10`(c15-24)· `ENEMY_ITEMPROB1-10`(c25-34)⇒ **批次 I.3 已建**为
//    上方 `item[]` / `item_prob[]`,`spawnEnemy` 逐槽千分率摇 → `Enemy.dropped_items`。
//    ★ 实测 **948/2154 行(44%)配了掉落**;概率是千分数(源码 :1121 `RAND(0,999) <  prob`)。
//    ⚠️ 源码的循环上界写成 `(ENEMY_ITEMPROB10 - ENEMY_ITEM1 + 1) / 2`(:1119)——
//      即"两段列宽的一半" ⇒ 它**要求两段等长且相邻**,已用 `kMaxDrops` 钉住两段等长。
//
// ⑦ `ENEMY_ID` 写进 `Model::Enemy`(`CHAR_PETENEMYID`)⇒ 见 `enemy_id` 字段那条。
//
// ⑧ `ENEMY_NAME`(c1)⇒ **不建,而这不是推迟,是它在生成路径上根本不被读** ——
//    见 `EnemyTemplate::name` 那条。★ 建了它就会有人拿它当敌人名字用,
//    而真名在模板表 ⇒ 那会是一个"显示正确了 99% 的行"的静默错误。

// ── 遇敌坐标表与敌人编组表(批次 M.6)────────────────────────────────────
//
// ★ 两张表接力回答 M.5 留下的那个问题:「刷哪几行敌人」。
//     玩家坐标 ──encount.txt──▶ 区域(最多 10 个编组 + 各自权重)
//              ──group1.txt──▶ 编组(最多 10 个 `ENEMY_ID` + 各自权重)
//              ──enemy1.txt──▶ M.5 的 `EnemyEncounter` 逐行
//   ⚠️ **本批只做前两跳**(坐标 → 编组),第三跳(编组 → 敌人列表)见文末「本批不做」。
//
// ★ 放 `world/` 而非 `shared/`:同 `EnemyTemplate` / `EnemyEncounter` 的判据 ——
//   内容表的形状不是双端共享的规则,**客户端不刷怪**。
//
// ⚠️★★ **数据基准是 `csa8.0/gmsv/data/`,不是 `StoneAge/gmsv/data/`** ——
//    两棵树**文件名完全相同而内容不同**,且**两份 `setup.cf` 都指向同名文件**
//    ⇒ 无法靠文件名区分,只能靠路径。实测行数(csa8.0 / StoneAge):
//    `encount.txt` **1051** / 1199 · `group1.txt` **1220** / 1395 · `enemy1.txt` **2154** / 2998。
//    ★ 这比 M.5 踩的 `enemy.txt` vs `enemy1.txt` 更隐蔽:那次靠文件名可分。
//    ⚠️ 且 `group.txt`(旧版)恰好是 1220 行 —— 与 csa8.0 的 `group1.txt` **撞数**
//    ⇒ 拿错基准时行数校验**会通过**。

// 一个区域最多挂几个编组(`ENCOUNT_GROUPMAXNUM`,`include/encount.h:4`)。
inline constexpr int kEncountGroupMaxNum = 10;

// 一个编组最多挂几个敌人(`CREATEPROB1 - ENEMY_ID1`,`include/enemy.h:129-148`)。
//
// ★ 源码用这个减法表达"两段等长且相邻",而不是写个 10 ——
//   ⚠️ 那个隐含约束同样出现在掉落列上(`EnemyEncounter` 文末 ⑥),改表结构时会断。
inline constexpr int kEnemyGroupSlotMaxNum = 10;

// 单次遇敌的敌人索引表长度(`ENEMY_INDEXTABLEMAXSIZE`,`char/enemy.c:25`)。批次 M.7。
//
// ★ 它是 `ENEMY_getEnemy` 的产出数组 `ENEMY_indextable[]` 与候选 `work[]/wr[]` 的定长上限。
//   ⚠️ 大怪布阵要按**位置索引**读写(把大怪换到前排、NORMAL 顶后排)⇒ `rollEnemyList`
//     内部保留这个定长数组 + 末尾裁剪,而不是一路 `push_back` —— 后者做不到按位置换。
// ★ 出场数 `entrymax ≤ enemy_max_num ≤ 10 < 16` ⇒ 16 是安全余量,不会越界。
inline constexpr int kEnemyIndexTableMaxSize = 16;

// `encount.txt` 的一行(33 列,`ENCOUNT_Table`,`include/encount.h:20`)。
//
// ⚠️ 结构体整体在 `#ifdef _ADD_ENCOUNT` 下,而 `version.h:93` **已定义**
//    ⇒ 末三列(`event_now` / `event_end` / `enemy_group`)存在,列数是 33 不是 30。
struct EncountArea
{
	std::int32_t index = -1; // c1:表内编号(仅标识,查表不用它)
	std::int32_t floor = 0;  // c2:地图号,与 `CHAR_FLOOR` 比对

	// c3-c6(`x1,y1,x2,y2`)⇒ 载入期归一成**左上 + 宽高**(源码 `encount.c:222-226`):
	//   `x = min(x1,x2)` · `width = max(x1,x2) - min(x1,x2)`(⚠️ **不 +1**)
	//
	// ★★ 判定用 `PointInRect`(`util.c:1363`),而它是**闭区间**:
	//      `rect.x <= px && px <= rect.x + rect.width`
	//    ⇒ 与"宽度不 +1"正好配对 ⇒ 语义是「`x1..x2` 两端都含」,单点区域(x1==x2)也匹配。
	// ⚠️★ 同一个 `RECT` 在**同一个文件**里被两种口径解读:`clipRect`(`util.c:1379`)
	//    用的是 `x + width - 1`(把 width 当格数)。⇒ 本项目只走 `PointInRect` 那条口径;
	//    将来若复用这两个字段做别的事,**两种口径会打架**。
	std::int32_t x = 0;
	std::int32_t y = 0;
	std::int32_t width = 0;
	std::int32_t height = 0;

	// c7 / c8:遇敌**概率**区间(千分比语义待下一批核)。
	//
	// ⚠️★ **本批不消费这两列** —— 它们属「要不要遇敌」(遇敌率骰子在 `char_walk.c`,
	//    分母 `rand()%(120*getEnemyAction())`),而本批做的是「遇到什么」。
	//    ⇒ 建它们只为让那条外键在代码里看得见,和 `EnemyEncounter::temp_no` 同理。
	// ⚠️★★ 载入期有一处**原版缺陷**,移植时不要照抄:`encount.c:214-215` 与 `:271-272`
	//    两处都写着 `encountprob_min = 1; encountprob_min = 50;`
	//    ⇒ **第二行本该是 `_max`** ⇒ `encountprob_max` 从未获得默认值。
	//    ★ 实际无后果(33 列全给,默认值被逐行覆盖),但它是"复制粘贴传播"的活样本。
	// ★ 另有一条载入期归一**要移植**:`min/max` 写反了自动纠正(`:245-253`),
	//   与敌人表 `lv_min/lv_max` 完全同族。
	std::int32_t prob_min = 0;
	std::int32_t prob_max = 0;

	// c9:本区域一次最多刷几只(`enemymaxnum`)。
	//
	// ⚠️ 载入期有**范围门**:`maxnum < 1 || maxnum > 10` ⇒ 打「文件语法错误」并**丢弃整行**
	//    (`:262-266`)⇒ 运行期该值恒在 `[1,10]`。⇒ 那道门属 D 线导入器。
	// ⚠️★ 它在本批**不被消费**:消费点是下一批的 `min(enemymaxnum, Σ CREATEMAXNUM)`
	//    ⇒ 与 `EnemyEncounter` 文末 ① 说的 `CREATEMAXNUM` 消费点是**同一处**。
	std::int32_t enemy_max_num = 4;

	// c10:`zorder` —— ⚠️★★ **它兼任"启用开关",而名字完全看不出这件事**。
	//
	// 源码 `ENCOUNT_getEncountAreaArray`(`:377-381`):坐标匹配之后还有一道
	// `if (curZorder > 0)`,**不满足就整行跳过**;多个区域都匹配时取 `zorder` **最大**的。
	// ⇒ 语义是两件事:① 重叠区域的优先级;② **`zorder <= 0` 的行永不生效**。
	//
	// ★ 实测 1050 行**全部 > 0**(取值分布:1 → 588 行 · 10 → 144 · 50 → 69 · 5 → 59 · 100 → 50)
	//   ⇒ ② 那一半**一次都不触发**。⚠️ **仍然移植** —— 判据同 M.5 两条归一(经 DR-BT23 改口径后):
	//   **原版就是这么做的**,不移植就是让原本不生效的行生效,而**真实数据里没有这种行
	//   ⇒ 也就没有任何用例会因为漏掉它而变红**(只能靠手造数据钉住)。
	std::int32_t zorder = 0;

	// c11-c20:本区域可能出现的编组号(`GROUP_ID`),`-1` = 空槽。
	// c21-c30:与上一一对应的**权重**。
	//
	// ⚠️★★ **它不是百分比** —— 实测「权重和 == 100」的行只有 **2/1051(0.2%)**;
	//    抽签是 `r = RAND(0, Σ−1)` 再累加找区间(`enemy.c:1346-1350`)⇒ 任意正整数权重即可。
	//    ★ 实测权重和 min=1 / max=801 / 均值 34.8,**无一行为 0** ⇒ 本表不会触发退化区间。
	// ⚠️ 载入期对 `group_id` 查重(`checkRedundancy`,`:304-310`)⇒ 重复即**丢弃整行**;
	//    ★ 该函数**显式跳过 -1**(`util.c:1606`)⇒ 多个空槽不算重复。
	std::array<std::int32_t, kEncountGroupMaxNum> group_id{};
	std::array<std::int32_t, kEncountGroupMaxNum> group_prob{};

	// c31-c33:NPC 事件改组(`event_now` / `event_end` / `enemy_group`)。
	//
	// ⚠️★ **本批建字段但不实现那道门** —— 它依赖 NPC 旗标系统(`NPC_NowEventCheckFlg` /
	//    `NPC_EventCheckFlg`,`enemy.c:1376-1377`),整个 NPC 事件系统未移植(`07`)。
	// ★ 规模实测:`event_now != -1` **8/1051 行(0.76%)** · `event_end != -1` **0 行** ·
	//   `enemy_group != -1` **8 行** ⇒ 影响面 0.76%,且**恰好就是那 8 行**。
	// ⇒ 与「不建」的区别要紧:建了字段,那道门将来接上时**不需要改表结构**,
	//   而现在它是一个**在代码里看得见的缺口**(见 `pickEnemyGroup` 文末)。
	std::int32_t event_now = -1;
	std::int32_t event_end = -1;
	std::int32_t enemy_group = -1;
};

// `group1.txt` 的一行(24 列 = 1 char + 23 int,枚举见 `include/enemy.h:126-155`)。
//
// ⚠️★ **列序与枚举序不一致**:文件第 1 列是 `GROUP_NAME`(char),而枚举里它排在
//    全部 int 之后。载入器靠 `GROUP_STARTINTNUM = 2` 把文件第 2 列对到枚举 0
//    (`enemy.c:677-688`)⇒ 读表时**不能按枚举序数文件列**。
struct EnemyGroup
{
	std::int32_t group_id = -1; // c2:被 `EncountArea::group_id` 引用的键

	// c3 / c4:两道**道具门**(`GROUP_APPEARBYITEMID` / `GROUP_NOTAPPEARBYITEMID`)。
	//
	// 源码 `enemy.c:1307-1332`:
	//   · `appear_by_item_id != -1` 且玩家**没有**该道具 ⇒ 本组不参与抽签;
	//   · `not_appear_by_item_id != -1` 且玩家**持有**该道具 ⇒ 本组不参与抽签。
	//
	// ★ 实测:`APPEARBY != -1` **139/1220 行(11.4%)** · `NOTAPPEARBY != -1` **47 行(3.9%)**。
	// ★★ **本批完整实现这两道门**(不是留空)—— 判据只需要「玩家持有哪些道具 ID」,
	//    那是一个**快照**,不是运行时状态 ⇒ 作为入参传入即可,不破 D2。
	// ⚠️★★ 而道具系统未移植 ⇒ 调用方目前传**空**背包 ⇒ 后果要精确说:
	//    · 对**空背包玩家**,我们的行为与原版 **100% 一致**(原版对没带道具的人也排除);
	//    · 偏差只在"玩家其实带着那个道具"这一情形 ⇒ 那 139 组暂时永不出现。
	//    · 实测连带后果:**25/969 个有编组的区域(2.58%)** 在空背包下**完全不遇敌**
	//      (它们的全部编组都要求持有道具),另 **104 个(10.7%)** 编组组合发生变化。
	//    ⇒ ★ 这是**依赖未就绪**,不是留空的门:道具系统接上后那 25 个区域自动恢复。
	std::int32_t appear_by_item_id = -1;
	std::int32_t not_appear_by_item_id = -1;

	// c5-c14:本组的敌人(`ENEMY_ID`),`-1` = 空槽。c15-c24:各自权重(`CREATEPROB`)。
	//
	// ⚠️★ 载入期把 `ENEMY_ID` 解析成敌人表行下标并缓存(`enemy.c:690-710`),
	//    ★ **找不到就把该槽置 -1**(不是丢整行);而**一个都没解析成功**才丢整行,
	//    重复 `ENEMY_ID` 也丢整行 ⇒ 三种处置各不相同,别混。
	// ★ 实测敌人槽:平均 **1.79** 个 / 最多 9 个 / **只配 1 个的占 52.6%**。
	// ⚠️★★ 权重和实测 **4 行为 0** ⇒ 下一批的 `r = RAND(0, Σ−1)` 会拿到 `RAND(0,−1)`
	//    ⇒ **那正是 DR-BT23 的退化区间**(确定返回 0 且照常消耗一次);
	//    其中 **3 行被 `encount.txt` 引用 ⇒ 可达**,不是死数据。
	//    ⇒ ★ 本批不消费权重,但这条实测**是 R.1 排在本批之前的直接理由**。
	std::array<std::int32_t, kEnemyGroupSlotMaxNum> enemy_id{};
	std::array<std::int32_t, kEnemyGroupSlotMaxNum> create_prob{};

	// ⚠️ c1 `GROUP_NAME` **不建** —— 同 `EnemyEncounter` 文末 ⑧ 与 `EnemyTemplate::name`:
	//    它是配表人的标签(实测 `sai_e_113_1` / `dan_1_04_44_32/33` 这种),
	//    ★ 生成路径一次都不读它,建了就会有人拿它当显示名。
};

// ── 世界刷怪点(批次 W.2)──────────────────────────────────────────────────
//
// ★ 让敌人成为**地图上的常驻游荡怪**(明雷),区别于 W.4 的**遇敌即时生成到战场**(暗雷):
//   前者在世界地图上存在、会游荡、被玩家视野看到,撞上才开战;后者走路骰子命中即建战斗。
//
// ⚠️★★ **这不是原版某张表的 1:1 移植** —— 原版地图上的敌人 / NPC 由 **NPC 脚本 / Lua**
//    调 `ENEMY_createEnemy(enemy_id, level)` 逐个刷(`mylua/npcbase.c:472` / `npc_lua`),
//    而**脚本层(D6)尚未落地** ⇒ 照搬要先做脚本层(依赖未就绪,同 W.4 的传送点抑制)。
//    ⇒ 本结构是**不阻塞 D6 的最小切法**:把"在哪刷 / 刷哪只 / 刷几只 / 多久走一步"做成
//    注入式配置(同 `loadEncounterTables` 默认空的取向)。真玩法接脚本层后,由脚本产出
//    这些参数,本接口就是那时的灌入点。⇒ 登记为**依赖未就绪的替身**,不是留空。
//
// ★ 敌人来源仍走单一真源:`enemy_id`(敌人表主键)→ `findEnemyEncounter` 得 `EnemyEncounter`
//   → `findEnemyTemplate(enc.temp_no)` 得 `EnemyTemplate` → `spawnEnemy` 生成
//   (复用 M.4b / M.7,不另造敌人构造路径)⇒ 刷怪点表与遇敌四表都经注入,同属内容数据。
struct SpawnPoint
{
	std::int32_t floor = 0;
	std::int32_t x = 0; // 刷怪中心
	std::int32_t y = 0;
	std::int32_t enemy_id = 0;              // 敌人表 ENEMY_ID(查 encounters/templates 生成)
	std::int32_t count = 1;                 // 本点维持的活敌人只数(kNpcSpawn 补齐到它)
	std::int32_t wander_radius = 4;         // 游荡半径:走一步不超过离中心这么远(0 = 原地不动)
	std::int64_t wander_interval_ms = 1000; // 游荡节拍(ms):每这么久走一步(原 CHAR_LOOPINTERVAL)
	std::int32_t level = -1;                // 等级;<=0 走敌人表 LV_MIN/MAX 摇号(同 spawnEnemy)
};

// 世界态敌人的位置快照(批次 W.2 / W.3 的观察面)。
//
// ★ 加它的理由同 `PlayerPos` / `enemyCount`:欠债 20 那族「地基绿而运行时不接,ctest 一样全过」——
//   没有观察面,「敌人真的 spawn 到地图了 / 游荡后位置真的变了」无从断言。
//   ⚠️ `entity_id` = 敌人 `EntityHandle` 的编码,**与 `CharAppear.entity_id` 同一口径**
//     ⇒ 用例既能追踪同一只敌人跨 tick 的位移,又能对着视野下行消息核 id 是否一致。
struct WorldEnemyPos
{
	std::uint64_t entity_id = 0;
	std::int32_t floor = 0;
	std::int32_t x = 0;
	std::int32_t y = 0;
	std::uint8_t dir = 0;
	std::int32_t image = 0; // E_T_IMGNUMBER(CharAppear.image 的源)
};

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

	// 注入遇敌数据表(批次 W.4)—— 坐标→区域→编组→敌人行的四张内容表。
	//
	// ★★ 这是遇敌触发链的**数据源**:`kCharLoop` 玩家段走一格后据它判「要不要遇敌 /
	//    遇到什么」(`findEncountArea` → `pickEnemyGroup` → `rollEnemyList` → 逐行 `spawnEnemyToField`)。
	// ⚠️★ **默认空 ⇒ 永不遇敌** —— 现有走路用例(W.1)不注入即不受影响;遇敌用例显式注入 fixture。
	//    ★ 这不是脚手架:真玩法里这四张表由 **D 线内容导入入库**后加载(阶段 2),本接口就是
	//      那时的灌入点 —— 同 `spawnEnemyToField` 卷首「行以内是本批,数据源在导入期」。
	void loadEncounterTables(std::vector<EncountArea> areas,
	                         std::vector<EnemyGroup> groups,
	                         std::vector<EnemyEncounter> encounters,
	                         std::vector<EnemyTemplate> templates);

	// 注入世界刷怪点(批次 W.2)—— 让 `kNpcSpawn` 据它把敌人刷到地图上。
	//
	// ⚠️★ **默认空 ⇒ 世界里没有常驻怪** —— 现有走路 / 遇敌用例不注入即不受影响(同
	//    `loadEncounterTables`)。⇒ 刷怪点用 `enemy_id` 查的是 `loadEncounterTables` 注入的
	//    那份敌人表 / 模板表 ⇒ **两个 load 都要调**,否则查不到即该点刷不出(落 warn、不崩)。
	void loadSpawnPoints(std::vector<SpawnPoint> points);

	// 往某会话玩家的背包放一个道具(批次「捕获扣道具」的**注入 seam**)。
	//
	// ★★ 与 `spawnEnemyToField` / `loadEncounterTables` 同性质:是个**灌入点**,不是玩法。
	//    捕获扣道具批次要**删**背包里的条件道具,而写入链路(掉落 / 捡起)是后续批次 ——
	//    本 seam 让"扣"这条链现在就有东西可扣、可观察(同那两个注入表让遇敌有数据可遇)。
	//    ⇒ 真玩法接掉落 / 捡起后,道具由那些路径进背包,本 seam 退回纯测试注入。
	// 返回:放入的背包槽下标(∈ [kStartItemArray, kMaxItemHave));失败(无 L2 玩家 /
	//    背包满 / 道具池满)返 −1,且**世界一个字节都没动**(门在任何写之前,同捕获三门)。
	int giveItemToPlayer(SA::Net::SessionId session, const SA::Model::Item &item);

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
	void onWalk(SA::Net::SessionId id, const SA::Domain::WalkRequest &req) override;
	void onEvent(SA::Net::SessionId id, const SA::Domain::EventRequest &req) override;
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

	// 道具池的活跃数(批次 I.1)。
	//
	// ★★ 加它的理由与 `petCount`/`enemyCount` 同一条:背包 L2 是"地基先落、写入链路
	//    后接"的分层,而**没有观察面就没有任何东西能断言接上了**(欠债 20 的要害)。
	// ⚠️ 本批它**恒 0** —— 没有任何写入者 allocate 道具。这不是 bug,是登记在案的留白;
	//    用例专门断言"本批恒 0",将来接掉落 / 捡起时,这条断言会被新用例改写成"非 0"。
	//    ★ 它同时是将来的**泄漏探针**(同 petCount:释放道具不清槽 ⇒ 池只增不减)。
	std::size_t itemCount() const noexcept;

	// 当前活跃战斗数(批次 W.4 遇敌触发的探针)。
	// ★ 遇敌用例断言「走动后从 0 变 1」—— 没有它,「遇敌真的开了一场战」无从断言
	//   (同 `enemyCount` 的理由:不接的静默只有观察面能戳破)。
	std::size_t battleCount() const noexcept;

	// 世界态活跃敌人数 / 全部位置快照(批次 W.2 / W.3 的观察面)。
	//
	// ★ 与战斗态敌人分开:`enemyCount()` 数的是 **EnemyPool 里的全部实体**(含战斗态 +
	//   世界态);本组只数 / 只列**世界态**那部分(在地图上、会游荡、被视野广播的那些)。
	//   ⇒ 用例能分别断言「刷了几只到世界」与「战斗里生成了几只」,两条静默各有探针。
	std::size_t worldEnemyCount() const noexcept;
	std::vector<WorldEnemyPos> worldEnemies() const;

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

	// 某会话背后 Player 已占**背包**槽数(批次 I.1)。-1 = 会话无 L2 实体(同上,与真 0 区分)。
	// ★ 只数背包段 `[kStartItemArray, kMaxItemHave)` 的**有引用**槽(悬空句柄也算,
	//   两步分工同 `playerPetSlotsUsed`)。本批恒 0(无写入者)⇒ 用例断言之。
	int playerItemSlotsUsed(SA::Net::SessionId session) const;

	// 某会话背后 Player 的第 slot 个道具槽(只读,批次「捕获扣道具」)。
	// 空槽 / 悬空句柄 / 无实体返回 nullptr。★ 与 `playerPetAt` 同形(返回整个 const 视图,
	//   不逐字段漏)—— 捕获扣道具用例要能断言"命中的道具被删、不命中的还在"。
	// ⚠️ slot 是**全域**下标 [0, kMaxItemHave):装备位段也可读(卸装校验将来会用)。
	const SA::Model::Item *playerItemAt(SA::Net::SessionId session, int slot) const;

	// 当前出战宠在 `pets[]` 的槽号(原 `CHAR_DEFAULTPET`)。-1 = 无实体 / 无出战宠。
	// ★ DR-BT21 的测试观察面:换宠(PET_OUT/PET_IN)是否写对 `default_pet`。
	int playerDefaultPet(SA::Net::SessionId session) const;

	// 某会话背后 Player 的累计经验值(原 `CHAR_EXP`,战果结算批次)。
	// -1 = 会话无 L2 实体(与真 0 区分,同 `playerCaptureCount`;exp 非负 ⇒ -1 无歧义)。
	//
	// ★ 关闭判据 = 可断言:战斗胜利后玩家经验涨没涨、涨多少,靠它对着 `enemyExp()`
	//   × 等级差衰减比 —— 没有它,「打赢涨经验」这条闭环无从证明(同欠债 20 / 25 那族)。
	int playerExp(SA::Net::SessionId session) const;

	// 某会话背后 Player 的位置(批次 W.1)。valid == false ⇒ 该会话无 L2 实体。
	//   ★ 移动用例的观察面:走一步坐标变化 / 撞墙不变 / 转身只改 dir。
	struct PlayerPos
	{
		bool valid = false;
		std::int32_t floor = 0;
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::uint8_t dir = 0;
	};
	PlayerPos playerPos(SA::Net::SessionId session) const;

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
	// 遇敌命中后的开战组装(批次 W.4,内部)——移植 `EN_recv`(`callfromcli.c:1249`)+
	//   `BATTLE_CreateVsEnemy(charaindex,0,-1)` 净核(`battle.c:2528`):遇敌链选怪
	//   (`pickEnemyGroup` → `rollEnemyList`)→ 建场 → 玩家入场(Side[0])→ 逐只敌人入场
	//   (Side[1],`baselevel=-1` 野外摇号)→ 战斗自动进 tick。
	// ⚠️ `area_row` 是 `findEncountArea` 已命中的区域下标(kCharLoop 里算好);返回是否真开了战
	//   (无可用编组 / 空敌人列表 ⇒ false,与原版「本次不遇敌」等价)。
	bool triggerEncounter(SA::Net::SessionId session, std::int32_t area_row);

	// 明雷开战(批次 W.5,内部)——移植 EV 事件链 `EVENT_main`(event.c:37)→ `NPC_NPCEnemy_Encount`
	//   → `NPC_NPCEnemy_BattleIn` → `BATTLE_CreateVsEnemy(player,_,enemy)`(npc_npcenemy.c:672/674)。
	// ★★ 与暗雷 `triggerEncounter` 的关键区别:明雷用**世界态已存在的敌人实体**
	//    (`world_enemies[idx]`,把 `EntityHandle` 从世界态**转移**给战斗态 `enemy_of_slot`),
	//    ⇒ 不 `allocate`、不 `spawnEnemy`、**不耗战斗 rng**(敌人已在地图上,四维早已定)。
	//    开战即从 `world_enemies` 移除 + `broadcastEnemyDespawn`(原版明雷进战斗态即从地图消失)。
	// ⚠️ 战斗结束时 `enemy_of_slot` 的 handle 按暗雷同一路径回池 ⇒ 下个 tick `spawnWorldEnemies`
	//    据 `SpawnPoint.count` 补齐 ⇒ 自然复活(精确 REVIVALTIME 划出)。返回是否真开了战。
	bool triggerNpcEnemyBattle(SA::Net::SessionId session, std::size_t world_enemy_idx);

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
//   判据不是"照抄源码"也**不再是** `Random::rand` 的契约(DR-BT23 后退化区间已有定义),
//   而是**原版在载入期就做了**(`enemy.c:479-486`)⇒ 位置可换、行为须等价。
std::int32_t rollEncounterLevel(const EnemyEncounter &enc, SA::Rules::Random &rng);

// ── 遇敌:坐标 → 区域 → 编组(批次 M.6,移植 `ENEMY_getEnemy` 的前两段)────────
//
// ★ 切分点说明:原版 `ENEMY_getEnemy`(`char/enemy.c:1273-1467`,**195 行**)一口气做四件事
//   —— ① 坐标定区域 · ② 抽编组 · ③ 收候选敌人 · ④ 逐只放入阵列(含大怪布阵)。
//   ⚠️ ③④ 需要**敌人表全表 + 模板表全表 + NPC 旗标**三个数据源,而 ①② 只需要两张表
//   ⇒ 本批做 ①②,界面是**选中的编组行下标**(一个清晰、可断言的产出)。
//   ★ 这与 M.5 的切分同一思路:**切在函数入参上**,而不是切在"做一半"。

// 坐标 → `encount` 表行下标;`-1` = 该坐标不遇敌。
//
// 1:1 移植 `ENCOUNT_getEncountAreaArray`(`char/encount.c:370-392`)。三条语义:
//   ① `floor` 相等 **且** 坐标落在闭区间矩形内(`PointInRect`);
//   ② ⚠️★★ 还要 `zorder > 0` —— 见 `EncountArea::zorder`,那一列**兼任启用开关**;
//   ③ 多个区域都匹配时取 `zorder` **最大**的(★ 相等时保留**先遇到**的,
//      源码判据是严格 `>` ⇒ **顺序敏感**,而表的顺序就是文件行序)。
//
// ⚠️★ 它是**线性扫全表**(实测 1051 行),而原版每次移动判遇敌都调一次。
//    ⇒ 本批**照原样保留**:`01` §8.2 那条「索引不得线性扫描」约束的是**实体索引**,
//      内容表按坐标查没有等价的 O(1) 结构(矩形包含查询要空间索引)。
//    ⇒ 登记为性能事实而非缺陷,属阶段 3 的优化面(同欠债 13 `poll` 那条的性质)。
std::int32_t findEncountArea(const std::vector<EncountArea> &areas, std::int32_t floor,
                             std::int32_t x, std::int32_t y);

// `GROUP_ID` → `group` 表行下标;`-1` = 找不到。
//
// 1:1 移植 `GROUP_getGroupArray`(`char/enemy.c:745-755`)。⚠️★ 同样是线性扫(1220 行),
// 而**原版每次遇敌最多调它 11 次**(编组循环里最多 10 次 + 抽中后 1 次)⇒ 单次遇敌
// 最坏 13,420 次比较。★ 照原样保留,理由同 `findEncountArea`;⚠️ 但这条更值得将来动手 ——
// 按 `group_id` 查是**等值查询**,建 `unordered_map` 就够,不需要空间索引。
std::int32_t findEnemyGroup(const std::vector<EnemyGroup> &groups, std::int32_t group_id);

// 区域 → 选中的 `group` 表行下标;`-1` = 无可用编组(该坐标本次不遇敌)。
//
// 1:1 移植 `ENEMY_getEnemy` 的第二段(`char/enemy.c:1301-1355`):
//   ① 遍历 10 个编组槽,跳过 `-1`;
//   ② 两道**道具门**(见 `EnemyGroup::appear_by_item_id`)—— `player_item_ids` 是玩家
//      背包里的道具 ID **快照**;⚠️ 道具系统未移植 ⇒ 调用方传空,后果见那条注释;
//   ③ ⚠️★ 编组号查不到对应 group 行时**照原版跳过**(源码 `GROUP_getGroupArray` 返 -1
//      后 `GROUP_getInt` 会撞 `GROUP_CHECKINDEX` 返回 -1 ⇒ 两道门都视作"无门"⇒ 该组
//      仍会入选,而它是个**坏组**)。★★ **这一处我们不照抄**:直接跳过该槽,
//      理由是原版那条路径依赖 `GROUP_getInt` 越界返回 -1 的**防御性副作用**,
//      而它在我们这里是 `std::vector::at` ⇒ 照抄等于把"越界即 -1"这个约定移植过来。
//      ⚠️ 实测该情形 **0 次**(全部 `group_id` 都能查到),⇒ 是防御而非行为差异。
//   ④ 按权重抽签:`r = rand(0, Σ−1)`,累加找第一个 `w != 0 && r < 累加` 的槽。
//      ⚠️★★ **这一行里有两处细节是「等价写法」而不是行为判据**,别把它们当成判据去验:
//        · 上界 `found - 1`(最后一个候选不参与判定,落空即兜底);
//        · `w != 0` 这半个条件。
//      ★ 2026-09-09 穷举验证(1..4 槽 × 权重 {−1,0,1,2,3} × r 遍历 [0,Σ−1],2,580 组):
//        两处各自改掉、以及同时改掉,**结果差异均为 0 组** ⇒ 上界等价、`w != 0` **冗余**。
//      ⇒ ★ 仍**照抄源码**(它们是源码原文),但**不为它们编造"要紧"的理由** ——
//        ⚠️ 本注释初稿正是这么写的(「少了它会选中权重 0 的槽」),而那句话是错的:
//        权重 0 的槽不让 acc 增长,而 `r < acc_prev` 若成立、前一轮就 break 了。
//        ⇒ 反向验证 + 穷举当场揭穿(`00` §9.0.37 ⑥,与 §9.0.35 ⑤ / §9.0.36 ⑥ 同族第三次)。
std::int32_t pickEnemyGroup(const EncountArea &area, const std::vector<EnemyGroup> &groups,
                            const std::vector<std::int32_t> &player_item_ids,
                            SA::Rules::Random &rng);

// ── 遇敌:编组 → 敌人列表(批次 M.7,移植 `ENEMY_getEnemy` 的第三、四段)──────────
//
// ★ 切分点续 M.6:M.6 做完前两跳(坐标 → 区域 → 编组),本批做后两段 ——
//   ③ 收候选敌人 + 摇出场数(`:1356-1401`)· ④ 逐只抽 + 同族上限门 + 大怪布阵(`:1402-1465`)。
//   界面是**选中的敌人表行下标序列**(接 M.5 的 `spawnEnemy`/`spawnEnemyToField` 逐行生成)。
//
// ★ 三条与 M.5/M.6 一致的裁定(逐条依据见 `11` §2.16 DR-DT14):
//   · 外键解析:原版载入期缓存(`GROUP_group[].enemyarray[]` / `ENEMY_enemy[].enemytemparray`),
//     我们无载入期 ⇒ 运行时线性扫(`findEnemyEncounter` / `findEnemyTemplate`),找不到 → -1,等价;
//   · NPC 事件改组(`:1367-1383`)**不实现**:依赖 NPC 旗标(同 M.6,`EncountArea::event_now`);
//   · `ENEMY_RandomEnemyArray`(`:1385`)**不移植**:M.5 已登记 ⇒ 等价于"所有 ENEMY_ID 都不在
//     随机区间 `[945,956]∪[964,969]`",★ 实测该区间被 group1.txt 引用 **18 槽 / 6 行(0.49%)**,
//     ⚠️ 且它命中时会摇一次 rng ⇒ 不移植使序列偏差(P1:原版不可运行,无可比对序列)。

// ENEMY_ID → 敌人表行下标;`-1` = 找不到。1:1 移植 `ENEMY_getEnemyArrayFromId`(`enemy.c:519-528`)。
//
// ★ 原版在**载入期**就把 group 的 `ENEMY_ID` 解析成敌人表行下标缓存(`:690-710`),
//   运行期直接读 `GROUP_group[].enemyarray[]`(`ENEMY_getEnemyArrayFromIndex`:510)。
//   我们没有载入期 ⇒ 遇敌时现扫,语义等价(找不到 → -1,原版载入期同样置 -1)。
// ⚠️★ 线性扫全表(实测 2154 行),而单次遇敌最多扫 10 次 ⇒ 同 `findEnemyGroup` 登记为
//    性能事实(阶段 3 可换等值查询的 `unordered_map`),不是缺陷。
std::int32_t findEnemyEncounter(const std::vector<EnemyEncounter> &encounters,
                                std::int32_t enemy_id);

// TEMPNO → 模板表行下标;`-1` = 找不到。1:1 移植 `ENEMYTEMP_getEnemyTempArrayFromTempNo`
// (`enemy.c:337-347`)。★ 与 `findEnemyEncounter` 同族:原版载入期缓存,我们运行时扫。
std::int32_t findEnemyTemplate(const std::vector<EnemyTemplate> &templates,
                               std::int32_t temp_no);

// 选中的编组 → 敌人表行下标序列(含大怪布阵顺序;空 = 本次不刷出任何敌人)。
// 1:1 移植 `ENEMY_getEnemy` 的第三、四段(`char/enemy.c:1356-1466`)。批次 M.7。
//
// 入参:选中的 `group`(M.6 的 `pickEnemyGroup` 产出行)· 敌人表全表 · 模板表全表 ·
//       `enemy_max_num`(选中区域的 `EncountArea::enemy_max_num`,∈ [1,10]) · 战斗 rng。
//
// ── 第三段:收候选(`:1356-1401`)────────────────────────────────────────────
//   遍历编组 10 个 `ENEMY_ID` 槽,解析成敌人表行下标,收进候选 `work[]`(权重 = `CREATEPROB`),
//   同时累加 `createenemynum += CREATEMAXNUM`。出场数 `entrymax = RAND(1, min(enemy_max_num,
//   createenemynum))` —— ★ `CREATEMAXNUM` 的**第一个消费点**(M.5 文末 ① 预告的)。
//   ⚠️ 无候选(全部 `ENEMY_ID` 解析失败或槽全空)⇒ 在 `RAND` **之前**返回空 ⇒ 不消耗 rng(源码 :1399)。
//
// ── 第四段:逐只抽 + 同族上限门 + 大怪布阵(`:1402-1465`)────────────────────────
//   循环 `i < entrymax`(★ 兜底 `loopcounter < 100`,源码 :1403:同族门 / 大怪换位失败会
//   `continue` 而不推进 `i` ⇒ 无兜底会死循环)。每轮:
//     · 权重抽签(同 `pickEnemyGroup`:`found-1` 兜底 + `wr!=0`,等价/冗余见 DR-DT13 ④);
//     · **同族上限门**(`:1426`):已放入 `cnt` 只该行 ≥ `CREATEMAXNUM × samecount` ⇒ 跳过
//       (`samecount` = 候选里该行出现几次;`cnt` = 索引表里已有几只该行);
//     · **大怪布阵**(`:1430-1464`,查模板 `E_T_SIZE`;★ 模板查不到 ⇒ 整只不放,`i` 不推进):
//         - `bigcnt >= 5` ⇒ `entrymax--` 并跳过(★ **减少总出场数**,前 5 位已满大怪);
//         - 要放到第 6 位起(`i > 4`)⇒ 去前 5 位找第一只 NORMAL,把大怪换到它的位置、
//           NORMAL 顶到 `i`(★ 保证大怪永远站前排);前 5 位无 NORMAL 可换 ⇒ 跳过;
//         - `i <= 4` ⇒ 直接放到 `i`。
//   ⚠️★ **大怪布阵是真实主路径**(模板表 BIG 占 50.6%,见 `EnemyTemplate::size`)——
//      不是 M.6 那种"实测一次不触发"的防御分支,用例必须逐条覆盖上面每个分支。
//
// ⚠️★ **仍不做**(留给后续批次,逐条记明):
//   · NPC 事件改组 / `ENEMY_RandomEnemyArray` —— 见本组卷首那两条(未移植依赖);
//   · 「要不要遇敌」**整个不在这条链上**:遇敌率骰子在 `char/char_walk.c`(分母
//     `rand()%(120*getEnemyAction())`,消费 `EncountArea::prob_min/max`),★ `06` §F 实测
//     `EN`(遇敌)**不是网络入口**(无 `EN_RECV` 分支)⇒ 玩家无法主动请求遇敌,由服务端在
//     移动时判定 ⇒ 那一批的前置是移动系统 + tick 的 `kCharLoop`(现仍阶段 2 占位);
//   · **接进 tick / `spawnEnemyToField` 批量入场**:本函数是纯函数(产出行下标),不碰池与战场
//     —— 同 M.6 的 `pickEnemyGroup`,靠用例断言,不接 `kNpcSpawn`(那需要移动系统先落地)。
std::vector<std::int32_t> rollEnemyList(const EnemyGroup &group,
                                        const std::vector<EnemyEncounter> &encounters,
                                        const std::vector<EnemyTemplate> &templates,
                                        std::int32_t enemy_max_num, SA::Rules::Random &rng);

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
