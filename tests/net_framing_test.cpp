// tests/net_framing_test.cpp —— 帧层、信封层与会话状态机
//
// ★★ 这些用例**不需要 socket** —— 那正是本批次先做成帧与会话、
//    把 TcpTransport 留到下一批的理由(net/api.h 卷首)。
//    与 §9.0.5 那条纠正同源:能脱离网络验的东西不该被排在网络之后。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "net/api.h"

#include <cstring>
#include <vector>

using namespace sa::net;

namespace {

// 记录会话回调,免得用"有没有发出某条消息"去间接推断状态。
class RecordingHost final : public ISessionHost {
 public:
  void OnSessionReady(SessionId id) override {
    ready.push_back(id);
  }
  void OnBattleCommand(SessionId id, const sa::domain::BattleCommand& cmd) override {
    commands.push_back(cmd);
    last_command_session = id;
  }
  void OnSessionClosed(SessionId id) override { closed.push_back(id); }

  std::vector<SessionId> ready;
  std::vector<SessionId> closed;
  std::vector<sa::domain::BattleCommand> commands;
  SessionId last_command_session = 0;
};

// 把一条消息编成帧(客户端视角)。
template <typename M>
std::vector<std::uint8_t> Framed(std::uint64_t corr_id, const M& msg) {
  std::vector<std::uint8_t> out;
  REQUIRE(EncodeFramed(corr_id, msg, out));
  return out;
}

// 从一段出站字节里取第 index 条帧的信封。
// ⚠️★ body **必须复制出来**,不能把 env.body 直接带出本函数。
//    FrameReader::Next() 的契约写得很清楚:「*payload 指向内部缓冲,
//    在下一次 Push()/Pop() 之前有效」—— 而 r 是本函数的局部变量,
//    一返回就析构 ⇒ env.body 成为悬垂指针。
//
// ★ 这不是理论风险:本文件首次被编译运行时(2026-09-04,src/ 接入构建当天),
//   4 个用例读出的 body 字段**全是 0**,而 msg_id / corr_id 却对 ——
//   因为后两者是值拷贝。症状看起来像「IDL 解码器把字段读丢了」,
//   根因却在测试自己。⇒ 先怀疑测试,再怀疑被测对象。
bool NthEnvelope(const std::vector<std::uint8_t>& bytes, std::size_t index,
                 EnvelopeView& out, std::vector<std::uint8_t>& body_storage) {
  FrameReader r;
  if (!r.Push(bytes.data(), bytes.size())) return false;
  for (std::size_t i = 0;; ++i) {
    const std::uint8_t* p = nullptr;
    std::uint32_t len = 0;
    if (r.Next(&p, &len) != FrameStatus::kOk) return false;
    if (i == index) {
      if (!DecodeEnvelope(p, len, out)) return false;
      body_storage.assign(out.body, out.body + out.body_len);
      out.body = body_storage.data();
      return true;
    }
    r.Pop();
  }
}

std::size_t FrameCount(const std::vector<std::uint8_t>& bytes) {
  FrameReader r;
  if (!r.Push(bytes.data(), bytes.size())) return 0;
  std::size_t n = 0;
  for (;;) {
    const std::uint8_t* p = nullptr;
    std::uint32_t len = 0;
    if (r.Next(&p, &len) != FrameStatus::kOk) return n;
    r.Pop();
    ++n;
  }
}

}  // namespace

// ══ 帧层 ═════════════════════════════════════════════════════════
//
// 02 §1.2:长度前缀,**不按分隔符切帧**。旧实现按 '\n' 切行(GetOneLine)
// 与"负载里可能有换行"直接冲突,靠转义硬撑。

TEST_CASE("成帧往返") {
  const std::uint8_t payload[] = {1, 2, 3, 4, 5};
  std::vector<std::uint8_t> wire;
  REQUIRE(WriteFrame(payload, 5, wire));
  CHECK(wire.size() == 4 + 5);

  FrameReader r;
  REQUIRE(r.Push(wire.data(), wire.size()));
  const std::uint8_t* p = nullptr;
  std::uint32_t len = 0;
  REQUIRE(r.Next(&p, &len) == FrameStatus::kOk);
  CHECK(len == 5);
  CHECK(std::memcmp(p, payload, 5) == 0);
  r.Pop();
  CHECK(r.Next(&p, &len) == FrameStatus::kNeedMore);
}

