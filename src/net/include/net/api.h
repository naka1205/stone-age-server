// src/net/include/net/api.h —— L1 网络与会话的**唯一**对外面
//
// ★★ 与 platform 同理:模块只暴露这一个头(00 §3.1)。
//
// ── 阶段 1.5 的切面(00 §9.0.4)──────────────────────────────
//   ✅ 要:ITransport · ★ 长度前缀成帧 · IDL 编解码接入 · 握手会话
//   ⬜ 不要:WsTransport(D4 冻着)· 限流 · 重连窗口
//
// ⚠️★ ~~本批次**没有** TcpTransport —— 这是有意切分,不是漏了。~~
//    ⇒ ✅ **2026-09-04 补齐,1.5 收尾**。原文与它的理由保留在下面,因为
//      **那个切分本身是对的**,值得留作先例:
//    ~~01 §12 定的网络栈是 **asio + C++20 协程**,那是一笔独立的引入
//    (第三方依赖 + 三平台 CI + 协程调度)。~~而本模块真正难写对、
//    也真正值得先被用例钉死的是**成帧与会话状态机**,它们**不需要 socket**
//    —— 与 §9.0.5 那条纠正同源:D2 的第一次实证也不需要网络,
//      当初却被排在了最后。⇒ 先把能脱离 socket 验的部分验完。
//    ★ 事后看这个切分买到了什么:TcpTransport 落地时**上层一行没动** ——
//      成帧、会话、编解码接入已经在 Loopback 上被 18 条用例钉死,
//      新代码里出的错只可能是 socket 那一层的,归因面小一个数量级。
//    ⚠️ 选型最终**没走 asio**(用户 2026-09-04 裁定),理由见下方 TcpTransport。
//    ⇒ LoopbackTransport 保留:它同时是 01 §5.1 列的 InProcTransport 的雏形
//      (单容器形态下的模块间传输),也是全部非 socket 用例的载体。

#ifndef SA_NET_API_H
#define SA_NET_API_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "domain/battle_events.sa.h"
#include "transport/envelope.sa.h"
#include "transport/handshake.sa.h"
#include "ids.h"
#include "wire/framing.h"

namespace sa::net {

// ── 帧层与信封层:★★ 已于 2026-09-06 移出本模块(DR-TS9 乙案)──────────
//
// 它们现在住在 **`shared/wire/`**,与服务端和客户端**共编同一份**:
//     FrameReader · WriteFrame · FrameStatus · EnvelopeView · DecodeEnvelope ·
//     EncodeFramed · kMaxFrameBytes · kFrameHeaderBytes
//
// ★ 裁定的理由(`11` §1.6):成帧与信封是**双端语义完全相同**的东西,
//   而客户端 1.4 也要它们。三案里选了「双端共享一份」,与 D2 · DR-TS3 ·
//   黄金用例集「复用不复制」· DR-BT5 反对双份实现同一条:
//   **凡双端同一语义的只留一份,让漂移在编译期不可能发生。**
//
// ⚠️ 本模块**保留 `sa::net::` 下的类型别名**(见下),不要求调用方改写 ——
//    `world` / `tests` 的 `sa::net::FrameReader` 一行不用动。
//    ★ 这不是偷懒:重构的验收凭据正是「18 条既有用例断言一个不改、全部仍绿」,
//      若同时改调用方,就分不清红是搬错了还是改错了。
//
// ★ 留在本模块的是**宿主侧**:传输(socket / Loopback)与会话状态机。
//   ⇒ `shared/wire` 处理**字节与结构**,`src/net` 处理**连接**。
//     这条切分正是前者能双端共享的原因。

using sa::wire::kMaxFrameBytes;
using sa::wire::kFrameHeaderBytes;
using sa::wire::FrameStatus;
using sa::wire::FrameReader;
using sa::wire::WriteFrame;
using sa::wire::EnvelopeView;
using sa::wire::DecodeEnvelope;
using sa::wire::EncodeFramed;

// ── 传输层 ────────────────────────────────────────────────────
//
// 01 §5.1:ITransport ├─ TcpTransport ├─ InProcTransport └─ WsTransport(D4)
// ⚠️ 本批次只有 Loopback;TcpTransport 见本文件卷首的切分说明。

using ConnectionId = std::uint64_t;

class ITransportEvents {
 public:
  virtual ~ITransportEvents() = default;
  virtual void OnConnected(ConnectionId id) = 0;
  // ⚠️ 给的是**原始字节**,不保证帧对齐 —— 成帧是上层的事(FrameReader)。
  //    这正是 TCP 与 WS 的差别被吸收掉的地方。
  virtual void OnBytes(ConnectionId id, const std::uint8_t* data,
                       std::size_t n) = 0;
  virtual void OnDisconnected(ConnectionId id) = 0;

