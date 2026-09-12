#ifndef __SA_ContentBundle_H__
#define __SA_ContentBundle_H__
#include "data/Json.h"
#include <cstdint>
#include <string>
#include <vector>

namespace SA::Content
{
struct Bundle
{
	std::string directory;
	std::string version;
	std::int32_t floor = 0;
	std::int32_t width = 0;
	std::int32_t height = 0;
	std::vector<std::uint32_t> tiles;
	std::vector<std::uint32_t> objects;
	std::vector<std::uint8_t> walkable;
	SA::Data::Json::Value manifest;
	SA::Data::Json::Value world;
};
const SA::Data::Json::Value &field(const SA::Data::Json::Value &object, const char *name);
std::int32_t integer(const SA::Data::Json::Value &value);
std::string text(const SA::Data::Json::Value &value);
SA::Data::Json::Value readJson(const std::string &path);
// Startup/file-loading phase only. Hash and geometry errors throw; never silently repair content.
Bundle load(const std::string &directory, bool verify_graphics);
} // namespace SA::Content
#endif
