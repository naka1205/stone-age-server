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

#include "model/Pet.h"
#include "net/Api.h"
#include "platform/Api.h"
#include "rules/Battle.h"
#include "rules/Combatant.h"
#include "rules/Config.h"
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
// ⚠️★ 战斗三围 `attack`/`defense`/`quick`/`max_hp` **留 0** —— `Pet` 只有原始四维,
//    推导三围的 `complianceParameter` 未移植(06 域)。这是登记在案的残缺(同 M.1 捕获
//    宠物四维=0):宠物能进出场 / 被打 / 死亡回池,但暂无战力,等属性推导移植后回填。
bool enterPetToField(SA::Rules::BattleField &field, int owner_field_slot,
                     const SA::Model::Pet &pet);

// 把宠物从战场撤下(占位清空)。仿 `BATTLE_PetDefaultExit:1377`。
// ★ 只置 `occupied=false` —— 撤下不是战死,**不置 `dead`**(同逃跑成功 / 捕获离场:
//   记成阵亡会污染战果 / 经验结算,阶段 2)。owner_field_slot 越界或非玩家段则无操作。
void exitPetFromField(SA::Rules::BattleField &field, int owner_field_slot);

} // namespace SA::World

#endif // __SA_WorldApi_H__