 protected:
  ITransportEvents() = default;
  ITransportEvents(const ITransportEvents&) = default;
  ITransportEvents& operator=(const ITransportEvents&) = default;
};

class ITransport {
 public:
  virtual ~ITransport() = default;
  virtual void SetEvents(ITransportEvents* events) = 0;
  virtual bool Send(ConnectionId id, const std::uint8_t* data,
                    std::size_t n) = 0;
  virtual void Close(ConnectionId id) = 0;
  // 由主线程在 tick 第 2 步调用(01 §3.1「网络入站」)。
  // ⚠️ 01 §2:主线程绝不允许阻塞 ⇒ 这个方法**不得**等待 I/O。
  virtual void Poll() = 0;

 protected:
  ITransport() = default;
  ITransport(const ITransport&) = default;
  ITransport& operator=(const ITransport&) = default;
};

// 进程内传输。测试用,同时是 01 §5.1 里 InProcTransport 的雏形。
class LoopbackTransport final : public ITransport {
 public:
  void SetEvents(ITransportEvents* events) override { events_ = events; }
  bool Send(ConnectionId id, const std::uint8_t* data,
            std::size_t n) override;
  void Close(ConnectionId id) override;
  void Poll() override;

  // ── 测试侧驱动 ──
  ConnectionId Connect();                       // 建立一条连接
  void Deliver(ConnectionId id, const std::uint8_t* data, std::size_t n);
  // 服务端经 Send() 发出的字节,按连接累积。
  const std::vector<std::uint8_t>& sent(ConnectionId id) const;
  void ClearSent(ConnectionId id);
  bool closed(ConnectionId id) const;

 private:
  struct Conn {
    ConnectionId id = 0;
    std::vector<std::uint8_t> outbound;
    std::vector<std::uint8_t> inbound;
    bool closed = false;
  };
  Conn* Get(ConnectionId id);
  const Conn* Get(ConnectionId id) const;

