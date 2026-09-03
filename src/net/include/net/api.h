// src/net/include/net/api.h —— L1 网络与会话的**唯一**对外面
//
// ★★ 与 platform 同理:模块只暴露这一个头(00 §3.1)。
//
// ── 阶段 1.5 的切面(00 §9.0.4)──────────────────────────────
//   ✅ 要:ITransport · ★ 长度前缀成帧 · IDL 编解码接入 · 握手会话
//   ⬜ 不要:WsTransport(D4 冻着)· 限流 · 重连窗口
//
// ⚠️★ 本批次**没有** TcpTransport —— 这是有意切分,不是漏了。
//    01 §12 定的网络栈是 **asio + C++20 协程**,那是一笔独立的引入
//    (第三方依赖 + 三平台 CI + 协程调度)。而本模块真正难写对、
//    也真正值得先被用例钉死的是**成帧与会话状态机**,它们**不需要 socket**
//    —— 与 §9.0.5 那条纠正同源:D2 的第一次实证也不需要网络,
//      当初却被排在了最后。⇒ 先把能脱离 socket 验的部分验完。
//    ⇒ LoopbackTransport 是本批次的传输实现,它同时也是 01 §5.1
//      列的 InProcTransport 的雏形(单容器形态下的模块间传输)。

#ifndef SA_NET_API_H
#define SA_NET_API_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "domain/battle_events.sa.h"
#include "transport/envelope.sa.h"
#include "transport/handshake.sa.h"
#include "ids.h"

namespace sa::net {

// ── 帧层:[u32 length][payload] ────────────────────────────────
//
// 02 §1.2:长度前缀。理由不是"约定俗成",而是 **TCP 是字节流、
//   WebSocket 保留消息边界,长度前缀同时满足两者** ⇒ D4 解冻换 WsTransport 不动上层。
// ⚠️ 旧实现按 '\n' 切行(GetOneLine),与"负载里可能有换行"直接冲突,靠转义硬撑。
//    **新实现不按分隔符切帧。**

// 单帧上限。★ 这个数字是**熔断**,不是容量规划:
//   15 §4.3 实测原版每连接固定 324–452 KB 双缓冲,是 sizeof(Char)(12.3 KB)的
//   26–37 倍,且是每连接成本主项。01 §5.3 裁定「按需增长 + 上限熔断」。
//   ⇒ 这里是那个上限。最大的已知消息是 BattleEvents(7 KB 量级),留了 8 倍余量。
inline constexpr std::uint32_t kMaxFrameBytes = 64u * 1024u;

// 长度前缀本身的宽度。
inline constexpr std::size_t kFrameHeaderBytes = 4;

enum class FrameStatus : std::uint8_t {
  kOk = 0,        // 取到一条完整帧
  kNeedMore = 1,  // 还没收够,继续等
  kTooLarge = 2,  // ★ 声明长度超过 kMaxFrameBytes ⇒ 连接必须关闭
  kEmpty = 3,     // 声明长度为 0 ⇒ 协议违规(信封头本身就不止 0 字节)
};

// 增量成帧器。喂字节进去,取完整帧出来。
//
// ⚠️★ kTooLarge / kEmpty 一旦出现就是**不可恢复**的:字节流已经无法再对齐,
//    调用方必须关闭连接,不能"跳过这一帧继续读"。
//    ⇒ 因此这两个状态是粘性的,Next() 会一直返回它。
class FrameReader {
 public:
  FrameReader() = default;

  // 收到的原始字节。返回 false 表示累积缓冲超过了单帧上限 + 余量,
  // 说明对端在灌垃圾 ⇒ 关闭连接。
  bool Push(const std::uint8_t* data, std::size_t n);

  // 取下一条完整帧。kOk 时 *payload / *len 指向内部缓冲,
  // **在下一次 Push() 或 Pop() 之前有效**。
  FrameStatus Next(const std::uint8_t** payload, std::uint32_t* len);

  // 丢弃刚由 Next() 返回的那一帧。★ 与 Next() 分开是为了让调用方
  //   可以零拷贝地处理帧内容,处理完再推进。
  void Pop();

  bool failed() const noexcept { return failed_; }
  std::size_t buffered() const noexcept { return buf_.size() - read_; }

 private:
  std::vector<std::uint8_t> buf_;
  std::size_t read_ = 0;        // 已消费的前缀长度
  std::uint32_t pending_ = 0;   // 刚由 Next() 交出的帧长(含头)
  bool failed_ = false;
};

// 把一段负载写成一帧,追加到 out。负载超限返回 false(**不截断**)。
bool WriteFrame(const std::uint8_t* payload, std::uint32_t len,
                std::vector<std::uint8_t>& out);

// ── 信封层:EnvelopeHeader { msg_id, corr_id } + body ──────────
//
// 02 §2.1:body 不作为字段出现 —— 它是「紧跟在信封头之后、由 msg_id 决定类型
//   的字节」,长度由帧层给出。把 body 建模成 bytes 会引入无上限变长字段。

struct EnvelopeView {
  std::uint32_t msg_id = 0;
  std::uint64_t corr_id = 0;
  const std::uint8_t* body = nullptr;
  std::uint32_t body_len = 0;
};

// 从一条完整帧里剥出信封。格式不对返回 false ⇒ 整条消息作废,不存在部分成功。
bool DecodeEnvelope(const std::uint8_t* frame, std::uint32_t len,
                    EnvelopeView& out);

// 把一条 IDL 消息编成「帧 + 信封 + body」并追加到 out。
//
// ★ 模板而不是虚接口:msg_id 由 sa::idl::msg_id_of<M>() **编译期**取,
//   调用点写不出"消息类型与编号对不上"这种错。
//
// ★★ 就地编码进 out 的尾部,**不用栈缓冲**:单帧上限是 64 KB,
//    在栈上摆一个那么大的数组、每发一条消息摆一次,是一条自找的爆栈路径。
//    ⇒ 先把 out 撑到最坏情况,编完再缩回实际长度。out 通常是每连接复用的
//      出站缓冲 ⇒ 容量只涨一次,之后零分配(15 §9.1 的取向)。
//
// ⚠️ encode 用**非限定调用**:生成物的 encode 分别在 sa::domain 与
//    sa::transport 两个命名空间里,靠 ADL 各自找到自己那个。
//    写成限定调用就要为两组各写一份重载,那正是"同一语义两份实现"。
template <typename M>
bool EncodeFramed(std::uint64_t corr_id, const M& msg,
                  std::vector<std::uint8_t>& out) {
  const std::size_t start = out.size();
  out.resize(start + kFrameHeaderBytes + kMaxFrameBytes);

  sa::idl::Writer w(out.data() + start + kFrameHeaderBytes, kMaxFrameBytes);

  sa::transport::EnvelopeHeader head;
  head.msg_id = sa::idl::msg_id_of<M>();
  head.corr_id = corr_id;
  encode(w, head);
  encode(w, msg);

  if (!w.ok()) {
    out.resize(start);
    return false;
  }

  const std::uint32_t payload_len = static_cast<std::uint32_t>(w.size());
  out.resize(start + kFrameHeaderBytes + payload_len);
  // 长度前缀:小端,与 IDL 运行时的整数序一致。
  out[start + 0] = static_cast<std::uint8_t>(payload_len & 0xFFu);
  out[start + 1] = static_cast<std::uint8_t>((payload_len >> 8) & 0xFFu);
  out[start + 2] = static_cast<std::uint8_t>((payload_len >> 16) & 0xFFu);
  out[start + 3] = static_cast<std::uint8_t>((payload_len >> 24) & 0xFFu);
  return true;
}

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