// ★★ 这条是成帧真正要对付的东西:TCP 是**字节流**,
//    它可以在任意位置把一帧切开,包括切在长度前缀中间。
TEST_CASE("逐字节喂入 —— 分片在任何位置都不能出错") {
  const std::uint8_t payload[] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
  std::vector<std::uint8_t> wire;
  REQUIRE(WriteFrame(payload, 10, wire));

  FrameReader r;
  const std::uint8_t* p = nullptr;
  std::uint32_t len = 0;
  for (std::size_t i = 0; i + 1 < wire.size(); ++i) {
    REQUIRE(r.Push(&wire[i], 1));
    // 最后一个字节到达之前,永远只能是 kNeedMore
    REQUIRE(r.Next(&p, &len) == FrameStatus::kNeedMore);
  }
  REQUIRE(r.Push(&wire[wire.size() - 1], 1));
  REQUIRE(r.Next(&p, &len) == FrameStatus::kOk);
  CHECK(len == 10);
  CHECK(std::memcmp(p, payload, 10) == 0);
}

TEST_CASE("一次喂入多帧,逐条取出") {
  const std::uint8_t a[] = {1};
  const std::uint8_t b[] = {2, 2};
  const std::uint8_t c[] = {3, 3, 3};
  std::vector<std::uint8_t> wire;
  REQUIRE(WriteFrame(a, 1, wire));
  REQUIRE(WriteFrame(b, 2, wire));
  REQUIRE(WriteFrame(c, 3, wire));

  FrameReader r;
  REQUIRE(r.Push(wire.data(), wire.size()));
  for (std::uint32_t expect = 1; expect <= 3; ++expect) {
    const std::uint8_t* p = nullptr;
    std::uint32_t len = 0;
    REQUIRE(r.Next(&p, &len) == FrameStatus::kOk);
    CHECK(len == expect);
    CHECK(p[0] == expect);
    r.Pop();
  }
}

// ⚠️★ 长度字段一旦不可信,字节流就再也无法对齐 ——
//    "跳过这一帧继续读"是没有意义的,因为我们并不知道这一帧到哪结束。
//    ⇒ 这两个失败必须是**粘性**的。
TEST_CASE("超长帧:拒绝,且不可恢复") {
  std::vector<std::uint8_t> wire(4);
  const std::uint32_t bogus = kMaxFrameBytes + 1;
  wire[0] = static_cast<std::uint8_t>(bogus & 0xFFu);
  wire[1] = static_cast<std::uint8_t>((bogus >> 8) & 0xFFu);
  wire[2] = static_cast<std::uint8_t>((bogus >> 16) & 0xFFu);
  wire[3] = static_cast<std::uint8_t>((bogus >> 24) & 0xFFu);

  FrameReader r;
  REQUIRE(r.Push(wire.data(), wire.size()));
  const std::uint8_t* p = nullptr;
  std::uint32_t len = 0;
  CHECK(r.Next(&p, &len) == FrameStatus::kTooLarge);
  CHECK(r.failed());
  // 粘性:再问一次还是失败,不会因为又喂了字节就"好了"
  const std::uint8_t more[] = {0, 0, 0, 0};
  CHECK_FALSE(r.Push(more, 4));
  CHECK(r.Next(&p, &len) == FrameStatus::kTooLarge);
}

TEST_CASE("零长帧是协议违规 —— 信封头本身就不止 0 字节") {
  const std::uint8_t wire[4] = {0, 0, 0, 0};
  FrameReader r;
  REQUIRE(r.Push(wire, 4));
  const std::uint8_t* p = nullptr;
  std::uint32_t len = 0;
  CHECK(r.Next(&p, &len) == FrameStatus::kEmpty);
  CHECK(r.failed());
}

