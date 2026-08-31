// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：domain/battle_events.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/sgidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SG_IDL_DOMAIN_BATTLE_EVENTS_SG_H
#define SG_IDL_DOMAIN_BATTLE_EVENTS_SG_H

#include "sg_idl_runtime.h"
#include "domain/battle_status.sg.h"

namespace sg {
namespace domain {

enum class DamageFlag : std::uint32_t {
  DAMAGE_FLAG_NONE = 0,
  DAMAGE_FLAG_DEATH = 1,
  DAMAGE_FLAG_NORMAL = 2,
  DAMAGE_FLAG_CRITICAL = 4,
  DAMAGE_FLAG_GUARD = 8,
  DAMAGE_FLAG_COUNTER = 16,
  DAMAGE_FLAG_DODGE = 32,
  DAMAGE_FLAG_ULTIMATE_1 = 64,
  DAMAGE_FLAG_ULTIMATE_2 = 128,
  DAMAGE_FLAG_GBREAK = 256,
  DAMAGE_FLAG_GUARDIAN = 512,
  DAMAGE_FLAG_REFLECT = 1024,
  DAMAGE_FLAG_ABSORB = 2048,
  DAMAGE_FLAG_VANISH = 4096,
  DAMAGE_FLAG_CRUSH = 8192,
  DAMAGE_FLAG_FALL = 16384,
  DAMAGE_FLAG_TOOTH = 32768,
  DAMAGE_FLAG_ATT_DOUBLE = 65536,
  DAMAGE_FLAG_ROAR = 131072,
  DAMAGE_FLAG_DEFMAGICATT = 524288,
  DAMAGE_FLAG_SUPERWALL = 1048576,
  DAMAGE_FLAG_MODIFY = 2097152,
  DAMAGE_FLAG_F_SKILLACT = 4194304,
  DAMAGE_FLAG_B_SKILLACT = 8388608,
  DAMAGE_FLAG_B_ARRANGE = 16777216,
  DAMAGE_FLAG_DOUBLE_ATTACK = 67108864,
  DAMAGE_FLAG_ACUPUNCTURE = 134217728,
  DAMAGE_FLAG_ANTINTER = 268435456,
  DAMAGE_FLAG_EXPLODE = 536870912,
};

enum class CombatantFlag : std::uint32_t {
  COMBATANT_FLAG_NONE = 0,
  COMBATANT_FLAG_NEW = 1,
  COMBATANT_FLAG_DEAD = 2,
  COMBATANT_FLAG_PLAYER = 4,
  COMBATANT_FLAG_POISON = 8,
  COMBATANT_FLAG_PARALYSIS = 16,
  COMBATANT_FLAG_SLEEP = 32,
  COMBATANT_FLAG_STONE = 64,
  COMBATANT_FLAG_DRUNK = 128,
  COMBATANT_FLAG_CONFUSION = 256,
  COMBATANT_FLAG_HIDE = 512,
  COMBATANT_FLAG_REVERSE = 1024,
  COMBATANT_FLAG_WEAKEN = 2048,
  COMBATANT_FLAG_DEEPPOISON = 4096,
  COMBATANT_FLAG_BARRIER = 8192,
  COMBATANT_FLAG_NOCAST = 16384,
  COMBATANT_FLAG_SARS = 32768,
  COMBATANT_FLAG_DIZZY = 65536,
  COMBATANT_FLAG_ENTWINE = 131072,
  COMBATANT_FLAG_DRAGNET = 262144,
  COMBATANT_FLAG_ICECRACK = 524288,
  COMBATANT_FLAG_OBLIVION = 1048576,
  COMBATANT_FLAG_ICEARROW = 2097152,
  COMBATANT_FLAG_BLOODWORMS = 4194304,
  COMBATANT_FLAG_SIGN = 8388608,
  COMBATANT_FLAG_INSTIGATE = 16777216,
  COMBATANT_FLAG_F_ENCLOSE = 33554432,
  COMBATANT_FLAG_I_ENCLOSE = 67108864,
  COMBATANT_FLAG_T_ENCLOSE = 134217728,
  COMBATANT_FLAG_WATER = 268435456,
  COMBATANT_FLAG_FEAR = 536870912,
  COMBATANT_FLAG_CHANGE = 1073741824,
};

enum class MenuFlag : std::uint8_t {
  MENU_FLAG_NONE = 0,
  MENU_FLAG_JOIN = 1,
  MENU_FLAG_PLAYER_MENU_OFF = 2,
  MENU_FLAG_BOOMERANG = 4,
  MENU_FLAG_PET_MENU_OFF = 8,
  MENU_FLAG_ENEMY_SURPRISAL = 16,
  MENU_FLAG_PLAYER_SURPRISAL = 32,
};

enum class RideState : std::uint8_t {
  RIDE_STATE_NONE = 0,
  RIDE_STATE_RIDING = 1,
  RIDE_STATE_FELL = 2,
  RIDE_STATE_FELL_FOX = 3,
  RIDE_STATE_FELL_PIG = 4,
};

enum class AttackKind : std::uint8_t {
  ATTACK_KIND_INVALID = 0,
  ATTACK_KIND_MELEE = 1,
  ATTACK_KIND_RANGED = 2,
  ATTACK_KIND_PROF_SKILL = 3,
  ATTACK_KIND_SPELL = 4,
  ATTACK_KIND_DEEP_POISON = 5,
  ATTACK_KIND_BLOOD_DRAIN = 6,
  ATTACK_KIND_MP_DAMAGE = 7,
};

struct CombatantState {
  std::uint32_t slot;
  sg::idl::FixedStr<127> name;
  sg::idl::FixedStr<127> title;
  std::uint32_t image_id;
  std::uint32_t level;
  std::int32_t hp;
  std::int32_t max_hp;
  std::uint32_t flags;
  sg::domain::RideState ride;
  sg::idl::FixedStr<127> pet_name;
  std::uint32_t pet_level;
  std::int32_t pet_hp;
  std::int32_t pet_max_hp;
};

inline void encode(sg::idl::Writer& w, const CombatantState& m) {
  w.u32(m.slot);
  sg::idl::write_str(w, m.name);
  sg::idl::write_str(w, m.title);
  w.u32(m.image_id);
  w.u32(m.level);
  w.i32(m.hp);
  w.i32(m.max_hp);
  w.u32(m.flags);
  w.u8(static_cast<std::uint8_t>(m.ride));
  sg::idl::write_str(w, m.pet_name);
  w.u32(m.pet_level);
  w.i32(m.pet_hp);
  w.i32(m.pet_max_hp);
}

inline void decode(sg::idl::Reader& r, CombatantState& m) {
  m.slot = r.u32();
  if (!r.ok()) return;
  sg::idl::read_str(r, m.name);
  if (!r.ok()) return;
  sg::idl::read_str(r, m.title);
  if (!r.ok()) return;
  m.image_id = r.u32();
  if (!r.ok()) return;
  m.level = r.u32();
  if (!r.ok()) return;
  m.hp = r.i32();
  if (!r.ok()) return;
  m.max_hp = r.i32();
  if (!r.ok()) return;
  m.flags = r.u32();
  if (!r.ok()) return;
  m.ride = static_cast<sg::domain::RideState>(r.u8());
  if (!r.ok()) return;
  sg::idl::read_str(r, m.pet_name);
  if (!r.ok()) return;
  m.pet_level = r.u32();
  if (!r.ok()) return;
  m.pet_hp = r.i32();
  if (!r.ok()) return;
  m.pet_max_hp = r.i32();
  if (!r.ok()) return;
}

struct BattleSnapshot {
  std::uint64_t battle_id;
  std::uint32_t field_attribute;
  sg::idl::FixedVec<sg::domain::CombatantState, 20> combatants;
};

inline void encode(sg::idl::Writer& w, const BattleSnapshot& m) {
  w.u64(m.battle_id);
  w.u32(m.field_attribute);
  sg::idl::write_vec(w, m.combatants,
      [](sg::idl::Writer& w, const sg::domain::CombatantState& e) { encode(w, e); });
}

inline void decode(sg::idl::Reader& r, BattleSnapshot& m) {
  m.battle_id = r.u64();
  if (!r.ok()) return;
  m.field_attribute = r.u32();
  if (!r.ok()) return;
  sg::idl::read_vec(r, m.combatants,
      [](sg::idl::Reader& r, sg::domain::CombatantState& e) { decode(r, e); });
  if (!r.ok()) return;
}

struct BattleTurnBegin {
  std::uint64_t battle_id;
  std::uint32_t turn;
  std::uint32_t ready_mask;
};

inline void encode(sg::idl::Writer& w, const BattleTurnBegin& m) {
  w.u64(m.battle_id);
  w.u32(m.turn);
  w.u32(m.ready_mask);
}

inline void decode(sg::idl::Reader& r, BattleTurnBegin& m) {
  m.battle_id = r.u64();
  if (!r.ok()) return;
  m.turn = r.u32();
  if (!r.ok()) return;
  m.ready_mask = r.u32();
  if (!r.ok()) return;
}

struct BattleSelfInfo {
  std::uint64_t battle_id;
  std::uint32_t slot;
  std::uint32_t menu_flags;
  std::int32_t mp;
  sg::domain::CannotActReason cannot_act;
};

inline void encode(sg::idl::Writer& w, const BattleSelfInfo& m) {
  w.u64(m.battle_id);
  w.u32(m.slot);
  w.u32(m.menu_flags);
  w.i32(m.mp);
  w.u8(static_cast<std::uint8_t>(m.cannot_act));
}

inline void decode(sg::idl::Reader& r, BattleSelfInfo& m) {
  m.battle_id = r.u64();
  if (!r.ok()) return;
  m.slot = r.u32();
  if (!r.ok()) return;
  m.menu_flags = r.u32();
  if (!r.ok()) return;
  m.mp = r.i32();
  if (!r.ok()) return;
  m.cannot_act = static_cast<sg::domain::CannotActReason>(r.u8());
  if (!r.ok()) return;
}

struct BattleLeave {
  std::uint64_t battle_id;
  std::uint32_t reason;
};

inline void encode(sg::idl::Writer& w, const BattleLeave& m) {
  w.u64(m.battle_id);
  w.u32(m.reason);
}

inline void decode(sg::idl::Reader& r, BattleLeave& m) {
  m.battle_id = r.u64();
  if (!r.ok()) return;
  m.reason = r.u32();
  if (!r.ok()) return;
}

struct Hit {
  std::uint32_t attacker;
  sg::domain::AttackKind kind;
  std::uint32_t skill_id;
  std::uint32_t variant;
  std::uint32_t target_count;
};

inline void encode(sg::idl::Writer& w, const Hit& m) {
  w.u32(m.attacker);
  w.u8(static_cast<std::uint8_t>(m.kind));
  w.u32(m.skill_id);
  w.u32(m.variant);
  w.u32(m.target_count);
}

inline void decode(sg::idl::Reader& r, Hit& m) {
  m.attacker = r.u32();
  if (!r.ok()) return;
  m.kind = static_cast<sg::domain::AttackKind>(r.u8());
  if (!r.ok()) return;
  m.skill_id = r.u32();
  if (!r.ok()) return;
  m.variant = r.u32();
  if (!r.ok()) return;
  m.target_count = r.u32();
  if (!r.ok()) return;
}

struct Damage {
  std::uint32_t target;
  std::int32_t hp_delta;
  std::int32_t pet_hp_delta;
  std::int32_t mp_delta;
  std::uint32_t flags;
  sg::domain::BattleStatus status_applied;
};

inline void encode(sg::idl::Writer& w, const Damage& m) {
  w.u32(m.target);
  w.i32(m.hp_delta);
  w.i32(m.pet_hp_delta);
  w.i32(m.mp_delta);
  w.u32(m.flags);
  w.u8(static_cast<std::uint8_t>(m.status_applied));
}

inline void decode(sg::idl::Reader& r, Damage& m) {
  m.target = r.u32();
  if (!r.ok()) return;
  m.hp_delta = r.i32();
  if (!r.ok()) return;
  m.pet_hp_delta = r.i32();
  if (!r.ok()) return;
  m.mp_delta = r.i32();
  if (!r.ok()) return;
  m.flags = r.u32();
  if (!r.ok()) return;
  m.status_applied = static_cast<sg::domain::BattleStatus>(r.u8());
  if (!r.ok()) return;
}

struct StatusChange {
  std::uint32_t target;
  sg::domain::BattleStatus status;
  bool applied;
};

inline void encode(sg::idl::Writer& w, const StatusChange& m) {
  w.u32(m.target);
  w.u8(static_cast<std::uint8_t>(m.status));
  w.b(m.applied);
}

inline void decode(sg::idl::Reader& r, StatusChange& m) {
  m.target = r.u32();
  if (!r.ok()) return;
  m.status = static_cast<sg::domain::BattleStatus>(r.u8());
  if (!r.ok()) return;
  m.applied = r.b();
  if (!r.ok()) return;
}

struct SetHp {
  std::uint32_t target;
  std::int32_t hp;
};

inline void encode(sg::idl::Writer& w, const SetHp& m) {
  w.u32(m.target);
  w.i32(m.hp);
}

inline void decode(sg::idl::Reader& r, SetHp& m) {
  m.target = r.u32();
  if (!r.ok()) return;
  m.hp = r.i32();
  if (!r.ok()) return;
}

struct TextBox {
  std::uint32_t message_id;
  sg::idl::FixedVec<std::int32_t, 4> args;
};

inline void encode(sg::idl::Writer& w, const TextBox& m) {
  w.u32(m.message_id);
  sg::idl::write_vec(w, m.args,
      [](sg::idl::Writer& w, const std::int32_t& e) { w.i32(e); });
}

inline void decode(sg::idl::Reader& r, TextBox& m) {
  m.message_id = r.u32();
  if (!r.ok()) return;
  sg::idl::read_vec(r, m.args,
      [](sg::idl::Reader& r, std::int32_t& e) { e = r.i32(); });
  if (!r.ok()) return;
}

struct Escape {
  std::uint32_t actor;
  bool succeeded;
  bool vanish;
};

inline void encode(sg::idl::Writer& w, const Escape& m) {
  w.u32(m.actor);
  w.b(m.succeeded);
  w.b(m.vanish);
}

inline void decode(sg::idl::Reader& r, Escape& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
  m.succeeded = r.b();
  if (!r.ok()) return;
  m.vanish = r.b();
  if (!r.ok()) return;
}

struct Quit {
  std::uint32_t actor;
};

inline void encode(sg::idl::Writer& w, const Quit& m) {
  w.u32(m.actor);
}

inline void decode(sg::idl::Reader& r, Quit& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
}

struct Enter {
  std::uint32_t actor;
};

inline void encode(sg::idl::Writer& w, const Enter& m) {
  w.u32(m.actor);
}

inline void decode(sg::idl::Reader& r, Enter& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
}

struct FadeOut {
  std::uint32_t actor;
};

inline void encode(sg::idl::Writer& w, const FadeOut& m) {
  w.u32(m.actor);
}

inline void decode(sg::idl::Reader& r, FadeOut& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
}

struct SwitchEquip {
  std::uint32_t actor;
};

inline void encode(sg::idl::Writer& w, const SwitchEquip& m) {
  w.u32(m.actor);
}

inline void decode(sg::idl::Reader& r, SwitchEquip& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
}

struct Reverse {
  std::uint32_t actor;
};

inline void encode(sg::idl::Writer& w, const Reverse& m) {
  w.u32(m.actor);
}

inline void decode(sg::idl::Reader& r, Reverse& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
}

struct Transform {
  std::uint32_t actor;
  std::uint32_t image_id;
};

inline void encode(sg::idl::Writer& w, const Transform& m) {
  w.u32(m.actor);
  w.u32(m.image_id);
}

inline void decode(sg::idl::Reader& r, Transform& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
  m.image_id = r.u32();
  if (!r.ok()) return;
}

struct Nix {
  std::uint32_t actor;
};

inline void encode(sg::idl::Writer& w, const Nix& m) {
  w.u32(m.actor);
}

inline void decode(sg::idl::Reader& r, Nix& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
}

struct Synchronous {
  std::uint32_t target;
};

inline void encode(sg::idl::Writer& w, const Synchronous& m) {
  w.u32(m.target);
}

inline void decode(sg::idl::Reader& r, Synchronous& m) {
  m.target = r.u32();
  if (!r.ok()) return;
}

struct Boomerang {
  std::uint32_t actor;
};

inline void encode(sg::idl::Writer& w, const Boomerang& m) {
  w.u32(m.actor);
}

inline void decode(sg::idl::Reader& r, Boomerang& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
}

struct Summon {
  std::uint32_t actor;
  std::uint32_t image_id;
};

inline void encode(sg::idl::Writer& w, const Summon& m) {
  w.u32(m.actor);
  w.u32(m.image_id);
}

inline void decode(sg::idl::Reader& r, Summon& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
  m.image_id = r.u32();
  if (!r.ok()) return;
}

struct CaptureAct {
  std::uint32_t actor;
  std::uint32_t target;
  std::uint32_t flags;
};

inline void encode(sg::idl::Writer& w, const CaptureAct& m) {
  w.u32(m.actor);
  w.u32(m.target);
  w.u32(m.flags);
}

inline void decode(sg::idl::Reader& r, CaptureAct& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
  m.target = r.u32();
  if (!r.ok()) return;
  m.flags = r.u32();
  if (!r.ok()) return;
}

struct CallCompanions {
  std::uint32_t actor;
  std::int32_t amount;
  std::uint32_t flags;
};

inline void encode(sg::idl::Writer& w, const CallCompanions& m) {
  w.u32(m.actor);
  w.i32(m.amount);
  w.u32(m.flags);
}

inline void decode(sg::idl::Reader& r, CallCompanions& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
  m.amount = r.i32();
  if (!r.ok()) return;
  m.flags = r.u32();
  if (!r.ok()) return;
}

struct Steal {
  std::uint32_t actor;
  std::int32_t amount;
  std::uint32_t flags;
};

inline void encode(sg::idl::Writer& w, const Steal& m) {
  w.u32(m.actor);
  w.i32(m.amount);
  w.u32(m.flags);
}

inline void decode(sg::idl::Reader& r, Steal& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
  m.amount = r.i32();
  if (!r.ok()) return;
  m.flags = r.u32();
  if (!r.ok()) return;
}

struct CaptureSelect {
  std::uint32_t slot;
  std::uint32_t flags;
  std::uint32_t level;
  std::int32_t hp;
  std::int32_t max_hp;
};

inline void encode(sg::idl::Writer& w, const CaptureSelect& m) {
  w.u32(m.slot);
  w.u32(m.flags);
  w.u32(m.level);
  w.i32(m.hp);
  w.i32(m.max_hp);
}

inline void decode(sg::idl::Reader& r, CaptureSelect& m) {
  m.slot = r.u32();
  if (!r.ok()) return;
  m.flags = r.u32();
  if (!r.ok()) return;
  m.level = r.u32();
  if (!r.ok()) return;
  m.hp = r.i32();
  if (!r.ok()) return;
  m.max_hp = r.i32();
  if (!r.ok()) return;
}

struct Boundary {
  std::uint32_t actor;
  std::uint32_t element;
};

inline void encode(sg::idl::Writer& w, const Boundary& m) {
  w.u32(m.actor);
  w.u32(m.element);
}

inline void decode(sg::idl::Reader& r, Boundary& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
  m.element = r.u32();
  if (!r.ok()) return;
}

struct PetBattleModel {
  std::uint32_t actor;
};

inline void encode(sg::idl::Writer& w, const PetBattleModel& m) {
  w.u32(m.actor);
}

inline void decode(sg::idl::Reader& r, PetBattleModel& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
}

struct FireHunt {
  std::uint32_t actor;
};

inline void encode(sg::idl::Writer& w, const FireHunt& m) {
  w.u32(m.actor);
}

inline void decode(sg::idl::Reader& r, FireHunt& m) {
  m.actor = r.u32();
  if (!r.ok()) return;
}

struct BattleEvent {
  enum class BodyKind : std::uint16_t {
    NONE = 0,
    HIT = 1,
    DAMAGE = 2,
    STATUS_CHANGE = 3,
    SET_HP = 4,
    TEXT_BOX = 5,
    ESCAPE = 6,
    QUIT = 7,
    ENTER = 8,
    FADE_OUT = 9,
    SWITCH_EQUIP = 10,
    REVERSE = 11,
    TRANSFORM = 12,
    NIX = 13,
    SYNCHRONOUS = 14,
    BOOMERANG = 15,
    SUMMON = 16,
    CAPTURE_ACT = 17,
    CAPTURE_SELECT = 18,
    CALL_COMPANIONS = 19,
    STEAL = 20,
    BOUNDARY = 21,
    PET_BATTLE_MODEL = 22,
    FIRE_HUNT = 23,
  };
  BodyKind body_kind;
  union BodyUnion {
    sg::domain::Hit hit;
    sg::domain::Damage damage;
    sg::domain::StatusChange status_change;
    sg::domain::SetHp set_hp;
    sg::domain::TextBox text_box;
    sg::domain::Escape escape;
    sg::domain::Quit quit;
    sg::domain::Enter enter;
    sg::domain::FadeOut fade_out;
    sg::domain::SwitchEquip switch_equip;
    sg::domain::Reverse reverse;
    sg::domain::Transform transform;
    sg::domain::Nix nix;
    sg::domain::Synchronous synchronous;
    sg::domain::Boomerang boomerang;
    sg::domain::Summon summon;
    sg::domain::CaptureAct capture_act;
    sg::domain::CaptureSelect capture_select;
    sg::domain::CallCompanions call_companions;
    sg::domain::Steal steal;
    sg::domain::Boundary boundary;
    sg::domain::PetBattleModel pet_battle_model;
    sg::domain::FireHunt fire_hunt;
  } body;
};

inline void encode(sg::idl::Writer& w, const BattleEvent& m) {
  w.u16(static_cast<std::uint16_t>(m.body_kind));
  switch (m.body_kind) {
    case BattleEvent::BodyKind::HIT:
      encode(w, m.body.hit);
      break;
    case BattleEvent::BodyKind::DAMAGE:
      encode(w, m.body.damage);
      break;
    case BattleEvent::BodyKind::STATUS_CHANGE:
      encode(w, m.body.status_change);
      break;
    case BattleEvent::BodyKind::SET_HP:
      encode(w, m.body.set_hp);
      break;
    case BattleEvent::BodyKind::TEXT_BOX:
      encode(w, m.body.text_box);
      break;
    case BattleEvent::BodyKind::ESCAPE:
      encode(w, m.body.escape);
      break;
    case BattleEvent::BodyKind::QUIT:
      encode(w, m.body.quit);
      break;
    case BattleEvent::BodyKind::ENTER:
      encode(w, m.body.enter);
      break;
    case BattleEvent::BodyKind::FADE_OUT:
      encode(w, m.body.fade_out);
      break;
    case BattleEvent::BodyKind::SWITCH_EQUIP:
      encode(w, m.body.switch_equip);
      break;
    case BattleEvent::BodyKind::REVERSE:
      encode(w, m.body.reverse);
      break;
    case BattleEvent::BodyKind::TRANSFORM:
      encode(w, m.body.transform);
      break;
    case BattleEvent::BodyKind::NIX:
      encode(w, m.body.nix);
      break;
    case BattleEvent::BodyKind::SYNCHRONOUS:
      encode(w, m.body.synchronous);
      break;
    case BattleEvent::BodyKind::BOOMERANG:
      encode(w, m.body.boomerang);
      break;
    case BattleEvent::BodyKind::SUMMON:
      encode(w, m.body.summon);
      break;
    case BattleEvent::BodyKind::CAPTURE_ACT:
      encode(w, m.body.capture_act);
      break;
    case BattleEvent::BodyKind::CAPTURE_SELECT:
      encode(w, m.body.capture_select);
      break;
    case BattleEvent::BodyKind::CALL_COMPANIONS:
      encode(w, m.body.call_companions);
      break;
    case BattleEvent::BodyKind::STEAL:
      encode(w, m.body.steal);
      break;
    case BattleEvent::BodyKind::BOUNDARY:
      encode(w, m.body.boundary);
      break;
    case BattleEvent::BodyKind::PET_BATTLE_MODEL:
      encode(w, m.body.pet_battle_model);
      break;
    case BattleEvent::BodyKind::FIRE_HUNT:
      encode(w, m.body.fire_hunt);
      break;
    case BattleEvent::BodyKind::NONE:
    default:
      break;
  }
}

inline void decode(sg::idl::Reader& r, BattleEvent& m) {
  {
    const std::uint16_t tag = r.u16();
    if (!r.ok()) return;
    switch (tag) {
      case 1:
        decode(r, m.body.hit);
        m.body_kind = BattleEvent::BodyKind::HIT;
        break;
      case 2:
        decode(r, m.body.damage);
        m.body_kind = BattleEvent::BodyKind::DAMAGE;
        break;
      case 3:
        decode(r, m.body.status_change);
        m.body_kind = BattleEvent::BodyKind::STATUS_CHANGE;
        break;
      case 4:
        decode(r, m.body.set_hp);
        m.body_kind = BattleEvent::BodyKind::SET_HP;
        break;
      case 5:
        decode(r, m.body.text_box);
        m.body_kind = BattleEvent::BodyKind::TEXT_BOX;
        break;
      case 6:
        decode(r, m.body.escape);
        m.body_kind = BattleEvent::BodyKind::ESCAPE;
        break;
      case 7:
        decode(r, m.body.quit);
        m.body_kind = BattleEvent::BodyKind::QUIT;
        break;
      case 8:
        decode(r, m.body.enter);
        m.body_kind = BattleEvent::BodyKind::ENTER;
        break;
      case 9:
        decode(r, m.body.fade_out);
        m.body_kind = BattleEvent::BodyKind::FADE_OUT;
        break;
      case 10:
        decode(r, m.body.switch_equip);
        m.body_kind = BattleEvent::BodyKind::SWITCH_EQUIP;
        break;
      case 11:
        decode(r, m.body.reverse);
        m.body_kind = BattleEvent::BodyKind::REVERSE;
        break;
      case 12:
        decode(r, m.body.transform);
        m.body_kind = BattleEvent::BodyKind::TRANSFORM;
        break;
      case 13:
        decode(r, m.body.nix);
        m.body_kind = BattleEvent::BodyKind::NIX;
        break;
      case 14:
        decode(r, m.body.synchronous);
        m.body_kind = BattleEvent::BodyKind::SYNCHRONOUS;
        break;
      case 15:
        decode(r, m.body.boomerang);
        m.body_kind = BattleEvent::BodyKind::BOOMERANG;
        break;
      case 16:
        decode(r, m.body.summon);
        m.body_kind = BattleEvent::BodyKind::SUMMON;
        break;
      case 17:
        decode(r, m.body.capture_act);
        m.body_kind = BattleEvent::BodyKind::CAPTURE_ACT;
        break;
      case 18:
        decode(r, m.body.capture_select);
        m.body_kind = BattleEvent::BodyKind::CAPTURE_SELECT;
        break;
      case 19:
        decode(r, m.body.call_companions);
        m.body_kind = BattleEvent::BodyKind::CALL_COMPANIONS;
        break;
      case 20:
        decode(r, m.body.steal);
        m.body_kind = BattleEvent::BodyKind::STEAL;
        break;
      case 21:
        decode(r, m.body.boundary);
        m.body_kind = BattleEvent::BodyKind::BOUNDARY;
        break;
      case 22:
        decode(r, m.body.pet_battle_model);
        m.body_kind = BattleEvent::BodyKind::PET_BATTLE_MODEL;
        break;
      case 23:
        decode(r, m.body.fire_hunt);
        m.body_kind = BattleEvent::BodyKind::FIRE_HUNT;
        break;
      case 0:
        m.body_kind = BattleEvent::BodyKind::NONE;
        break;
      default:
        r.fail();
        return;
    }
    if (!r.ok()) return;
  }
}

struct BattleEvents {
  std::uint64_t battle_id;
  std::uint32_t turn;
  sg::idl::FixedVec<sg::domain::BattleEvent, 256> events;
};

inline void encode(sg::idl::Writer& w, const BattleEvents& m) {
  w.u64(m.battle_id);
  w.u32(m.turn);
  sg::idl::write_vec(w, m.events,
      [](sg::idl::Writer& w, const sg::domain::BattleEvent& e) { encode(w, e); });
}

inline void decode(sg::idl::Reader& r, BattleEvents& m) {
  m.battle_id = r.u64();
  if (!r.ok()) return;
  m.turn = r.u32();
  if (!r.ok()) return;
  sg::idl::read_vec(r, m.events,
      [](sg::idl::Reader& r, sg::domain::BattleEvent& e) { decode(r, e); });
  if (!r.ok()) return;
}

struct Attack {
  std::uint32_t target;
};

inline void encode(sg::idl::Writer& w, const Attack& m) {
  w.u32(m.target);
}

inline void decode(sg::idl::Reader& r, Attack& m) {
  m.target = r.u32();
  if (!r.ok()) return;
}

struct Guard {
};

inline void encode(sg::idl::Writer& w, const Guard& m) {
  (void)w; (void)m;
}

inline void decode(sg::idl::Reader& r, Guard& m) {
  (void)r; (void)m;
}

struct EscapeCmd {
};

inline void encode(sg::idl::Writer& w, const EscapeCmd& m) {
  (void)w; (void)m;
}

inline void decode(sg::idl::Reader& r, EscapeCmd& m) {
  (void)r; (void)m;
}

struct Capture {
  std::uint32_t target;
};

inline void encode(sg::idl::Writer& w, const Capture& m) {
  w.u32(m.target);
}

inline void decode(sg::idl::Reader& r, Capture& m) {
  m.target = r.u32();
  if (!r.ok()) return;
}

struct PetIn {
  std::uint32_t pet_slot;
};

inline void encode(sg::idl::Writer& w, const PetIn& m) {
  w.u32(m.pet_slot);
}

inline void decode(sg::idl::Reader& r, PetIn& m) {
  m.pet_slot = r.u32();
  if (!r.ok()) return;
}

struct PetOut {
};

inline void encode(sg::idl::Writer& w, const PetOut& m) {
  (void)w; (void)m;
}

inline void decode(sg::idl::Reader& r, PetOut& m) {
  (void)r; (void)m;
}

struct UseItem {
  std::uint32_t item_slot;
  std::uint32_t target;
};

inline void encode(sg::idl::Writer& w, const UseItem& m) {
  w.u32(m.item_slot);
  w.u32(m.target);
}

inline void decode(sg::idl::Reader& r, UseItem& m) {
  m.item_slot = r.u32();
  if (!r.ok()) return;
  m.target = r.u32();
  if (!r.ok()) return;
}

struct PetSkill {
  std::uint32_t skill_id;
  std::uint32_t target;
};

inline void encode(sg::idl::Writer& w, const PetSkill& m) {
  w.u32(m.skill_id);
  w.u32(m.target);
}

inline void decode(sg::idl::Reader& r, PetSkill& m) {
  m.skill_id = r.u32();
  if (!r.ok()) return;
  m.target = r.u32();
  if (!r.ok()) return;
}

struct ProfSkill {
  std::uint32_t skill_id;
  std::uint32_t target;
};

inline void encode(sg::idl::Writer& w, const ProfSkill& m) {
  w.u32(m.skill_id);
  w.u32(m.target);
}

inline void decode(sg::idl::Reader& r, ProfSkill& m) {
  m.skill_id = r.u32();
  if (!r.ok()) return;
  m.target = r.u32();
  if (!r.ok()) return;
}

struct SpellCmd {
  std::uint32_t spell_id;
  std::uint32_t target;
};

inline void encode(sg::idl::Writer& w, const SpellCmd& m) {
  w.u32(m.spell_id);
  w.u32(m.target);
}

inline void decode(sg::idl::Reader& r, SpellCmd& m) {
  m.spell_id = r.u32();
  if (!r.ok()) return;
  m.target = r.u32();
  if (!r.ok()) return;
}

struct WaitCmd {
};

inline void encode(sg::idl::Writer& w, const WaitCmd& m) {
  (void)w; (void)m;
}

inline void decode(sg::idl::Reader& r, WaitCmd& m) {
  (void)r; (void)m;
}

struct BattleCommand {
  std::uint64_t battle_id;
  std::uint32_t turn;
  enum class CommandKind : std::uint16_t {
    NONE = 0,
    ATTACK = 3,
    GUARD = 4,
    ESCAPE = 5,
    CAPTURE = 6,
    PET_IN = 7,
    PET_OUT = 8,
    USE_ITEM = 9,
    PET_SKILL = 10,
    PROF_SKILL = 11,
    SPELL = 12,
    WAIT = 13,
  };
  CommandKind command_kind;
  union CommandUnion {
    sg::domain::Attack attack;
    sg::domain::Guard guard;
    sg::domain::EscapeCmd escape;
    sg::domain::Capture capture;
    sg::domain::PetIn pet_in;
    sg::domain::PetOut pet_out;
    sg::domain::UseItem use_item;
    sg::domain::PetSkill pet_skill;
    sg::domain::ProfSkill prof_skill;
    sg::domain::SpellCmd spell;
    sg::domain::WaitCmd wait;
  } command;
};

inline void encode(sg::idl::Writer& w, const BattleCommand& m) {
  w.u64(m.battle_id);
  w.u32(m.turn);
  w.u16(static_cast<std::uint16_t>(m.command_kind));
  switch (m.command_kind) {
    case BattleCommand::CommandKind::ATTACK:
      encode(w, m.command.attack);
      break;
    case BattleCommand::CommandKind::GUARD:
      encode(w, m.command.guard);
      break;
    case BattleCommand::CommandKind::ESCAPE:
      encode(w, m.command.escape);
      break;
    case BattleCommand::CommandKind::CAPTURE:
      encode(w, m.command.capture);
      break;
    case BattleCommand::CommandKind::PET_IN:
      encode(w, m.command.pet_in);
      break;
    case BattleCommand::CommandKind::PET_OUT:
      encode(w, m.command.pet_out);
      break;
    case BattleCommand::CommandKind::USE_ITEM:
      encode(w, m.command.use_item);
      break;
    case BattleCommand::CommandKind::PET_SKILL:
      encode(w, m.command.pet_skill);
      break;
    case BattleCommand::CommandKind::PROF_SKILL:
      encode(w, m.command.prof_skill);
      break;
    case BattleCommand::CommandKind::SPELL:
      encode(w, m.command.spell);
      break;
    case BattleCommand::CommandKind::WAIT:
      encode(w, m.command.wait);
      break;
    case BattleCommand::CommandKind::NONE:
    default:
      break;
  }
}

inline void decode(sg::idl::Reader& r, BattleCommand& m) {
  m.battle_id = r.u64();
  if (!r.ok()) return;
  m.turn = r.u32();
  if (!r.ok()) return;
  {
    const std::uint16_t tag = r.u16();
    if (!r.ok()) return;
    switch (tag) {
      case 3:
        decode(r, m.command.attack);
        m.command_kind = BattleCommand::CommandKind::ATTACK;
        break;
      case 4:
        decode(r, m.command.guard);
        m.command_kind = BattleCommand::CommandKind::GUARD;
        break;
      case 5:
        decode(r, m.command.escape);
        m.command_kind = BattleCommand::CommandKind::ESCAPE;
        break;
      case 6:
        decode(r, m.command.capture);
        m.command_kind = BattleCommand::CommandKind::CAPTURE;
        break;
      case 7:
        decode(r, m.command.pet_in);
        m.command_kind = BattleCommand::CommandKind::PET_IN;
        break;
      case 8:
        decode(r, m.command.pet_out);
        m.command_kind = BattleCommand::CommandKind::PET_OUT;
        break;
      case 9:
        decode(r, m.command.use_item);
        m.command_kind = BattleCommand::CommandKind::USE_ITEM;
        break;
      case 10:
        decode(r, m.command.pet_skill);
        m.command_kind = BattleCommand::CommandKind::PET_SKILL;
        break;
      case 11:
        decode(r, m.command.prof_skill);
        m.command_kind = BattleCommand::CommandKind::PROF_SKILL;
        break;
      case 12:
        decode(r, m.command.spell);
        m.command_kind = BattleCommand::CommandKind::SPELL;
        break;
      case 13:
        decode(r, m.command.wait);
        m.command_kind = BattleCommand::CommandKind::WAIT;
        break;
      case 0:
        m.command_kind = BattleCommand::CommandKind::NONE;
        break;
      default:
        r.fail();
        return;
    }
    if (!r.ok()) return;
  }
}

}  // namespace domain
}  // namespace sg

#endif  // SG_IDL_DOMAIN_BATTLE_EVENTS_SG_H
