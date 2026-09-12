#!/usr/bin/env python3
"""Build the P2 map-100 slice from original client graphics and csa8.0 server data.

No original executable is run. Output is a versioned read-only content bundle.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import zlib

for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, 'reconfigure'):
        stream.reconfigure(encoding='utf-8', errors='replace')


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def png(path, width, height, pixels):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    rows = b''.join(b'\0' + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
                    chunk(b'IDAT', zlib.compress(rows, 7)) + chunk(b'IEND', b''))


def blit_bottom_up(page, page_width, x, y, width, height, pixels, rgba):
    """Place one decoded REAL bitmap into a top-down PNG atlas.

    sprmgr.cpp starts at bmpHeight - 1; directdraw.cpp advances the destination
    while subtracting bmpWidth from the source after each scanline. ADRN offsets
    already describe the displayed top-left corner and must remain unchanged.
    """
    if len(pixels) != width * height:
        raise ValueError('decoded bitmap size does not match its dimensions')
    for row in range(height):
        source = (height - 1 - row) * width
        destination = ((y + row) * page_width + x) * 4
        page[destination:destination + width * 4] = b''.join(
            rgba[pixel] for pixel in pixels[source:source + width])


def rows(path):
    # Encoding is strict: malformed names fail the import rather than disappearing.
    return [line.split(',') for line in path.read_bytes().decode('gbk').splitlines()
            if line.strip() and not line.lstrip().startswith('#')]


def number(row, index):
    return int(row[index].strip() or '0') if index < len(row) else 0


def map_bitmap(original, reverse):
    # map.cpp renders only IDs > CG_INVISIBLE (99, anim_tbl.h). Lower IDs
    # are collision/sound/editor markers even when ADRN has a debug bitmap.
    return reverse[original] if original > 99 else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--client-output', type=Path, required=True)
    parser.add_argument('--server-output', type=Path, required=True)
    args = parser.parse_args()
    root = args.source_root.resolve()
    client = root / 'SA8.0客户端'
    data = root / 'csa8.0/gmsv/data'
    output = args.client_output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    args.server_output.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('sa_original_decoder', root / 'stoneage-plan/tools/extract_samples.py')
    decoder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(decoder)
    used_sources = [client / 'data/adrn_136.bin', client / 'data/real_136.bin', client / 'data/spradrn_115.bin',
                    client / 'data/spr_115.bin', client / 'map/100.dat', data / 'map/sainasu/sainasu', data / 'map/mapset.txt',
                    data / 'encount.txt', data / 'group1.txt', data / 'enemy1.txt', data / 'enemybase1.txt',
                    root / 'stoneage-plan/tools/extract_samples.py']
    index = {}
    reverse = {0: 0}
    duplicate_ids = []
    skipped_reverse = []
    raw = used_sources[0].read_bytes()
    for offset in range(0, len(raw), 80):
        bitmap, address, size, x, y, width, height = struct.unpack_from('<IIIiiII', raw, offset)
        original = struct.unpack_from('<I', raw, offset + 76)[0]
        if bitmap in index:
            duplicate_ids.append(bitmap)
        index[bitmap] = (address, size, x, y, width, height)
        if original:
            if original <= 33 and bitmap > 230000:
                skipped_reverse.append(bitmap)
            else:
                reverse[original] = bitmap
    geometry = (client / 'map/100.dat').read_bytes()
    width, height = struct.unpack_from('<II', geometry)
    count = width * height
    assert len(geometry) == 8 + count * 6
    tiles = struct.unpack_from(f'<{count}H', geometry, 8)
    objects = struct.unpack_from(f'<{count}H', geometry, 8 + count * 2)
    server = (data / 'map/sainasu/sainasu').read_bytes()
    assert server[:6] == b'LS2MAP' and struct.unpack_from('>H', server, 6)[0] == 100
    assert struct.unpack_from('>HH', server, 40) == (width, height)
    server_tiles = struct.unpack_from(f'>{count}H', server, 44)
    server_objects = struct.unpack_from(f'>{count}H', server, 44 + count * 2)
    attributes = {}
    for line in (data / 'map/mapset.txt').read_bytes().decode('gbk').splitlines():
        fields = line.split()
        if not fields or not fields[0].isdigit():
            continue
        assert len(fields) == 20
        attributes[int(fields[0])] = int(fields[3])
    walkable = bytes(1 if attributes.get(obj, 0) == 2 or
                     attributes.get(obj, 0) == 1 and attributes.get(tile, 0) == 1 else 0
                     for tile, obj in zip(server_tiles, server_objects))
    spawn = (643, 459)
    if not walkable[spawn[1] * width + spawn[0]]:
        candidates = [(abs(x - spawn[0]) + abs(y - spawn[1]), x, y)
                      for y in range(445, 475) for x in range(630, 665) if walkable[y * width + x]]
        _, x, y = min(candidates)
        spawn = (x, y)
    # A real low-level encounter region; conditional anniversary group is retained.
    areas = [row for row in rows(data / 'encount.txt') if number(row, 0) == 21]
    assert len(areas) == 1 and number(areas[0], 1) == 100
    group_ids = {number(row, i) for row in areas for i in range(10, 20) if number(row, i)}
    groups = [row for row in rows(data / 'group1.txt') if number(row, 1) in group_ids]
    assert {number(row, 1) for row in groups} == group_ids
    enemy_ids = {number(row, i) for row in groups for i in range(4, 14) if number(row, i)}
    encounters = [row for row in rows(data / 'enemy1.txt') if number(row, 3) in enemy_ids]
    assert {number(row, 3) for row in encounters} == enemy_ids
    template_ids = {number(row, 4) for row in encounters}
    templates = [row for row in rows(data / 'enemybase1.txt') if number(row, 6) in template_ids]
    assert {number(row, 6) for row in templates} == template_ids
    images = {100000} | {number(row, 36) for row in templates}
    # Fully reviewed source patch families. None of this slice's actors belongs to one.
    patched_sprites = {1059, 1058, 1283, 1404, 1409, 373, 102, 260, 502, 382} | set(range(1965, 1987)) | set(range(1988, 1990)) | set(range(3347, 3371))
    assert not ({image - 100000 for image in images} & patched_sprites), 'Selected actor needs an explicit source patch'
    sprite_index = (client / 'data/spradrn_115.bin').read_bytes()
    actors = {}
    with (client / 'data/spr_115.bin').open('rb') as source:
        for offset in range(0, len(sprite_index), 12):
            image, address, size = struct.unpack_from('<IIH', sprite_index, offset)
            if image in images:
                actions = decoder.read_sprite_actions(source, address, size)
                assert len(actions) == size and all(len(a['frames']) == a['frameCnt'] for a in actions)
                actors[str(image)] = [{'direction': a['dir'], 'action': a['no'], 'frame_ms': max(1, a['perFrameMs']) * 16,
                                       'frames': [{'bitmap': b, 'x': x, 'y': y, 'sound': sound} for b, x, y, sound in a['frames']]}
                                      for a in actions]
    assert set(actors) == {str(i) for i in images}
    invisible = {t for t in set(tiles) | set(objects) if 0 < t <= 99}
    unmapped = {t for t in set(tiles) | set(objects) if t > 99} - set(reverse)
    assert not unmapped, ('unmapped map graphic', unmapped)
    map_ids = {map_bitmap(t, reverse) for t in set(tiles) | set(objects)} - {0}
    actor_ids = {frame['bitmap'] for actions in actors.values() for action in actions for frame in action['frames']}
    pal = decoder.load_palette(str(client / 'data/pal/PALET_1.SAP'))
    palette_report = []
    for palette in sorted((client / 'data/pal').iterdir()):
        if palette.suffix.lower() != '.sap': continue
        assert palette.stat().st_size >= 672
        used_sources.append(palette)
        palette_report.append({'file': palette.name, 'sha256': digest(palette), 'bytes': palette.stat().st_size})
    rgba = [bytes((*rgb, 0 if i == 0 else 255)) for i, rgb in enumerate(pal)]
    bitmaps = {}
    overshoots = []
    pages = []
    with (client / 'data/real_136.bin').open('rb') as source:
        for group, selected in [('map', map_ids), ('actors', actor_ids - map_ids)]:
            page = bytearray(2048 * 2048 * 4)
            page_no = 0
            x = y = row_height = 0
            page_name = f'{group}-{page_no}.png'
            for bitmap in sorted(selected, key=lambda b: (-index[b][5], b)):
                address, size, xoff, yoff, w, h = index[bitmap]
                assert 0 < w <= 2046 and 0 < h <= 2046 and size >= 16, (bitmap, index[bitmap])
                if x + w + 1 > 2048:
                    x = 0; y += row_height + 1; row_height = 0
                if y + h + 1 > 2048:
                    png(output / page_name, 2048, 2048, page)
                    pages.append(page_name)
                    page = bytearray(2048 * 2048 * 4); page_no += 1; x = y = row_height = 0
                    page_name = f'{group}-{page_no}.png'
                source.seek(address)
                pixels, flag, header, overflow = decoder.decode_rle(source.read(size), w, h)
                assert header[:2] == (w, h) and overflow >= 0, (bitmap, header, overflow)
                if overflow: overshoots.append({'bitmap': bitmap, 'bytes': overflow})
                blit_bottom_up(page, 2048, x, y, w, h, pixels, rgba)
                bitmaps[str(bitmap)] = {'atlas': page_name, 'x': x, 'y': y, 'width': w, 'height': h, 'xoff': xoff, 'yoff': yoff}
                x += w + 1; row_height = max(row_height, h)
            png(output / page_name, 2048, 2048, page); pages.append(page_name)
            print(group, len(selected), 'bitmaps converted', flush=True)
    binary = b'SAM1' + struct.pack('<III', 100, width, height)
    binary += struct.pack(f'<{count}I', *(map_bitmap(t, reverse) for t in tiles))
    binary += struct.pack(f'<{count}I', *(map_bitmap(t, reverse) for t in objects)) + walkable
    world = {'schema_ver': 1, 'floor': 100, 'name': server[8:40].split(b'\0')[0].decode('gbk'),
             'spawn': list(spawn), 'player_image': 100000, 'areas': areas, 'groups': groups,
             'encounters': encounters, 'templates': templates}
    world_text = json.dumps(world, ensure_ascii=False, separators=(',', ':'))
    source_manifest = {str(path.relative_to(root)): digest(path) for path in used_sources}
    manifest = {'schema_ver': 1, 'world': 'world.json', 'map': 'map.bin', 'width': width, 'height': height,
                'bitmaps': bitmaps, 'actors': actors, 'pages': {name: digest(output / name) for name in pages},
                'sources': source_manifest, 'palette': 'PALET_1.SAP', 'palettes': palette_report,
                'pixel_layout': {'source_scanlines': 'bottom-up', 'atlas_scanlines': 'top-down',
                                 'adrn_offsets': 'display-top-left'},
                'overrides': {'duplicate_bitmap_ids_last_wins': duplicate_ids, 'skipped_reverse_sound_aliases': skipped_reverse,
                              'patched_sprite_families_reviewed': sorted(patched_sprites), 'selected_patch_hits': [],
                              'invisible_map_marker_ids': sorted(invisible),
                              'rle_truncated_overshoots': overshoots},
                'map_comparison': {'tile_differences': sum(a != b for a, b in zip(tiles, server_tiles)),
                                   'object_differences': sum(a != b for a, b in zip(objects, server_objects)),
                                   'geometry_source': 'client .dat', 'collision_source': 'server LS2MAP + mapset'}}
    version = hashlib.sha256(binary + world_text.encode() + json.dumps(manifest, sort_keys=True).encode()).hexdigest()
    manifest['content_version'] = 'p2-' + version[:32]
    manifest['map_sha256'] = hashlib.sha256(binary).hexdigest()
    manifest['world_sha256'] = hashlib.sha256(world_text.encode()).hexdigest()
    manifest_text = json.dumps(manifest, ensure_ascii=False, separators=(',', ':'))
    for destination in [output, args.server_output]:
        (destination / 'map.bin').write_bytes(binary)
        (destination / 'world.json').write_text(world_text, encoding='utf-8')
        (destination / 'manifest.json').write_text(manifest_text, encoding='utf-8')
    print(manifest['content_version'], 'spawn', spawn, 'atlas pages', len(pages), 'actors', len(actors), flush=True)


if __name__ == '__main__':
    main()