  ITransportEvents* events_ = nullptr;
  std::vector<Conn> conns_;
  ConnectionId next_id_ = 1;
};

// ── TCP 传输(2026-09-04,1.5 收尾项)─────────────────────────────
//
// ★★ 选型与 01 §12 那张技术栈表的字面偏离,**由用户于 2026-09-04 裁定**:
//    §12 写的是「网络 = asio + C++20 协程」,本实现走**原生 socket + poll(2)**。
//    三条理由,按分量排:
//    ① ★ `ITransport::Poll()` 的契约(主线程 tick 第 2 步调用、不得阻塞)
//       **本来就是 reactor**。asio 协程的价值在这个接口下发挥不出来 ——
//       要么改接口让 io_context 独占一个线程(那推翻的是 01 §2 的线程模型),
//       要么退化成 `io_context.poll()`,那只是把下面这段 poll 包了一层。
//    ② 它会是服务端 `src/` 的**第一个运行时第三方依赖**(doctest 只进 tests/)
//       ⇒ FetchContent + 三平台 CI 是一笔独立的引入成本。
//    ③ ★ 与 `platform/api.h` §日志 那处偏离**同一条先例**:接口先立死、库延后引,
//       因为 `ITransport` 已经把替换成本压到局部 —— 换 asio 不动上层一行。
//    ⚠️ 认下的代价:poll(2) 是 O(连接数)。⇒ 上规模时要换 epoll/kqueue/IOCP,
//       **但那不是本批次的事**,且 §5.1 批的是原版「每连接一次 select」
//       (1000 连接 = 1000 次系统调用),poll 一次调用传整个数组已经不同量级。
//
// ⚠️★ **本类刻意不出现任何平台类型**(`SOCKET` / `fd` / `WSAPOLLFD` 一个都没有):
//    `net/api.h` 是 PUBLIC 头,`world` 与 tests 都吃它。一旦这里 include
//    `<winsock2.h>`,`windows.h` 那套宏(`min`/`max`/`ERROR`/`near`)就顺着
//    传染给每一个引用者 —— ★ 与客户端 00 §9.0.11 ② 那条依赖方向教训同族:
//    **谁依赖谁,要在头文件的形状上就不可能搞反**,不能靠"记得别 include"。
//    ⇒ 一切平台细节关在 tcp_transport.cpp 的 Impl 里。
//
// ── 本批次的切面 ──
//   ✅ 要:监听 · accept · 非阻塞收发 · ★ 出站背压(写不完要排队)· 上限熔断
//   ⬜ 不要:限流 · 重连窗口 · TLS · IPv6 · WsTransport(D4 冻着)

// 每连接出站队列上限。★ 它是**熔断**,与 kMaxFrameBytes 同一条理由(01 §5.3
//   「按需增长 + 上限熔断」):对端连上却不读,出站队列就会无限涨 ——
//   那是一条不需要任何攻击技巧的内存耗尽路径。⇒ 超限即断连,不是等待。
inline constexpr std::size_t kMaxOutboundBytes = 4u * 1024u * 1024u;

class TcpTransport final : public ITransport {
 public:
  TcpTransport();
  ~TcpTransport() override;
  TcpTransport(const TcpTransport&) = delete;
  TcpTransport& operator=(const TcpTransport&) = delete;

  // 绑定并开始监听。⚠️ 失败返回 false —— 调用方**必须拒绝启动**
  //   (01 §11.1「任一步失败即拒绝启动」),原因见 last_error()。
  // ★ port = 0 表示由系统分配,之后用 listen_port() 取回实际端口。
  //   这不是测试专用后门:它是让用例能在 CI 上并行跑而不撞端口的唯一干净办法。
  bool Listen(const char* bind_addr, std::uint16_t port);

  // 实际监听的端口。未监听时为 0。
  std::uint16_t listen_port() const noexcept;

  // 最近一次失败的原因。★ net **不链 sa_platform**(见 CMakeLists 里那条注释)
  //   ⇒ 本模块不打日志,把错误交给宿主去打成 kConnectionClosed 之类的结构化事件。
  const char* last_error() const noexcept;

  // 停止监听并关闭全部连接。⚠️ 会为每条连接回调 OnDisconnected。
  void Stop();

  // ── ITransport ──
  void SetEvents(ITransportEvents* events) override;
  bool Send(ConnectionId id, const std::uint8_t* data, std::size_t n) override;
  void Close(ConnectionId id) override;
  // ⚠️ 非阻塞:poll 超时为 0。01 §2「主线程绝不允许阻塞」。
  void Poll() override;

