#ifndef __SA_Saac_Client_H__
#define __SA_Saac_Client_H__

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "saac/Api.h"

namespace SA::Saac
{

struct InFlightItem
{
	RequestEnvelope envelope;
	std::int64_t expire_at_ms = 0;
	PacketType req_type = PacketType::kNone;

	std::function<void(SaacAccountStatus status, const std::vector<std::uint8_t> &payload)> on_completion;
	std::function<void()> on_timeout;
};

class SaacClient : public ISaacClient
{
  public:
	SaacClient(std::uint32_t instance_id, std::uint32_t generation);
	~SaacClient() override = default;

	std::uint32_t instanceId() const noexcept override { return _instanceId; }
	std::uint32_t generation() const noexcept override { return _generation.load(std::memory_order_relaxed); }
	void setGeneration(std::uint32_t gen) noexcept override { _generation.store(gen, std::memory_order_relaxed); }

	std::uint64_t requestLogin(const std::string &cdkey,
	                           const std::string &password,
	                           LoginCallback cb,
	                           std::int64_t timeout_ms = 5000) override;

	std::uint64_t requestCharLoad(const std::string &cdkey,
	                              int slot,
	                              CharLoadCallback cb,
	                              std::int64_t timeout_ms = 5000) override;

	std::uint64_t requestCharSave(const std::string &cdkey,
	                              int slot,
	                              const SA::Domain::CharacterRecord &record,
	                              bool logout,
	                              const std::string &lease_token,
	                              CharSaveCallback cb,
	                              std::int64_t timeout_ms = 5000) override;

	std::uint64_t requestLock(const std::string &cdkey,
	                          AccountLockType lock_type,
	                          LockCallback cb,
	                          std::int64_t timeout_ms = 5000) override;

	std::uint64_t requestUnlock(const std::string &cdkey,
	                            const std::string &lease_token,
	                            LockCallback cb,
	                            std::int64_t timeout_ms = 5000) override;

	std::uint64_t broadcastWorldMessage(int channel,
	                                    const std::string &sender,
	                                    const std::string &text,
	                                    BroadcastCallback cb = nullptr) override;

	void registerBroadcastListener(BroadcastListener listener) override;

	void tick(std::int64_t now_ms) override;

	std::size_t inFlightCount() const noexcept override;
	std::uint64_t staleDropCount() const noexcept override { return _staleDropCount.load(std::memory_order_relaxed); }
	std::uint64_t timeoutDropCount() const noexcept override { return _timeoutDropCount.load(std::memory_order_relaxed); }

	bool feedResponse(const ResponseEnvelope &header,
	                  std::function<void()> payload_dispatcher = nullptr) override;

	bool completeInFlight(std::uint64_t request_id, SaacAccountStatus status,
	                      const std::vector<std::uint8_t> &payload = {}) override;

  private:
	std::uint64_t nextRequestId() noexcept;

	std::uint32_t _instanceId = 1;
	std::atomic<std::uint32_t> _generation{1};
	std::atomic<std::uint64_t> _reqCounter{1};

	std::atomic<std::uint64_t> _staleDropCount{0};
	std::atomic<std::uint64_t> _timeoutDropCount{0};

	mutable std::mutex _mutex;
	std::unordered_map<std::uint64_t, InFlightItem> _inFlight;

	std::vector<BroadcastListener> _broadcastListeners;
};

} // namespace SA::Saac

#endif // __SA_Saac_Client_H__
