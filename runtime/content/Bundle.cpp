#include "content/Bundle.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <openssl/evp.h>
#include <stdexcept>

namespace SA::Content
{
namespace
{
std::string read(const std::string &path, std::size_t limit)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input || input.tellg() < 0 || static_cast<std::uint64_t>(input.tellg()) > limit)
		throw std::runtime_error("content file missing or oversized: " + path);
	std::string out(static_cast<std::size_t>(input.tellg()), '\0');
	input.seekg(0);
	if (!out.empty() && !input.read(out.data(), static_cast<std::streamsize>(out.size())))
		throw std::runtime_error("content short read");
	return out;
}
void verify(const std::string &bytes, const std::string &expected)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	if (EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr) != 1)
		throw std::runtime_error("content digest failed");
	const char digits[] = "0123456789abcdef";
	std::string actual;
	for (unsigned int i = 0; i < size; ++i)
	{
		actual += digits[digest[i] >> 4u];
		actual += digits[digest[i] & 15u];
	}
	if (actual != expected)
		throw std::runtime_error("content digest mismatch");
}
std::uint32_t u32(const std::string &bytes, std::size_t offset)
{
	std::uint32_t out = 0;
	for (std::size_t i = 0; i < 4; ++i)
		out |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes.at(offset + i))) << (i * 8u);
	return out;
}
} // namespace
const SA::Data::Json::Value &field(const SA::Data::Json::Value &object, const char *name)
{
	const auto *value = object.find(name);
	if (!value)
		throw std::invalid_argument(std::string("missing content field: ") + name);
	return *value;
}
std::int32_t integer(const SA::Data::Json::Value &value)
{
	if (!value.isNumber() || std::trunc(value.asNumber()) != value.asNumber() ||
	    value.asNumber() < std::numeric_limits<std::int32_t>::min() || value.asNumber() > std::numeric_limits<std::int32_t>::max())
		throw std::invalid_argument("content integer out of range");
	return static_cast<std::int32_t>(value.asNumber());
}
std::string text(const SA::Data::Json::Value &value)
{
	if (!value.isString())
		throw std::invalid_argument("content string required");
	return value.asString();
}
SA::Data::Json::Value readJson(const std::string &path)
{
	const auto parsed = SA::Data::Json::parse(read(path, 16u * 1024u * 1024u));
	if (!parsed.ok)
		throw std::invalid_argument("invalid content JSON: " + path);
	return parsed.value;
}
Bundle load(const std::string &directory, bool verify_graphics)
{
	Bundle out;
	out.directory = directory;
	out.manifest = readJson(directory + "/manifest.json");
	if (integer(field(out.manifest, "schema_ver")) != 1)
		throw std::invalid_argument("unsupported content schema");
	out.version = text(field(out.manifest, "content_version"));
	if (out.version.empty() || out.version.size() > 63)
		throw std::invalid_argument("invalid content version");
	const auto map = read(directory + "/map.bin", 64u * 1024u * 1024u);
	verify(map, text(field(out.manifest, "map_sha256")));
	if (map.size() < 16 || map.compare(0, 4, "SAM1") != 0)
		throw std::invalid_argument("invalid map header");
	const auto width = u32(map, 8), height = u32(map, 12), floor = u32(map, 4);
	if (!width || !height || width > 2048 || height > 2048 || floor > 65535)
		throw std::invalid_argument("invalid map dimensions");
	const auto count = static_cast<std::size_t>(width) * height;
	if (map.size() != 16 + count * 9)
		throw std::invalid_argument("invalid map size");
	out.width = static_cast<std::int32_t>(width);
	out.height = static_cast<std::int32_t>(height);
	out.floor = static_cast<std::int32_t>(floor);
	if (out.width != integer(field(out.manifest, "width")) || out.height != integer(field(out.manifest, "height")))
		throw std::invalid_argument("map/manifest mismatch");
	out.tiles.reserve(count);
	out.objects.reserve(count);
	out.walkable.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		out.tiles.push_back(u32(map, 16 + i * 4));
		out.objects.push_back(u32(map, 16 + count * 4 + i * 4));
		const auto walkable = static_cast<std::uint8_t>(map[16 + count * 8 + i]);
		if (walkable > 1)
			throw std::invalid_argument("invalid collision value");
		out.walkable.push_back(walkable);
	}
	const auto world = read(directory + "/world.json", 4u * 1024u * 1024u);
	verify(world, text(field(out.manifest, "world_sha256")));
	const auto parsed = SA::Data::Json::parse(world);
	if (!parsed.ok || integer(field(parsed.value, "schema_ver")) != 1 || integer(field(parsed.value, "floor")) != out.floor)
		throw std::invalid_argument("invalid world content");
	out.world = parsed.value;
	if (verify_graphics)
	{
		for (const auto &page : field(out.manifest, "pages").asObject())
		{
			if (std::filesystem::path(page.first).filename() != page.first)
				throw std::invalid_argument("invalid atlas path");
			verify(read(directory + "/" + page.first, 32u * 1024u * 1024u), text(page.second));
		}
		const auto &bitmaps = field(out.manifest, "bitmaps");
		for (auto bitmap : out.tiles)
			if (bitmap && !bitmaps.find(std::to_string(bitmap)))
				throw std::invalid_argument("missing tile bitmap");
		for (auto bitmap : out.objects)
			if (bitmap && !bitmaps.find(std::to_string(bitmap)))
				throw std::invalid_argument("missing object bitmap");
	}
	return out;
}
} // namespace SA::Content
