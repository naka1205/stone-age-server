// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：domain/world_map.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/saidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SA_IDL_DOMAIN_WORLD_MAP_SA_H
#define SA_IDL_DOMAIN_WORLD_MAP_SA_H

#include "sa_idl_runtime.h"

namespace SA {
namespace Domain {

enum class EntityType : std::uint32_t {
  ENTITY_PLAYER = 0,
  ENTITY_ENEMY = 1,
};

struct WalkRequest {
  std::int32_t x;
  std::int32_t y;
  SA::IDL::FixedStr<32> direction;
};

inline void encode(SA::IDL::Writer& w, const WalkRequest& m) {
  w.i32(m.x);
  w.i32(m.y);
  SA::IDL::write_str(w, m.direction);
}

inline void decode(SA::IDL::Reader& r, WalkRequest& m) {
  m.x = r.i32();
  m.y = r.i32();
  SA::IDL::read_str(r, m.direction);
}

struct CharAppear {
  std::uint64_t entity_id;
  std::int32_t floor;
  std::int32_t x;
  std::int32_t y;
  std::uint32_t dir;
  std::uint32_t entity_type;
  std::int32_t image;
};

inline void encode(SA::IDL::Writer& w, const CharAppear& m) {
  w.u64(m.entity_id);
  w.i32(m.floor);
  w.i32(m.x);
  w.i32(m.y);
  w.u32(m.dir);
  w.u32(m.entity_type);
  w.i32(m.image);
}

inline void decode(SA::IDL::Reader& r, CharAppear& m) {
  m.entity_id = r.u64();
  m.floor = r.i32();
  m.x = r.i32();
  m.y = r.i32();
  m.dir = r.u32();
  m.entity_type = r.u32();
  m.image = r.i32();
}

struct CharMove {
  std::uint64_t entity_id;
  std::int32_t x;
  std::int32_t y;
  std::uint32_t dir;
  std::uint32_t entity_type;
};

inline void encode(SA::IDL::Writer& w, const CharMove& m) {
  w.u64(m.entity_id);
  w.i32(m.x);
  w.i32(m.y);
  w.u32(m.dir);
  w.u32(m.entity_type);
}

inline void decode(SA::IDL::Reader& r, CharMove& m) {
  m.entity_id = r.u64();
  m.x = r.i32();
  m.y = r.i32();
  m.dir = r.u32();
  m.entity_type = r.u32();
}

struct CharDisappear {
  std::uint64_t entity_id;
  std::uint32_t entity_type;
};

inline void encode(SA::IDL::Writer& w, const CharDisappear& m) {
  w.u64(m.entity_id);
  w.u32(m.entity_type);
}

inline void decode(SA::IDL::Reader& r, CharDisappear& m) {
  m.entity_id = r.u64();
  m.entity_type = r.u32();
}

struct EventRequest {
  std::int32_t x;
  std::int32_t y;
  std::uint32_t dir;
  std::uint32_t event_type;
  std::uint32_t seqno;
};

inline void encode(SA::IDL::Writer& w, const EventRequest& m) {
  w.i32(m.x);
  w.i32(m.y);
  w.u32(m.dir);
  w.u32(m.event_type);
  w.u32(m.seqno);
}

inline void decode(SA::IDL::Reader& r, EventRequest& m) {
  m.x = r.i32();
  m.y = r.i32();
  m.dir = r.u32();
  m.event_type = r.u32();
  m.seqno = r.u32();
}

struct EventResult {
  std::uint32_t seqno;
  bool ok;
};

inline void encode(SA::IDL::Writer& w, const EventResult& m) {
  w.u32(m.seqno);
  w.b(m.ok);
}

inline void decode(SA::IDL::Reader& r, EventResult& m) {
  m.seqno = r.u32();
  m.ok = r.b();
}

}  // namespace Domain
}  // namespace SA

#endif  // SA_IDL_DOMAIN_WORLD_MAP_SA_H