// 15 §4.3:原版每连接固定 324–452 KB 双缓冲,是每连接成本主项。
// 01 §5.3 裁定「按需增长 + 上限熔断」⇒ 这条验的是那个熔断真的在。
TEST_CASE("累积缓冲有上限,对端灌垃圾时熔断") {
  FrameReader r;
  const std::vector<std::uint8_t> junk(16 * 1024, 0xAB);
  bool tripped = false;
  for (int i = 0; i < 64; ++i) {
    if (!r.Push(junk.data(), junk.size())) {
      tripped = true;
      break;
    }
  }
  CHECK(tripped);
}

TEST_CASE("负载超限时 WriteFrame 失败而不是截断") {
  // 05 §10.4:原版 szAllBattleString 用 strncat 第三参写错、等价无上界 strcat。
  // ⇒ 新实现宁可失败,不可写出半条。
  const std::vector<std::uint8_t> big(kMaxFrameBytes + 1, 0x5A);
  std::vector<std::uint8_t> out;
  CHECK_FALSE(WriteFrame(big.data(), static_cast<std::uint32_t>(big.size()), out));
  CHECK(out.empty());
  CHECK_FALSE(WriteFrame(big.data(), 0, out));
}

// ══ 信封层 ═══════════════════════════════════════════════════════
TEST_CASE("信封往返:msg_id 由类型编译期决定") {
  sa::transport::Ping ping;
  ping.client_time_ms = 0xDEADBEEFull;
  const std::vector<std::uint8_t> wire = Framed(42, ping);

  EnvelopeView env;
  std::vector<std::uint8_t> body;
  REQUIRE(NthEnvelope(wire, 0, env, body));
  CHECK(env.msg_id == static_cast<std::uint32_t>(sa::idl::MsgId::Ping));
  CHECK(env.corr_id == 42u);

  sa::idl::Reader rd(env.body, env.body_len);
  sa::transport::Ping back{};
  decode(rd, back);
  REQUIRE(rd.ok());
  CHECK(back.client_time_ms == 0xDEADBEEFull);
}

TEST_CASE("信封头都不够长的帧 ⇒ 整条作废") {
  const std::uint8_t tiny[3] = {1, 2, 3};
  EnvelopeView env;
  CHECK_FALSE(DecodeEnvelope(tiny, 3, env));
  CHECK_FALSE(DecodeEnvelope(nullptr, 0, env));
}

// ══ 会话状态机(01 §5.2)═════════════════════════════════════════
namespace {

constexpr std::uint32_t kVersion = 3;
constexpr std::uint32_t kHeartbeat = 30000;

std::vector<std::uint8_t> HandshakeFrame(std::uint32_t version) {
  sa::transport::HandshakeRequest req{};
  req.protocol_version = version;
  req.client_build.assign("test");
  return Framed(1, req);
}

}  // namespace

TEST_CASE("握手通过 ⇒ 已认证,并回 HandshakeAccepted") {
  RecordingHost host;
  Session s(7, kVersion, kHeartbeat, &host);
  CHECK(s.state() == SessionState::kAnonymous);

  const std::vector<std::uint8_t> in = HandshakeFrame(kVersion);
  std::vector<std::uint8_t> out;
  REQUIRE(s.HandleFrame(in.data() + 4, static_cast<std::uint32_t>(in.size() - 4), out));

  CHECK(s.state() == SessionState::kAuthenticated);
  REQUIRE(host.ready.size() == 1);
  CHECK(host.ready[0] == 7u);

  EnvelopeView env;
  std::vector<std::uint8_t> body;
  REQUIRE(NthEnvelope(out, 0, env, body));
  CHECK(env.msg_id == static_cast<std::uint32_t>(sa::idl::MsgId::HandshakeAccepted));
  CHECK(env.corr_id == 1u);   // corr_id 原样回带(02 §1.3)

  sa::idl::Reader rd(env.body, env.body_len);
  sa::transport::HandshakeAccepted acc{};
  decode(rd, acc);
  REQUIRE(rd.ok());
  CHECK(acc.session_id == 7u);
  // ★ 心跳间隔由服务端下发,客户端不硬编码(handshake.proto)
  CHECK(acc.heartbeat_interval_ms == kHeartbeat);
}

