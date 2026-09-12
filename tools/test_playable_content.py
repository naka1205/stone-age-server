#!/usr/bin/env python3
"""Regression for REAL bitmap orientation at the PNG export boundary."""
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from build_playable_content import blit_bottom_up, map_bitmap, png


class BitmapOrientationTest(unittest.TestCase):
    def test_map_editor_markers_never_become_visible_graphics(self):
        # The original bitmap lookup includes debug art for invisible markers.
        reverse = {2: 123, 99: 456, 100: 789}
        self.assertEqual([map_bitmap(value, reverse) for value in (0, 2, 33, 99, 100)],
                         [0, 0, 0, 0, 789])
        with self.assertRaises(KeyError):
            map_bitmap(101, reverse)

    def test_asymmetric_bitmaps_keep_their_own_top_edge_in_packed_png(self):
        transparent = bytes((0, 0, 0, 0))
        red = bytes((255, 0, 0, 255))
        green = bytes((0, 255, 0, 255))
        blue = bytes((0, 0, 255, 255))
        yellow = bytes((255, 255, 0, 255))
        cyan = bytes((0, 255, 255, 255))
        palette = [transparent, red, green, blue, yellow, cyan]
        page = bytearray(5 * 5 * 4)
        # Displayed 2x3 bitmap: red/clear, green/yellow, blue/cyan.
        # REAL stores the bottom scanline first, with columns still left-to-right.
        blit_bottom_up(page, 5, 1, 1, 2, 3, bytes((3, 5, 2, 4, 1, 0)), palette)
        # A separate image at another atlas position must not move with the first.
        blit_bottom_up(page, 5, 4, 0, 1, 2, bytes((3, 1)), palette)
        expected = [
            [transparent, transparent, transparent, transparent, red],
            [transparent, red, transparent, transparent, blue],
            [transparent, green, yellow, transparent, transparent],
            [transparent, blue, cyan, transparent, transparent],
            [transparent, transparent, transparent, transparent, transparent],
        ]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'atlas.png'
            png(output, 5, 5, page)
            data = output.read_bytes()
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        offset = 8
        compressed = bytearray()
        while offset < len(data):
            size = struct.unpack_from('>I', data, offset)[0]
            if data[offset + 4:offset + 8] == b'IDAT':
                compressed += data[offset + 8:offset + 8 + size]
            offset += size + 12
        self.assertEqual(zlib.decompress(compressed),
                         b''.join(b'\0' + b''.join(row) for row in expected))


if __name__ == '__main__':
    unittest.main()
