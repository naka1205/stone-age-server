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

} // namespace SA::World

#endif // __SA_WorldApi_H__