// ★ 02 §2.1:协议版本不匹配**在握手阶段直接拒绝**,不做版本分支。
TEST_CASE("版本不符 ⇒ 拒绝,但要先把拒绝理由发出去再关") {
  RecordingHost host;
  Session s(7, kVersion, kHeartbeat, &host);
  const std::vector<std::uint8_t> in = HandshakeFrame(kVersion + 1);
  std::vector<std::uint8_t> out;
  CHECK_FALSE(s.HandleFrame(in.data() + 4, static_cast<std::uint32_t>(in.size() - 4), out));
  CHECK(s.state() == SessionState::kClosed);

  // ⚠️ 不发就关,客户端只看到断连,无从提示"请更新到 vN"。
  EnvelopeView env;
  std::vector<std::uint8_t> body;
  REQUIRE(NthEnvelope(out, 0, env, body));
  CHECK(env.msg_id == static_cast<std::uint32_t>(sa::idl::MsgId::HandshakeRejected));
  sa::idl::Reader rd(env.body, env.body_len);
  sa::transport::HandshakeRejected rej{};
  decode(rd, rej);
  REQUIRE(rd.ok());
  CHECK(rej.reason == sa::transport::RejectReason::REJECT_VERSION_MISMATCH);
  CHECK(rej.required_protocol_version == kVersion);
}

// ★★ 握手必须**确实发生在最前面**,否则版本检查可以被绕过去:
//    先发一条业务消息,服务端按当前版本解析,握手再也不会被检查。
TEST_CASE("握手之前的任何其它消息 ⇒ 协议违规") {
  RecordingHost host;
  Session s(7, kVersion, kHeartbeat, &host);
  sa::transport::Ping ping{};
  const std::vector<std::uint8_t> in = Framed(1, ping);
  std::vector<std::uint8_t> out;
  CHECK_FALSE(s.HandleFrame(in.data() + 4, static_cast<std::uint32_t>(in.size() - 4), out));
  CHECK(s.last_reject_msg_id() == static_cast<std::uint32_t>(sa::idl::MsgId::Ping));
}

TEST_CASE("重复握手 ⇒ 协议违规(状态机不许被重放绕过)") {
  RecordingHost host;
  Session s(7, kVersion, kHeartbeat, &host);
  const std::vector<std::uint8_t> in = HandshakeFrame(kVersion);
  std::vector<std::uint8_t> out;
  REQUIRE(s.HandleFrame(in.data() + 4, static_cast<std::uint32_t>(in.size() - 4), out));
  CHECK_FALSE(s.HandleFrame(in.data() + 4, static_cast<std::uint32_t>(in.size() - 4), out));
}

TEST_CASE("Ping ⇒ Pong,client_time_ms 原样回带") {
  RecordingHost host;
  Session s(7, kVersion, kHeartbeat, &host);
  std::vector<std::uint8_t> out;
  const std::vector<std::uint8_t> hs = HandshakeFrame(kVersion);
  REQUIRE(s.HandleFrame(hs.data() + 4, static_cast<std::uint32_t>(hs.size() - 4), out));
  out.clear();

  sa::transport::Ping ping{};
  ping.client_time_ms = 123456789ull;
  const std::vector<std::uint8_t> in = Framed(99, ping);
  REQUIRE(s.HandleFrame(in.data() + 4, static_cast<std::uint32_t>(in.size() - 4), out));

  EnvelopeView env;
  std::vector<std::uint8_t> body;
  REQUIRE(NthEnvelope(out, 0, env, body));
  CHECK(env.msg_id == static_cast<std::uint32_t>(sa::idl::MsgId::Pong));
  CHECK(env.corr_id == 99u);
  sa::idl::Reader rd(env.body, env.body_len);
  sa::transport::Pong pong{};
  decode(rd, pong);
  REQUIRE(rd.ok());
  CHECK(pong.client_time_ms == 123456789ull);
}

