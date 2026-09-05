// shared/wire/framing.h —— 帧层与信封层的**双端共享**声明(DR-TS9 乙案)
//
// ★★ 本文件是 2026-09-06 由 `src/net/include/net/api.h` 拆出来的,理由是一条决策:
//
//     成帧与信封编解码是**双端语义完全相同**的东西。客户端 `01` §12.1 要 `sa_net`
//     也有「长度前缀成帧 + IDL 编解码接入」,而服务端 `src/net` 已有一份被
//     18 条用例钉死的实现 ⇒ 三条路可走(`11` §1.6):自写第二份 / 双端共享一份 /
//     客户端直编服务端源文件。**用户 2026-09-06 拍板:双端共享一份,新立本目录。**
//
//   ⇒ 理由与 D2 · DR-TS3 · 黄金用例集「复用不复制」· DR-BT5 反对双份实现,是同一条:
//     **凡双端同一语义的东西只留一份,让漂移在编译期就不可能发生。**
//
// ── ⚠️★ 本目录与 `shared/rules/` 的边界**不同**,这是本次唯一放宽的一处 ──────
//
//   `02` §9 原写「`shared/` 不得引用 `transport/` 组 IDL」。裁定把它**按子树**收紧:
//
//     shared/rules/   ❌ 仍然不得引用 transport/   ← 不放宽。L3 是纯函数,
//                                                    信封是传输概念,进来就破了 §1.5 的契约
//     shared/wire/    ✅ 可**且只可**引用 transport/ 组   ← 它的职责就是信封
//
//   ⚠️★ 放宽只对本子树生效。若哪天有人把 `check_shared_purity.py` 实现成
//     "整个 `shared/` 都放宽",L3 的纯度会一起被放掉,**而那不会有任何东西报错**
//     —— 与 `00` §10.4 三类静默错误同族。⇒ 那条按子树的规则是本裁定的一部分,不是配套细节。
//
// ── 仍然不许出现在本目录的东西(与 `rules/` 一致)────────────────────
//   socket / 平台头 / 日志 / 时钟 / 线程 / I/O —— 一样都不行。
//   ★ 本目录处理的是**字节与结构**,不是**连接**:谁来把字节交给它、
//     发出去的字节谁写进 socket,全在宿主侧(`src/net` 与客户端 `src/net`)。
//     ⇒ 这条切分正是它能双端共享的原因。

#ifndef SA_WIRE_FRAMING_H
#define SA_WIRE_FRAMING_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "transport/envelope.sa.h"
#include "ids.h"

namespace sa::wire {

// ── 帧层:[u32 length][payload] ────────────────────────────────
//
// 02 §1.2:长度前缀。理由不是"约定俗成",而是 **TCP 是字节流、
//   WebSocket 保留消息边界,长度前缀同时满足两者** ⇒ D4 解冻换 WsTransport 不动上层。
// ⚠️ 旧实现按 '\n' 切行(GetOneLine),与"负载里可能有换行"直接冲突,靠转义硬撑。
//    **新实现不按分隔符切帧。**

// 单帧上限。★ 这个数字是**熔断**,不是容量规划:
//   15 §4.3 实测原版每连接固定 324–452 KB 双缓冲,是 sizeof(Char)(12.3 KB)的
//   26–37 倍,且是每连接成本主项。01 §5.3 裁定「按需增长 + 上限熔断」。
//   ⇒ 这里是那个上限。最大的已知消息是 BattleEvents(7 KB 量级),留了 8 倍余量。
inline constexpr std::uint32_t kMaxFrameBytes = 64u * 1024u;

// 长度前缀本身的宽度。
inline constexpr std::size_t kFrameHeaderBytes = 4;

enum class FrameStatus : std::uint8_t {
  kOk = 0,        // 取到一条完整帧
  kNeedMore = 1,  // 还没收够,继续等
  kTooLarge = 2,  // ★ 声明长度超过 kMaxFrameBytes ⇒ 连接必须关闭
  kEmpty = 3,     // 声明长度为 0 ⇒ 协议违规(信封头本身就不止 0 字节)
};

// 增量成帧器。喂字节进去,取完整帧出来。
//
// ⚠️★ kTooLarge / kEmpty 一旦出现就是**不可恢复**的:字节流已经无法再对齐,
//    调用方必须关闭连接,不能"跳过这一帧继续读"。
//    ⇒ 因此这两个状态是粘性的,Next() 会一直返回它。
class FrameReader {
 public:
  FrameReader() = default;

