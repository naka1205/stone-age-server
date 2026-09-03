// src/net/loopback_transport.cpp —— 进程内传输
//
// 用途有二,缺一不可:
//   ① 本批次的测试载体 —— 成帧与会话状态机不需要 socket 就能端到端验(见 api.h 卷首);
//   ② 01 §5.1 里 InProcTransport 的雏形 —— 单容器形态下的模块间传输。
//
// ⚠️★ 但它**不是** 00 §3.1 那条约束的兑现。§3.1 要求
//    「进程内形态下,服务之间只能通过接口通信,不得有共享内存捷径」,
//    而 00 §9.0.4 已明确认下:1.5 是单模块,**不验证服务边界**,那留到阶段 3。
//    ⇒ 别把"Loopback 能跑通"读成"进程内形态已经验过了"。

#include "net/api.h"

namespace sa::net {
namespace {
const std::vector<std::uint8_t> kEmpty;
}

LoopbackTransport::Conn* LoopbackTransport::Get(ConnectionId id) {
  for (Conn& c : conns_) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

const LoopbackTransport::Conn* LoopbackTransport::Get(ConnectionId id) const {
  for (const Conn& c : conns_) {
    if (c.id == id) return &c;
  }
  return nullptr;
}

ConnectionId LoopbackTransport::Connect() {
  Conn c;
  c.id = next_id_++;
  conns_.push_back(std::move(c));
  if (events_ != nullptr) events_->OnConnected(conns_.back().id);
  return conns_.back().id;
}

bool LoopbackTransport::Send(ConnectionId id, const std::uint8_t* data,
                             std::size_t n) {
  Conn* c = Get(id);
  if (c == nullptr || c->closed) return false;
  c->outbound.insert(c->outbound.end(), data, data + n);
  return true;
}

void LoopbackTransport::Close(ConnectionId id) {
  Conn* c = Get(id);
  if (c == nullptr || c->closed) return;
  c->closed = true;
  if (events_ != nullptr) events_->OnDisconnected(id);
}

void LoopbackTransport::Deliver(ConnectionId id, const std::uint8_t* data,
                                std::size_t n) {
  Conn* c = Get(id);
  if (c == nullptr || c->closed) return;
  c->inbound.insert(c->inbound.end(), data, data + n);
}

void LoopbackTransport::Poll() {
  if (events_ == nullptr) return;
  // ★ 按连接逐条交付,且**一次交完** —— 真 TCP 会把它切成任意大小的片段,
  //   那正是 FrameReader 存在的理由;测试里要分片就自己分多次 Deliver。
  for (Conn& c : conns_) {
    if (c.closed || c.inbound.empty()) continue;
    std::vector<std::uint8_t> batch;
    batch.swap(c.inbound);
    events_->OnBytes(c.id, batch.data(), batch.size());
  }
}

const std::vector<std::uint8_t>& LoopbackTransport::sent(
    ConnectionId id) const {
  const Conn* c = Get(id);
  return c == nullptr ? kEmpty : c->outbound;
}

void LoopbackTransport::ClearSent(ConnectionId id) {
  Conn* c = Get(id);
  if (c != nullptr) c->outbound.clear();
}

bool LoopbackTransport::closed(ConnectionId id) const {
  const Conn* c = Get(id);
  return c == nullptr || c->closed;
}

}  // namespace sa::net
