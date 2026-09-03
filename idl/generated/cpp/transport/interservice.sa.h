// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：transport/interservice.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/saidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SA_IDL_TRANSPORT_INTERSERVICE_SA_H
#define SA_IDL_TRANSPORT_INTERSERVICE_SA_H

#include "sa_idl_runtime.h"
#include "transport/errors.sa.h"

namespace sa {
namespace transport {

struct RequestHeader {
  std::uint32_t instance_id;
  std::uint32_t generation;
  std::uint64_t request_id;
  std::uint32_t deadline_ms;
};

inline void encode(sa::idl::Writer& w, const RequestHeader& m) {
  w.u32(m.instance_id);
  w.u32(m.generation);
  w.u64(m.request_id);
  w.u32(m.deadline_ms);
}

inline void decode(sa::idl::Reader& r, RequestHeader& m) {
  m.instance_id = r.u32();
  if (!r.ok()) return;
  m.generation = r.u32();
  if (!r.ok()) return;
  m.request_id = r.u64();
  if (!r.ok()) return;
  m.deadline_ms = r.u32();
  if (!r.ok()) return;
}

struct ResponseHeader {
  std::uint64_t request_id;
  sa::transport::Status status;
  std::uint32_t error_code;
};

inline void encode(sa::idl::Writer& w, const ResponseHeader& m) {
  w.u64(m.request_id);
  w.u8(static_cast<std::uint8_t>(m.status));
  w.u32(m.error_code);
}

inline void decode(sa::idl::Reader& r, ResponseHeader& m) {
  m.request_id = r.u64();
  if (!r.ok()) return;
  m.status = static_cast<sa::transport::Status>(r.u8());
  if (!r.ok()) return;
  m.error_code = r.u32();
  if (!r.ok()) return;
}

struct Ack {
  bool accepted;
};

inline void encode(sa::idl::Writer& w, const Ack& m) {
  w.b(m.accepted);
}

inline void decode(sa::idl::Reader& r, Ack& m) {
  m.accepted = r.b();
  if (!r.ok()) return;
}

}  // namespace transport
}  // namespace sa

#endif  // SA_IDL_TRANSPORT_INTERSERVICE_SA_H
