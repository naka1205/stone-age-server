// idl/tests/smoke.cpp —— 生成物冒烟测试
//
// 这是 02-protocol.md §8 三条 CI 检查之外的**第四道**:前三条检查 schema,
// 本文件检查**生成物本身能不能用**。
//   ① DR-TS1 边界 ② 的 POD 约束(可 memcpy、无虚函数、无堆成员)
//   ② (sg.width) 的枚举窄化确实生效
//   ③ msg_id 的编译期映射
//   ④ 编解码往返、截断输入、写缓冲不足
//   ⑤ ★ 体积回归 —— 把「多大」变成断言,而不是估算
//
// 构建:c++ -std=c++20 -Wall -Wextra -I../generated/cpp smoke.cpp -o smoke && ./smoke

#include "ids.h"
#include "domain/common.sa.h"
#include "domain/battle_events.sa.h"
#include "domain/window.sa.h"
#include "transport/envelope.sa.h"
#include "transport/errors.sa.h"
#include "transport/handshake.sa.h"
#include "transport/interservice.sa.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace sa;

// ── ① POD 约束(DR-TS1 边界 ②)──────────────────────────────────
static_assert(std::is_trivially_copyable_v<transport::HandshakeRequest>);
static_assert(std::is_trivially_copyable_v<transport::RequestHeader>);
static_assert(std::is_trivially_copyable_v<domain::EntityRef>);
static_assert(std::is_trivially_copyable_v<domain::BattleEvent>);
static_assert(std::is_trivially_copyable_v<domain::BattleEvents>);
static_assert(std::is_trivially_copyable_v<domain::BattleSnapshot>);
static_assert(std::is_trivially_copyable_v<domain::WindowOpen>);
static_assert(std::is_standard_layout_v<domain::BattleEvents>);
static_assert(std::is_standard_layout_v<domain::WindowOpen>);

// ── ② (sg.width) 的窄化生效 ───────────────────────────────────
static_assert(sizeof(transport::RejectReason) == 1);
static_assert(sizeof(transport::Status) == 1);
static_assert(sizeof(domain::Direction) == 1);
static_assert(sizeof(domain::BattleStatus) == 1);
static_assert(sizeof(domain::CannotActReason) == 1);
static_assert(sizeof(domain::WindowKind) == 2);
static_assert(sizeof(domain::DamageFlag) == 4);

// ── ③ msg_id 编译期映射 ──────────────────────────────────────
static_assert(idl::msg_id_of<transport::HandshakeRequest>() == 0x0001);
static_assert(idl::msg_id_of<domain::BattleSnapshot>()      == 0x0201);
static_assert(idl::msg_id_of<domain::BattleEvents>()        == 0x0205);
static_assert(idl::msg_id_of<domain::BattleCommand>()       == 0x0210);
static_assert(idl::msg_id_of<domain::WindowOpen>()          == 0x0601);
static_assert(idl::msg_id_of<domain::WindowReply>()         == 0x0602);

// ── ⑤ 体积回归 ───────────────────────────────────────────────
//
// ★ 为什么把体积写成断言:POD + union + 定长 repeated 的组合下,
//   **一个字段的宽度会被 oneof 的所有变体一起买单**。
//   实测教训:BattleEvent 里曾有一个 FixedStr<127> 的 name_arg,
//   它把 union 从 24 B 撑到 160 B,使每回合事件缓冲 7 KB → 41 KB(5.7×)。
//   改成参数 ID 化(DR-CP5 的精神)后归零。
//   ⇒ 这类回归静默且昂贵,必须由断言而不是 code review 来挡。
static_assert(sizeof(domain::BattleEvent) <= 32,
              "BattleEvent 的 union 变大了：检查是否有变体引入了定长字符串/数组。"
              "它会乘以 max_count=256 计入每回合事件缓冲。");
static_assert(sizeof(domain::BattleEvents) <= 8 * 1024,
              "每回合事件缓冲超过 8 KB —— 见上一条。");
static_assert(sizeof(domain::CombatantState) <= 512);
static_assert(sizeof(domain::BattleCommand) <= 32);

