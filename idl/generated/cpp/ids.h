// ★ 本文件由 idl/codegen 生成，请勿手工编辑。
//
// 02-protocol.md §1.1：每个 message 的编号在 IDL 里写死数字，
// 不依赖声明顺序；编号一经发布不得复用。

#ifndef SA_IDL_IDS_H
#define SA_IDL_IDS_H

#include <cstdint>

namespace SA {
namespace IDL {

enum class MsgId : std::uint32_t {
  HandshakeRequest = 0x0001,
  HandshakeAccepted = 0x0002,
  HandshakeRejected = 0x0003,
  Ping = 0x0004,
  Pong = 0x0005,
  BattleSnapshot = 0x0201,
  BattleTurnBegin = 0x0202,
  BattleSelfInfo = 0x0203,
  BattleLeave = 0x0204,
  BattleEvents = 0x0205,
  BattleResult = 0x0206,
  BattleCommand = 0x0210,
  WalkRequest = 0x0301,
  CharAppear = 0x0302,
  CharMove = 0x0303,
  CharDisappear = 0x0304,
  WindowOpen = 0x0601,
  WindowReply = 0x0602,
};

// 编译期把消息类型映射到编号：msg_id_of<SA::Domain::Foo>()
template <typename T>
struct MsgTraits;

template <typename T>
constexpr std::uint32_t msg_id_of() {
  return static_cast<std::uint32_t>(MsgTraits<T>::kId);
}

}  // namespace IDL
}  // namespace SA

#include "transport/handshake.sa.h"
#include "domain/battle_events.sa.h"
#include "domain/world_map.sa.h"
#include "domain/window.sa.h"

namespace SA {
namespace IDL {

template <>
struct MsgTraits<SA::Transport::HandshakeRequest> {
  static constexpr MsgId kId = MsgId::HandshakeRequest;
  static constexpr const char* kName = "sa.transport.HandshakeRequest";
};

template <>
struct MsgTraits<SA::Transport::HandshakeAccepted> {
  static constexpr MsgId kId = MsgId::HandshakeAccepted;
  static constexpr const char* kName = "sa.transport.HandshakeAccepted";
};

template <>
struct MsgTraits<SA::Transport::HandshakeRejected> {
  static constexpr MsgId kId = MsgId::HandshakeRejected;
  static constexpr const char* kName = "sa.transport.HandshakeRejected";
};

template <>
struct MsgTraits<SA::Transport::Ping> {
  static constexpr MsgId kId = MsgId::Ping;
  static constexpr const char* kName = "sa.transport.Ping";
};

template <>
struct MsgTraits<SA::Transport::Pong> {
  static constexpr MsgId kId = MsgId::Pong;
  static constexpr const char* kName = "sa.transport.Pong";
};

template <>
struct MsgTraits<SA::Domain::BattleSnapshot> {
  static constexpr MsgId kId = MsgId::BattleSnapshot;
  static constexpr const char* kName = "sa.domain.BattleSnapshot";
};

template <>
struct MsgTraits<SA::Domain::BattleTurnBegin> {
  static constexpr MsgId kId = MsgId::BattleTurnBegin;
  static constexpr const char* kName = "sa.domain.BattleTurnBegin";
};

template <>
struct MsgTraits<SA::Domain::BattleSelfInfo> {
  static constexpr MsgId kId = MsgId::BattleSelfInfo;
  static constexpr const char* kName = "sa.domain.BattleSelfInfo";
};

template <>
struct MsgTraits<SA::Domain::BattleLeave> {
  static constexpr MsgId kId = MsgId::BattleLeave;
  static constexpr const char* kName = "sa.domain.BattleLeave";
};

template <>
struct MsgTraits<SA::Domain::BattleEvents> {
  static constexpr MsgId kId = MsgId::BattleEvents;
  static constexpr const char* kName = "sa.domain.BattleEvents";
};

template <>
struct MsgTraits<SA::Domain::BattleResult> {
  static constexpr MsgId kId = MsgId::BattleResult;
  static constexpr const char* kName = "sa.domain.BattleResult";
};

template <>
struct MsgTraits<SA::Domain::BattleCommand> {
  static constexpr MsgId kId = MsgId::BattleCommand;
  static constexpr const char* kName = "sa.domain.BattleCommand";
};

template <>
struct MsgTraits<SA::Domain::WalkRequest> {
  static constexpr MsgId kId = MsgId::WalkRequest;
  static constexpr const char* kName = "sa.domain.WalkRequest";
};

template <>
struct MsgTraits<SA::Domain::CharAppear> {
  static constexpr MsgId kId = MsgId::CharAppear;
  static constexpr const char* kName = "sa.domain.CharAppear";
};

template <>
struct MsgTraits<SA::Domain::CharMove> {
  static constexpr MsgId kId = MsgId::CharMove;
  static constexpr const char* kName = "sa.domain.CharMove";
};

template <>
struct MsgTraits<SA::Domain::CharDisappear> {
  static constexpr MsgId kId = MsgId::CharDisappear;
  static constexpr const char* kName = "sa.domain.CharDisappear";
};

template <>
struct MsgTraits<SA::Domain::WindowOpen> {
  static constexpr MsgId kId = MsgId::WindowOpen;
  static constexpr const char* kName = "sa.domain.WindowOpen";
};

template <>
struct MsgTraits<SA::Domain::WindowReply> {
  static constexpr MsgId kId = MsgId::WindowReply;
  static constexpr const char* kName = "sa.domain.WindowReply";
};

}  // namespace IDL
}  // namespace SA

#endif  // SA_IDL_IDS_H
