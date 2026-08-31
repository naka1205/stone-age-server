// ★ 本文件由 idl/codegen 生成，请勿手工编辑。
//
// 02-protocol.md §1.1：每个 message 的编号在 IDL 里写死数字，
// 不依赖声明顺序；编号一经发布不得复用。

#ifndef SG_IDL_IDS_H
#define SG_IDL_IDS_H

#include <cstdint>

namespace sg {
namespace idl {

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
  BattleCommand = 0x0210,
  WindowOpen = 0x0601,
  WindowReply = 0x0602,
};

// 编译期把消息类型映射到编号：msg_id_of<sg::domain::Foo>()
template <typename T>
struct MsgTraits;

template <typename T>
constexpr std::uint32_t msg_id_of() {
  return static_cast<std::uint32_t>(MsgTraits<T>::kId);
}

}  // namespace idl
}  // namespace sg

#include "transport/handshake.sg.h"
#include "domain/battle_events.sg.h"
#include "domain/window.sg.h"

namespace sg {
namespace idl {

template <>
struct MsgTraits<sg::transport::HandshakeRequest> {
  static constexpr MsgId kId = MsgId::HandshakeRequest;
  static constexpr const char* kName = "sg.transport.HandshakeRequest";
};

template <>
struct MsgTraits<sg::transport::HandshakeAccepted> {
  static constexpr MsgId kId = MsgId::HandshakeAccepted;
  static constexpr const char* kName = "sg.transport.HandshakeAccepted";
};

template <>
struct MsgTraits<sg::transport::HandshakeRejected> {
  static constexpr MsgId kId = MsgId::HandshakeRejected;
  static constexpr const char* kName = "sg.transport.HandshakeRejected";
};

template <>
struct MsgTraits<sg::transport::Ping> {
  static constexpr MsgId kId = MsgId::Ping;
  static constexpr const char* kName = "sg.transport.Ping";
};

template <>
struct MsgTraits<sg::transport::Pong> {
  static constexpr MsgId kId = MsgId::Pong;
  static constexpr const char* kName = "sg.transport.Pong";
};

template <>
struct MsgTraits<sg::domain::BattleSnapshot> {
  static constexpr MsgId kId = MsgId::BattleSnapshot;
  static constexpr const char* kName = "sg.domain.BattleSnapshot";
};

template <>
struct MsgTraits<sg::domain::BattleTurnBegin> {
  static constexpr MsgId kId = MsgId::BattleTurnBegin;
  static constexpr const char* kName = "sg.domain.BattleTurnBegin";
};

template <>
struct MsgTraits<sg::domain::BattleSelfInfo> {
  static constexpr MsgId kId = MsgId::BattleSelfInfo;
  static constexpr const char* kName = "sg.domain.BattleSelfInfo";
};

template <>
struct MsgTraits<sg::domain::BattleLeave> {
  static constexpr MsgId kId = MsgId::BattleLeave;
  static constexpr const char* kName = "sg.domain.BattleLeave";
};

template <>
struct MsgTraits<sg::domain::BattleEvents> {
  static constexpr MsgId kId = MsgId::BattleEvents;
  static constexpr const char* kName = "sg.domain.BattleEvents";
};

template <>
struct MsgTraits<sg::domain::BattleCommand> {
  static constexpr MsgId kId = MsgId::BattleCommand;
  static constexpr const char* kName = "sg.domain.BattleCommand";
};

template <>
struct MsgTraits<sg::domain::WindowOpen> {
  static constexpr MsgId kId = MsgId::WindowOpen;
  static constexpr const char* kName = "sg.domain.WindowOpen";
};

template <>
struct MsgTraits<sg::domain::WindowReply> {
  static constexpr MsgId kId = MsgId::WindowReply;
  static constexpr const char* kName = "sg.domain.WindowReply";
};

}  // namespace idl
}  // namespace sg

#endif  // SG_IDL_IDS_H