int main() {
  std::uint8_t buf[64 * 1024];

  // ── 往返 1:定长字符串 ──────────────────────────────────────
  transport::HandshakeRequest in{};
  in.protocol_version = 7;
  in.client_build.assign("sa-client-8.0.0-rc1");

  idl::Writer w(buf, sizeof(buf));
  encode(w, in);
  assert(w.ok());

  transport::HandshakeRequest out{};
  idl::Reader r(buf, w.size());
  decode(r, out);
  assert(r.ok());
  assert(out.protocol_version == 7);
  assert(std::strcmp(out.client_build.c_str(), "sa-client-8.0.0-rc1") == 0);

  // ── 往返 2:窄化枚举 ────────────────────────────────────────
  transport::HandshakeRejected rej{};
  rej.reason = transport::RejectReason::REJECT_VERSION_MISMATCH;
  rej.required_protocol_version = 9;
  idl::Writer w2(buf, sizeof(buf));
  encode(w2, rej);
  transport::HandshakeRejected rej2{};
  idl::Reader r2(buf, w2.size());
  decode(r2, rej2);
  assert(r2.ok());
  assert(rej2.reason == transport::RejectReason::REJECT_VERSION_MISMATCH);
  assert(rej2.required_protocol_version == 9);

  // ── 往返 3:嵌套 message ───────────────────────────────────
  domain::EntityRef ref{};
  ref.source = domain::EntitySource::ENTITY_SOURCE_ENTITY;
  ref.entity_id = 4242;
  idl::Writer w3(buf, sizeof(buf));
  encode(w3, ref);
  domain::EntityRef ref2{};
  idl::Reader r3(buf, w3.size());
  decode(r3, ref2);
  assert(r3.ok());
  assert(ref2.entity_id == 4242);

  // ── 往返 4:★ 战斗事件流(oneof + repeated)────────────────
  //
  // 构造一个「近战命中 2 个目标」的回合,验证 02 §6.2 改变 1 的形状:
  // 原版靠 FF| 终止符表达变长目标列表,新形状是 Hit.target_count + 后续 Damage。
  domain::BattleEvents ev{};
  ev.battle_id = 0xDEAD'BEEFull;
  ev.turn = 3;

  domain::BattleEvent e0{};
  e0.body_kind = domain::BattleEvent::BodyKind::HIT;
  e0.body.hit.attacker = 2;
  e0.body.hit.kind = domain::AttackKind::ATTACK_KIND_MELEE;
  e0.body.hit.skill_id = 0;          // MELEE 无技能 id
  e0.body.hit.target_count = 2;      // ★ 其后紧跟 2 个 Damage
  ev.events.push_back(e0);

  for (std::uint32_t t = 0; t < 2; ++t) {
    domain::BattleEvent d{};
    d.body_kind = domain::BattleEvent::BodyKind::DAMAGE;
    d.body.damage.target = 10 + t;   // 10.. 为敌方侧(SIDE_OFFSET = 10)
    d.body.damage.hp_delta = -37;
    d.body.damage.pet_hp_delta = 0;
    d.body.damage.flags =
        static_cast<std::uint32_t>(domain::DamageFlag::DAMAGE_FLAG_NORMAL) |
        static_cast<std::uint32_t>(domain::DamageFlag::DAMAGE_FLAG_CRITICAL);
    d.body.damage.status_applied = domain::BattleStatus::BATTLE_ST_NONE;
    ev.events.push_back(d);
  }

  domain::BattleEvent e3{};
  e3.body_kind = domain::BattleEvent::BodyKind::STATUS_CHANGE;
  e3.body.status_change.target = 11;
  // ★ DR-BT4 修正区的状态(31..43):原版三张表都停在它之前 ⇒ 无法被抵抗。
  //   这里断言它在 schema 里是**一等成员**,不是越界值。
  e3.body.status_change.status = domain::BattleStatus::BATTLE_ST_ICECRACK5;
  e3.body.status_change.applied = true;
  ev.events.push_back(e3);

  idl::Writer w4(buf, sizeof(buf));
  encode(w4, ev);
  assert(w4.ok());

  domain::BattleEvents ev2{};
  idl::Reader r4(buf, w4.size());
  decode(r4, ev2);
  assert(r4.ok());
  assert(ev2.battle_id == 0xDEAD'BEEFull);
  assert(ev2.turn == 3);
  assert(ev2.events.size() == 4);
  assert(ev2.events[0].body_kind == domain::BattleEvent::BodyKind::HIT);
  assert(ev2.events[0].body.hit.target_count == 2);
  assert(ev2.events[1].body.damage.hp_delta == -37);
  assert(ev2.events[2].body.damage.target == 11);
  assert(ev2.events[3].body.status_change.status ==
         domain::BattleStatus::BATTLE_ST_ICECRACK5);

  // ── 往返 5:窗口(oneof + repeated string + 显式 choice_id)──
  domain::WindowOpen win{};
  win.window_id = 77;
  win.kind = domain::WindowKind::WINDOW_KIND_SELECT;
  win.buttons = static_cast<std::uint32_t>(domain::ButtonFlag::BUTTON_FLAG_OK) |
                static_cast<std::uint32_t>(domain::ButtonFlag::BUTTON_FLAG_CANCEL);
  win.source.source = domain::EntitySource::ENTITY_SOURCE_ENTITY;
  win.source.entity_id = 1234;
  win.body_kind = domain::WindowOpen::BodyKind::SELECT;
  {
    idl::FixedStr<255> line{};
    line.assign("你要买点什么？");
    win.body.select.lines.push_back(line);

    domain::Choice c0{};
    c0.choice_id = 100;              // ★ 显式 id,不是位置序号(DR-PR1)
    c0.text.assign("买东西");
    c0.enabled = true;
    win.body.select.choices.push_back(c0);

    domain::Choice c1{};
    c1.choice_id = 200;
    c1.text.assign("离开");
    c1.enabled = true;
    win.body.select.choices.push_back(c1);
  }

  idl::Writer w5(buf, sizeof(buf));
  encode(w5, win);
  assert(w5.ok());

  domain::WindowOpen win2{};
  idl::Reader r5(buf, w5.size());
  decode(r5, win2);
  assert(r5.ok());
  assert(win2.window_id == 77);
  assert(win2.kind == domain::WindowKind::WINDOW_KIND_SELECT);
  assert(win2.body_kind == domain::WindowOpen::BodyKind::SELECT);
  assert(win2.body.select.choices.size() == 2);
  assert(win2.body.select.choices[1].choice_id == 200);
  assert(std::strcmp(win2.body.select.choices[0].text.c_str(), "买东西") == 0);

  // ── 边界:截断输入必须被挡住,不得越界读 ─────────────────────
  {
    idl::Reader rt(buf, 1);
    domain::WindowOpen tmp{};
    decode(rt, tmp);
    assert(!rt.ok());
  }
  {
    idl::Reader rt(buf, w5.size() / 2);   // 半个包
    domain::WindowOpen tmp{};
    decode(rt, tmp);
    assert(!rt.ok());
  }

  // ── ★★ 边界:解码失败 ⇒ 全部字段被写成**零值**,不是"保持原样"(2026-09-06)
  //
  // 起因是 GCC 15 在 `-O2` 下报的一条 `-Wmaybe-uninitialized`:生成的 decode
  // 原先每个字段后跟 `if (!r.ok()) return;` ⇒ 尾部字段在失败路径上**不被写**,
  // 与 sa_idl_runtime.h 卷首承诺的「解码失败是整条消息作废」相矛盾 ——
  // 那句承诺的兑现依赖 Reader 短路返回 0,而早退恰好绕过了它。详见 Reader 卷首。
  //
  // ⚠️★ 手法上有一处是本段的**全部价值**所在:**被测对象必须先填成非零**。
  //   上面那两段截断测试用的是 `WindowOpen tmp{}` —— 它已经全零,
  //   ⇒ 即使 decode 一个字段都不写,`assert(!rt.ok())` 也照样通过。
  //   那两段挡的是「越界读」,挡不住「字段没被写」。
  //   本段先把字段填成显眼的非零值:任何一个字段漏写,它就保留那个值 ⇒ 当场红。
  //   ★ 这也是这条修复能不能被守住的关键 —— 早退若被加回去,clang 与 MSVC
  //     一个字都不会说,而 GCC 只在恰好存在那种调用点时才报。
  {
    // ① 纯标量消息 —— 正是 GCC 点名的那个类型
    domain::BattleTurnBegin b{};
    b.battle_id = 0x1111'2222'3333'4444ull;
    b.turn = 0xABCD;
    b.ready_mask = 0xFFFF'FFFFu;

    idl::Reader rt(buf, 3);              // 3 字节:连第一个 u64 都读不满
    decode(rt, b);
    assert(!rt.ok());
    assert(b.battle_id == 0);            // 首字段:早退之前就被写,一直成立
    assert(b.turn == 0);                 // ★★ GCC 点名的正是这一个
    assert(b.ready_mask == 0);           // ★ 早退最深处的字段
  }
  {
    // ② FixedStr —— len 归零不等于 data 可用
    transport::HandshakeRequest req{};
    req.protocol_version = 999;
    req.client_build.assign("留在这里就是漏写的证据");

    idl::Reader rt(buf, 2);              // 读不满 protocol_version(u32)
    decode(rt, req);
    assert(!rt.ok());
    assert(req.protocol_version == 0);
    assert(req.client_build.len == 0);
    // ★ 这一条单独立着:c_str() 直接返回 data,data[0] 不是终止符就是越界读 ——
    //   比读到一个错的数值更坏。read_str 的三条失败路径原先一条都不写它。
    assert(req.client_build.c_str()[0] == '\0');
  }
  {
    // ③ ★★ FixedVec —— 本段四条里后果最重的一条
    domain::BattleEvents ev3{};
    ev3.battle_id = 7;
    ev3.turn = 7;
    domain::BattleEvent filler{};
    filler.body_kind = domain::BattleEvent::BodyKind::DAMAGE;
    filler.body.damage.hp_delta = -1;
    assert(ev3.events.push_back(filler));
    assert(ev3.events.push_back(filler));
    assert(ev3.events.size() == 2);

    idl::Reader rt(buf, 4);              // battle_id 是 u64,读不满
    decode(rt, ev3);
    assert(!rt.ok());
    assert(ev3.battle_id == 0);
    assert(ev3.turn == 0);
    // ★★ count 不归零时 end() = data + 垃圾长度 ⇒ 一次 range-for 就是越界读,
    //   而不只是读到错的值。read_vec 的失败路径原先不写 count。
    assert(ev3.events.empty());
  }
  {
    // ④ oneof 未知 tag ⇒ 作废,且 tag 不留旧值
    //
    // ⚠️ 诚实标注:同批把 `default:` 的 `return` 换成了 `break`(让 oneof 之后的
    //   字段仍被写)。**当前 schema 下这一改行为不可区分** —— 4 个含 oneof 的
    //   消息(BattleEvent / BattleCommand / WindowOpen / WindowReply)里 oneof
    //   都排在最后,后面没有字段。⇒ 它防的是将来往 oneof 之后加字段的那一天,
    //   而那时不会有任何东西报错。本条只能验到 fail + tag 归 NONE 这两样。
    const std::uint8_t bad[8] = {0xFF, 0xFF, 0, 0, 0, 0, 0, 0};   // tag = 0xFFFF
    domain::BattleEvent e{};
    e.body_kind = domain::BattleEvent::BodyKind::DAMAGE;          // 预填有效 kind
    idl::Reader rt(bad, sizeof(bad));
    decode(rt, e);
    assert(!rt.ok());                    // 未知 tag 不静默跳过(DR-CP4)
    assert(e.body_kind == domain::BattleEvent::BodyKind::NONE);
  }

  // ── 边界:写缓冲不足必须被挡住 ──────────────────────────────
  {
    std::uint8_t tiny[2];
    idl::Writer wt(tiny, sizeof(tiny));
    encode(wt, ev);
    assert(!wt.ok());
  }

  std::printf(
      "OK  BattleEvent=%zu  BattleEvents=%zu  BattleSnapshot=%zu  WindowOpen=%zu\n",
      sizeof(domain::BattleEvent), sizeof(domain::BattleEvents),
      sizeof(domain::BattleSnapshot), sizeof(domain::WindowOpen));
  return 0;
}
