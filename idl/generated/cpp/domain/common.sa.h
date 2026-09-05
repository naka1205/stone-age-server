// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：domain/common.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/saidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SA_IDL_DOMAIN_COMMON_SA_H
#define SA_IDL_DOMAIN_COMMON_SA_H

#include "sa_idl_runtime.h"

namespace sa {
namespace domain {

enum class Direction : std::uint8_t {
  DIR_NORTH = 0,
  DIR_NORTH_EAST = 1,
  DIR_EAST = 2,
  DIR_SOUTH_EAST = 3,
  DIR_SOUTH = 4,
  DIR_SOUTH_WEST = 5,
  DIR_WEST = 6,
  DIR_NORTH_WEST = 7,
};

enum class EntitySource : std::uint32_t {
  ENTITY_SOURCE_INVALID = 0,
  ENTITY_SOURCE_SYSTEM = 1,
  ENTITY_SOURCE_ENTITY = 2,
};

struct Vec2 {
  std::int32_t x;
  std::int32_t y;
};

inline void encode(sa::idl::Writer& w, const Vec2& m) {
  w.i32(m.x);
  w.i32(m.y);
}

inline void decode(sa::idl::Reader& r, Vec2& m) {
  m.x = r.i32();
  m.y = r.i32();
}

struct EntityRef {
  sa::domain::EntitySource source;
  std::uint32_t entity_id;
};

inline void encode(sa::idl::Writer& w, const EntityRef& m) {
  w.u32(static_cast<std::uint32_t>(m.source));
  w.u32(m.entity_id);
}

inline void decode(sa::idl::Reader& r, EntityRef& m) {
  m.source = static_cast<sa::domain::EntitySource>(r.u32());
  m.entity_id = r.u32();
}

}  // namespace domain
}  // namespace sa

#endif  // SA_IDL_DOMAIN_COMMON_SA_H
