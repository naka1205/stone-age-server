// src/net/tcp_transport.cpp —— TCP 传输(1.5 收尾项,2026-09-04)
//
// ★★ 全部平台细节关在本文件里。net/api.h 的 TcpTransport 声明中
//    **没有一个平台类型** —— 理由见那里的卷首注释(windows.h 的宏会顺着
//    PUBLIC 头传染给 world 与每一个 tests 目标)。
//
// ── 这一层真正要做对的三件事(其余都是样板)──────────────────────
//
//   ① ★★ **出站背压**。`send()` 在内核发送缓冲满时只写一部分,甚至一个字节
//      都不写(EWOULDBLOCK)。⇒ 未写出的尾巴必须**排队**,并在下一轮 POLLOUT
//      时续写。⚠️ 把 send() 的返回值当成"写完了"是这一层最经典的错,
//      而它的表现是**大帧偶发截断** —— 小消息永远不会暴露它。
//      ⇒ kMaxOutboundBytes 是配套的熔断:对端连上却不读时,队列不能无限涨。
//
//   ② ★ **短读与粘包不是本层的事**。OnBytes 交出去的就是"这一轮读到的字节",
//      不保证帧对齐 —— 上层 FrameReader 早已按这个契约被 18 条用例钉死
//      (api.h 卷首)。⇒ 本层**绝不**试图凑整帧。
//
//   ③ ★ **回调期间容器不得失效**。OnBytes / OnDisconnected 里,宿主完全可能
//      反过来调 Send() 或 Close()(实测:World 收到握手就立刻回包)。
//      ⇒ 连接表用 std::deque(引用稳定),删除延到本轮末尾统一做,
//        且回调期间只按 ConnectionId 索引,不持有裸指针跨回调。
//
// ── 平台差异面(只有四处,逐处标注)────────────────────────────
//   winsock 需要 WSAStartup / closesocket / ioctlsocket / WSAPoll / WSAEWOULDBLOCK
//   POSIX  用 close / fcntl(O_NONBLOCK) / poll / EWOULDBLOCK,且要挡 SIGPIPE。

