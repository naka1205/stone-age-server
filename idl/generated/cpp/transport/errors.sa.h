// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：transport/errors.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/sgidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SG_IDL_TRANSPORT_ERRORS_SG_H
#define SG_IDL_TRANSPORT_ERRORS_SG_H

#include "sg_idl_runtime.h"

namespace sg {
namespace transport {

enum class Status : std::uint8_t {
  STATUS_OK = 0,
  STATUS_INVALID_ARGUMENT = 1,
  STATUS_NOT_FOUND = 2,
  STATUS_CONFLICT = 3,
  STATUS_PERMISSION_DENIED = 4,
  STATUS_RESOURCE_EXHAUSTED = 5,
  STATUS_FAILED_PRECONDITION = 6,
  STATUS_ABORTED = 7,
  STATUS_DEADLINE_EXCEEDED = 8,
  STATUS_UNAVAILABLE = 9,
  STATUS_INTERNAL = 10,
};

enum class Disposition : std::uint8_t {
  DISPOSITION_UNSPECIFIED = 0,
  DISPOSITION_RETRY = 1,
  DISPOSITION_COMPENSATE = 2,
  DISPOSITION_ABORT = 3,
};

}  // namespace transport
}  // namespace sg

#endif  // SG_IDL_TRANSPORT_ERRORS_SG_H
