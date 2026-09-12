// src/net/session.cpp —— 会话状态机(01 §5.2)
//
// ⚠️★ 关于「认证」这一步在 IDL 里不存在,见 net/api.h 里 SessionState 上方的说明。
//    那不是本文件绕过去的问题,是一处**排期空白**,已在 docs 里登记。

#include "net/Api.h"

namespace SA::Net
{

const char *sessionStateName(SessionState s) noexcept
{
	switch (s)
	{
	case SessionState::kAnonymous:
		return "anonymous";
	case SessionState::kAuthenticating:
		return "authenticating";
	case SessionState::kAuthenticated:
		return "authenticated";
	case SessionState::kSelectingChar:
		return "selecting_char";
	case SessionState::kOnline:
		return "online";
	case SessionState::kLoggingOut:
		return "logging_out";
	case SessionState::kClosed:
		return "closed";
	}
	return "unknown";
}

Session::Session(SessionId id, std::uint32_t protocol_version,
                 std::uint32_t heartbeat_interval_ms,
                 SessionHost *host) noexcept
    : _id(id),
      _protocolVersion(protocol_version),
      _heartbeatIntervalMs(heartbeat_interval_ms),
      _host(host) {}

void Session::markOnline() noexcept
{
	if (_state == SessionState::kAuthenticated || _state == SessionState::kAuthenticating ||
	    _state == SessionState::kSelectingChar || _state == SessionState::kLoggingOut)
		_state = SessionState::kOnline;
}

void Session::close() noexcept
{
	if (_state == SessionState::kClosed)
		return;
	_state = SessionState::kClosed;
	if (_host != nullptr)
		_host->onSessionClosed(_id);
}

bool Session::handleFrame(const std::uint8_t *frame, std::uint32_t len,
                          std::vector<std::uint8_t> &out)
{
	if (_state == SessionState::kClosed)
		return false;

	EnvelopeView env;
	if (!decodeEnvelope(frame, len, env))
	{
		// 02 §2.1:解码失败是「整条消息作废」,不存在部分成功的中间态。
		_lastRejectMsgId = 0;
		return false;
	}
	++_framesHandled;

	const auto id = static_cast<SA::IDL::MsgId>(env.msg_id);

	// ★★ 握手之前只接受握手。
	//   02 §2.1 已裁定「协议版本不匹配时在握手阶段直接拒绝」——
	//   那要求握手**确实发生在最前面**,否则版本检查可以被绕过去:
	//   先发一条业务消息,服务端按当前版本解析,握手再也不会被检查。
	if (_state == SessionState::kAnonymous &&
	    id != SA::IDL::MsgId::HandshakeRequest)
	{
		_lastRejectMsgId = env.msg_id;
		return false;
	}

	switch (id)
	{
	case SA::IDL::MsgId::LoginRequest:
	case SA::IDL::MsgId::CreateCharacterRequest:
	case SA::IDL::MsgId::SelectCharacterRequest:
	case SA::IDL::MsgId::SaveRequest:
		return handleLifecycle(env);
	case SA::IDL::MsgId::HandshakeRequest:
		return handleHandshake(env, out);
	case SA::IDL::MsgId::Ping:
		return handlePing(env, out);
	case SA::IDL::MsgId::BattleCommand:
		return handleBattleCommand(env);
	case SA::IDL::MsgId::WalkRequest:
		return handleWalkRequest(env);
	case SA::IDL::MsgId::EventRequest:
		return handleEventRequest(env);
	default:
		// ⚠️ 未知或方向错的消息 ⇒ 协议违规,关闭连接。
		//   不"忽略并继续":那会让客户端的 bug 表现为"服务端没反应",
		//   而不是一个能被立刻看见的断连。
		_lastRejectMsgId = env.msg_id;
		return false;
	}
}

bool Session::handleLifecycle(const EnvelopeView &env)
{
	if (!env.corr_id || !_host)
		return false;
	SA::IDL::Reader reader(env.body, env.body_len);
	const auto id = static_cast<SA::IDL::MsgId>(env.msg_id);
	if (id == SA::IDL::MsgId::LoginRequest && _state == SessionState::kAuthenticated)
	{
		SA::Transport::LoginRequest req{};
		decode(reader, req);
		if (!reader.ok() || reader.remaining() != 0)
			return false;
		_state = SessionState::kAuthenticating;
		_host->onLogin(_id, req, env.corr_id);
		return true;
	}
	if (id == SA::IDL::MsgId::CreateCharacterRequest && _state == SessionState::kSelectingChar)
	{
		SA::Transport::CreateCharacterRequest req{};
		decode(reader, req);
		if (!reader.ok() || reader.remaining() != 0)
			return false;
		_state = SessionState::kAuthenticating;
		_host->onCreateCharacter(_id, req, env.corr_id);
		return true;
	}
	if (id == SA::IDL::MsgId::SelectCharacterRequest && _state == SessionState::kSelectingChar)
	{
		SA::Transport::SelectCharacterRequest req{};
		decode(reader, req);
		if (!reader.ok() || reader.remaining() != 0)
			return false;
		_state = SessionState::kAuthenticating;
		_host->onSelectCharacter(_id, req, env.corr_id);
		return true;
	}
	if (id == SA::IDL::MsgId::SaveRequest &&
	    (_state == SessionState::kOnline || _state == SessionState::kSelectingChar || _state == SessionState::kLoggingOut))
	{
		SA::Transport::SaveRequest req{};
		decode(reader, req);
		if (!reader.ok() || reader.remaining() != 0)
			return false;
		_host->onSave(_id, req, env.corr_id);
		return true;
	}
	_lastRejectMsgId = env.msg_id;
	return false;
}

bool Session::handleHandshake(const EnvelopeView &env,
                              std::vector<std::uint8_t> &out)
{
	// 重复握手是协议违规:握手改变会话状态,允许重放等于允许状态机被绕。
	if (_state != SessionState::kAnonymous)
	{
		_lastRejectMsgId = env.msg_id;
		return false;
	}

	SA::IDL::Reader r(env.body, env.body_len);
	SA::Transport::HandshakeRequest req;
	decode(r, req);
	if (!r.ok())
		return false;

	if (req.protocol_version != _protocolVersion)
	{
		// ★ 不做「主版本兼容、次版本忽略」的分支(02 §2.1)。不等即拒。
		SA::Transport::HandshakeRejected rej;
		rej.reason = SA::Transport::RejectReason::REJECT_VERSION_MISMATCH;
		rej.required_protocol_version = _protocolVersion;
		// ⚠️ 拒绝也要发出去再关 —— 否则客户端只看到断连,无从提示"请更新"。
		//   corr_id 原样回带,让客户端能把它对上自己那条请求(02 §1.3)。
		(void)encodeFramed(env.corr_id, rej, out);
		_state = SessionState::kClosed;
		if (_host != nullptr)
			_host->onSessionClosed(_id);
		return false;
	}

	SA::Transport::HandshakeAccepted acc;
	acc.session_id = _id;
	// ★ 心跳间隔由服务端下发,客户端不硬编码(handshake.proto 的原话)。
	acc.heartbeat_interval_ms = _heartbeatIntervalMs;
	if (!encodeFramed(env.corr_id, acc, out))
		return false;

	_state = SessionState::kAuthenticated;
	if (_host != nullptr)
		_host->onSessionReady(_id);
	return true;
}

bool Session::handlePing(const EnvelopeView &env,
                         std::vector<std::uint8_t> &out)
{
	SA::IDL::Reader r(env.body, env.body_len);
	SA::Transport::Ping ping;
	decode(r, ping);
	if (!r.ok())
		return false;

	SA::Transport::Pong pong;
	pong.client_time_ms = ping.client_time_ms; // 原样回带,客户端据此算 RTT
	// ⚠️ server_time_ms 留 0:本模块**没有时钟** —— 时钟是 platform 的东西,
	//    而 net 依赖 platform 只为了一个时间戳,会把 L1 的依赖面撑大。
	//    ⇒ 阶段 2 接入时由宿主填,或把 Pong 的产出上移到 world。
	//      现在留 0 而不是随手 time(nullptr):01 §3.1「不用墙钟做逻辑判断」,
	//      而一个"看起来有值其实是墙钟"的字段比 0 更难查。
	pong.server_time_ms = 0;
	return encodeFramed(env.corr_id, pong, out);
}

bool Session::handleBattleCommand(const EnvelopeView &env)
{
	// ⚠️ 只有在世的会话能下指令。原版把这类校验散在各处,
	//    这里集中在状态机上 —— 02 §5.5「回执侧的两条强校验必须保留」同一取向。
	if (_state != SessionState::kOnline)
	{
		_lastRejectMsgId = env.msg_id;
		return false;
	}

	SA::IDL::Reader r(env.body, env.body_len);
	SA::Domain::BattleCommand cmd;
	decode(r, cmd);
	if (!r.ok())
		return false;

	// ★ 指令的**玩法**合法性(能不能行动、学没学过这个技能)不在这里判 ——
	//   shared/rules/battle.h 明写 L3 的输入是"已通过合法性校验"的指令,
	//   而 DR-BT5 把「能否行动」统一到 Rules::CheckCanAct 这一个真源。
	//   net 只负责"这条消息在这个状态下允不允许出现"。
	if (_host != nullptr)
		_host->onBattleCommand(_id, cmd);
	return true;
}

bool Session::handleWalkRequest(const EnvelopeView &env)
{
	// ⚠️ 只有在世(kOnline)的会话能走路 —— 同 handleBattleCommand 的取向。
	if (_state != SessionState::kOnline)
	{
		_lastRejectMsgId = env.msg_id;
		return false;
	}

	SA::IDL::Reader r(env.body, env.body_len);
	SA::Domain::WalkRequest req;
	decode(r, req);
	if (!r.ok())
		return false;

	// ★ 防瞬移与碰撞校验是**世界态**判定(要读地图与角色位置)⇒ 归宿主的 onWalk,
	//   net 只负责"这条消息在这个状态下允不允许出现"(同 onBattleCommand)。
	if (_host != nullptr)
		_host->onWalk(_id, req);
	return true;
}

bool Session::handleEventRequest(const EnvelopeView &env)
{
	// ⚠️ 只有在世(kOnline)的会话能触发事件 —— 同 handleWalkRequest 的取向。
	if (_state != SessionState::kOnline)
	{
		_lastRejectMsgId = env.msg_id;
		return false;
	}

	SA::IDL::Reader r(env.body, env.body_len);
	SA::Domain::EventRequest req;
	decode(r, req);
	if (!r.ok())
		return false;

	// ★ 事件命中判定(面前格有没有明雷)是**世界态**判定 ⇒ 归宿主的 onEvent;
	//   回执 EventResult 由 world 侧经会话下推(靠 seqno 关联,同原版 EV_send),net 不管回执。
	if (_host != nullptr)
		_host->onEvent(_id, req);
	return true;
}

} // namespace SA::Net
