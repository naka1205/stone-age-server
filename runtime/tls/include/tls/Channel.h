#ifndef __SA_TlsChannel_H__
#define __SA_TlsChannel_H__

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace SA::TLS
{
// Memory BIOs let each native reactor retain ownership of socket I/O.
class Context
{
  public:
	static std::unique_ptr<Context> server(const std::string &certificate, const std::string &key,
	                                       std::string &error);
	static std::unique_ptr<Context> client(const std::string &ca, std::string &error);
	~Context();
	Context(const Context &) = delete;
	Context &operator=(const Context &) = delete;

  private:
	Context();
	struct Impl;
	std::shared_ptr<Impl> _impl;
	friend class Channel;
};

class Channel
{
  public:
	explicit Channel(Context &context, const std::string &peer_name = {});
	~Channel();
	Channel(const Channel &) = delete;
	Channel &operator=(const Channel &) = delete;
	bool feed(const std::uint8_t *data, std::size_t size);
	bool send(const std::uint8_t *data, std::size_t size);
	bool advance();
	void takeEncrypted(std::vector<std::uint8_t> &out);
	void takePlain(std::vector<std::uint8_t> &out);
	bool ready() const noexcept;
	const std::string &error() const noexcept;
	std::size_t pending() const noexcept;

  private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
} // namespace SA::TLS
#endif
