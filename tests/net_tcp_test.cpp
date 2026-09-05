// tests/net_tcp_test.cpp —— TcpTransport(1.5 收尾项,2026-09-04)
//
// ★★ 这些用例**必须真的开一个 socket 连进来** —— 这正是它们与
//    net_framing_test.cpp 的分工:那一批刻意不碰网络(成帧与会话状态机
//    不需要 socket),这一批只测那批测不到的东西。
//
// ⚠️★ 客户端一侧**故意用裸 socket 自己写**,不复用 TcpTransport ——
//    用被测代码去驱动被测代码是循环论证:若 Send 与 recv 犯了对称的错
//    (比如都把长度前缀写成大端),用例会一路绿到底。
//    ⇒ 下面那个 TestClient 是阻塞式的、蠢的、和被测实现无关的。
//
// ⚠️ 端口一律 **0 = 由系统分配**:CI 三平台并行跑,写死端口迟早撞车,
//    而撞车的表现是"偶发红",是最难归因的那种红(00 §10.4)。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "net/api.h"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

using namespace sa::net;

namespace {

constexpr std::uint32_t kVersion = 3;
constexpr std::uint32_t kHeartbeat = 30000;

#if defined(_WIN32)
using RawSocket = SOCKET;
constexpr RawSocket kBad = INVALID_SOCKET;
void RawClose(RawSocket s) { ::closesocket(s); }
#else
using RawSocket = int;
constexpr RawSocket kBad = -1;
void RawClose(RawSocket s) { ::close(s); }
#endif

// winsock 的显式初始化。★ doctest 没有全局 fixture,而每个用例都要 socket
//   ⇒ 做成计数守卫,构造即可用。POSIX 侧是空壳。
struct SocketGuard {
  SocketGuard() {
#if defined(_WIN32)
    WSADATA d;
    ::WSAStartup(MAKEWORD(2, 2), &d);
#endif
  }
  ~SocketGuard() {
#if defined(_WIN32)
    ::WSACleanup();
#endif
  }
};

// 阻塞式测试客户端。★ 带收超时 —— 没有超时的 recv 会把一次失败的用例
//   变成一次**挂死的 CI**,那比红更坏(要等 job 超时才有人发现)。
class TestClient {
 public:
  // rcvbuf_bytes > 0 ⇒ 连接**之前**显式设 SO_RCVBUF。
  //   ★ 显式设置会关掉该 socket 的接收缓冲自动调大(macOS autorcvbuf / Linux tcp_rmem),
  //     于是对端能往这条连接灌多少字节就有了一个**已知上界** —— 大负载用例靠它制造背压。
  //   ⚠️ 必须在 connect 之前:窗口缩放因子在 SYN 里协商,之后再改不影响对端看到的窗口。
  bool Connect(std::uint16_t port, int rcvbuf_bytes = 0) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ == kBad) return false;
    if (rcvbuf_bytes > 0) {
#if defined(_WIN32)
      ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&rcvbuf_bytes),
                   sizeof(rcvbuf_bytes));
#else
      ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf_bytes,
                   sizeof(rcvbuf_bytes));
#endif
    }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (::connect(fd_, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) {
      RawClose(fd_);
      fd_ = kBad;
      return false;
    }
#if defined(_WIN32)
    DWORD tv = 2000;  // 毫秒
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec = 2;
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    return true;
  }

  bool SendAll(const std::uint8_t* p, std::size_t n) {
    std::size_t sent = 0;
    while (sent < n) {
#if defined(_WIN32)
      const int r = ::send(fd_, reinterpret_cast<const char*>(p) + sent,
                           static_cast<int>(n - sent), 0);
#else
      const ssize_t r = ::send(fd_, p + sent, n - sent, 0);
#endif
      if (r <= 0) return false;
      sent += static_cast<std::size_t>(r);
    }
    return true;
  }
  bool SendAll(const std::vector<std::uint8_t>& v) {
    return SendAll(v.data(), v.size());
  }

  // 收够 want 个字节或超时。★ 返回实收长度,由调用方断言 —— 收不够时
  //   报"实收 N / 期望 M"比报一个 false 有用得多。
  std::size_t RecvExactly(std::size_t want, std::vector<std::uint8_t>& out) {
    out.clear();
    std::uint8_t buf[8192];
    while (out.size() < want) {
#if defined(_WIN32)
      const int n = ::recv(fd_, reinterpret_cast<char*>(buf),
                           static_cast<int>(sizeof(buf)), 0);
#else
      const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
#endif
      if (n <= 0) break;  // 超时 / 对端关闭
      out.insert(out.end(), buf, buf + n);
    }
    return out.size();
  }

  // 读到对端关闭为止。用于验证"优雅关闭:先把出站排空再关"。
  std::size_t RecvUntilClosed(std::vector<std::uint8_t>& out) {
    out.clear();
    std::uint8_t buf[8192];
    for (;;) {
#if defined(_WIN32)
      const int n = ::recv(fd_, reinterpret_cast<char*>(buf),
                           static_cast<int>(sizeof(buf)), 0);
#else
      const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
#endif
      if (n <= 0) break;
      out.insert(out.end(), buf, buf + n);
    }
    return out.size();
  }

  void Close() {
    if (fd_ != kBad) {
      RawClose(fd_);
      fd_ = kBad;
    }
  }
  ~TestClient() { Close(); }

 private:
  RawSocket fd_ = kBad;
};