#include "net/api.h"

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#if defined(_WIN32)
// ⚠️ WIN32_LEAN_AND_MEAN 要在 winsock2.h 之前;winsock2.h 要在 windows.h 之前
//    (否则 windows.h 会先拉进 winsock.h v1,与 v2 冲突)。这个顺序是有讲究的,
//    别按字母序"整理"它。
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
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <signal.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace sa::net {
namespace {

// ── 平台差异 1/4:句柄类型与无效值 ────────────────────────────────
#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
using PollFd = WSAPOLLFD;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
using PollFd = struct pollfd;
#endif

// ── 平台差异 2/4:关闭 / 非阻塞 / "现在写不了" ────────────────────
void CloseSocket(SocketHandle s) noexcept {
#if defined(_WIN32)
  ::closesocket(s);
#else
  ::close(s);
#endif
}

bool SetNonBlocking(SocketHandle s) noexcept {
#if defined(_WIN32)
  u_long mode = 1;
  return ::ioctlsocket(s, static_cast<long>(FIONBIO), &mode) == 0;
#else
  const int flags = ::fcntl(s, F_GETFL, 0);
  if (flags < 0) return false;
  return ::fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// ★ 「这次不行,下次再来」与「真的坏了」必须分开 —— 把前者当错误处理,
//   表现是**高负载下随机断连**,而低负载永远复现不出来。
bool WouldBlock() noexcept {
#if defined(_WIN32)
  const int e = ::WSAGetLastError();
  return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

int PollSockets(PollFd* fds, std::size_t n) noexcept {
#if defined(_WIN32)
  return ::WSAPoll(fds, static_cast<ULONG>(n), 0);
#else
  return ::poll(fds, static_cast<nfds_t>(n), 0);
#endif
}

// ── 平台差异 3/4:winsock 要显式初始化,且要计数 ───────────────────
//
// ⚠️ 用**引用计数**而不是「进程里初始化一次就不管了」:测试会构造多个
//    TcpTransport 并逐个析构,而 WSACleanup 是按 WSAStartup 次数配对的。
//    POSIX 侧这个类整个是空壳,不留 #ifdef 在调用点。
class SocketLibrary {
 public:
  static bool Acquire() noexcept {
#if defined(_WIN32)
    WSADATA data;
    return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    // ★ SIGPIPE:对端已关闭时 send() 默认会**杀掉进程**。macOS 有
    //   SO_NOSIGPIPE、Linux 有 MSG_NOSIGNAL,但两者都要在每个调用点记得写
    //   ⇒ 直接忽略信号,一次解决。⚠️ 这是进程级副作用,写在这里而不是
    //     藏在 send 附近,是为了让它可被看见。
    ::signal(SIGPIPE, SIG_IGN);
    return true;
#endif
  }
  static void Release() noexcept {
#if defined(_WIN32)
    ::WSACleanup();
#endif
  }
};

}  // namespace

struct TcpTransport::Impl {
  struct Conn {
    ConnectionId id = 0;
    SocketHandle fd = kInvalidSocket;
    std::vector<std::uint8_t> outbound;  // 尚未写出去的尾巴(见卷首 ①)
    std::size_t out_sent = 0;            // outbound 里已写出的前缀长度
    bool want_close = false;             // 已请求关闭,等出站排空
    bool dead = false;                   // 本轮末尾清理
  };

  ITransportEvents* events = nullptr;
  SocketHandle listener = kInvalidSocket;
  std::uint16_t port = 0;
  std::string error;
  bool lib_ready = false;

  // ★ deque 而不是 vector:回调里宿主可能 Send/Close,进而增删连接。
  //   deque 的**引用在两端插入时保持有效**,而 vector 会整体搬家 ——
  //   那是一条典型的"回调里对象被移动"的悬垂路径(卷首 ③)。
  std::deque<Conn> conns;
  ConnectionId next_id = 1;
  std::vector<PollFd> pollfds;
  std::vector<ConnectionId> poll_ids;

  Conn* Get(ConnectionId id) noexcept {
    for (Conn& c : conns) {
      if (c.id == id && !c.dead) return &c;
    }
    return nullptr;
  }
  const Conn* Get(ConnectionId id) const noexcept {
    for (const Conn& c : conns) {
      if (c.id == id && !c.dead) return &c;
    }
    return nullptr;
  }

  void Fail(const char* what) {
    error = what;
#if defined(_WIN32)
    error += " (WSA error ";
    error += std::to_string(::WSAGetLastError());
    error += ")";
#else
    error += " (errno ";
    error += std::to_string(errno);
    error += ": ";
    error += std::strerror(errno);
    error += ")";
#endif
  }

  // 尽力把 c 的出站队列写出去。返回 false 表示连接坏了。
  bool FlushOutbound(Conn& c) {
    while (c.out_sent < c.outbound.size()) {
      const char* p = reinterpret_cast<const char*>(c.outbound.data()) +
                      c.out_sent;
      const std::size_t remain = c.outbound.size() - c.out_sent;
#if defined(_WIN32)
      const int n = ::send(c.fd, p, static_cast<int>(remain), 0);
#else
      const ssize_t n = ::send(c.fd, p, remain, 0);
#endif
      if (n > 0) {
        c.out_sent += static_cast<std::size_t>(n);
        continue;
      }
      // ★ n == 0 也走这里:对 send() 而言那同样是"没写进去",
      //   继续循环会空转 ⇒ 当作 would-block 退出,下一轮 POLLOUT 再来。
      if (n < 0 && !WouldBlock()) return false;
      break;
    }
    if (c.out_sent == c.outbound.size()) {
      c.outbound.clear();
      c.out_sent = 0;
    } else if (c.out_sent > 64 * 1024) {
      // ★ 队列前缀已写出很多时才搬家 —— 每次写一点就 erase 是 O(n²)。
      c.outbound.erase(c.outbound.begin(),
                       c.outbound.begin() +
                           static_cast<std::ptrdiff_t>(c.out_sent));
      c.out_sent = 0;
    }
    return true;
  }

  void Kill(Conn& c) {
    if (c.dead) return;
    c.dead = true;
    if (c.fd != kInvalidSocket) {
      CloseSocket(c.fd);
      c.fd = kInvalidSocket;
    }
  }
};

TcpTransport::TcpTransport() : impl_(new Impl) {}

TcpTransport::~TcpTransport() {
  // ⚠️ 析构里**不回调** OnDisconnected:宿主可能已经先于传输层销毁,
  //    那正是"析构顺序依赖"这类错的温床。要通知就显式调 Stop()。
  for (Impl::Conn& c : impl_->conns) {
    if (c.fd != kInvalidSocket) CloseSocket(c.fd);
  }
  if (impl_->listener != kInvalidSocket) CloseSocket(impl_->listener);
  if (impl_->lib_ready) SocketLibrary::Release();
}

bool TcpTransport::Listen(const char* bind_addr, std::uint16_t port) {
  Impl& d = *impl_;
  if (d.listener != kInvalidSocket) {
    d.error = "已经在监听了 —— 重复 Listen 是调用方的逻辑错,不是可恢复状态";
    return false;
  }
  if (!d.lib_ready) {
    if (!SocketLibrary::Acquire()) {
      d.Fail("socket 库初始化失败");
      return false;
    }
    d.lib_ready = true;
  }

  const SocketHandle fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd == kInvalidSocket) {
    d.Fail("socket() 失败");
    return false;
  }

  // ── 平台差异 4/4:SO_REUSEADDR 在两边含义不同 ──────────────────
  //
  // ⚠️★ POSIX:允许绑一个处于 TIME_WAIT 的端口 —— 重启服务必需。
  //    Windows:SO_REUSEADDR 允许**两个进程同时绑同一端口**,后者静默劫持
  //    前者的流量(这是 winsock 的历史设计)。⇒ **Windows 侧不设它**。
  //    这不是"两边写法不同",是**同名选项语义不同**,照抄会开出一个后门。
#if !defined(_WIN32)
  const int on = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const void*>(&on), sizeof(on));
#endif

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  // ⚠️★ htons / htonl / ntohs **不加 `::`**,而本文件其余系统调用都加了。
  //    不是笔误:它们在 macOS(与多数 libc)上是**宏**,`::htons` 展开后是
  //    `::__DARWIN_OSSwapInt16(...)` ⇒ 编译错。⇒ 一律非限定调用。
  //    ★ 实测于本文件首次编译,记在这里免得有人"顺手统一风格"再踩一次。
  addr.sin_port = htons(port);
  if (bind_addr == nullptr || bind_addr[0] == '\0') {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else if (::inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {
    d.error = "bind 地址不是合法的 IPv4 字面量:";
    d.error += bind_addr;
    CloseSocket(fd);
    return false;
  }

  if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    d.Fail("bind() 失败");
    CloseSocket(fd);
    return false;
  }
  if (::listen(fd, 128) != 0) {
    d.Fail("listen() 失败");
    CloseSocket(fd);
    return false;
  }
  if (!SetNonBlocking(fd)) {
    d.Fail("监听 socket 设非阻塞失败");
    CloseSocket(fd);
    return false;
  }

  // ★ 取回实际端口:port 传 0 时由系统分配,不回读就没人知道它是几。
  sockaddr_in bound{};
#if defined(_WIN32)
  int len = static_cast<int>(sizeof(bound));
#else
  socklen_t len = sizeof(bound);
#endif
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
    d.port = ntohs(bound.sin_port);
  } else {
    d.port = port;
  }

  d.listener = fd;
  d.error.clear();
  return true;
}

std::uint16_t TcpTransport::listen_port() const noexcept {
  return impl_->port;
}

const char* TcpTransport::last_error() const noexcept {
  return impl_->error.c_str();
}

void TcpTransport::Stop() {
  Impl& d = *impl_;
  if (d.listener != kInvalidSocket) {
    CloseSocket(d.listener);
    d.listener = kInvalidSocket;
    d.port = 0;
  }
  for (Impl::Conn& c : d.conns) {
    if (c.dead) continue;
    const ConnectionId id = c.id;
    d.Kill(c);
    if (d.events != nullptr) d.events->OnDisconnected(id);
  }
  d.conns.clear();
}

void TcpTransport::SetEvents(ITransportEvents* events) {
  impl_->events = events;
}

bool TcpTransport::Send(ConnectionId id, const std::uint8_t* data,
                        std::size_t n) {
  Impl& d = *impl_;
  Impl::Conn* c = d.Get(id);
  if (c == nullptr || c->want_close) return false;

  // ★ 熔断先判:队列已经超限说明对端不读,再排进去只是把内存耗尽推后一点。
  if (c->outbound.size() - c->out_sent + n > kMaxOutboundBytes) {
    d.error = "出站队列超过上限 —— 对端连上却不读,连接已断开";
    c->want_close = true;
    return false;
  }

  c->outbound.insert(c->outbound.end(), data, data + n);
  // ⚠️★ **不在这里直接 send()**,哪怕队列原本是空的。
  //    理由是顺序:世界侧一个 tick 里可能对同一条连接 Push 多条消息,
  //    立即写会让"先排队的后发"成为可能(前一条 would-block 排着队,
  //    后一条却因为队列刚好排空而直接写出去)。
  //    ⇒ 出站一律走 Poll() 的 POLLOUT 那条路径,**只有一个出口**。
  return true;
}

void TcpTransport::Close(ConnectionId id) {
  Impl& d = *impl_;
  Impl::Conn* c = d.Get(id);
  if (c == nullptr || c->want_close) return;
  // ★ 优雅关闭:标记后先把出站排空,Poll() 再真正关掉并回调。
  //   否则"发一条拒绝理由然后关连接"(02 §5.5 / session.cpp 的握手拒绝路径)
  //   会把那条理由丢掉 —— 而客户端将看到一次无理由的断连。
  c->want_close = true;
}

void TcpTransport::Poll() {
  Impl& d = *impl_;

  // ── 1. accept:一轮吃干净,不留到下一 tick ──────────────────────
  if (d.listener != kInvalidSocket) {
    for (;;) {
      const SocketHandle fd = ::accept(d.listener, nullptr, nullptr);
      if (fd == kInvalidSocket) break;  // would-block ⇒ 没有更多待接连接
      if (!SetNonBlocking(fd)) {
        CloseSocket(fd);
        continue;
      }
      // ★ TCP_NODELAY:回合制的消息小而稀,Nagle 会把回执压在 40ms 上 ——
      //   那是**协议层看不出来的延迟**,不是吞吐问题。
      const int on = 1;
      ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&on), sizeof(on));

      Impl::Conn c;
      c.id = d.next_id++;
      c.fd = fd;
      d.conns.push_back(std::move(c));
      if (d.events != nullptr) d.events->OnConnected(d.conns.back().id);
    }
  }

  // ── 2. poll:一次系统调用问全部连接 ────────────────────────────
  //
  // ⚠️ 每轮重建 pollfds 数组。它是 O(连接数) 的**内存写**,不是系统调用 ——
  //    §5.1 批的原版是 O(连接数) 次 select **系统调用**,不是同一件事。
  d.pollfds.clear();
  d.poll_ids.clear();
  for (Impl::Conn& c : d.conns) {
    if (c.dead) continue;
    PollFd p{};
    p.fd = c.fd;
    p.events = POLLIN;
    if (c.out_sent < c.outbound.size()) p.events |= POLLOUT;
    p.revents = 0;
    d.pollfds.push_back(p);
    d.poll_ids.push_back(c.id);
  }

  if (!d.pollfds.empty()) {
    const int ready = PollSockets(d.pollfds.data(), d.pollfds.size());
    if (ready > 0) {
      // ⚠️ 先写后读。理由:读回调里宿主会 Push 新的出站数据,若顺序反过来,
      //    那批数据要等下一 tick 才可能被 poll 看到 —— 每次往返白等一个 tick。
      //    ★ 这不影响正确性,影响的是 1.4 demo 里"一回合几个 tick"这种可观测量。
      for (std::size_t i = 0; i < d.pollfds.size(); ++i) {
        if ((d.pollfds[i].revents & POLLOUT) == 0) continue;
        Impl::Conn* c = d.Get(d.poll_ids[i]);
        if (c == nullptr) continue;
        if (!d.FlushOutbound(*c)) c->want_close = true;
      }

      // ── 3. 读 ────────────────────────────────────────────────
      std::uint8_t buf[16 * 1024];
      for (std::size_t i = 0; i < d.pollfds.size(); ++i) {
        const short re = d.pollfds[i].revents;
        if (re == 0) continue;
        Impl::Conn* c = d.Get(d.poll_ids[i]);
        if (c == nullptr) continue;

        if ((re & POLLIN) != 0) {
          for (;;) {
#if defined(_WIN32)
            const int n = ::recv(c->fd, reinterpret_cast<char*>(buf),
                                 static_cast<int>(sizeof(buf)), 0);
#else
            const ssize_t n = ::recv(c->fd, buf, sizeof(buf), 0);
#endif
            if (n > 0) {
              if (d.events != nullptr) {
                d.events->OnBytes(c->id, buf, static_cast<std::size_t>(n));
              }
              // ⚠️★ 回调里宿主可能 Close() 了这条连接,甚至 accept 进了新连接
              //    ⇒ **重新取指针**,不复用回调前那个。
              c = d.Get(d.poll_ids[i]);
              if (c == nullptr) break;
              if (static_cast<std::size_t>(n) < sizeof(buf)) break;
              continue;  // 读满了缓冲,可能还有 ⇒ 再来一次
            }
            if (n == 0) {  // ★ 对端正常关闭
              c->want_close = true;
              c->outbound.clear();  // 对面走了,没什么可发的了
              c->out_sent = 0;
            } else if (!WouldBlock()) {
              c->want_close = true;
              c->outbound.clear();
              c->out_sent = 0;
            }
            break;
          }
        }
        // ★ POLLHUP / POLLERR:对端异常消失。POLLIN 分支的 recv 也会看到,
        //   但连接从未有过入站数据时只有这里报得出来。
        if (c != nullptr && (re & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
          c->want_close = true;
          c->outbound.clear();
          c->out_sent = 0;
        }
      }
    }
  }

  // ── 4. 收尾:排空的待关连接才真正关闭,并回调 ───────────────────
  for (Impl::Conn& c : d.conns) {
    if (c.dead || !c.want_close) continue;
    // 还有没写完的出站且连接仍活着 ⇒ 再试一次,写完了才关。
    if (c.out_sent < c.outbound.size() && d.FlushOutbound(c) &&
        c.out_sent < c.outbound.size()) {
      continue;  // 下一轮再来
    }
    const ConnectionId id = c.id;
    d.Kill(c);
    if (d.events != nullptr) d.events->OnDisconnected(id);
  }

  // ★ 统一在末尾摘除,避免在遍历/回调中间改动容器(卷首 ③)。
  while (!d.conns.empty() && d.conns.front().dead) d.conns.pop_front();
  if (!d.conns.empty()) {
    std::deque<Impl::Conn> keep;
    for (Impl::Conn& c : d.conns) {
      if (!c.dead) keep.push_back(std::move(c));
    }
    d.conns.swap(keep);
  }
}

std::size_t TcpTransport::connection_count() const noexcept {
  std::size_t n = 0;
  for (const Impl::Conn& c : impl_->conns) {
    if (!c.dead) ++n;
  }
  return n;
}

std::size_t TcpTransport::pending_outbound(ConnectionId id) const noexcept {
  const Impl::Conn* c = impl_->Get(id);
  return c == nullptr ? 0 : c->outbound.size() - c->out_sent;
}

}  // namespace sa::net
