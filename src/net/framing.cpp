// src/net/framing.cpp —— 帧层与信封层
//
// 02 §2 的分层:
//   应用层   IDL 生成的 message
//   信封层   EnvelopeHeader { msg_id, corr_id } + body
//   帧层     [u32 length][payload]        ★ 本文件
//   传输层   ITransport

#include "net/api.h"

#include <cstring>

namespace sa::net {
namespace {

// 信封头的线上长度:u32 + u64。
constexpr std::uint32_t kEnvelopeHeaderBytes = 12;

std::uint32_t ReadU32LE(const std::uint8_t* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

bool FrameReader::Push(const std::uint8_t* data, std::size_t n) {
  if (failed_) return false;

  // 已消费的前缀攒够一半就回收,避免缓冲无限前移。
  // ⚠️ 不是每次都 erase:那会把成帧变成 O(n²)。
  if (read_ > 0 && read_ * 2 >= buf_.size()) {
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(read_));
    read_ = 0;
  }

  // ★ 累积上限 = 单帧上限 + 头 + 一帧余量。超过说明对端在灌垃圾
  //   (或者我们把长度读错了)⇒ 关闭连接,不要继续吃内存。
  //   15 §4.3:每连接缓冲是原版的成本主项,新实现「按需增长 + 上限熔断」。
  const std::size_t limit =
      static_cast<std::size_t>(kMaxFrameBytes) * 2 + kFrameHeaderBytes;
  if (buffered() + n > limit) {
    failed_ = true;
    return false;
  }

  buf_.insert(buf_.end(), data, data + n);
  return true;
}

FrameStatus FrameReader::Next(const std::uint8_t** payload,
                              std::uint32_t* len) {
  if (failed_) return FrameStatus::kTooLarge;

  const std::size_t avail = buffered();
  if (avail < kFrameHeaderBytes) return FrameStatus::kNeedMore;

  const std::uint32_t declared = ReadU32LE(buf_.data() + read_);

  // ⚠️★ 这两种失败是**粘性**的:长度字段一旦不可信,字节流就再也无法对齐,
  //    "跳过这一帧"是没有意义的 —— 我们并不知道这一帧到哪结束。
  if (declared == 0) {
    failed_ = true;
    return FrameStatus::kEmpty;
  }
  if (declared > kMaxFrameBytes) {
    failed_ = true;
    return FrameStatus::kTooLarge;
  }

  if (avail < kFrameHeaderBytes + declared) return FrameStatus::kNeedMore;

  *payload = buf_.data() + read_ + kFrameHeaderBytes;
  *len = declared;
  pending_ = declared + static_cast<std::uint32_t>(kFrameHeaderBytes);
  return FrameStatus::kOk;
}

void FrameReader::Pop() {
  read_ += pending_;
  pending_ = 0;
}

bool WriteFrame(const std::uint8_t* payload, std::uint32_t len,
                std::vector<std::uint8_t>& out) {
  // ★ 不截断。05 §10.4 记着原版 szAllBattleString 用 strncat 第三参写错、
  //   等价无上界 strcat 的教训 —— 新实现宁可失败,不可写出半条。
  if (len == 0 || len > kMaxFrameBytes) return false;

  out.push_back(static_cast<std::uint8_t>(len & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((len >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((len >> 24) & 0xFFu));
  out.insert(out.end(), payload, payload + len);
  return true;
}

bool DecodeEnvelope(const std::uint8_t* frame, std::uint32_t len,
                    EnvelopeView& out) {
  if (frame == nullptr || len < kEnvelopeHeaderBytes) return false;

  sa::idl::Reader r(frame, len);
  sa::transport::EnvelopeHeader head;
  decode(r, head);
  if (!r.ok()) return false;

  out.msg_id = head.msg_id;
  out.corr_id = head.corr_id;
  out.body = frame + kEnvelopeHeaderBytes;
  out.body_len = len - kEnvelopeHeaderBytes;
  return true;
}

}  // namespace sa::net