// ⚠️ 只有在世的会话能下战斗指令 —— 02 §5.5「回执侧的强校验必须保留」同一取向。
TEST_CASE("战斗指令要求 kOnline") {
  RecordingHost host;
  Session s(7, kVersion, kHeartbeat, &host);
  std::vector<std::uint8_t> out;
  const std::vector<std::uint8_t> hs = HandshakeFrame(kVersion);
  REQUIRE(s.HandleFrame(hs.data() + 4, static_cast<std::uint32_t>(hs.size() - 4), out));

  sa::domain::BattleCommand cmd{};
  cmd.battle_id = 1;
  cmd.turn = 0;
  cmd.command_kind = sa::domain::BattleCommand::CommandKind::ATTACK;
  cmd.command.attack.target = 10;
  const std::vector<std::uint8_t> in = Framed(0, cmd);

  SUBCASE("仅已认证 ⇒ 拒绝") {
    CHECK_FALSE(s.HandleFrame(in.data() + 4, static_cast<std::uint32_t>(in.size() - 4), out));
    CHECK(host.commands.empty());
  }
  SUBCASE("在世 ⇒ 转交宿主") {
    s.MarkOnline();
    CHECK(s.HandleFrame(in.data() + 4, static_cast<std::uint32_t>(in.size() - 4), out));
    REQUIRE(host.commands.size() == 1);
    CHECK(host.commands[0].command.attack.target == 10u);
    CHECK(host.last_command_session == 7u);
  }
}

TEST_CASE("未知 msg_id ⇒ 协议违规,不是「忽略并继续」") {
  RecordingHost host;
  Session s(7, kVersion, kHeartbeat, &host);
  std::vector<std::uint8_t> out;
  const std::vector<std::uint8_t> hs = HandshakeFrame(kVersion);
  REQUIRE(s.HandleFrame(hs.data() + 4, static_cast<std::uint32_t>(hs.size() - 4), out));

  // 手搓一条 msg_id 不存在的帧
  std::vector<std::uint8_t> bogus(12, 0);
  bogus[0] = 0xEF;
  bogus[1] = 0xBE;
  CHECK_FALSE(s.HandleFrame(bogus.data(), 12, out));
  CHECK(s.last_reject_msg_id() == 0xBEEFu);
}

// ══ LoopbackTransport ════════════════════════════════════════════
namespace {

class ByteSink final : public ITransportEvents {
 public:
  void OnConnected(ConnectionId id) override { connected.push_back(id); }
  void OnBytes(ConnectionId id, const std::uint8_t* d, std::size_t n) override {
    (void)id;
    received.insert(received.end(), d, d + n);
  }
  void OnDisconnected(ConnectionId id) override { disconnected.push_back(id); }

  std::vector<ConnectionId> connected;
  std::vector<ConnectionId> disconnected;
  std::vector<std::uint8_t> received;
};

}  // namespace

TEST_CASE("Loopback:连接、投递、发送、关闭") {
  LoopbackTransport t;
  ByteSink sink;
  t.SetEvents(&sink);

  const ConnectionId id = t.Connect();
  REQUIRE(sink.connected.size() == 1);

  const std::uint8_t in[] = {1, 2, 3};
  t.Deliver(id, in, 3);
  CHECK(sink.received.empty());   // ★ Poll 之前不交付 —— 入站发生在 tick 第 2 步
  t.Poll();
  CHECK(sink.received.size() == 3);

  const std::uint8_t out[] = {9, 9};
  CHECK(t.Send(id, out, 2));
  CHECK(t.sent(id).size() == 2);

  t.Close(id);
  CHECK(t.closed(id));
  CHECK(sink.disconnected.size() == 1);
  CHECK_FALSE(t.Send(id, out, 2));
}

TEST_CASE("多帧编码进同一个出站缓冲") {
  std::vector<std::uint8_t> out;
  sa::transport::Ping a{};
  a.client_time_ms = 1;
  sa::transport::Ping b{};
  b.client_time_ms = 2;
  REQUIRE(EncodeFramed(0, a, out));
  REQUIRE(EncodeFramed(0, b, out));
  CHECK(FrameCount(out) == 2);
}
