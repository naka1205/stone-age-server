// src/world/Map.cpp —— 地图通行性与 fixture(批次 W.1)。声明见 include/world/Api.h(地图节)。

#include "world/Api.h"

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

} // namespace SA::World
