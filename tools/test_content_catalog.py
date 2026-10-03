#!/usr/bin/env python3
"""tools/test_content_catalog.py —— 大世界全量资产编目数据合法性与回归测试"""

import json
from pathlib import Path
import sys
import unittest

# ★ Windows 控制台的 cp936 / cp1252 编不出非 ASCII 字符 (ci_verify §0 强制守卫)
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


class TestContentCatalog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parent.parent / "content/catalog"
        cls.maps_file = cls.root / "maps_catalog.json"
        cls.warps_file = cls.root / "warps_catalog.json"
        cls.npcs_file = cls.root / "npcs_catalog.json"
        cls.summary_file = cls.root / "summary.json"

    def test_catalog_files_exist(self):
        self.assertTrue(self.maps_file.exists(), "maps_catalog.json 缺失")
        self.assertTrue(self.warps_file.exists(), "warps_catalog.json 缺失")
        self.assertTrue(self.npcs_file.exists(), "npcs_catalog.json 缺失")
        self.assertTrue(self.summary_file.exists(), "summary.json 缺失")

    def test_maps_integrity(self):
        maps = json.loads(self.maps_file.read_text(encoding="utf-8"))
        self.assertGreater(len(maps), 1000, "地图数量应大于 1000")
        # 验证核心主村存在 (例如 1000 萨姆吉尔, 2000 玛丽娜斯)
        sample = next(iter(maps.values()))
        self.assertIn("name", sample)
        self.assertIn("width", sample)
        self.assertIn("height", sample)
        self.assertGreater(sample["width"], 0)
        self.assertGreater(sample["height"], 0)

    def test_warps_integrity(self):
        warps = json.loads(self.warps_file.read_text(encoding="utf-8"))
        self.assertGreater(len(warps), 5000, "传送点数量应大于 5000")
        w = warps[0]
        self.assertIn("src_floor", w)
        self.assertIn("src_x", w)
        self.assertIn("src_y", w)
        self.assertIn("dst_floor", w)
        self.assertIn("dst_x", w)
        self.assertIn("dst_y", w)
        self.assertGreater(w["src_floor"], 0)
        self.assertGreater(w["dst_floor"], 0)

    def test_npcs_classification(self):
        npcs = json.loads(self.npcs_file.read_text(encoding="utf-8"))
        self.assertGreater(len(npcs), 3000, "NPC 数量应大于 3000")
        categories = {n["category"] for n in npcs}
        expected = {"exchangeman", "enemy", "townpeople", "shop", "warpman", "signboard", "healer"}
        self.assertTrue(expected.issubset(categories), f"NPC 分类不完整: {categories}")
        for n in npcs[:50]:
            self.assertGreater(n["id"], 0)
            self.assertGreater(n["floor"], 0)
            self.assertGreaterEqual(n["x"], 0)
            self.assertGreaterEqual(n["y"], 0)
            self.assertGreaterEqual(n["image"], 0)


if __name__ == '__main__':
    unittest.main()
