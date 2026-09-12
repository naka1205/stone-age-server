// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：domain/character_data.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/saidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SA_IDL_DOMAIN_CHARACTER_DATA_SA_H
#define SA_IDL_DOMAIN_CHARACTER_DATA_SA_H

#include "sa_idl_runtime.h"

namespace SA {
namespace Domain {

struct PlayerData {
  SA::IDL::FixedStr<31> name;
  std::int32_t default_pet;
  std::int32_t capture_count;
  std::int32_t exp;
  std::int32_t level;
  std::int32_t hp;
  std::int32_t mp;
  std::int32_t max_mp;
  std::int32_t vital;
  std::int32_t str;
  std::int32_t tough;
  std::int32_t dex;
  std::int32_t luck;
  std::int32_t charm;
  std::int32_t earth;
  std::int32_t water;
  std::int32_t fire;
  std::int32_t wind;
  std::int32_t floor;
  std::int32_t x;
  std::int32_t y;
  std::int32_t image;
  std::int32_t face_image;
  std::int32_t gold;
  std::uint32_t dir;
};

template <typename F>
inline void visitFields(PlayerData& m, F&& visit) {
  visit("name", m.name);
  visit("default_pet", m.default_pet);
  visit("capture_count", m.capture_count);
  visit("exp", m.exp);
  visit("level", m.level);
  visit("hp", m.hp);
  visit("mp", m.mp);
  visit("max_mp", m.max_mp);
  visit("vital", m.vital);
  visit("str", m.str);
  visit("tough", m.tough);
  visit("dex", m.dex);
  visit("luck", m.luck);
  visit("charm", m.charm);
  visit("earth", m.earth);
  visit("water", m.water);
  visit("fire", m.fire);
  visit("wind", m.wind);
  visit("floor", m.floor);
  visit("x", m.x);
  visit("y", m.y);
  visit("image", m.image);
  visit("face_image", m.face_image);
  visit("gold", m.gold);
  visit("dir", m.dir);
}

template <typename F>
inline void visitFields(const PlayerData& m, F&& visit) {
  visit("name", m.name);
  visit("default_pet", m.default_pet);
  visit("capture_count", m.capture_count);
  visit("exp", m.exp);
  visit("level", m.level);
  visit("hp", m.hp);
  visit("mp", m.mp);
  visit("max_mp", m.max_mp);
  visit("vital", m.vital);
  visit("str", m.str);
  visit("tough", m.tough);
  visit("dex", m.dex);
  visit("luck", m.luck);
  visit("charm", m.charm);
  visit("earth", m.earth);
  visit("water", m.water);
  visit("fire", m.fire);
  visit("wind", m.wind);
  visit("floor", m.floor);
  visit("x", m.x);
  visit("y", m.y);
  visit("image", m.image);
  visit("face_image", m.face_image);
  visit("gold", m.gold);
  visit("dir", m.dir);
}

template <typename Source, typename Target>
inline void copyPlayerData(const Source& source, Target& target) {
  target.name = static_cast<decltype(target.name)>(source.name);
  target.default_pet = static_cast<decltype(target.default_pet)>(source.default_pet);
  target.capture_count = static_cast<decltype(target.capture_count)>(source.capture_count);
  target.exp = static_cast<decltype(target.exp)>(source.exp);
  target.level = static_cast<decltype(target.level)>(source.level);
  target.hp = static_cast<decltype(target.hp)>(source.hp);
  target.mp = static_cast<decltype(target.mp)>(source.mp);
  target.max_mp = static_cast<decltype(target.max_mp)>(source.max_mp);
  target.vital = static_cast<decltype(target.vital)>(source.vital);
  target.str = static_cast<decltype(target.str)>(source.str);
  target.tough = static_cast<decltype(target.tough)>(source.tough);
  target.dex = static_cast<decltype(target.dex)>(source.dex);
  target.luck = static_cast<decltype(target.luck)>(source.luck);
  target.charm = static_cast<decltype(target.charm)>(source.charm);
  target.earth = static_cast<decltype(target.earth)>(source.earth);
  target.water = static_cast<decltype(target.water)>(source.water);
  target.fire = static_cast<decltype(target.fire)>(source.fire);
  target.wind = static_cast<decltype(target.wind)>(source.wind);
  target.floor = static_cast<decltype(target.floor)>(source.floor);
  target.x = static_cast<decltype(target.x)>(source.x);
  target.y = static_cast<decltype(target.y)>(source.y);
  target.image = static_cast<decltype(target.image)>(source.image);
  target.face_image = static_cast<decltype(target.face_image)>(source.face_image);
  target.gold = static_cast<decltype(target.gold)>(source.gold);
  target.dir = static_cast<decltype(target.dir)>(source.dir);
}

inline void encode(SA::IDL::Writer& w, const PlayerData& m) {
  SA::IDL::write_str(w, m.name);
  w.i32(m.default_pet);
  w.i32(m.capture_count);
  w.i32(m.exp);
  w.i32(m.level);
  w.i32(m.hp);
  w.i32(m.mp);
  w.i32(m.max_mp);
  w.i32(m.vital);
  w.i32(m.str);
  w.i32(m.tough);
  w.i32(m.dex);
  w.i32(m.luck);
  w.i32(m.charm);
  w.i32(m.earth);
  w.i32(m.water);
  w.i32(m.fire);
  w.i32(m.wind);
  w.i32(m.floor);
  w.i32(m.x);
  w.i32(m.y);
  w.i32(m.image);
  w.i32(m.face_image);
  w.i32(m.gold);
  w.u32(m.dir);
}

inline void decode(SA::IDL::Reader& r, PlayerData& m) {
  SA::IDL::read_str(r, m.name);
  m.default_pet = r.i32();
  m.capture_count = r.i32();
  m.exp = r.i32();
  m.level = r.i32();
  m.hp = r.i32();
  m.mp = r.i32();
  m.max_mp = r.i32();
  m.vital = r.i32();
  m.str = r.i32();
  m.tough = r.i32();
  m.dex = r.i32();
  m.luck = r.i32();
  m.charm = r.i32();
  m.earth = r.i32();
  m.water = r.i32();
  m.fire = r.i32();
  m.wind = r.i32();
  m.floor = r.i32();
  m.x = r.i32();
  m.y = r.i32();
  m.image = r.i32();
  m.face_image = r.i32();
  m.gold = r.i32();
  m.dir = r.u32();
}

struct PetData {
  SA::IDL::FixedStr<31> owner_char_name;
  SA::IDL::FixedStr<31> name;
  std::int32_t hp;
  std::int32_t mp;
  std::int32_t max_mp;
  std::int32_t vital;
  std::int32_t str;
  std::int32_t tough;
  std::int32_t dex;
  std::int32_t luck;
  std::int32_t fire;
  std::int32_t water;
  std::int32_t earth;
  std::int32_t wind;
  std::int32_t level;
  std::int32_t growth_vital;
  std::int32_t growth_str;
  std::int32_t growth_tough;
  std::int32_t growth_dex;
  std::int32_t capture_level;
  std::int32_t origin_image;
  std::int32_t base_image;
  std::int32_t pet_rank;
  std::int32_t mod_ai;
  std::int32_t variable_ai;
  std::int32_t y_hp;
  std::int32_t y_atk;
  std::int32_t y_def;
  std::int32_t y_quick;
  std::int32_t y_lv;
};

template <typename F>
inline void visitFields(PetData& m, F&& visit) {
  visit("owner_char_name", m.owner_char_name);
  visit("name", m.name);
  visit("hp", m.hp);
  visit("mp", m.mp);
  visit("max_mp", m.max_mp);
  visit("vital", m.vital);
  visit("str", m.str);
  visit("tough", m.tough);
  visit("dex", m.dex);
  visit("luck", m.luck);
  visit("fire", m.fire);
  visit("water", m.water);
  visit("earth", m.earth);
  visit("wind", m.wind);
  visit("level", m.level);
  visit("growth_vital", m.growth_vital);
  visit("growth_str", m.growth_str);
  visit("growth_tough", m.growth_tough);
  visit("growth_dex", m.growth_dex);
  visit("capture_level", m.capture_level);
  visit("origin_image", m.origin_image);
  visit("base_image", m.base_image);
  visit("pet_rank", m.pet_rank);
  visit("mod_ai", m.mod_ai);
  visit("variable_ai", m.variable_ai);
  visit("y_hp", m.y_hp);
  visit("y_atk", m.y_atk);
  visit("y_def", m.y_def);
  visit("y_quick", m.y_quick);
  visit("y_lv", m.y_lv);
}

template <typename F>
inline void visitFields(const PetData& m, F&& visit) {
  visit("owner_char_name", m.owner_char_name);
  visit("name", m.name);
  visit("hp", m.hp);
  visit("mp", m.mp);
  visit("max_mp", m.max_mp);
  visit("vital", m.vital);
  visit("str", m.str);
  visit("tough", m.tough);
  visit("dex", m.dex);
  visit("luck", m.luck);
  visit("fire", m.fire);
  visit("water", m.water);
  visit("earth", m.earth);
  visit("wind", m.wind);
  visit("level", m.level);
  visit("growth_vital", m.growth_vital);
  visit("growth_str", m.growth_str);
  visit("growth_tough", m.growth_tough);
  visit("growth_dex", m.growth_dex);
  visit("capture_level", m.capture_level);
  visit("origin_image", m.origin_image);
  visit("base_image", m.base_image);
  visit("pet_rank", m.pet_rank);
  visit("mod_ai", m.mod_ai);
  visit("variable_ai", m.variable_ai);
  visit("y_hp", m.y_hp);
  visit("y_atk", m.y_atk);
  visit("y_def", m.y_def);
  visit("y_quick", m.y_quick);
  visit("y_lv", m.y_lv);
}

template <typename Source, typename Target>
inline void copyPetData(const Source& source, Target& target) {
  target.owner_char_name = static_cast<decltype(target.owner_char_name)>(source.owner_char_name);
  target.name = static_cast<decltype(target.name)>(source.name);
  target.hp = static_cast<decltype(target.hp)>(source.hp);
  target.mp = static_cast<decltype(target.mp)>(source.mp);
  target.max_mp = static_cast<decltype(target.max_mp)>(source.max_mp);
  target.vital = static_cast<decltype(target.vital)>(source.vital);
  target.str = static_cast<decltype(target.str)>(source.str);
  target.tough = static_cast<decltype(target.tough)>(source.tough);
  target.dex = static_cast<decltype(target.dex)>(source.dex);
  target.luck = static_cast<decltype(target.luck)>(source.luck);
  target.fire = static_cast<decltype(target.fire)>(source.fire);
  target.water = static_cast<decltype(target.water)>(source.water);
  target.earth = static_cast<decltype(target.earth)>(source.earth);
  target.wind = static_cast<decltype(target.wind)>(source.wind);
  target.level = static_cast<decltype(target.level)>(source.level);
  target.growth_vital = static_cast<decltype(target.growth_vital)>(source.growth_vital);
  target.growth_str = static_cast<decltype(target.growth_str)>(source.growth_str);
  target.growth_tough = static_cast<decltype(target.growth_tough)>(source.growth_tough);
  target.growth_dex = static_cast<decltype(target.growth_dex)>(source.growth_dex);
  target.capture_level = static_cast<decltype(target.capture_level)>(source.capture_level);
  target.origin_image = static_cast<decltype(target.origin_image)>(source.origin_image);
  target.base_image = static_cast<decltype(target.base_image)>(source.base_image);
  target.pet_rank = static_cast<decltype(target.pet_rank)>(source.pet_rank);
  target.mod_ai = static_cast<decltype(target.mod_ai)>(source.mod_ai);
  target.variable_ai = static_cast<decltype(target.variable_ai)>(source.variable_ai);
  target.y_hp = static_cast<decltype(target.y_hp)>(source.y_hp);
  target.y_atk = static_cast<decltype(target.y_atk)>(source.y_atk);
  target.y_def = static_cast<decltype(target.y_def)>(source.y_def);
  target.y_quick = static_cast<decltype(target.y_quick)>(source.y_quick);
  target.y_lv = static_cast<decltype(target.y_lv)>(source.y_lv);
}

inline void encode(SA::IDL::Writer& w, const PetData& m) {
  SA::IDL::write_str(w, m.owner_char_name);
  SA::IDL::write_str(w, m.name);
  w.i32(m.hp);
  w.i32(m.mp);
  w.i32(m.max_mp);
  w.i32(m.vital);
  w.i32(m.str);
  w.i32(m.tough);
  w.i32(m.dex);
  w.i32(m.luck);
  w.i32(m.fire);
  w.i32(m.water);
  w.i32(m.earth);
  w.i32(m.wind);
  w.i32(m.level);
  w.i32(m.growth_vital);
  w.i32(m.growth_str);
  w.i32(m.growth_tough);
  w.i32(m.growth_dex);
  w.i32(m.capture_level);
  w.i32(m.origin_image);
  w.i32(m.base_image);
  w.i32(m.pet_rank);
  w.i32(m.mod_ai);
  w.i32(m.variable_ai);
  w.i32(m.y_hp);
  w.i32(m.y_atk);
  w.i32(m.y_def);
  w.i32(m.y_quick);
  w.i32(m.y_lv);
}

inline void decode(SA::IDL::Reader& r, PetData& m) {
  SA::IDL::read_str(r, m.owner_char_name);
  SA::IDL::read_str(r, m.name);
  m.hp = r.i32();
  m.mp = r.i32();
  m.max_mp = r.i32();
  m.vital = r.i32();
  m.str = r.i32();
  m.tough = r.i32();
  m.dex = r.i32();
  m.luck = r.i32();
  m.fire = r.i32();
  m.water = r.i32();
  m.earth = r.i32();
  m.wind = r.i32();
  m.level = r.i32();
  m.growth_vital = r.i32();
  m.growth_str = r.i32();
  m.growth_tough = r.i32();
  m.growth_dex = r.i32();
  m.capture_level = r.i32();
  m.origin_image = r.i32();
  m.base_image = r.i32();
  m.pet_rank = r.i32();
  m.mod_ai = r.i32();
  m.variable_ai = r.i32();
  m.y_hp = r.i32();
  m.y_atk = r.i32();
  m.y_def = r.i32();
  m.y_quick = r.i32();
  m.y_lv = r.i32();
}

struct ItemData {
  SA::IDL::FixedStr<63> name;
  SA::IDL::FixedStr<63> unique_code;
  std::int32_t item_id;
  std::int32_t type;
  std::int32_t level;
  std::int32_t cost;
  std::int32_t can_be_pile;
  std::int32_t use_pile_nums;
  std::int32_t current_pile;
  std::int32_t vanish_at_drop;
  std::int32_t drop_at_logout;
};

template <typename F>
inline void visitFields(ItemData& m, F&& visit) {
  visit("name", m.name);
  visit("unique_code", m.unique_code);
  visit("item_id", m.item_id);
  visit("type", m.type);
  visit("level", m.level);
  visit("cost", m.cost);
  visit("can_be_pile", m.can_be_pile);
  visit("use_pile_nums", m.use_pile_nums);
  visit("current_pile", m.current_pile);
  visit("vanish_at_drop", m.vanish_at_drop);
  visit("drop_at_logout", m.drop_at_logout);
}

template <typename F>
inline void visitFields(const ItemData& m, F&& visit) {
  visit("name", m.name);
  visit("unique_code", m.unique_code);
  visit("item_id", m.item_id);
  visit("type", m.type);
  visit("level", m.level);
  visit("cost", m.cost);
  visit("can_be_pile", m.can_be_pile);
  visit("use_pile_nums", m.use_pile_nums);
  visit("current_pile", m.current_pile);
  visit("vanish_at_drop", m.vanish_at_drop);
  visit("drop_at_logout", m.drop_at_logout);
}

template <typename Source, typename Target>
inline void copyItemData(const Source& source, Target& target) {
  target.name = static_cast<decltype(target.name)>(source.name);
  target.unique_code = static_cast<decltype(target.unique_code)>(source.unique_code);
  target.item_id = static_cast<decltype(target.item_id)>(source.item_id);
  target.type = static_cast<decltype(target.type)>(source.type);
  target.level = static_cast<decltype(target.level)>(source.level);
  target.cost = static_cast<decltype(target.cost)>(source.cost);
  target.can_be_pile = static_cast<decltype(target.can_be_pile)>(source.can_be_pile);
  target.use_pile_nums = static_cast<decltype(target.use_pile_nums)>(source.use_pile_nums);
  target.current_pile = static_cast<decltype(target.current_pile)>(source.current_pile);
  target.vanish_at_drop = static_cast<decltype(target.vanish_at_drop)>(source.vanish_at_drop);
  target.drop_at_logout = static_cast<decltype(target.drop_at_logout)>(source.drop_at_logout);
}

inline void encode(SA::IDL::Writer& w, const ItemData& m) {
  SA::IDL::write_str(w, m.name);
  SA::IDL::write_str(w, m.unique_code);
  w.i32(m.item_id);
  w.i32(m.type);
  w.i32(m.level);
  w.i32(m.cost);
  w.i32(m.can_be_pile);
  w.i32(m.use_pile_nums);
  w.i32(m.current_pile);
  w.i32(m.vanish_at_drop);
  w.i32(m.drop_at_logout);
}

inline void decode(SA::IDL::Reader& r, ItemData& m) {
  SA::IDL::read_str(r, m.name);
  SA::IDL::read_str(r, m.unique_code);
  m.item_id = r.i32();
  m.type = r.i32();
  m.level = r.i32();
  m.cost = r.i32();
  m.can_be_pile = r.i32();
  m.use_pile_nums = r.i32();
  m.current_pile = r.i32();
  m.vanish_at_drop = r.i32();
  m.drop_at_logout = r.i32();
}

struct PetSlot {
  std::uint64_t uid;
  std::uint32_t slot;
  SA::Domain::PetData value;
};

template <typename F>
inline void visitFields(PetSlot& m, F&& visit) {
  visit("uid", m.uid);
  visit("slot", m.slot);
  visit("value", m.value);
}

template <typename F>
inline void visitFields(const PetSlot& m, F&& visit) {
  visit("uid", m.uid);
  visit("slot", m.slot);
  visit("value", m.value);
}

template <typename Source, typename Target>
inline void copyPetSlot(const Source& source, Target& target) {
  target.uid = static_cast<decltype(target.uid)>(source.uid);
  target.slot = static_cast<decltype(target.slot)>(source.slot);
  target.value = static_cast<decltype(target.value)>(source.value);
}

inline void encode(SA::IDL::Writer& w, const PetSlot& m) {
  w.u64(m.uid);
  w.u32(m.slot);
  encode(w, m.value);
}

inline void decode(SA::IDL::Reader& r, PetSlot& m) {
  m.uid = r.u64();
  m.slot = r.u32();
  decode(r, m.value);
}

struct ItemSlot {
  std::uint64_t uid;
  std::uint32_t slot;
  SA::Domain::ItemData value;
};

template <typename F>
inline void visitFields(ItemSlot& m, F&& visit) {
  visit("uid", m.uid);
  visit("slot", m.slot);
  visit("value", m.value);
}

template <typename F>
inline void visitFields(const ItemSlot& m, F&& visit) {
  visit("uid", m.uid);
  visit("slot", m.slot);
  visit("value", m.value);
}

template <typename Source, typename Target>
inline void copyItemSlot(const Source& source, Target& target) {
  target.uid = static_cast<decltype(target.uid)>(source.uid);
  target.slot = static_cast<decltype(target.slot)>(source.slot);
  target.value = static_cast<decltype(target.value)>(source.value);
}

inline void encode(SA::IDL::Writer& w, const ItemSlot& m) {
  w.u64(m.uid);
  w.u32(m.slot);
  encode(w, m.value);
}

inline void decode(SA::IDL::Reader& r, ItemSlot& m) {
  m.uid = r.u64();
  m.slot = r.u32();
  decode(r, m.value);
}

struct CharacterRecord {
  std::uint32_t schema_ver;
  std::uint64_t char_id;
  std::uint64_t revision;
  SA::Domain::PlayerData player;
  SA::IDL::FixedVec<SA::Domain::PetSlot, 5> pets;
  SA::IDL::FixedVec<SA::Domain::ItemSlot, 54> items;
};

template <typename F>
inline void visitFields(CharacterRecord& m, F&& visit) {
  visit("schema_ver", m.schema_ver);
  visit("char_id", m.char_id);
  visit("revision", m.revision);
  visit("player", m.player);
  visit("pets", m.pets);
  visit("items", m.items);
}

template <typename F>
inline void visitFields(const CharacterRecord& m, F&& visit) {
  visit("schema_ver", m.schema_ver);
  visit("char_id", m.char_id);
  visit("revision", m.revision);
  visit("player", m.player);
  visit("pets", m.pets);
  visit("items", m.items);
}

template <typename Source, typename Target>
inline void copyCharacterRecord(const Source& source, Target& target) {
  target.schema_ver = static_cast<decltype(target.schema_ver)>(source.schema_ver);
  target.char_id = static_cast<decltype(target.char_id)>(source.char_id);
  target.revision = static_cast<decltype(target.revision)>(source.revision);
  target.player = static_cast<decltype(target.player)>(source.player);
  target.pets = static_cast<decltype(target.pets)>(source.pets);
  target.items = static_cast<decltype(target.items)>(source.items);
}

inline void encode(SA::IDL::Writer& w, const CharacterRecord& m) {
  w.u32(m.schema_ver);
  w.u64(m.char_id);
  w.u64(m.revision);
  encode(w, m.player);
  SA::IDL::write_vec(w, m.pets,
      [](SA::IDL::Writer& we, const SA::Domain::PetSlot& e) { encode(we, e); });
  SA::IDL::write_vec(w, m.items,
      [](SA::IDL::Writer& we, const SA::Domain::ItemSlot& e) { encode(we, e); });
}

inline void decode(SA::IDL::Reader& r, CharacterRecord& m) {
  m.schema_ver = r.u32();
  m.char_id = r.u64();
  m.revision = r.u64();
  decode(r, m.player);
  SA::IDL::read_vec(r, m.pets,
      [](SA::IDL::Reader& re, SA::Domain::PetSlot& e) { decode(re, e); });
  SA::IDL::read_vec(r, m.items,
      [](SA::IDL::Reader& re, SA::Domain::ItemSlot& e) { decode(re, e); });
}

}  // namespace Domain
}  // namespace SA

#endif  // SA_IDL_DOMAIN_CHARACTER_DATA_SA_H
