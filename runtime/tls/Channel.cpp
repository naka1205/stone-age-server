#include "tls/Channel.h"

#include <algorithm>
#include <deque>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

namespace SA::TLS
{
namespace
{
constexpr std::size_t kLimit = 4u * 1024u * 1024u;
}

struct Context::Impl
{
	SSL_CTX *ctx = nullptr;
	bool server = false;
	~Impl() { SSL_CTX_free(ctx); }
};
Context::Context() : _impl(std::make_shared<Impl>()) {}
Context::~Context() = default;

std::unique_ptr<Context> Context::server(const std::string &certificate, const std::string &key,
                                         std::string &error)
{
	std::unique_ptr<Context> out(new Context);
	auto &d = *out->_impl;
	d.server = true;
	d.ctx = SSL_CTX_new(TLS_server_method());
	if (!d.ctx || SSL_CTX_set_min_proto_version(d.ctx, TLS1_2_VERSION) != 1 ||
	    SSL_CTX_use_certificate_chain_file(d.ctx, certificate.c_str()) != 1 ||
	    SSL_CTX_use_PrivateKey_file(d.ctx, key.c_str(), SSL_FILETYPE_PEM) != 1 ||
	    SSL_CTX_check_private_key(d.ctx) != 1)
	{
		error = "TLS certificate/private key configuration failed";
		return nullptr;
	}
	SSL_CTX_set_options(d.ctx, SSL_OP_NO_RENEGOTIATION);
	error.clear();
	return out;
}

std::unique_ptr<Context> Context::client(const std::string &ca, std::string &error)
{
	std::unique_ptr<Context> out(new Context);
	auto &d = *out->_impl;
	d.ctx = SSL_CTX_new(TLS_client_method());
	if (!d.ctx || SSL_CTX_set_min_proto_version(d.ctx, TLS1_2_VERSION) != 1 ||
	    (ca.empty() ? SSL_CTX_set_default_verify_paths(d.ctx)
	                : SSL_CTX_load_verify_locations(d.ctx, ca.c_str(), nullptr)) != 1)
	{
		error = "TLS trust store configuration failed";
		return nullptr;
	}
	SSL_CTX_set_verify(d.ctx, SSL_VERIFY_PEER, nullptr);
	SSL_CTX_set_options(d.ctx, SSL_OP_NO_RENEGOTIATION);
	error.clear();
	return out;
}

struct Channel::Impl
{
	std::shared_ptr<Context::Impl> context;
	SSL *ssl = nullptr;
	bool ready = false;
	std::string error;
	std::deque<std::vector<std::uint8_t>> writes;
	std::size_t offset = 0;
	std::size_t queued = 0;
	std::vector<std::uint8_t> plain;
	~Impl() { SSL_free(ssl); }
	bool check(int result)
	{
		if (result > 0)
			return true;
		const int reason = SSL_get_error(ssl, result);
		if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE)
			return true;
		error = reason == SSL_ERROR_ZERO_RETURN ? "TLS peer closed" : "TLS handshake or record verification failed";
		return false;
	}
};

