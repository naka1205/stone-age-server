// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：transport/handshake.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/saidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SA_IDL_TRANSPORT_HANDSHAKE_SA_H
#define SA_IDL_TRANSPORT_HANDSHAKE_SA_H

#include "sa_idl_runtime.h"

namespace sa {
namespace transport {

enum class RejectReason : std::uint8_t {
  REJECT_UNSPECIFIED = 0,
  REJECT_VERSION_MISMATCH = 1,
  REJECT_SERVER_FULL = 2,
  REJECT_MAINTENANCE = 3,
  REJECT_BANNED = 4,
};

struct HandshakeRequest {
  std::uint32_t protocol_version;
  sa::idl::FixedStr<63> client_build;
};

inline void encode(sa::idl::Writer& w, const HandshakeRequest& m) {
  w.u32(m.protocol_version);
  sa::idl::write_str(w, m.client_build);
}

inline void decode(sa::idl::Reader& r, HandshakeRequest& m) {
  m.protocol_version = r.u32();
  sa::idl::read_str(r, m.client_build);
}

struct HandshakeAccepted {
  std::uint64_t session_id;
  std::uint32_t heartbeat_interval_ms;
};

inline void encode(sa::idl::Writer& w, const HandshakeAccepted& m) {
  w.u64(m.session_id);
  w.u32(m.heartbeat_interval_ms);
}

inline void decode(sa::idl::Reader& r, HandshakeAccepted& m) {
  m.session_id = r.u64();
  m.heartbeat_interval_ms = r.u32();
}

struct HandshakeRejected {
  sa::transport::RejectReason reason;
  std::uint32_t required_protocol_version;
};

inline void encode(sa::idl::Writer& w, const HandshakeRejected& m) {
  w.u8(static_cast<std::uint8_t>(m.reason));
  w.u32(m.required_protocol_version);
}

inline void decode(sa::idl::Reader& r, HandshakeRejected& m) {
  m.reason = static_cast<sa::transport::RejectReason>(r.u8());
  m.required_protocol_version = r.u32();
}

struct Ping {
  std::uint64_t client_time_ms;
};

inline void encode(sa::idl::Writer& w, const Ping& m) {
  w.u64(m.client_time_ms);
}

inline void decode(sa::idl::Reader& r, Ping& m) {
  m.client_time_ms = r.u64();
}

struct Pong {
  std::uint64_t client_time_ms;
  std::uint64_t server_time_ms;
};

inline void encode(sa::idl::Writer& w, const Pong& m) {
  w.u64(m.client_time_ms);
  w.u64(m.server_time_ms);
}

inline void decode(sa::idl::Reader& r, Pong& m) {
  m.client_time_ms = r.u64();
  m.server_time_ms = r.u64();
}

}  // namespace transport
}  // namespace sa

#endif  // SA_IDL_TRANSPORT_HANDSHAKE_SA_H
