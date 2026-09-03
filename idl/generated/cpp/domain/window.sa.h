// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。
// 来源：domain/window.proto
//
// 修改方式：改 schema → 重跑 `python3 idl/codegen/saidl_gen.py` → 提交生成物
// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。

#ifndef SA_IDL_DOMAIN_WINDOW_SA_H
#define SA_IDL_DOMAIN_WINDOW_SA_H

#include "sa_idl_runtime.h"
#include "domain/common.sa.h"

namespace sa {
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
  sa::idl::FixedVec<sa::idl::FixedStr<255>, 16> lines;
  bool wide;
};

inline void encode(sa::idl::Writer& w, const MessageBody& m) {
  sa::idl::write_vec(w, m.lines,
      [](sa::idl::Writer& we, const sa::idl::FixedStr<255>& e) { sa::idl::write_str(we, e); });
  w.b(m.wide);
}

inline void decode(sa::idl::Reader& r, MessageBody& m) {
  sa::idl::read_vec(r, m.lines,
      [](sa::idl::Reader& re, sa::idl::FixedStr<255>& e) { sa::idl::read_str(re, e); });
  if (!r.ok()) return;
  m.wide = r.b();
  if (!r.ok()) return;
}

struct LineInputBody {
  sa::idl::FixedVec<sa::idl::FixedStr<255>, 16> lines;
  std::uint32_t max_len;
  bool wide;
};

inline void encode(sa::idl::Writer& w, const LineInputBody& m) {
  sa::idl::write_vec(w, m.lines,
      [](sa::idl::Writer& we, const sa::idl::FixedStr<255>& e) { sa::idl::write_str(we, e); });
  w.u32(m.max_len);
  w.b(m.wide);
}

inline void decode(sa::idl::Reader& r, LineInputBody& m) {
  sa::idl::read_vec(r, m.lines,
      [](sa::idl::Reader& re, sa::idl::FixedStr<255>& e) { sa::idl::read_str(re, e); });
  if (!r.ok()) return;
  m.max_len = r.u32();
  if (!r.ok()) return;
  m.wide = r.b();
  if (!r.ok()) return;
}

struct Choice {
  std::uint32_t choice_id;
  sa::idl::FixedStr<255> text;
  bool enabled;
};

inline void encode(sa::idl::Writer& w, const Choice& m) {
  w.u32(m.choice_id);
  sa::idl::write_str(w, m.text);
  w.b(m.enabled);
}

inline void decode(sa::idl::Reader& r, Choice& m) {
  m.choice_id = r.u32();
  if (!r.ok()) return;
  sa::idl::read_str(r, m.text);
  if (!r.ok()) return;
  m.enabled = r.b();
  if (!r.ok()) return;
}

struct SelectBody {
  sa::idl::FixedVec<sa::idl::FixedStr<255>, 16> lines;
  sa::idl::FixedVec<sa::domain::Choice, 32> choices;
};

inline void encode(sa::idl::Writer& w, const SelectBody& m) {
  sa::idl::write_vec(w, m.lines,
      [](sa::idl::Writer& we, const sa::idl::FixedStr<255>& e) { sa::idl::write_str(we, e); });
  sa::idl::write_vec(w, m.choices,
      [](sa::idl::Writer& we, const sa::domain::Choice& e) { encode(we, e); });
}

inline void decode(sa::idl::Reader& r, SelectBody& m) {
  sa::idl::read_vec(r, m.lines,
      [](sa::idl::Reader& re, sa::idl::FixedStr<255>& e) { sa::idl::read_str(re, e); });
  if (!r.ok()) return;
  sa::idl::read_vec(r, m.choices,
      [](sa::idl::Reader& re, sa::domain::Choice& e) { decode(re, e); });
  if (!r.ok()) return;
}

struct ShopHeader {
  bool can_buy;
  bool reuse_previous;
  sa::idl::FixedStr<63> shop_name;
  sa::idl::FixedStr<255> message;
  sa::idl::FixedStr<255> shop_message;
  sa::idl::FixedStr<255> count_message;
  sa::idl::FixedStr<255> level_low_message;
  sa::idl::FixedStr<255> confirm_message;
  sa::idl::FixedStr<255> item_full_message;
};

inline void encode(sa::idl::Writer& w, const ShopHeader& m) {
  w.b(m.can_buy);
  w.b(m.reuse_previous);
  sa::idl::write_str(w, m.shop_name);
  sa::idl::write_str(w, m.message);
  sa::idl::write_str(w, m.shop_message);
  sa::idl::write_str(w, m.count_message);
  sa::idl::write_str(w, m.level_low_message);
  sa::idl::write_str(w, m.confirm_message);
  sa::idl::write_str(w, m.item_full_message);
}

