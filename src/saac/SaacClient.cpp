#include "SaacClient.h"

#include <chrono>

namespace SA::Saac
{

std::unique_ptr<ISaacClient> createSaacClient(std::uint32_t instance_id, std::uint32_t generation)
{
	return std::make_unique<SaacClient>(instance_id, generation);
}

SaacClient::SaacClient(std::uint32_t instance_id, std::uint32_t generation)
    : _instanceId(instance_id), _generation(generation)
{
}

std::uint64_t SaacClient::nextRequestId() noexcept
{
	return _reqCounter.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t SaacClient::requestLogin(const std::string &cdkey,
                                       const std::string & /*password*/,
                                       LoginCallback cb,
                                       std::int64_t timeout_ms)
{
	const std::uint64_t req_id = nextRequestId();
	const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	                                std::chrono::steady_clock::now().time_since_epoch())
	                                .count();

	InFlightItem item;
	item.envelope.instance_id = _instanceId;
	item.envelope.generation = _generation.load(std::memory_order_relaxed);
	item.envelope.request_id = req_id;
	item.envelope.deadline_ms = timeout_ms;
	item.expire_at_ms = now_ms + timeout_ms;
	item.req_type = PacketType::kLoginReq;

	item.on_completion = [cb, cdkey](SaacAccountStatus status, const std::vector<std::uint8_t> & /*payload*/)
	{
		if (cb)
		{
			std::vector<SA::Transport::CharacterSummary> chars;
			cb(status, cdkey, chars);
		}
	};

	item.on_timeout = [cb, cdkey]()
	{
		if (cb)
		{
			cb(SaacAccountStatus::kTimeout, cdkey, {});
		}
	};

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_inFlight.emplace(req_id, std::move(item));
	}
	return req_id;
}

std::uint64_t SaacClient::requestCharLoad(const std::string & /*cdkey*/,
                                          int /*slot*/,
                                          CharLoadCallback cb,
                                          std::int64_t timeout_ms)
{
	const std::uint64_t req_id = nextRequestId();
	const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	                                std::chrono::steady_clock::now().time_since_epoch())
	                                .count();

	InFlightItem item;
	item.envelope.instance_id = _instanceId;
	item.envelope.generation = _generation.load(std::memory_order_relaxed);
	item.envelope.request_id = req_id;
	item.envelope.deadline_ms = timeout_ms;
	item.expire_at_ms = now_ms + timeout_ms;
	item.req_type = PacketType::kCharLoadReq;

	item.on_completion = [cb](SaacAccountStatus status, const std::vector<std::uint8_t> & /*payload*/)
	{
		if (cb)
		{
			SA::Domain::CharacterRecord record{};
			cb(status, record);
		}
	};

	item.on_timeout = [cb]()
	{
		if (cb)
		{
			SA::Domain::CharacterRecord empty{};
			cb(SaacAccountStatus::kTimeout, empty);
		}
	};

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_inFlight.emplace(req_id, std::move(item));
	}
	return req_id;
}

std::uint64_t SaacClient::requestCharSave(const std::string & /*cdkey*/,
                                          int /*slot*/,
                                          const SA::Domain::CharacterRecord & /*record*/,
                                          bool /*logout*/,
                                          const std::string & /*lease_token*/,
                                          CharSaveCallback cb,
                                          std::int64_t timeout_ms)
{
	const std::uint64_t req_id = nextRequestId();
	const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	                                std::chrono::steady_clock::now().time_since_epoch())
	                                .count();

	InFlightItem item;
	item.envelope.instance_id = _instanceId;
	item.envelope.generation = _generation.load(std::memory_order_relaxed);
	item.envelope.request_id = req_id;
	item.envelope.deadline_ms = timeout_ms;
	item.expire_at_ms = now_ms + timeout_ms;
	item.req_type = PacketType::kCharSaveReq;

	item.on_completion = [cb](SaacAccountStatus status, const std::vector<std::uint8_t> & /*payload*/)
	{
		if (cb)
		{
			cb(status, 1);
		}
	};

	item.on_timeout = [cb]()
	{
		if (cb)
		{
			cb(SaacAccountStatus::kTimeout, 0);
		}
	};

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_inFlight.emplace(req_id, std::move(item));
	}
	return req_id;
}

