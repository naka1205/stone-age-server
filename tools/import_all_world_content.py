#!/usr/bin/env python3
"""tools/import_all_world_content.py —— 全量大世界地图、传送门与 NPC 批处理解析与编目工具

依据: 实施路线图 Phase 3.1 内容批处理管线规范
数据源: csa8.0/gmsv/data (含 1,235 张 LS2MAP、mapwarp.txt、7,719 个 NPC 实例)
产出物:
  - stone-age-server/content/catalog/maps_catalog.json
  - stone-age-server/content/catalog/warps_catalog.json
  - stone-age-server/content/catalog/npcs_catalog.json
  - stone-age-server/content/catalog/summary.json
"""

import argparse
import json
from pathlib import Path
import struct
import sys
import time

for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, 'reconfigure'):
        stream.reconfigure(encoding='utf-8', errors='replace')


def parse_blocks(path: Path):
    blocks = []
    current = None
    try:
        content = path.read_bytes().decode('gbk', errors='replace')
    except Exception:
        return blocks
    for line in content.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        if line == '{':
            current = {}
            continue
        if line == '}':
            if current is not None:
                blocks.append(current)
            current = None
            continue
        if current is not None and '=' in line:
            k, v = line.split('=', 1)
            current[k.strip().lower()] = v.strip()
    return blocks


def parse_all_maps(data_map_dir: Path):
    maps = {}
    for p in data_map_dir.rglob('*'):
        if not p.is_file() or p.name.endswith('.txt') or p.name.endswith('.screen'):
            continue
        try:
            head = p.read_bytes()[:6]
            if head != b'LS2MAP':
                continue
            buf = p.read_bytes()
            w, h = struct.unpack_from('>HH', buf, 40)
            name = buf[8:40].split(b'\0')[0].decode('gbk', errors='replace').split('|')[0].strip()
            
            # 提取 floor ID: 优先根据文件名推导
            fname = p.stem
            floor_id = None
            if fname.isdigit():
                floor_id = int(fname)
            else:
                # 针对形如 dan_2-12-01 或 命名文件，尝试匹配数字
                digits = ''.join(c for c in fname if c.isdigit())
                if digits:
                    floor_id = int(digits[:8])
            
            key = floor_id if floor_id is not None else fname
            maps[str(key)] = {
                'id': floor_id,
                'name': name,
                'width': w,
                'height': h,
                'rel_path': str(p.relative_to(data_map_dir))
            }
        except Exception:
            continue
    return maps


def parse_static_warps(mapwarp_file: Path):
    warps = []
    if not mapwarp_file.exists():
        return warps
    content = mapwarp_file.read_bytes().decode('gbk', errors='replace')
    for line in content.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split(':')
        if len(parts) >= 4:
            src = parts[2].split(',')
            dst = parts[3].split(',')
            if len(src) >= 3 and len(dst) >= 3:
                try:
                    warps.append({
                        'src_floor': int(src[0]),
                        'src_x': int(src[1]),
                        'src_y': int(src[2]),
                        'dst_floor': int(dst[0]),
                        'dst_x': int(dst[1]),
                        'dst_y': int(dst[2])
                    })
                except ValueError:
                    pass
    return warps