Channel::Channel(Context &context, const std::string &peer_name) : _impl(new Impl)
{
	auto &d = *_impl;
	d.context = context._impl;
	d.ssl = SSL_new(d.context->ctx);
	if (!d.ssl)
	{
		d.error = "TLS allocation failed";
		return;
	}
	BIO *input = BIO_new(BIO_s_mem());
	BIO *output = BIO_new(BIO_s_mem());
	if (!input || !output)
	{
		BIO_free(input);
		BIO_free(output);
		d.error = "TLS buffer allocation failed";
		return;
	}
	BIO_set_mem_eof_return(input, -1);
	BIO_set_mem_eof_return(output, -1);
	SSL_set_bio(d.ssl, input, output);
	SSL_set_mode(d.ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
	if (d.context->server)
		SSL_set_accept_state(d.ssl);
	else
	{
		SSL_set_connect_state(d.ssl);
		auto *param = SSL_get0_param(d.ssl);
		X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
		if (peer_name.empty())
			d.error = "TLS peer name is required";
		else if (X509_VERIFY_PARAM_set1_ip_asc(param, peer_name.c_str()) != 1)
		{
			ERR_clear_error();
			if (SSL_set1_host(d.ssl, peer_name.c_str()) != 1 ||
			    SSL_set_tlsext_host_name(d.ssl, peer_name.c_str()) != 1)
				d.error = "TLS peer name configuration failed";
		}
	}
}
Channel::~Channel() = default;

bool Channel::feed(const std::uint8_t *data, std::size_t size)
{
	auto &d = *_impl;
	if (!d.error.empty())
		return false;
	if (size > kLimit || BIO_ctrl_pending(SSL_get_rbio(d.ssl)) + size > kLimit ||
	    BIO_write(SSL_get_rbio(d.ssl), data, static_cast<int>(size)) != static_cast<int>(size))
	{
		d.error = "TLS input exceeds limit";
		return false;
	}
	return advance();
}

bool Channel::send(const std::uint8_t *data, std::size_t size)
{
	auto &d = *_impl;
	if (!d.ready || !d.error.empty())
		return false;
	if (size > kLimit || d.queued + size > kLimit)
	{
		d.error = "TLS output exceeds limit";
		return false;
	}
	if (size)
	{
		d.writes.emplace_back(data, data + size);
		d.queued += size;
	}
	return advance();
}

bool Channel::advance()
{
	auto &d = *_impl;
	if (!d.error.empty())
		return false;
	if (!d.ready)
	{
		ERR_clear_error();
		const int rc = SSL_do_handshake(d.ssl);
		if (!d.check(rc))
			return false;
		d.ready = rc == 1;
		if (!d.ready)
			return true;
	}
	std::uint8_t buffer[16384];
	for (int i = 0; i < 64; ++i)
	{
		ERR_clear_error();
		const int count = SSL_read(d.ssl, buffer, static_cast<int>(sizeof(buffer)));
		if (!d.check(count))
			return false;
		if (count <= 0)
			break;
		if (d.plain.size() + static_cast<std::size_t>(count) > kLimit)
		{
			d.error = "TLS plaintext exceeds limit";
			return false;
		}
		d.plain.insert(d.plain.end(), buffer, buffer + count);
	}
	while (!d.writes.empty() && BIO_ctrl_pending(SSL_get_wbio(d.ssl)) < 65536)
	{
		const auto &front = d.writes.front();
		ERR_clear_error();
		const auto size = std::min<std::size_t>(16384, front.size() - d.offset);
		const int count = SSL_write(d.ssl, front.data() + d.offset, static_cast<int>(size));
		if (!d.check(count))
			return false;
		if (count <= 0)
			break;
		d.offset += static_cast<std::size_t>(count);
		d.queued -= static_cast<std::size_t>(count);
		if (d.offset == front.size())
		{
			d.writes.pop_front();
			d.offset = 0;
		}
	}
	return true;
}

void Channel::takeEncrypted(std::vector<std::uint8_t> &out)
{
	auto &d = *_impl;
	std::uint8_t buffer[16384];
	while (d.ssl && BIO_ctrl_pending(SSL_get_wbio(d.ssl)))
	{
		const int count = BIO_read(SSL_get_wbio(d.ssl), buffer, static_cast<int>(sizeof(buffer)));
		if (count <= 0)
			break;
		out.insert(out.end(), buffer, buffer + count);
	}
}
void Channel::takePlain(std::vector<std::uint8_t> &out)
{
	auto &plain = _impl->plain;
	out.insert(out.end(), plain.begin(), plain.end());
	plain.clear();
}
bool Channel::ready() const noexcept { return _impl->ready; }
const std::string &Channel::error() const noexcept { return _impl->error; }
std::size_t Channel::pending() const noexcept { return _impl->queued; }
} // namespace SA::TLS