// 记录传输层回调。★ 字节**存下来**,不是数个数 ——
//   "收到了 N 字节"与"收到的是那 N 个字节"是两件事。
class RecordingEvents final : public ITransportEvents {
 public:
  void OnConnected(ConnectionId id) override {
    connected.push_back(id);
    last = id;
  }
  void OnBytes(ConnectionId id, const std::uint8_t* data,
               std::size_t n) override {
    bytes.insert(bytes.end(), data, data + n);
    ++on_bytes_calls;
    last = id;
  }
  void OnDisconnected(ConnectionId id) override { disconnected.push_back(id); }

  std::vector<ConnectionId> connected;
  std::vector<ConnectionId> disconnected;
  std::vector<std::uint8_t> bytes;
  std::size_t on_bytes_calls = 0;
  ConnectionId last = 0;
};

// 反复 Poll 直到条件成立或超时。
//
// ⚠️★ 这是全文件唯一一处容易写出"偶发红"的地方。两条纪律:
//   ① **绝不** sleep 一个固定时长然后断言 —— 那在 CI 的负载下必然偶发失败;
//   ② 超时要足够长(2 秒)、每轮间隔要足够短(1ms):前者防偶发红,
//      后者保证用例本身跑得快(条件通常在头几轮就成立)。
template <typename Pred>
bool PumpUntil(TcpTransport& t, Pred pred, int timeout_ms = 2000) {
  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    t.Poll();
    if (pred()) return true;
    if (clock::now() >= deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

template <typename M>
std::vector<std::uint8_t> Framed(std::uint64_t corr_id, const M& msg) {
  std::vector<std::uint8_t> out;
  REQUIRE(EncodeFramed(corr_id, msg, out));
  return out;
}

std::vector<std::uint8_t> HandshakeFrame(std::uint32_t version) {
  sa::transport::HandshakeRequest req{};
  req.protocol_version = version;
  req.client_build.assign("tcp-test");
  return Framed(1, req);
}

}  // namespace

// ══ 监听 ═════════════════════════════════════════════════════════

TEST_CASE("端口 0 由系统分配,并能回读出来") {
  SocketGuard g;
  TcpTransport t;
  REQUIRE(t.Listen("127.0.0.1", 0));
  // ★ 回读不出端口,用例就没法连它 —— 这条断言同时是后面所有用例的前提。
  CHECK(t.listen_port() != 0);
  CHECK(t.connection_count() == 0u);
}

TEST_CASE("重复 Listen 是调用方的逻辑错,不是可恢复状态") {
  SocketGuard g;
  TcpTransport t;
  REQUIRE(t.Listen("127.0.0.1", 0));
  CHECK_FALSE(t.Listen("127.0.0.1", 0));
  CHECK(std::strlen(t.last_error()) > 0);
}

TEST_CASE("bind 地址非法 ⇒ 拒绝,且说得出原因") {
  SocketGuard g;
  TcpTransport t;
  CHECK_FALSE(t.Listen("不是一个地址", 0));
  // ★ 01 §11.1「任一步失败即拒绝启动」的前提是**失败说得出原因**,
  //   否则运维看到的只是一个静默退出。
  CHECK(std::strlen(t.last_error()) > 0);
  CHECK(t.listen_port() == 0u);
}

// ══ 连接生命周期 ═════════════════════════════════════════════════

TEST_CASE("接受连接 ⇒ OnConnected;对端关闭 ⇒ OnDisconnected") {
  SocketGuard g;
  TcpTransport t;
  RecordingEvents ev;
  t.SetEvents(&ev);
  REQUIRE(t.Listen("127.0.0.1", 0));

  {
    TestClient c;
    REQUIRE(c.Connect(t.listen_port()));
    REQUIRE(PumpUntil(t, [&] { return !ev.connected.empty(); }));
    CHECK(t.connection_count() == 1u);
    c.Close();
  }

  REQUIRE(PumpUntil(t, [&] { return !ev.disconnected.empty(); }));
  CHECK(ev.disconnected[0] == ev.connected[0]);
  // ★ 连接必须真的从表里摘掉 —— 只回调不摘除,就是一条慢速内存泄漏,
  //   而它在短测试里完全看不出来。
  CHECK(t.connection_count() == 0u);
}

TEST_CASE("多条连接各自独立编号") {
  SocketGuard g;
  TcpTransport t;
  RecordingEvents ev;
  t.SetEvents(&ev);
  REQUIRE(t.Listen("127.0.0.1", 0));

  TestClient a, b;
  REQUIRE(a.Connect(t.listen_port()));
  REQUIRE(b.Connect(t.listen_port()));
  REQUIRE(PumpUntil(t, [&] { return ev.connected.size() == 2; }));
  CHECK(ev.connected[0] != ev.connected[1]);
  CHECK(t.connection_count() == 2u);
}

// ══ 字节流 ═══════════════════════════════════════════════════════

TEST_CASE("上行字节原样交给上层") {
  SocketGuard g;
  TcpTransport t;
  RecordingEvents ev;
  t.SetEvents(&ev);
  REQUIRE(t.Listen("127.0.0.1", 0));

  TestClient c;
  REQUIRE(c.Connect(t.listen_port()));
  REQUIRE(PumpUntil(t, [&] { return !ev.connected.empty(); }));

  const std::vector<std::uint8_t> payload{0x00, 0x01, 0xFF, 0x7F, 0x80, 0x0A};
  REQUIRE(c.SendAll(payload));
  REQUIRE(PumpUntil(t, [&] { return ev.bytes.size() >= payload.size(); }));
  CHECK(ev.bytes == payload);
}

TEST_CASE("★ 分片到达:传输层不凑整帧,拼装是上层的事") {
  SocketGuard g;
  TcpTransport t;
  RecordingEvents ev;
  t.SetEvents(&ev);
  REQUIRE(t.Listen("127.0.0.1", 0));

  TestClient c;
  REQUIRE(c.Connect(t.listen_port()));
  REQUIRE(PumpUntil(t, [&] { return !ev.connected.empty(); }));

  const std::vector<std::uint8_t> frame = HandshakeFrame(kVersion);
  REQUIRE(frame.size() > 6);

  // 一帧掰成两半,中间隔开发 —— 真实 TCP 随时会这么干。
  REQUIRE(c.SendAll(frame.data(), 3));
  REQUIRE(PumpUntil(t, [&] { return ev.bytes.size() >= 3; }));
  // ★ 关键断言:此刻上层只拿到 3 个字节,传输层**没有**替它等齐一帧。
  CHECK(ev.bytes.size() == 3u);

  REQUIRE(c.SendAll(frame.data() + 3, frame.size() - 3));
  REQUIRE(PumpUntil(t, [&] { return ev.bytes.size() >= frame.size(); }));
  CHECK(ev.bytes == frame);
  // 分了两次到达 ⇒ OnBytes 至少被叫了两次(这正是 FrameReader 存在的理由)
  CHECK(ev.on_bytes_calls >= 2u);
}

TEST_CASE("下行:Send 的字节客户端收得到") {
  SocketGuard g;
  TcpTransport t;
  RecordingEvents ev;
  t.SetEvents(&ev);
  REQUIRE(t.Listen("127.0.0.1", 0));

  TestClient c;
  REQUIRE(c.Connect(t.listen_port()));
  REQUIRE(PumpUntil(t, [&] { return !ev.connected.empty(); }));

  const std::vector<std::uint8_t> msg{'h', 'e', 'l', 'l', 'o'};
  REQUIRE(t.Send(ev.connected[0], msg.data(), msg.size()));
  // ⚠️★ Send 只是排队 —— 真正写出去发生在 Poll()。这条纪律写在
  //    tcp_transport.cpp 的 Send 里(出站只有一个出口,保证顺序)。
  REQUIRE(PumpUntil(t, [&] {
    return t.pending_outbound(ev.connected[0]) == 0;
  }));

  std::vector<std::uint8_t> got;
  CHECK(c.RecvExactly(msg.size(), got) == msg.size());
  CHECK(got == msg);
}

TEST_CASE("★★ 大负载往返 —— 内核缓冲装不下时必须排队续写,不能截断") {
  SocketGuard g;
  TcpTransport t;
  RecordingEvents ev;
  t.SetEvents(&ev);
  REQUIRE(t.Listen("127.0.0.1", 0));

  TestClient c;
  // ★ 客户端接收缓冲显式压到 64 KB —— 见 TestClient::Connect 的说明。
  REQUIRE(c.Connect(t.listen_port(), 64 * 1024));
  REQUIRE(PumpUntil(t, [&] { return !ev.connected.empty(); }));
  const ConnectionId conn = ev.connected[0];

  // ⚠️★ **负载大小与"客户端何时开始读"这两件事都是用例的一部分**,不是摆设。
  //
  // ★ 本用例第一版写的是「1 MB + 客户端在另一线程立刻开始读」,反向验证
  //   (把 `out_sent += n` 改成 `out_sent = size()`,即"把 send 的返回值
  //   当成写完了")**没有报红** —— 因为对端一直在读,内核缓冲从不填满,
  //   第一次 send() 就把 1 MB 全写出去了 ⇒ **would-block 分支根本没跑到**,
  //   那条用例测的东西与它的标题无关。
  //   ⇒ 第二版:先灌 3 MB 且**客户端故意不读**,把内核收/发缓冲双双撑满。
  //
  // ⚠️★ 第三版(2026-09-05)—— 第二版在本机约 1/6 概率红:内核把 3 MB **全吃下了**。
  //   macOS / Linux 的收发缓冲都会自动调大(autorcvbuf / tcp_rmem),上限因系统与
  //   版本而异,3 MB 并不总在它之上;而「偶发红」正是 00 §10.4 最难归因的那种。
  //   ⇒ 两手一起,让「背压发生了」由用例**制造**出来,而不是赌内核的缓冲策略:
  //     ① 客户端显式设小 SO_RCVBUF(上面那行)⇒ 对端窗口有已知上界;
  //     ② 负载不再固定,按 256 KB 一块灌到「Poll 到写不动且 pending > 0」为止,
  //        上限 3.75 MB 仍刻意压在 kMaxOutboundBytes = 4 MB 之下,不碰熔断那条路径。
  constexpr std::size_t kChunk = 256u * 1024u;
  constexpr std::size_t kCap = 3840u * 1024u;
  static_assert(kCap < kMaxOutboundBytes, "本用例不该碰到熔断");
  std::vector<std::uint8_t> big;
  big.reserve(kCap);
  std::size_t backlog = 0;
  while (big.size() < kCap) {
    const std::size_t off = big.size();
    big.resize(off + kChunk);
    for (std::size_t i = off; i < big.size(); ++i) {
      big[i] = static_cast<std::uint8_t>((i * 31u + 7u) & 0xFFu);
    }
    REQUIRE(t.Send(conn, big.data() + off, kChunk));

    // 对端不读,反复 Poll 到写不动为止(连续若干轮 pending 不再下降)。
    std::size_t stable = 0;
    std::size_t last_pending = t.pending_outbound(conn);
    PumpUntil(
        t,
        [&] {
          const std::size_t now = t.pending_outbound(conn);
          stable = (now == last_pending) ? stable + 1 : 0;
          last_pending = now;
          return stable >= 50 || now == 0;
        },
        5000);
    backlog = t.pending_outbound(conn);
    if (backlog > 0) break;
  }

  // ★★ 本用例的核心断言:**背压确实发生了**。
  //   若灌到上限 pending 仍是 0,说明内核把全部字节都吃下了 ⇒ 后面那段
  //   "续写"逻辑本轮压根没被执行 ⇒ 用例又退化成一次内容比对。
  //   ⇒ 宁可让它红,也不要一条测不到目标的绿(00 §10.4)。
  INFO("灌入 ", big.size(), " 字节后 pending = ", backlog);
  CHECK(backlog > 0u);

  // 现在才放客户端去读 —— 服务端要靠后续 Poll 的 POLLOUT 续写。
  std::vector<std::uint8_t> got;
  std::size_t received = 0;
  std::thread reader([&] { received = c.RecvExactly(big.size(), got); });

  const bool drained = PumpUntil(
      t, [&] { return t.pending_outbound(conn) == 0; }, 10000);
  reader.join();

  CHECK(drained);
  REQUIRE(received == big.size());
  CHECK(got == big);
}

// ══ 关闭 ═════════════════════════════════════════════════════════

TEST_CASE("★ Close 是优雅的:排队中的出站先发完再断") {
  SocketGuard g;
  TcpTransport t;
  RecordingEvents ev;
  t.SetEvents(&ev);
  REQUIRE(t.Listen("127.0.0.1", 0));

  TestClient c;
  REQUIRE(c.Connect(t.listen_port()));
  REQUIRE(PumpUntil(t, [&] { return !ev.connected.empty(); }));

  // 这正是 session.cpp 的握手拒绝路径:先把拒绝理由发出去,再关连接。
  // ⚠️ 若 Close 立即断开,客户端看到的是一次**无理由的断连** ——
  //    而 02 §5.5 要求拒绝必须说得出原因。
  const std::vector<std::uint8_t> reason{'n', 'o', 'p', 'e'};
  REQUIRE(t.Send(ev.connected[0], reason.data(), reason.size()));
  t.Close(ev.connected[0]);

  REQUIRE(PumpUntil(t, [&] { return !ev.disconnected.empty(); }));

  std::vector<std::uint8_t> got;
  c.RecvUntilClosed(got);
  CHECK(got == reason);
  CHECK(t.connection_count() == 0u);
}

TEST_CASE("Stop 关闭全部连接并回调") {
  SocketGuard g;
  TcpTransport t;
  RecordingEvents ev;
  t.SetEvents(&ev);
  REQUIRE(t.Listen("127.0.0.1", 0));

  TestClient a, b;
  REQUIRE(a.Connect(t.listen_port()));
  REQUIRE(b.Connect(t.listen_port()));
  REQUIRE(PumpUntil(t, [&] { return ev.connected.size() == 2; }));

  t.Stop();
  CHECK(ev.disconnected.size() == 2u);
  CHECK(t.connection_count() == 0u);
  CHECK(t.listen_port() == 0u);
}

// ══ ★★ 端到端:传输层 + 成帧 + 会话状态机 ═════════════════════════
//
// 前面的用例都在验传输层自己。这一条验的是**接起来还对不对** ——
// 它是 1.4 双端 demo 那条链路上服务端一侧的全部,只差 world。

namespace {

// 把传输层收到的字节喂给 Session,并把 Session 的出站发回去。
// ★ 这就是 world 在做的事(world.cpp),这里用最小复刻验证接线本身。
class SessionBridge final : public ITransportEvents, public ISessionHost {
 public:
  SessionBridge(TcpTransport& t) : t_(t) {}

  void OnConnected(ConnectionId id) override {
    conn_ = id;
    session_ = std::make_unique<Session>(id, kVersion, kHeartbeat, this);
  }
  void OnBytes(ConnectionId, const std::uint8_t* data,
               std::size_t n) override {
    if (session_ == nullptr) return;
    reader_.Push(data, n);
    for (;;) {
      const std::uint8_t* p = nullptr;
      std::uint32_t len = 0;
      if (reader_.Next(&p, &len) != FrameStatus::kOk) break;
      std::vector<std::uint8_t> out;
      const bool ok = session_->HandleFrame(p, len, out);
      reader_.Pop();
      if (!out.empty()) t_.Send(conn_, out.data(), out.size());
      if (!ok) {
        t_.Close(conn_);  // ★ 协议违规 ⇒ 关连接(02 §5.5),但理由先发出去
        break;
      }
    }
  }
  void OnDisconnected(ConnectionId) override { session_.reset(); }

  void OnSessionReady(SessionId id) override { ready.push_back(id); }
  void OnBattleCommand(SessionId, const sa::domain::BattleCommand&) override {}
  void OnSessionClosed(SessionId) override {}

  ConnectionId conn() const { return conn_; }

  std::vector<SessionId> ready;

 private:
  TcpTransport& t_;
  FrameReader reader_;
  std::unique_ptr<Session> session_;
  ConnectionId conn_ = 0;
};

}  // namespace

TEST_CASE("★★ 端到端:真 socket 上握手成功并收到 HandshakeAccepted") {
  SocketGuard g;
  TcpTransport t;
  SessionBridge bridge(t);
  t.SetEvents(&bridge);
  REQUIRE(t.Listen("127.0.0.1", 0));

  TestClient c;
  REQUIRE(c.Connect(t.listen_port()));

  const std::vector<std::uint8_t> hs = HandshakeFrame(kVersion);
  REQUIRE(c.SendAll(hs));

  // ⚠️★ 等的条件是「回执**发出去了**」,不是「会话就绪了」。
  //    ★ 这条用例第一次跑就红在这里(2026-09-04):当时等的是 ready 非空,
  //      而那一刻回执只是**排进了出站队列** —— 随后主线程去 recv 阻塞,
  //      再没有人调 Poll(),队列永远写不出去 ⇒ 客户端读到 0 字节后超时。
  //    ⇒ 这正是 tcp_transport.cpp 里「出站只有一个出口」那条纪律的另一面:
  //      **Send 返回 true 不代表对端能看到**。测试自己先绊了一跤,记在这里。
  REQUIRE(PumpUntil(t, [&] {
    return !bridge.ready.empty() && t.pending_outbound(bridge.conn()) == 0;
  }));

  // 回执:4 字节长度前缀 + 负载
  std::vector<std::uint8_t> got;
  REQUIRE(c.RecvExactly(kFrameHeaderBytes, got) >= kFrameHeaderBytes);
  const std::uint32_t len =
      static_cast<std::uint32_t>(got[0]) |
      (static_cast<std::uint32_t>(got[1]) << 8) |
      (static_cast<std::uint32_t>(got[2]) << 16) |
      (static_cast<std::uint32_t>(got[3]) << 24);
  REQUIRE(len > 0);
  REQUIRE(len <= kMaxFrameBytes);

  std::vector<std::uint8_t> rest;
  // ★ 一次 recv 可能已经把整条都带回来了(小消息几乎总是如此)⇒ 先看手上的。
  std::vector<std::uint8_t> payload(got.begin() + kFrameHeaderBytes, got.end());
  if (payload.size() < len) {
    c.RecvExactly(len - payload.size(), rest);
    payload.insert(payload.end(), rest.begin(), rest.end());
  }
  REQUIRE(payload.size() >= len);

  EnvelopeView env{};
  REQUIRE(DecodeEnvelope(payload.data(), len, env));
  CHECK(env.msg_id ==
        static_cast<std::uint32_t>(sa::idl::MsgId::HandshakeAccepted));
  CHECK(env.corr_id == 1u);   // corr_id 原样回带(02 §1.3)

  sa::idl::Reader rd(env.body, env.body_len);
  sa::transport::HandshakeAccepted acc{};
  decode(rd, acc);
  REQUIRE(rd.ok());
  CHECK(acc.heartbeat_interval_ms == kHeartbeat);
}

TEST_CASE("★ 端到端:版本不符 ⇒ 拒绝理由先发出去,再断连") {
  SocketGuard g;
  TcpTransport t;
  SessionBridge bridge(t);
  t.SetEvents(&bridge);
  REQUIRE(t.Listen("127.0.0.1", 0));

  TestClient c;
  REQUIRE(c.Connect(t.listen_port()));
  REQUIRE(c.SendAll(HandshakeFrame(kVersion + 1)));

  // ★ 这条用例真正验的是 Close 的优雅语义在**真 socket 上**成立 ——
  //   Loopback 上它是一次内存拷贝,永远不会失败;TCP 上它要靠出站排空。
  std::vector<std::uint8_t> got;
  PumpUntil(t, [&] { return t.connection_count() == 0; });
  c.RecvUntilClosed(got);

  REQUIRE(got.size() > kFrameHeaderBytes);
  EnvelopeView env{};
  const std::uint32_t len =
      static_cast<std::uint32_t>(got[0]) |
      (static_cast<std::uint32_t>(got[1]) << 8) |
      (static_cast<std::uint32_t>(got[2]) << 16) |
      (static_cast<std::uint32_t>(got[3]) << 24);
  REQUIRE(DecodeEnvelope(got.data() + kFrameHeaderBytes, len, env));
  CHECK(env.msg_id ==
        static_cast<std::uint32_t>(sa::idl::MsgId::HandshakeRejected));
  CHECK(bridge.ready.empty());
}
