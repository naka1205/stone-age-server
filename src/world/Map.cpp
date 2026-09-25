// src/world/Map.cpp —— 地图通行性与 fixture(批次 W.1)。声明见 include/world/Api.h(地图节)。

#include "world/Api.h"

#include <cstring>
#include <fstream>

namespace SA::World
{

bool mapWalkable(const GridMap &map, const TileAttrTable &attr, std::int32_t x,
                 std::int32_t y) noexcept
{
	// 原版 getTileAndObjData 取不到即 FALSE(map_deal.c:28)。
	if (!map.inBounds(x, y))
		return false;

	// ★ 主判据是 **obj 层**(map_deal.c:40 `switch(getImageInt(map[1], WALKABLE))`)。
	switch (attr.kindOf(map.objAt(x, y)))
	{
	case WalkKind::kBlocked:
		return false;
	case WalkKind::kNeedBoth:
		// obj 层 == 1 ⇒ tile 层也必须 == 1(:45)。
		return attr.kindOf(map.tileAt(x, y)) == WalkKind::kNeedBoth;
	case WalkKind::kFree:
		return true;
	}
	// default(:54):图元属性表里的未知值 ⇒ 不可走。
	return false;
}

GridMap makeFixtureMap(std::int32_t width, std::int32_t height)
{
	GridMap m;
	m.width = width;
	m.height = height;
	const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	// 全地面(图元号 2 = kFree)。调用方按需把某格 obj 改为 0(墙)/ 1(需双)。
	m.tile.assign(n, TileId{2});
	m.obj.assign(n, TileId{2});
	return m;
}

TileAttrTable makeFixtureAttr()
{
	TileAttrTable a;
	// 图元号 → 通行性:0 墙 / 1 需双 / 2 地面。下标即图元号。
	a.walkable = {WalkKind::kBlocked, WalkKind::kNeedBoth, WalkKind::kFree};
	return a;
}

std::optional<Ls2MapInfo> parseLs2Map(std::span<const std::uint8_t> bytes)
{
	// 1. 最小头长 44 字节 (10 §3.1)
	if (bytes.size() < 44)
		return std::nullopt;

	// 2. 魔数 "LS2MAP"
	if (std::memcmp(bytes.data(), "LS2MAP", 6) != 0)
		return std::nullopt;

	const auto readU16BE = [](const std::uint8_t *p) noexcept -> std::uint16_t
	{
		return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
	};

	Ls2MapInfo info;
	info.floor_id = readU16BE(bytes.data() + 6);

	// showstring: offset 8 长度 32 (GBK/字面, 遇到 \0 截断)
	const char *name_ptr = reinterpret_cast<const char *>(bytes.data() + 8);
	std::size_t name_len = 0;
	while (name_len < 32 && name_ptr[name_len] != '\0')
		++name_len;
	info.show_name.assign(name_ptr, name_len);

	const std::uint16_t xsiz = readU16BE(bytes.data() + 40);
	const std::uint16_t ysiz = readU16BE(bytes.data() + 42);
	if (xsiz == 0 || ysiz == 0)
		return std::nullopt;

	info.width = static_cast<std::int32_t>(xsiz);
	info.height = static_cast<std::int32_t>(ysiz);

	const std::size_t count = static_cast<std::size_t>(xsiz) * static_cast<std::size_t>(ysiz);
	// 校验数据区长度: 44 + 2 * count (tile) + 2 * count (obj) = 44 + 4 * count
	if (bytes.size() < 44 + 4 * count)
		return std::nullopt;

	info.grid.width = info.width;
	info.grid.height = info.height;
	info.grid.tile.resize(count);
	info.grid.obj.resize(count);

	const std::uint8_t *tile_ptr = bytes.data() + 44;
	const std::uint8_t *obj_ptr = bytes.data() + 44 + 2 * count;

	for (std::size_t i = 0; i < count; ++i)
	{
		info.grid.tile[i] = static_cast<TileId>(readU16BE(tile_ptr + 2 * i));
		info.grid.obj[i] = static_cast<TileId>(readU16BE(obj_ptr + 2 * i));
	}

	return info;
}

std::optional<Ls2MapInfo> loadLs2MapFile(const std::string &filepath)
{
	std::ifstream input(filepath, std::ios::binary | std::ios::ate);
	if (!input || input.tellg() < 0)
		return std::nullopt;

	const auto size = static_cast<std::uint64_t>(input.tellg());
	if (size < 44 || size > 64u * 1024u * 1024u)
		return std::nullopt;

	std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
	input.seekg(0, std::ios::beg);
	if (!input.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size())))
		return std::nullopt;

	return parseLs2Map(buffer);
}

std::vector<std::uint8_t> decodeBase64(std::string_view in)
{
	std::vector<std::uint8_t> out;
	out.reserve((in.size() * 3) / 4);
	std::uint32_t val = 0;
	int bits = -8;
	for (const char ch : in)
	{
		const auto c = static_cast<unsigned char>(ch);
		int d = -1;
		if (c >= 'A' && c <= 'Z')
			d = c - 'A';
		else if (c >= 'a' && c <= 'z')
			d = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			d = c - '0' + 52;
		else if (c == '+')
			d = 62;
		else if (c == '/')
			d = 63;
		else if (c == '=')
			break;
		else
			continue;
		val = (val << 6) | static_cast<std::uint32_t>(d);
		bits += 6;
		if (bits >= 0)
		{
			out.push_back(static_cast<std::uint8_t>((val >> bits) & 0xFF));
			bits -= 8;
		}
	}
	return out;
}

} // namespace SA::World