  // 收到的原始字节。返回 false 表示累积缓冲超过了单帧上限 + 余量,
  // 说明对端在灌垃圾 ⇒ 关闭连接。
  bool Push(const std::uint8_t* data, std::size_t n);

  // 取下一条完整帧。kOk 时 *payload / *len 指向内部缓冲,
  // **在下一次 Push() 或 Pop() 之前有效**。
  FrameStatus Next(const std::uint8_t** payload, std::uint32_t* len);

  // 丢弃刚由 Next() 返回的那一帧。★ 与 Next() 分开是为了让调用方
  //   可以零拷贝地处理帧内容,处理完再推进。
  void Pop();

  bool failed() const noexcept { return failed_; }
  std::size_t buffered() const noexcept { return buf_.size() - read_; }

 private:
  std::vector<std::uint8_t> buf_;
  std::size_t read_ = 0;        // 已消费的前缀长度
  std::uint32_t pending_ = 0;   // 刚由 Next() 交出的帧长(含头)
  bool failed_ = false;
};

// 把一段负载写成一帧,追加到 out。负载超限返回 false(**不截断**)。
bool WriteFrame(const std::uint8_t* payload, std::uint32_t len,
                std::vector<std::uint8_t>& out);

// ── 信封层:EnvelopeHeader { msg_id, corr_id } + body ──────────
//
// 02 §2.1:body 不作为字段出现 —— 它是「紧跟在信封头之后、由 msg_id 决定类型
//   的字节」,长度由帧层给出。把 body 建模成 bytes 会引入无上限变长字段。

struct EnvelopeView {
  std::uint32_t msg_id = 0;
  std::uint64_t corr_id = 0;
  const std::uint8_t* body = nullptr;
  std::uint32_t body_len = 0;
};

// 从一条完整帧里剥出信封。格式不对返回 false ⇒ 整条消息作废,不存在部分成功。
bool DecodeEnvelope(const std::uint8_t* frame, std::uint32_t len,
                    EnvelopeView& out);

// 把一条 IDL 消息编成「帧 + 信封 + body」并追加到 out。
//
// ★ 模板而不是虚接口:msg_id 由 sa::idl::msg_id_of<M>() **编译期**取,
//   调用点写不出"消息类型与编号对不上"这种错。
//
// ★★ 就地编码进 out 的尾部,**不用栈缓冲**:单帧上限是 64 KB,
//    在栈上摆一个那么大的数组、每发一条消息摆一次,是一条自找的爆栈路径。
//    ⇒ 先把 out 撑到最坏情况,编完再缩回实际长度。out 通常是每连接复用的
//      出站缓冲 ⇒ 容量只涨一次,之后零分配(15 §9.1 的取向)。
//
// ⚠️ encode 用**非限定调用**:生成物的 encode 分别在 sa::domain 与
//    sa::transport 两个命名空间里,靠 ADL 各自找到自己那个。
//    写成限定调用就要为两组各写一份重载,那正是"同一语义两份实现"。
template <typename M>
bool EncodeFramed(std::uint64_t corr_id, const M& msg,
                  std::vector<std::uint8_t>& out) {
  const std::size_t start = out.size();
  out.resize(start + kFrameHeaderBytes + kMaxFrameBytes);

  sa::idl::Writer w(out.data() + start + kFrameHeaderBytes, kMaxFrameBytes);

  sa::transport::EnvelopeHeader head;
  head.msg_id = sa::idl::msg_id_of<M>();
  head.corr_id = corr_id;
  encode(w, head);
  encode(w, msg);

  if (!w.ok()) {
    out.resize(start);
    return false;
  }

  const std::uint32_t payload_len = static_cast<std::uint32_t>(w.size());
  out.resize(start + kFrameHeaderBytes + payload_len);
  // 长度前缀:小端,与 IDL 运行时的整数序一致。
  out[start + 0] = static_cast<std::uint8_t>(payload_len & 0xFFu);
  out[start + 1] = static_cast<std::uint8_t>((payload_len >> 8) & 0xFFu);
  out[start + 2] = static_cast<std::uint8_t>((payload_len >> 16) & 0xFFu);
  out[start + 3] = static_cast<std::uint8_t>((payload_len >> 24) & 0xFFu);
  return true;
}

}  // namespace sa::wire

#endif  // SA_WIRE_FRAMING_H
