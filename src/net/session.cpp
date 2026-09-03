// src/net/session.cpp —— 会话状态机(01 §5.2)
//
// ⚠️★ 关于「认证」这一步在 IDL 里不存在,见 net/api.h 里 SessionState 上方的说明。
//    那不是本文件绕过去的问题,是一处**排期空白**,已在 docs 里登记。

#include "net/api.h"

namespace sa::net {

const char* SessionStateName(SessionState s) noexcept {
  switch (s) {
    case SessionState::kAnonymous:      return "anonymous";
    case SessionState::kAuthenticating: return "authenticating";
    case SessionState::kAuthenticated:  return "authenticated";
    case SessionState::kSelectingChar:  return "selecting_char";
    case SessionState::kOnline:         return "online";
    case SessionState::kLoggingOut:     return "logging_out";
    case SessionState::kClosed:         return "closed";
  }
  return "unknown";
}

Session::Session(SessionId id, std::uint32_t protocol_version,
                 std::uint32_t heartbeat_interval_ms,
                 ISessionHost* host) noexcept
    : id_(id),
      protocol_version_(protocol_version),
      heartbeat_interval_ms_(heartbeat_interval_ms),
      host_(host) {}

void Session::MarkOnline() noexcept {
  if (state_ == SessionState::kAuthenticated) state_ = SessionState::kOnline;
}

void Session::Close() noexcept {
  if (state_ == SessionState::kClosed) return;
  state_ = SessionState::kClosed;
  if (host_ != nullptr) host_->OnSessionClosed(id_);
}

bool Session::HandleFrame(const std::uint8_t* frame, std::uint32_t len,
                          std::vector<std::uint8_t>& out) {
  if (state_ == SessionState::kClosed) return false;

  EnvelopeView env;
  if (!DecodeEnvelope(frame, len, env)) {
    // 02 §2.1:解码失败是「整条消息作废」,不存在部分成功的中间态。
    last_reject_msg_id_ = 0;
    return false;
  }
  ++frames_handled_;

  const auto id = static_cast<sa::idl::MsgId>(env.msg_id);

  // ★★ 握手之前只接受握手。
  //   02 §2.1 已裁定「协议版本不匹配时在握手阶段直接拒绝」——
  //   那要求握手**确实发生在最前面**,否则版本检查可以被绕过去:
  //   先发一条业务消息,服务端按当前版本解析,握手再也不会被检查。
  if (state_ == SessionState::kAnonymous &&
      id != sa::idl::MsgId::HandshakeRequest) {
    last_reject_msg_id_ = env.msg_id;
    return false;
  }

  switch (id) {
    case sa::idl::MsgId::HandshakeRequest:
      return HandleHandshake(env, out);
    case sa::idl::MsgId::Ping:
      return HandlePing(env, out);
    case sa::idl::MsgId::BattleCommand:
      return HandleBattleCommand(env);
    default:
      // ⚠️ 未知或方向错的消息 ⇒ 协议违规,关闭连接。
      //   不"忽略并继续":那会让客户端的 bug 表现为"服务端没反应",
      //   而不是一个能被立刻看见的断连。
      last_reject_msg_id_ = env.msg_id;
      return false;
  }
}

bool Session::HandleHandshake(const EnvelopeView& env,
                              std::vector<std::uint8_t>& out) {
  // 重复握手是协议违规:握手改变会话状态,允许重放等于允许状态机被绕。
  if (state_ != SessionState::kAnonymous) {
    last_reject_msg_id_ = env.msg_id;
    return false;
  }

  sa::idl::Reader r(env.body, env.body_len);
  sa::transport::HandshakeRequest req;
  decode(r, req);
  if (!r.ok()) return false;

  if (req.protocol_version != protocol_version_) {
    // ★ 不做「主版本兼容、次版本忽略」的分支(02 §2.1)。不等即拒。
    sa::transport::HandshakeRejected rej;
    rej.reason = sa::transport::RejectReason::REJECT_VERSION_MISMATCH;
    rej.required_protocol_version = protocol_version_;
    // ⚠️ 拒绝也要发出去再关 —— 否则客户端只看到断连,无从提示"请更新"。
    //   corr_id 原样回带,让客户端能把它对上自己那条请求(02 §1.3)。
    (void)EncodeFramed(env.corr_id, rej, out);
    state_ = SessionState::kClosed;
    if (host_ != nullptr) host_->OnSessionClosed(id_);
    return false;
  }

  sa::transport::HandshakeAccepted acc;
  acc.session_id = id_;
  // ★ 心跳间隔由服务端下发,客户端不硬编码(handshake.proto 的原话)。
  acc.heartbeat_interval_ms = heartbeat_interval_ms_;
  if (!EncodeFramed(env.corr_id, acc, out)) return false;

  state_ = SessionState::kAuthenticated;
  if (host_ != nullptr) host_->OnSessionReady(id_);
  return true;
}

bool Session::HandlePing(const EnvelopeView& env,
                         std::vector<std::uint8_t>& out) {
  sa::idl::Reader r(env.body, env.body_len);
  sa::transport::Ping ping;
  decode(r, ping);
  if (!r.ok()) return false;

  sa::transport::Pong pong;
  pong.client_time_ms = ping.client_time_ms;  // 原样回带,客户端据此算 RTT
  // ⚠️ server_time_ms 留 0:本模块**没有时钟** —— 时钟是 platform 的东西,
  //    而 net 依赖 platform 只为了一个时间戳,会把 L1 的依赖面撑大。
  //    ⇒ 阶段 2 接入时由宿主填,或把 Pong 的产出上移到 world。
  //      现在留 0 而不是随手 time(nullptr):01 §3.1「不用墙钟做逻辑判断」,
  //      而一个"看起来有值其实是墙钟"的字段比 0 更难查。
  pong.server_time_ms = 0;
  return EncodeFramed(env.corr_id, pong, out);
}

bool Session::HandleBattleCommand(const EnvelopeView& env) {
  // ⚠️ 只有在世的会话能下指令。原版把这类校验散在各处,
  //    这里集中在状态机上 —— 02 §5.5「回执侧的两条强校验必须保留」同一取向。
  if (state_ != SessionState::kOnline) {
    last_reject_msg_id_ = env.msg_id;
    return false;
  }

  sa::idl::Reader r(env.body, env.body_len);
  sa::domain::BattleCommand cmd;
  decode(r, cmd);
  if (!r.ok()) return false;

  // ★ 指令的**玩法**合法性(能不能行动、学没学过这个技能)不在这里判 ——
  //   shared/rules/battle.h 明写 L3 的输入是"已通过合法性校验"的指令,
  //   而 DR-BT5 把「能否行动」统一到 rules::CheckCanAct 这一个真源。
  //   net 只负责"这条消息在这个状态下允不允许出现"。
  if (host_ != nullptr) host_->OnBattleCommand(id_, cmd);
  return true;
}

}  // namespace sa::net
