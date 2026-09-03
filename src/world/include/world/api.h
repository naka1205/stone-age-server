// src/world/include/world/api.h —— L2/L1 世界循环的**唯一**对外面
//
// ── 阶段 1.5 的切面(00 §9.0.4)──────────────────────────────
//   ✅ 要:最小 tick(01 §3.1 的 1/2/4/8 四步)· 一场战斗的生命周期
//   ⬜ 不要:NPC 生成 · 移动 · 视野 · 角色循环
//
// ⚠️★ 明确认下的三条边界,免得"跑通了"被读成"做完了":
//   ① **不落盘、无 Redis、单实例**(1.5 不要 storage / lock);
//   ② **不验证 00 §3.1 的服务边界** —— 1.5 是单模块,那留到阶段 3;
//   ③ **不做 L2 领域模型** —— 世界里没有"角色",只有战斗里的 rules::Combatant。
//      ⇒ 因此本批次**不下发 BattleSnapshot**:那需要把 rules::Combatant 映射成
//        domain::CombatantState,而那是 1.2 L2 实体族的活。
//        1.4 demo 的验收口径是**事件流端到端一致**(客户端 01 §12.1),
//        BattleEvents 就是它要的东西。

#ifndef SA_WORLD_API_H
#define SA_WORLD_API_H

#include <cstdint>
#include <memory>
#include <vector>

#include "net/api.h"
#include "platform/api.h"
#include "rules/battle.h"
#include "rules/combatant.h"
#include "rules/config.h"
#include "rules/random.h"

namespace sa::world {

using BattleId = std::uint64_t;

// tick 的阶段。★ 顺序**照抄** 01 §3.1,连未实现的四步也占位 ——
//   原版 mainloop() 的顺序是"整个服务端行为的骨架"(01 §2),
//   骨架的形状现在就要对,否则将来补 NPC 生成时会补在错的位置上。
enum class TickPhase : std::uint8_t {
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
struct BattleStats {
  std::uint32_t turns_resolved = 0;
  std::uint32_t events_emitted = 0;
  bool truncated_once = false;   // ResolveTurn 曾返回 false(见 battle.h)
  bool finished = false;
};

class World final : public sa::net::ITransportEvents,
                    public sa::net::ISessionHost {
 public:
  World(const sa::platform::ServerConfig& config,
        sa::platform::IClock& clock,
        sa::platform::Logger& logger,
        sa::platform::RandomSource& random,
        sa::net::ITransport& transport);
  ~World() override;

  World(const World&) = delete;
  World& operator=(const World&) = delete;

  // 推进一个 tick。⚠️ 01 §2:主线程绝不允许阻塞 ⇒ 本函数不等待任何 I/O。
  void Tick();

  // 开一场战斗。★ 种子由 platform::RandomSource 派发**并落日志** ——
  //   01 §10「战斗事件流 + 注入式随机源 = 可回放」,而可回放的前提是种子留得下来。
  BattleId StartBattle(const sa::rules::BattleField& field);

  // 把一条会话接进某场战斗的某个槽。1.5 没有选角,槽位由调用方指定。
  bool JoinBattle(BattleId battle, sa::net::SessionId session,
                  std::uint8_t slot);

  // ⚠️ 这两个不能写成内联 —— 状态在 pimpl 的 Impl 里,头文件看不见它。
  void RequestShutdown() noexcept;
  bool stopped() const noexcept;

  // ── ITransportEvents ──
  void OnConnected(sa::net::ConnectionId id) override;
  void OnBytes(sa::net::ConnectionId id, const std::uint8_t* data,
               std::size_t n) override;
  void OnDisconnected(sa::net::ConnectionId id) override;

  // ── ISessionHost ──
  void OnSessionReady(sa::net::SessionId id) override;
  void OnBattleCommand(sa::net::SessionId id,
                       const sa::domain::BattleCommand& cmd) override;
  void OnSessionClosed(sa::net::SessionId id) override;

  // ── 观察面(测试与运维)──
  std::uint64_t ticks() const noexcept;
  std::size_t session_count() const noexcept;
  const BattleStats* stats(BattleId id) const;
  sa::net::SessionState session_state(sa::net::SessionId id) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sa::world

#endif  // SA_WORLD_API_H