std::uint64_t SaacClient::requestLock(const std::string & /*cdkey*/,
                                      AccountLockType /*lock_type*/,
                                      LockCallback cb,
                                      std::int64_t timeout_ms)
{
	const std::uint64_t req_id = nextRequestId();
	const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	                                std::chrono::steady_clock::now().time_since_epoch())
	                                .count();

	InFlightItem item;
	item.envelope.instance_id = _instanceId;
	item.envelope.generation = _generation.load(std::memory_order_relaxed);
	item.envelope.request_id = req_id;
	item.envelope.deadline_ms = timeout_ms;
	item.expire_at_ms = now_ms + timeout_ms;
	item.req_type = PacketType::kLockReq;

	item.on_completion = [cb](SaacAccountStatus status, const std::vector<std::uint8_t> & /*payload*/)
	{
		if (cb)
		{
			cb(status, "lease_token_ok");
		}
	};

	item.on_timeout = [cb]()
	{
		if (cb)
		{
			cb(SaacAccountStatus::kTimeout, "");
		}
	};

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_inFlight.emplace(req_id, std::move(item));
	}
	return req_id;
}

std::uint64_t SaacClient::requestUnlock(const std::string & /*cdkey*/,
                                        const std::string & /*lease_token*/,
                                        LockCallback cb,
                                        std::int64_t timeout_ms)
{
	const std::uint64_t req_id = nextRequestId();
	const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	                                std::chrono::steady_clock::now().time_since_epoch())
	                                .count();

	InFlightItem item;
	item.envelope.instance_id = _instanceId;
	item.envelope.generation = _generation.load(std::memory_order_relaxed);
	item.envelope.request_id = req_id;
	item.envelope.deadline_ms = timeout_ms;
	item.expire_at_ms = now_ms + timeout_ms;
	item.req_type = PacketType::kLockReq;

	item.on_completion = [cb](SaacAccountStatus status, const std::vector<std::uint8_t> & /*payload*/)
	{
		if (cb)
		{
			cb(status, "");
		}
	};

	item.on_timeout = [cb]()
	{
		if (cb)
		{
			cb(SaacAccountStatus::kTimeout, "");
		}
	};

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_inFlight.emplace(req_id, std::move(item));
	}
	return req_id;
}

std::uint64_t SaacClient::broadcastWorldMessage(int channel,
                                                const std::string &sender,
                                                const std::string &text,
                                                BroadcastCallback cb)
{
	const std::uint64_t req_id = nextRequestId();

	WorldBroadcastMessage msg;
	msg.channel = channel;
	msg.sender = sender;
	msg.text = text;
	msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	                       std::chrono::steady_clock::now().time_since_epoch())
	                       .count();

	std::vector<BroadcastListener> listeners_copy;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		listeners_copy = _broadcastListeners;
	}

	for (const auto &listener : listeners_copy)
	{
		if (listener)
			listener(msg);
	}

	if (cb)
	{
		cb(true);
	}
	return req_id;
}

void SaacClient::registerBroadcastListener(BroadcastListener listener)
{
	if (!listener)
		return;
	std::lock_guard<std::mutex> lock(_mutex);
	_broadcastListeners.push_back(std::move(listener));
}

void SaacClient::tick(std::int64_t now_ms)
{
	std::vector<std::function<void()>> timeouts;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		for (auto it = _inFlight.begin(); it != _inFlight.end();)
		{
			if (it->second.expire_at_ms <= now_ms)
			{
				if (it->second.on_timeout)
				{
					timeouts.push_back(std::move(it->second.on_timeout));
				}
				it = _inFlight.erase(it);
				_timeoutDropCount.fetch_add(1, std::memory_order_relaxed);
			}
			else
			{
				++it;
			}
		}
	}

	for (auto &t : timeouts)
	{
		t();
	}
}

std::size_t SaacClient::inFlightCount() const noexcept
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _inFlight.size();
}

bool SaacClient::feedResponse(const ResponseEnvelope &header,
                              std::function<void()> payload_dispatcher)
{
	// 世代号与实例安全性强检查 (消弭原版 fdid 归零错发致命缺陷 C33)
	if (header.instance_id != _instanceId ||
	    header.generation != _generation.load(std::memory_order_relaxed))
	{
		_staleDropCount.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	InFlightItem item;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto it = _inFlight.find(header.request_id);
		if (it == _inFlight.end())
		{
			return false;
		}
		item = std::move(it->second);
		_inFlight.erase(it);
	}

	if (payload_dispatcher)
	{
		payload_dispatcher();
	}

	if (item.on_completion)
	{
		item.on_completion(header.status, {});
	}
	return true;
}

bool SaacClient::completeInFlight(std::uint64_t request_id, SaacAccountStatus status,
                                  const std::vector<std::uint8_t> &payload)
{
	InFlightItem item;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto it = _inFlight.find(request_id);
		if (it == _inFlight.end())
		{
			return false;
		}
		item = std::move(it->second);
		_inFlight.erase(it);
	}

	if (item.on_completion)
	{
		item.on_completion(status, payload);
	}
	return true;
}

} // namespace SA::Saac