  // ── 观察面(测试与运维)──
  std::size_t connection_count() const noexcept;
  // 尚未写出去的字节数 —— 背压是否真的发生过,只有这个数说得出来。
  std::size_t pending_outbound(ConnectionId id) const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ── 会话(01 §5.2)──────────────────────────────────────────────
//
// 原版 LoginType 是**连接级**状态机,把三种生命周期压在一起,
// 导致掉线路径与停服路径各自长出一串状态。新实现按 01 §5.2 拆开:
//
//   Connection(传输级)      : 建立 → 握手 → 活跃 → 关闭
//   Session(账号级)         : 匿名 → 认证中 → 已认证 → 选角中 → 在线 → 登出中
//   CharacterInstance(世界级): 载入中 → 在世 → 保存中 → 卸载
//
// ⚠️★ 本批次只实现 Session 这一层,且**只到"已认证"为止**。原因是一处
//    实现时才撞到的缺口,记在这里而不是绕过去:
//    **IDL 里根本没有"认证"这条消息。** 0x0001–0x0005 是
//    HandshakeRequest/Accepted/Rejected + Ping/Pong,没有 Login。
//    ⇒ 00 §9.0.4 写「握手 → 认证 → 进入战斗(走 0x0001–0x0005 已落地那段)」
//      时,默认了这段里有认证,而实际没有。
//    ⇒ 本批次的处置:握手通过即视为**匿名已认证**,选角与账号鉴权留到阶段 2
//      (它们要 storage,而 1.5 明确不要 storage)。战斗入场由 world 侧推,
//      不由客户端请求 —— 这对 1.4 demo 是足够的,它验的是**事件流端到端一致**。

enum class SessionState : std::uint8_t {
  kAnonymous = 0,      // 连上了,还没握手
  kAuthenticating = 1, // ⬜ 阶段 2:等账号校验回来
  kAuthenticated = 2,  // 握手通过
  kSelectingChar = 3,  // ⬜ 阶段 2
  kOnline = 4,         // 在世,可收发玩法消息
  kLoggingOut = 5,     // ⬜ 阶段 2
  kClosed = 6,
};

const char* SessionStateName(SessionState s) noexcept;

using SessionId = std::uint64_t;

// 会话把「该做什么」交给宿主。★ net **不认识** world ——
//   这正是 00 §3.1「各模块互相不可见,只暴露接口头,链接期换实现」的落点。
class ISessionHost {
 public:
  virtual ~ISessionHost() = default;

  // 握手通过。宿主可据此登记会话。
  virtual void OnSessionReady(SessionId id) = 0;
  // 客户端上行的战斗指令(0x0210)。
  virtual void OnBattleCommand(SessionId id,
                               const sa::domain::BattleCommand& cmd) = 0;
  virtual void OnSessionClosed(SessionId id) = 0;

 protected:
  ISessionHost() = default;
  ISessionHost(const ISessionHost&) = default;
  ISessionHost& operator=(const ISessionHost&) = default;
};

// 单条会话。⚠️ 不持有 socket —— 出站字节交给调用方发。
class Session {
 public:
  Session(SessionId id, std::uint32_t protocol_version,
          std::uint32_t heartbeat_interval_ms, ISessionHost* host) noexcept;

  SessionId id() const noexcept { return id_; }
  SessionState state() const noexcept { return state_; }
  bool closed() const noexcept { return state_ == SessionState::kClosed; }

  // 处理一条已成帧的消息。
  //
  // 返回 false ⇒ **必须关闭连接**。02 §5.5 的取向:回执侧的强校验必须保留,
  //   校验不过就是协议违规,不是"忽略这一条继续"。
  // 出站字节追加进 out(已成帧,可直接交给 ITransport::Send)。
  bool HandleFrame(const std::uint8_t* frame, std::uint32_t len,
                   std::vector<std::uint8_t>& out);

  // 供 world 侧下推消息(战斗快照 / 事件流)。
  template <typename M>
  bool Push(const M& msg, std::vector<std::uint8_t>& out) const {
    // Notify 的 corr_id 为 0(02 §2.1)。
    return EncodeFramed(0, msg, out);
  }

  void MarkOnline() noexcept;
  void Close() noexcept;

  // ── 供测试与运维观察 ──
  std::uint64_t frames_handled() const noexcept { return frames_handled_; }
  std::uint32_t last_reject_msg_id() const noexcept { return last_reject_msg_id_; }

 private:
  bool HandleHandshake(const EnvelopeView& env, std::vector<std::uint8_t>& out);
  bool HandlePing(const EnvelopeView& env, std::vector<std::uint8_t>& out);
  bool HandleBattleCommand(const EnvelopeView& env);

  SessionId id_;
  std::uint32_t protocol_version_;
  std::uint32_t heartbeat_interval_ms_;
  ISessionHost* host_;
  SessionState state_ = SessionState::kAnonymous;
  std::uint64_t frames_handled_ = 0;
  std::uint32_t last_reject_msg_id_ = 0;
};

}  // namespace sa::net

#endif  // SA_NET_API_H
