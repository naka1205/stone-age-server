// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：transport/login.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/saidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SA_IDL_TRANSPORT_LOGIN_SA_H
#define SA_IDL_TRANSPORT_LOGIN_SA_H

#include "sa_idl_runtime.h"
#include "domain/character_data.sa.h"

namespace SA {
namespace Transport {

enum class AccountCode : std::uint32_t {
  ACCOUNT_OK = 0,
  ACCOUNT_INVALID = 1,
  ACCOUNT_CREDENTIALS = 2,
  ACCOUNT_BUSY = 3,
  ACCOUNT_UNAVAILABLE = 4,
  ACCOUNT_CONFLICT = 5,
  ACCOUNT_NOT_FOUND = 6,
  ACCOUNT_IN_BATTLE = 7,
  ACCOUNT_LEASE_LOST = 8,
};

struct LoginRequest {
  SA::IDL::FixedStr<63> login;
  SA::IDL::FixedStr<127> password;
  bool register_account;
};

inline void encode(SA::IDL::Writer& w, const LoginRequest& m) {
  SA::IDL::write_str(w, m.login);
  SA::IDL::write_str(w, m.password);
  w.b(m.register_account);
}

inline void decode(SA::IDL::Reader& r, LoginRequest& m) {
  SA::IDL::read_str(r, m.login);
  SA::IDL::read_str(r, m.password);
  m.register_account = r.b();
}

struct CharacterSummary {
  std::uint64_t char_id;
  SA::IDL::FixedStr<31> name;
  std::int32_t level;
  std::int32_t image;
};

inline void encode(SA::IDL::Writer& w, const CharacterSummary& m) {
  w.u64(m.char_id);
  SA::IDL::write_str(w, m.name);
  w.i32(m.level);
  w.i32(m.image);
}

inline void decode(SA::IDL::Reader& r, CharacterSummary& m) {
  m.char_id = r.u64();
  SA::IDL::read_str(r, m.name);
  m.level = r.i32();
  m.image = r.i32();
}

struct LoginResult {
  SA::Transport::AccountCode code;
  SA::IDL::FixedVec<SA::Transport::CharacterSummary, 2> characters;
};

inline void encode(SA::IDL::Writer& w, const LoginResult& m) {
  w.u32(static_cast<std::uint32_t>(m.code));
  SA::IDL::write_vec(w, m.characters,
      [](SA::IDL::Writer& we, const SA::Transport::CharacterSummary& e) { encode(we, e); });
}

inline void decode(SA::IDL::Reader& r, LoginResult& m) {
  m.code = static_cast<SA::Transport::AccountCode>(r.u32());
  SA::IDL::read_vec(r, m.characters,
      [](SA::IDL::Reader& re, SA::Transport::CharacterSummary& e) { decode(re, e); });
}

struct CreateCharacterRequest {
  SA::IDL::FixedStr<31> name;
  std::int32_t image;
  std::int32_t vital;
  std::int32_t str;
  std::int32_t tough;
  std::int32_t dex;
  std::int32_t earth;
  std::int32_t water;
  std::int32_t fire;
  std::int32_t wind;
};

inline void encode(SA::IDL::Writer& w, const CreateCharacterRequest& m) {
  SA::IDL::write_str(w, m.name);
  w.i32(m.image);
  w.i32(m.vital);
  w.i32(m.str);
  w.i32(m.tough);
  w.i32(m.dex);
  w.i32(m.earth);
  w.i32(m.water);
  w.i32(m.fire);
  w.i32(m.wind);
}

inline void decode(SA::IDL::Reader& r, CreateCharacterRequest& m) {
  SA::IDL::read_str(r, m.name);
  m.image = r.i32();
  m.vital = r.i32();
  m.str = r.i32();
  m.tough = r.i32();
  m.dex = r.i32();
  m.earth = r.i32();
  m.water = r.i32();
  m.fire = r.i32();
  m.wind = r.i32();
}

struct SelectCharacterRequest {
  std::uint64_t char_id;
};

inline void encode(SA::IDL::Writer& w, const SelectCharacterRequest& m) {
  w.u64(m.char_id);
}

inline void decode(SA::IDL::Reader& r, SelectCharacterRequest& m) {
  m.char_id = r.u64();
}

struct CharacterResult {
  SA::Transport::AccountCode code;
  SA::Domain::CharacterRecord character;
  SA::IDL::FixedStr<63> content_version;
};

inline void encode(SA::IDL::Writer& w, const CharacterResult& m) {
  w.u32(static_cast<std::uint32_t>(m.code));
  encode(w, m.character);
  SA::IDL::write_str(w, m.content_version);
}

inline void decode(SA::IDL::Reader& r, CharacterResult& m) {
  m.code = static_cast<SA::Transport::AccountCode>(r.u32());
  decode(r, m.character);
  SA::IDL::read_str(r, m.content_version);
}

struct SaveRequest {
  bool logout;
};

inline void encode(SA::IDL::Writer& w, const SaveRequest& m) {
  w.b(m.logout);
}

inline void decode(SA::IDL::Reader& r, SaveRequest& m) {
  m.logout = r.b();
}

struct SaveResult {
  SA::Transport::AccountCode code;
  std::uint64_t revision;
  bool logout;
};

inline void encode(SA::IDL::Writer& w, const SaveResult& m) {
  w.u32(static_cast<std::uint32_t>(m.code));
  w.u64(m.revision);
  w.b(m.logout);
}

inline void decode(SA::IDL::Reader& r, SaveResult& m) {
  m.code = static_cast<SA::Transport::AccountCode>(r.u32());
  m.revision = r.u64();
  m.logout = r.b();
}

struct CharacterState {
  SA::Domain::CharacterRecord character;
};

inline void encode(SA::IDL::Writer& w, const CharacterState& m) {
  encode(w, m.character);
}

inline void decode(SA::IDL::Reader& r, CharacterState& m) {
  decode(r, m.character);
}

}  // namespace Transport
}  // namespace SA

#endif  // SA_IDL_TRANSPORT_LOGIN_SA_H
