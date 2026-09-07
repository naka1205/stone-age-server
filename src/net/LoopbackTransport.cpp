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

#include "net/Api.h"

namespace SA::Net
{
namespace
{
const std::vector<std::uint8_t> kEmpty;
}

LoopbackTransport::Conn *LoopbackTransport::get(ConnectionId id)
{
	for (Conn &c : _conns)
	{
		if (c.id == id)
			return &c;
	}
	return nullptr;
}

const LoopbackTransport::Conn *LoopbackTransport::get(ConnectionId id) const
{
	for (const Conn &c : _conns)
	{
		if (c.id == id)
			return &c;
	}
	return nullptr;
}

ConnectionId LoopbackTransport::connect()
{
	Conn c;
	c.id = _nextId++;
	_conns.push_back(std::move(c));
	if (_events != nullptr)
		_events->onConnected(_conns.back().id);
	return _conns.back().id;
}

bool LoopbackTransport::send(ConnectionId id, const std::uint8_t *data,
                             std::size_t n)
{
	Conn *c = get(id);
	if (c == nullptr || c->closed)
		return false;
	c->outbound.insert(c->outbound.end(), data, data + n);
	return true;
}

void LoopbackTransport::close(ConnectionId id)
{
	Conn *c = get(id);
	if (c == nullptr || c->closed)
		return;
	c->closed = true;
	if (_events != nullptr)
		_events->onDisconnected(id);
}

void LoopbackTransport::deliver(ConnectionId id, const std::uint8_t *data,
                                std::size_t n)
{
	Conn *c = get(id);
	if (c == nullptr || c->closed)
		return;
	c->inbound.insert(c->inbound.end(), data, data + n);
}

void LoopbackTransport::poll()
{
	if (_events == nullptr)
		return;
	// ★ 按连接逐条交付,且**一次交完** —— 真 TCP 会把它切成任意大小的片段,
	//   那正是 FrameReader 存在的理由;测试里要分片就自己分多次 Deliver。
	for (Conn &c : _conns)
	{
		if (c.closed || c.inbound.empty())
			continue;
		std::vector<std::uint8_t> batch;
		batch.swap(c.inbound);
		_events->onBytes(c.id, batch.data(), batch.size());
	}
}

const std::vector<std::uint8_t> &LoopbackTransport::sent(
    ConnectionId id) const
{
	const Conn *c = get(id);
	return c == nullptr ? kEmpty : c->outbound;
}

void LoopbackTransport::clearSent(ConnectionId id)
{
	Conn *c = get(id);
	if (c != nullptr)
		c->outbound.clear();
}

bool LoopbackTransport::closed(ConnectionId id) const
{
	const Conn *c = get(id);
	return c == nullptr || c->closed;
}

} // namespace SA::Net
