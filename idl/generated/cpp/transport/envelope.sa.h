// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：transport/envelope.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/saidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SA_IDL_TRANSPORT_ENVELOPE_SA_H
#define SA_IDL_TRANSPORT_ENVELOPE_SA_H

#include "sa_idl_runtime.h"

namespace sa {
namespace transport {

struct EnvelopeHeader {
  std::uint32_t msg_id;
  std::uint64_t corr_id;
};

inline void encode(sa::idl::Writer& w, const EnvelopeHeader& m) {
  w.u32(m.msg_id);
  w.u64(m.corr_id);
}

inline void decode(sa::idl::Reader& r, EnvelopeHeader& m) {
  m.msg_id = r.u32();
  if (!r.ok()) return;
  m.corr_id = r.u64();
  if (!r.ok()) return;
}

}  // namespace transport
}  // namespace sa

#endif  // SA_IDL_TRANSPORT_ENVELOPE_SA_H