inline void decode(sa::idl::Reader& r, ShopHeader& m) {
  m.can_buy = r.b();
  if (!r.ok()) return;
  m.reuse_previous = r.b();
  if (!r.ok()) return;
  sa::idl::read_str(r, m.shop_name);
  if (!r.ok()) return;
  sa::idl::read_str(r, m.message);
  if (!r.ok()) return;
  sa::idl::read_str(r, m.shop_message);
  if (!r.ok()) return;
  sa::idl::read_str(r, m.count_message);
  if (!r.ok()) return;
  sa::idl::read_str(r, m.level_low_message);
  if (!r.ok()) return;
  sa::idl::read_str(r, m.confirm_message);
  if (!r.ok()) return;
  sa::idl::read_str(r, m.item_full_message);
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

inline void encode(sa::idl::Writer& w, const ShopEntry& m) {
  w.u32(m.entry_id);
  w.u32(m.item_id);
  w.u32(m.image_id);
  w.u32(m.level);
  w.i32(m.price);
  w.b(m.purchasable);
}

inline void decode(sa::idl::Reader& r, ShopEntry& m) {
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
  sa::domain::ShopHeader header;
  sa::idl::FixedVec<sa::domain::ShopEntry, 32> entries;
};

inline void encode(sa::idl::Writer& w, const ShopBody& m) {
  encode(w, m.header);
  sa::idl::write_vec(w, m.entries,
      [](sa::idl::Writer& we, const sa::domain::ShopEntry& e) { encode(we, e); });
}

inline void decode(sa::idl::Reader& r, ShopBody& m) {
  decode(r, m.header);
  if (!r.ok()) return;
  sa::idl::read_vec(r, m.entries,
      [](sa::idl::Reader& re, sa::domain::ShopEntry& e) { decode(re, e); });
  if (!r.ok()) return;
}

struct RawListBody {
  sa::idl::FixedVec<sa::idl::FixedStr<255>, 32> rows;
};

inline void encode(sa::idl::Writer& w, const RawListBody& m) {
  sa::idl::write_vec(w, m.rows,
      [](sa::idl::Writer& we, const sa::idl::FixedStr<255>& e) { sa::idl::write_str(we, e); });
}

inline void decode(sa::idl::Reader& r, RawListBody& m) {
  sa::idl::read_vec(r, m.rows,
      [](sa::idl::Reader& re, sa::idl::FixedStr<255>& e) { sa::idl::read_str(re, e); });
  if (!r.ok()) return;
}

struct WindowOpen {
  std::uint32_t window_id;
  sa::domain::WindowKind kind;
  std::uint32_t buttons;
  sa::domain::EntityRef source;
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
    sa::domain::MessageBody message;
    sa::domain::LineInputBody line_input;
    sa::domain::SelectBody select;
    sa::domain::ShopBody shop;
    sa::domain::RawListBody raw_list;
  } body;
};

inline void encode(sa::idl::Writer& w, const WindowOpen& m) {
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

inline void decode(sa::idl::Reader& r, WindowOpen& m) {
  m.window_id = r.u32();
  if (!r.ok()) return;
  m.kind = static_cast<sa::domain::WindowKind>(r.u16());
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
  sa::domain::EntityRef source;
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
    sa::idl::FixedStr<255> text;
    std::uint32_t entry_id;
  } result;
};

inline void encode(sa::idl::Writer& w, const WindowReply& m) {
  w.u32(m.window_id);
  encode(w, m.source);
  w.u32(m.button);
  w.u16(static_cast<std::uint16_t>(m.result_kind));
  switch (m.result_kind) {
    case WindowReply::ResultKind::CHOICE_ID:
      w.u32(m.result.choice_id);
      break;
    case WindowReply::ResultKind::TEXT:
      sa::idl::write_str(w, m.result.text);
      break;
    case WindowReply::ResultKind::ENTRY_ID:
      w.u32(m.result.entry_id);
      break;
    case WindowReply::ResultKind::NONE:
    default:
      break;
  }
}

inline void decode(sa::idl::Reader& r, WindowReply& m) {
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
        sa::idl::read_str(r, m.result.text);
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
}  // namespace sa

#endif  // SA_IDL_DOMAIN_WINDOW_SA_H
