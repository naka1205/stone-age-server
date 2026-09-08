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
//      `array`  → **遇敌表** `enemy.txt`      —— `ENEMY_LV_MIN/MAX` · `ENEMY_PETFLG` ·
//                                                掉落 · 战术 · 经验 · `ENEMY_ID`
//      `tarray` → **模板表** `enemybase1.txt` —— `E_T_*` 全部列
//   ⇒ 本结构里 `capturable` 是**唯一**来自遇敌表的一列(源码 :1165 读
//     `*(p + ENEMY_PETFLG)`),其余都来自模板表。★ 同一只怪在不同遇敌配置下
//     可捕 / 不可捕,而捕获难度跟着模板走 —— 合并两张表会把这个区分抹掉。
//
// ⚠️ 遇敌表本身**未移植**(等级区间 / 掉落 / 战术 / 经验都在它上面)
//    ⇒ 本批 `level` 是入参、`capturable` 是入参,不在生成函数里摇 / 查。
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
	SA::Model::NameStr name{};

	// ★ **来自遇敌表**(`ENEMY_PETFLG` → `CHAR_WORK_PETFLG`,源码 :1165),见卷首。
	bool capturable = false;
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
	//     刷怪逻辑将来只需决定"何时、在哪、用哪个模板、什么等级",生成与入场就是这里。
	//
	// 三道门,失败即**世界一个字节都没动**(顺序 = 预留→提交,同 `createPetFromCombatant`):
	//   ① 战斗不存在 / 槽号越界;
	//   ② 敌人池满(`EntityPool::allocate` 返 kNullHandle ⇒ 落 `kEntityPoolExhausted`);
	//   ③ `enterEnemyToField` 的两道门(槽必须在玩家段 / 槽未被占)——
	//      ★ 这一步失败会把刚 allocate 的实体**释放掉**再返回,不留孤儿。
	//
	// ⚠️ 随机源取的是**该场战斗的 rng**(`b.rng`),不是 `Platform::RandomSource` ——
	//    可回放的凭据是"战斗种子 + 事件流"(`01` §10),敌人四维是那场战斗状态的一部分。
	//    ★ 后果:同一颗战斗种子下,先摇的 14 个数会被敌人生成用掉 ⇒ **入场顺序影响
	//      后续所有取值**。这不是缺陷(原版同理:刷怪也在同一个全局 rng 上),
	//      但它意味着"改变刷怪时机"会改变回放 ⇒ 回放必须连刷怪调用序一起重现。
	bool spawnEnemyToField(BattleId battle, std::uint8_t slot,
	                       const EnemyTemplate &tmpl, std::int32_t level);

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

// 据模板 + 等级生成一只敌人 —— 1:1 移植 `ENEMY_createEnemy`(`char/enemy.c:994-1180`)
// 里**本批做得到**的那一段。建 / 不建逐条见 `shared/model/Enemy.h` 文末。
//
// ★ 纯函数(不碰池、不碰战场)⇒ 可被单元测直接喂模板验证,同 `enterPetToField` 的取向。
//
// ⚠️★ **`level` 是入参,不在函数里摇**:源码 :1030-1035 是
//    `baselevel > 0 ? baselevel : RAND(ENEMY_LV_MIN, ENEMY_LV_MAX)`,而那次摇号读的是
//    **遇敌表**(未移植)⇒ 摇号属遇敌逻辑,不进生成函数(同 `rollSpawnStats` 同处的裁定)。
//
// ★ rng 消耗 = `rollSpawnStats` 的 **14 次**,一次不多:本函数自己不摇任何数。
//   ⚠️ 原版在此之后还有 `ENEMY_RandomChange`(会摇)与掉落 / 武器(会摇)——
//     均未移植 ⇒ 同种子下我们的序列与原版不同,而原版不可运行(P1)、无可比对序列。
//
// ⚠️★ **满血入场是推导的产物,不是模板列**:源码 :1153 先 `CHAR_complianceParameter`,
//    :1159 再 `CHAR_HP = CHAR_getWorkInt(WORKMAXHP)` ⇒ 本函数用
//    `deriveBaseStats(四维).max_hp` 填 `hp`,而**不存 max_hp**(不造第二真源)。
SA::Model::Enemy spawnEnemy(const EnemyTemplate &tmpl, std::int32_t level,
                            SA::Rules::Random &rng,
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
