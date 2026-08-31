// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：domain/window.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/sgidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SG_IDL_DOMAIN_WINDOW_SG_H
#define SG_IDL_DOMAIN_WINDOW_SG_H

#include "sg_idl_runtime.h"
#include "domain/common.sg.h"

namespace sg {
namespace domain {

enum class WindowKind : std::uint16_t {
  WINDOW_KIND_INVALID = 0,
  WINDOW_KIND_MESSAGE = 1,
  WINDOW_KIND_LINE_INPUT = 2,
  WINDOW_KIND_SELECT = 3,
  WINDOW_KIND_SHOP_MENU = 10,
  WINDOW_KIND_ITEM_SHOP = 11,
  WINDOW_KIND_DEPOT_SHOP = 12,
  WINDOW_KIND_PET_SKILL_SHOP = 13,
  WINDOW_KIND_PET_SKILL_SHOW = 14,
  WINDOW_KIND_PROFESSION_SHOP = 15,
  WINDOW_KIND_LOGIN_NOTICE = 16,
  WINDOW_KIND_FAMILY_CREATE = 20,
  WINDOW_KIND_FAMILY_MENU = 21,
  WINDOW_KIND_FAMILY_BOARD = 22,
  WINDOW_KIND_FAMILY_DETAIL = 23,
  WINDOW_KIND_FAMILY_DUEL_POINT = 24,
  WINDOW_KIND_LEADER_SELECT = 25,
  WINDOW_KIND_LEADER_SELECT_ANSWER = 26,
  WINDOW_KIND_MANOR_POINT_LIST = 30,
  WINDOW_KIND_MANOR_MOMENTUM_10 = 31,
  WINDOW_KIND_MANOR_MOMENTUM_FM = 32,
  WINDOW_KIND_MANOR_SCHEDULE = 33,
  WINDOW_KIND_PK_SCHEDULE_LIST = 34,
  WINDOW_KIND_PK_SELECT_FAMILY = 35,
  WINDOW_KIND_PK_SCHEDULE_DETAIL = 36,
  WINDOW_KIND_RIDE_PET_LIST = 40,
  WINDOW_KIND_ANGEL_MESSAGE = 41,
};

enum class ButtonFlag : std::uint8_t {
  BUTTON_FLAG_NONE = 0,
  BUTTON_FLAG_OK = 1,
  BUTTON_FLAG_CANCEL = 2,
  BUTTON_FLAG_YES = 4,
  BUTTON_FLAG_NO = 8,
  BUTTON_FLAG_PREV = 16,
  BUTTON_FLAG_NEXT = 32,
};

struct MessageBody {
  sg::idl::FixedVec<sg::idl::FixedStr<255>, 16> lines;
  bool wide;
};

inline void encode(sg::idl::Writer& w, const MessageBody& m) {
  sg::idl::write_vec(w, m.lines,
      [](sg::idl::Writer& w, const sg::idl::FixedStr<255>& e) { sg::idl::write_str(w, e); });
  w.b(m.wide);
}

inline void decode(sg::idl::Reader& r, MessageBody& m) {
  sg::idl::read_vec(r, m.lines,
      [](sg::idl::Reader& r, sg::idl::FixedStr<255>& e) { sg::idl::read_str(r, e); });
  if (!r.ok()) return;
  m.wide = r.b();
  if (!r.ok()) return;
}

struct LineInputBody {
  sg::idl::FixedVec<sg::idl::FixedStr<255>, 16> lines;
  std::uint32_t max_len;
  bool wide;
};

inline void encode(sg::idl::Writer& w, const LineInputBody& m) {
  sg::idl::write_vec(w, m.lines,
      [](sg::idl::Writer& w, const sg::idl::FixedStr<255>& e) { sg::idl::write_str(w, e); });
  w.u32(m.max_len);
  w.b(m.wide);
}

inline void decode(sg::idl::Reader& r, LineInputBody& m) {
  sg::idl::read_vec(r, m.lines,
      [](sg::idl::Reader& r, sg::idl::FixedStr<255>& e) { sg::idl::read_str(r, e); });
  if (!r.ok()) return;
  m.max_len = r.u32();
  if (!r.ok()) return;
  m.wide = r.b();
  if (!r.ok()) return;
}

struct Choice {
  std::uint32_t choice_id;
  sg::idl::FixedStr<255> text;
  bool enabled;
};

inline void encode(sg::idl::Writer& w, const Choice& m) {
  w.u32(m.choice_id);
  sg::idl::write_str(w, m.text);
  w.b(m.enabled);
}

inline void decode(sg::idl::Reader& r, Choice& m) {
  m.choice_id = r.u32();
  if (!r.ok()) return;
  sg::idl::read_str(r, m.text);
  if (!r.ok()) return;
  m.enabled = r.b();
  if (!r.ok()) return;
}

struct SelectBody {
  sg::idl::FixedVec<sg::idl::FixedStr<255>, 16> lines;
  sg::idl::FixedVec<sg::domain::Choice, 32> choices;
};

inline void encode(sg::idl::Writer& w, const SelectBody& m) {
  sg::idl::write_vec(w, m.lines,
      [](sg::idl::Writer& w, const sg::idl::FixedStr<255>& e) { sg::idl::write_str(w, e); });
  sg::idl::write_vec(w, m.choices,
      [](sg::idl::Writer& w, const sg::domain::Choice& e) { encode(w, e); });
}

inline void decode(sg::idl::Reader& r, SelectBody& m) {
  sg::idl::read_vec(r, m.lines,
      [](sg::idl::Reader& r, sg::idl::FixedStr<255>& e) { sg::idl::read_str(r, e); });
  if (!r.ok()) return;
  sg::idl::read_vec(r, m.choices,
      [](sg::idl::Reader& r, sg::domain::Choice& e) { decode(r, e); });
  if (!r.ok()) return;
}

struct ShopHeader {
  bool can_buy;
  bool reuse_previous;
  sg::idl::FixedStr<63> shop_name;
  sg::idl::FixedStr<255> message;
  sg::idl::FixedStr<255> shop_message;
  sg::idl::FixedStr<255> count_message;
  sg::idl::FixedStr<255> level_low_message;
  sg::idl::FixedStr<255> confirm_message;
  sg::idl::FixedStr<255> item_full_message;
};

inline void encode(sg::idl::Writer& w, const ShopHeader& m) {
  w.b(m.can_buy);
  w.b(m.reuse_previous);
  sg::idl::write_str(w, m.shop_name);
  sg::idl::write_str(w, m.message);
  sg::idl::write_str(w, m.shop_message);
  sg::idl::write_str(w, m.count_message);
  sg::idl::write_str(w, m.level_low_message);
  sg::idl::write_str(w, m.confirm_message);
  sg::idl::write_str(w, m.item_full_message);
}

inline void decode(sg::idl::Reader& r, ShopHeader& m) {
  m.can_buy = r.b();
  if (!r.ok()) return;
  m.reuse_previous = r.b();
  if (!r.ok()) return;
  sg::idl::read_str(r, m.shop_name);
  if (!r.ok()) return;
  sg::idl::read_str(r, m.message);
  if (!r.ok()) return;
  sg::idl::read_str(r, m.shop_message);
  if (!r.ok()) return;
  sg::idl::read_str(r, m.count_message);
  if (!r.ok()) return;
  sg::idl::read_str(r, m.level_low_message);
  if (!r.ok()) return;
  sg::idl::read_str(r, m.confirm_message);
  if (!r.ok()) return;
  sg::idl::read_str(r, m.item_full_message);
  if (!r.ok()) return;
}

struct ShopEntry {
  std::uint32_t entry_id;
  std::uint32_t item_id;
  std::uint32_t image_id;
  std::uint32_t level;
  std::int32_t price;
  bool purchasable;
};

inline void encode(sg::idl::Writer& w, const ShopEntry& m) {
  w.u32(m.entry_id);
  w.u32(m.item_id);
  w.u32(m.image_id);
  w.u32(m.level);
  w.i32(m.price);
  w.b(m.purchasable);
}

inline void decode(sg::idl::Reader& r, ShopEntry& m) {
  m.entry_id = r.u32();
  if (!r.ok()) return;
  m.item_id = r.u32();
  if (!r.ok()) return;
  m.image_id = r.u32();
  if (!r.ok()) return;
  m.level = r.u32();
  if (!r.ok()) return;
  m.price = r.i32();
  if (!r.ok()) return;
  m.purchasable = r.b();
  if (!r.ok()) return;
}

struct ShopBody {
  sg::domain::ShopHeader header;
  sg::idl::FixedVec<sg::domain::ShopEntry, 32> entries;
};

inline void encode(sg::idl::Writer& w, const ShopBody& m) {
  encode(w, m.header);
  sg::idl::write_vec(w, m.entries,
      [](sg::idl::Writer& w, const sg::domain::ShopEntry& e) { encode(w, e); });
}

inline void decode(sg::idl::Reader& r, ShopBody& m) {
  decode(r, m.header);
  if (!r.ok()) return;
  sg::idl::read_vec(r, m.entries,
      [](sg::idl::Reader& r, sg::domain::ShopEntry& e) { decode(r, e); });
  if (!r.ok()) return;
}

struct RawListBody {
  sg::idl::FixedVec<sg::idl::FixedStr<255>, 32> rows;
};

inline void encode(sg::idl::Writer& w, const RawListBody& m) {
  sg::idl::write_vec(w, m.rows,
      [](sg::idl::Writer& w, const sg::idl::FixedStr<255>& e) { sg::idl::write_str(w, e); });
}

inline void decode(sg::idl::Reader& r, RawListBody& m) {
  sg::idl::read_vec(r, m.rows,
      [](sg::idl::Reader& r, sg::idl::FixedStr<255>& e) { sg::idl::read_str(r, e); });
  if (!r.ok()) return;
}

struct WindowOpen {
  std::uint32_t window_id;
  sg::domain::WindowKind kind;
  std::uint32_t buttons;
  sg::domain::EntityRef source;
  enum class BodyKind : std::uint16_t {
    NONE = 0,
    MESSAGE = 5,
    LINE_INPUT = 6,
    SELECT = 7,
    SHOP = 8,
    RAW_LIST = 9,
  };
  BodyKind body_kind;
  union BodyUnion {
    sg::domain::MessageBody message;
    sg::domain::LineInputBody line_input;
    sg::domain::SelectBody select;
    sg::domain::ShopBody shop;
    sg::domain::RawListBody raw_list;
  } body;
};

inline void encode(sg::idl::Writer& w, const WindowOpen& m) {
  w.u32(m.window_id);
  w.u16(static_cast<std::uint16_t>(m.kind));
  w.u32(m.buttons);
  encode(w, m.source);
  w.u16(static_cast<std::uint16_t>(m.body_kind));
  switch (m.body_kind) {
    case WindowOpen::BodyKind::MESSAGE:
      encode(w, m.body.message);
      break;
    case WindowOpen::BodyKind::LINE_INPUT:
      encode(w, m.body.line_input);
      break;
    case WindowOpen::BodyKind::SELECT:
      encode(w, m.body.select);
      break;
    case WindowOpen::BodyKind::SHOP:
      encode(w, m.body.shop);
      break;
    case WindowOpen::BodyKind::RAW_LIST:
      encode(w, m.body.raw_list);
      break;
    case WindowOpen::BodyKind::NONE:
    default:
      break;
  }
}

inline void decode(sg::idl::Reader& r, WindowOpen& m) {
  m.window_id = r.u32();
  if (!r.ok()) return;
  m.kind = static_cast<sg::domain::WindowKind>(r.u16());
  if (!r.ok()) return;
  m.buttons = r.u32();
  if (!r.ok()) return;
  decode(r, m.source);
  if (!r.ok()) return;
  {
    const std::uint16_t tag = r.u16();
    if (!r.ok()) return;
    switch (tag) {
      case 5:
        decode(r, m.body.message);
        m.body_kind = WindowOpen::BodyKind::MESSAGE;
        break;
      case 6:
        decode(r, m.body.line_input);
        m.body_kind = WindowOpen::BodyKind::LINE_INPUT;
        break;
      case 7:
        decode(r, m.body.select);
        m.body_kind = WindowOpen::BodyKind::SELECT;
        break;
      case 8:
        decode(r, m.body.shop);
        m.body_kind = WindowOpen::BodyKind::SHOP;
        break;
      case 9:
        decode(r, m.body.raw_list);
        m.body_kind = WindowOpen::BodyKind::RAW_LIST;
        break;
      case 0:
        m.body_kind = WindowOpen::BodyKind::NONE;
        break;
      default:
        r.fail();
        return;
    }
    if (!r.ok()) return;
  }
}

struct WindowReply {
  std::uint32_t window_id;
  sg::domain::EntityRef source;
  std::uint32_t button;
  enum class ResultKind : std::uint16_t {
    NONE = 0,
    CHOICE_ID = 4,
    TEXT = 5,
    ENTRY_ID = 6,
  };
  ResultKind result_kind;
  union ResultUnion {
    std::uint32_t choice_id;
    sg::idl::FixedStr<255> text;
    std::uint32_t entry_id;
  } result;
};

inline void encode(sg::idl::Writer& w, const WindowReply& m) {
  w.u32(m.window_id);
  encode(w, m.source);
  w.u32(m.button);
  w.u16(static_cast<std::uint16_t>(m.result_kind));
  switch (m.result_kind) {
    case WindowReply::ResultKind::CHOICE_ID:
      w.u32(m.result.choice_id);
      break;
    case WindowReply::ResultKind::TEXT:
      sg::idl::write_str(w, m.result.text);
      break;
    case WindowReply::ResultKind::ENTRY_ID:
      w.u32(m.result.entry_id);
      break;
    case WindowReply::ResultKind::NONE:
    default:
      break;
  }
}

inline void decode(sg::idl::Reader& r, WindowReply& m) {
  m.window_id = r.u32();
  if (!r.ok()) return;
  decode(r, m.source);
  if (!r.ok()) return;
  m.button = r.u32();
  if (!r.ok()) return;
  {
    const std::uint16_t tag = r.u16();
    if (!r.ok()) return;
    switch (tag) {
      case 4:
        m.result.choice_id = r.u32();
        m.result_kind = WindowReply::ResultKind::CHOICE_ID;
        break;
      case 5:
        sg::idl::read_str(r, m.result.text);
        m.result_kind = WindowReply::ResultKind::TEXT;
        break;
      case 6:
        m.result.entry_id = r.u32();
        m.result_kind = WindowReply::ResultKind::ENTRY_ID;
        break;
      case 0:
        m.result_kind = WindowReply::ResultKind::NONE;
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

#endif  // SG_IDL_DOMAIN_WINDOW_SG_H
