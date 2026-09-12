#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "tls/Channel.h"
#include <chrono>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <string>
#include <vector>

namespace
{
struct Certificate
{
	std::filesystem::path root;
	std::string cert;
	std::string key;
	Certificate()
	{
		root = std::filesystem::temp_directory_path() / ("sa-tls-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directory(root);
		cert = (root / "cert.pem").string();
		key = (root / "key.pem").string();
		EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
		REQUIRE(context != nullptr);
		REQUIRE(EVP_PKEY_keygen_init(context) == 1);
		REQUIRE(EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) == 1);
		EVP_PKEY *private_key = nullptr;
		REQUIRE(EVP_PKEY_keygen(context, &private_key) == 1);
		EVP_PKEY_CTX_free(context);
		X509 *certificate = X509_new();
		REQUIRE(certificate != nullptr);
		REQUIRE(X509_set_version(certificate, 2) == 1);
		REQUIRE(ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1) == 1);
		X509_gmtime_adj(X509_getm_notBefore(certificate), -60);
		X509_gmtime_adj(X509_getm_notAfter(certificate), 3600);
		REQUIRE(X509_set_pubkey(certificate, private_key) == 1);
		X509_NAME *name = X509_get_subject_name(certificate);
		REQUIRE(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("localhost"), -1, -1, 0) == 1);
		REQUIRE(X509_set_issuer_name(certificate, name) == 1);
		X509V3_CTX extension_context;
		X509V3_set_ctx(&extension_context, certificate, certificate, nullptr, nullptr, 0);
		for (const auto &entry : {std::pair<int, const char *>{NID_basic_constraints, "critical,CA:TRUE"}, {NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1"}})
		{
			X509_EXTENSION *extension = X509V3_EXT_conf_nid(nullptr, &extension_context, entry.first, entry.second);
			REQUIRE(extension != nullptr);
			REQUIRE(X509_add_ext(certificate, extension, -1) == 1);
			X509_EXTENSION_free(extension);
		}
		REQUIRE(X509_sign(certificate, private_key, EVP_sha256()) > 0);
		BIO *output = BIO_new(BIO_s_mem());
		REQUIRE(PEM_write_bio_X509(output, certificate) == 1);
		char *bytes = nullptr;
		long size = BIO_get_mem_data(output, &bytes);
		std::ofstream(cert, std::ios::binary).write(bytes, size);
		BIO_reset(output);
		REQUIRE(PEM_write_bio_PrivateKey(output, private_key, nullptr, nullptr, 0, nullptr, nullptr) == 1);
		size = BIO_get_mem_data(output, &bytes);
		std::ofstream(key, std::ios::binary).write(bytes, size);
		BIO_free(output);
		X509_free(certificate);
		EVP_PKEY_free(private_key);
	}
	~Certificate()
	{
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}
};

bool transfer(SA::TLS::Channel &from, SA::TLS::Channel &to)
{
	if (!from.advance())
		return false;
	std::vector<std::uint8_t> wire;
	from.takeEncrypted(wire);
	for (std::size_t offset = 0; offset < wire.size();)
	{
		const auto size = std::min<std::size_t>(31, wire.size() - offset);
		if (!to.feed(wire.data() + offset, size))
			return false;
		offset += size;
	}
	return true;
}
} // namespace

TEST_CASE("TLS validates peer identity and tolerates fragmented handshake and records")
{
	Certificate files;
	std::string error;
	auto server_context = SA::TLS::Context::server(files.cert, files.key, error);
	auto client_context = SA::TLS::Context::client(files.cert, error);
	REQUIRE(server_context);
	REQUIRE(client_context);
	SA::TLS::Channel server(*server_context), client(*client_context, "127.0.0.1");
	CHECK_FALSE(client.ready());
	for (int i = 0; i < 32 && (!server.ready() || !client.ready()); ++i)
	{
		REQUIRE(transfer(client, server));
		REQUIRE(transfer(server, client));
	}
	REQUIRE(server.ready());
	REQUIRE(client.ready());
	std::vector<std::uint8_t> message(128u * 1024u);
	for (std::size_t i = 0; i < message.size(); ++i)
		message[i] = static_cast<std::uint8_t>(i & 255u);
	REQUIRE(client.send(message.data(), message.size()));
	std::vector<std::uint8_t> decoded;
	for (int i = 0; i < 64 && decoded.size() < message.size(); ++i)
	{
		REQUIRE(transfer(client, server));
		REQUIRE(transfer(server, client));
		server.takePlain(decoded);
	}
	CHECK(decoded == message);
	REQUIRE(client.send(message.data(), 32));
	std::vector<std::uint8_t> corrupt;
	client.takeEncrypted(corrupt);
	REQUIRE(!corrupt.empty());
	corrupt.back() ^= 1;
	CHECK_FALSE(server.feed(corrupt.data(), corrupt.size()));
}

TEST_CASE("TLS rejects a trusted certificate for the wrong host")
{
	Certificate files;
	std::string error;
	auto server_context = SA::TLS::Context::server(files.cert, files.key, error);
	auto client_context = SA::TLS::Context::client(files.cert, error);
	REQUIRE(server_context);
	REQUIRE(client_context);
	SA::TLS::Channel server(*server_context), client(*client_context, "wrong.invalid");
	bool accepted = true;
	for (int i = 0; i < 32 && accepted; ++i)
		accepted = transfer(client, server) && transfer(server, client);
	CHECK_FALSE(accepted);
	CHECK_FALSE(client.ready());
}