def parse_all_npcs(npc_dir: Path):
    templates = {}
    for p in npc_dir.rglob('*.template'):
        for b in parse_blocks(p):
            tname = b.get('templatename', '').lower()
            if tname:
                templates[tname] = b

    npcs = []
    npc_warps = []
    npc_seq = 100000

    for p in sorted(npc_dir.rglob('*.create')):
        blocks = parse_blocks(p)
        for b in blocks:
            fid_str = b.get('floorid', '')
            if not fid_str.isdigit():
                continue
            fl_id = int(fid_str)
            
            # 解析坐标范围
            bco = b.get('borncorner')
            bc = b.get('borncenter')
            x1, y1 = 0, 0
            if bco:
                coords = [int(v.strip()) for v in bco.split(',') if v.strip().lstrip('-').isdigit()]
                if len(coords) >= 2:
                    x1, y1 = coords[0], coords[1]
                else:
                    continue
            elif bc:
                coords = [int(v.strip()) for v in bc.split(',') if v.strip().lstrip('-').isdigit()]
                if len(coords) >= 2:
                    x1, y1 = coords[0], coords[1]
                else:
                    continue
            else:
                continue

            enemy = b.get('enemy', '')
            parts = enemy.split('|')
            kind = parts[0].strip()
            
            # 处理动态传送门 NPC (npcgen_warp / Warp)
            if kind.lower() in ('npcgen_warp', 'warp') and len(parts) >= 4:
                try:
                    dst_f = int(parts[1])
                    dst_x = int(parts[2])
                    dst_y = int(parts[3])
                    npc_warps.append({
                        'src_floor': fl_id,
                        'src_x': x1,
                        'src_y': y1,
                        'dst_floor': dst_f,
                        'dst_x': dst_x,
                        'dst_y': dst_y
                    })
                    continue
                except ValueError:
                    pass

            npc_seq += 1
            name = b.get('name', '').strip()
            image_str = b.get('graphicname', b.get('image', '100000'))
            image_id = int(image_str) if image_str.isdigit() else 100000
            dir_str = b.get('dir', '0')
            dir_val = int(dir_str) if dir_str.lstrip('-').isdigit() else 0

            # 关联模板 functionset
            tmpl = templates.get(kind.lower(), {})
            func_set = tmpl.get('functionset', kind).lower()

            # 读取外部 arg 文件内容
            arg_text = ''
            if len(parts) > 1 and parts[1].startswith('file:'):
                arg_path = npc_dir / parts[1][5:]
                if arg_path.exists():
                    arg_text = arg_path.read_bytes().decode('gbk', errors='replace')

            # 标准化类型
            category = 'other'
            if 'healer' in func_set or 'windowhealer' in func_set:
                category = 'healer'
            elif 'townpeople' in func_set:
                category = 'townpeople'
            elif 'exchangeman' in func_set:
                category = 'exchangeman'
            elif 'itemshop' in func_set or 'shop' in func_set:
                category = 'shop'
            elif 'petshop' in func_set:
                category = 'petshop'
            elif 'petskillshop' in func_set:
                category = 'petskillshop'
            elif 'signboard' in func_set or 'dengon' in func_set:
                category = 'signboard'
            elif 'warpman' in func_set:
                category = 'warpman'
            elif 'ridemaster' in func_set:
                category = 'ridemaster'
            elif 'enemy' in func_set or 'npcenemy' in func_set:
                category = 'enemy'

            record = {
                'id': npc_seq,
                'floor': fl_id,
                'x': x1,
                'y': y1,
                'dir': dir_val,
                'name': name or tmpl.get('name', 'NPC'),
                'image': image_id,
                'category': category,
                'function_set': func_set,
                'arg_len': len(arg_text),
                'source': str(p.relative_to(npc_dir))
            }
            if category == 'exchangeman' and arg_text:
                record['exchange_raw'] = arg_text[:4096] # 截取前置有效 DSL
            elif category == 'signboard' and arg_text:
                record['message'] = arg_text.strip()
            
            npcs.append(record)

    return npcs, npc_warps


def main():
    parser = argparse.ArgumentParser(description="石器时代大世界资产全量批处理解析与编目")
    parser.add_argument("--data-root", default="csa8.0/gmsv/data", help="csa8.0 数据根目录")
    parser.add_argument("--output-dir", default="stone-age-server/content/catalog", help="编目输出目录")
    args = parser.parse_args()

    data_root = Path(args.data_root)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    if not data_root.exists():
        print(f"❌ 找不到数据根目录: {data_root}", file=sys.stderr)
        return 1

    t0 = time.time()
    print(f"📦 正在扫描并解析数据: {data_root} ...")

    # 1. 解析全部地图
    maps = parse_all_maps(data_root / "map")
    print(f"  ✅ 地图库解析完成: 共 {len(maps)} 张有效 LS2MAP")

    # 2. 解析静态与 NPC 传送门
    static_warps = parse_static_warps(data_root / "map/mapwarp.txt")
    npcs, npc_warps = parse_all_npcs(data_root / "npc")
    all_warps = static_warps + npc_warps
    print(f"  ✅ 传送网络解析完成: 静态 {len(static_warps)} 条 + NPC 动态 {len(npc_warps)} 条 = 总计 {len(all_warps)} 处传送门")
    print(f"  ✅ NPC 实例解析完成: 共 {len(npcs)} 个具名/功能实体")

    # 统计分类
    cat_counts = {}
    for n in npcs:
        c = n['category']
        cat_counts[c] = cat_counts.get(c, 0) + 1

    # 写入编目文件
    (output_dir / "maps_catalog.json").write_text(json.dumps(maps, ensure_ascii=False, indent=2), encoding="utf-8")
    (output_dir / "warps_catalog.json").write_text(json.dumps(all_warps, ensure_ascii=False, indent=2), encoding="utf-8")
    (output_dir / "npcs_catalog.json").write_text(json.dumps(npcs, ensure_ascii=False, indent=2), encoding="utf-8")

    summary = {
        'schema_ver': 1,
        'generated_at': time.strftime("%Y-%m-%d %H:%M:%S"),
        'maps_count': len(maps),
        'warps_count': len(all_warps),
        'npcs_count': len(npcs),
        'npc_categories': cat_counts
    }
    (output_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")

    t1 = time.time()
    print(f"🎉 编目全量输出完成, 耗时 {t1 - t0:.2f}s, 输出至: {output_dir}")
    print(f"📊 NPC 类别分布:")
    for k, v in sorted(cat_counts.items(), key=lambda x: -x[1]):
        print(f"    · {k:15s}: {v}")

    return 0


if __name__ == '__main__':
    sys.exit(main())
