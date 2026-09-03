#!/usr/bin/env python3
"""sgidl_gen —— 石器时代协议 IDL 代码生成器。

    schema/*.proto
         ↓  protoc --descriptor_set_out          ★ protoc 只在构建期用于解析
    descriptor set
         ↓  本脚本（零第三方依赖）
    generated/cpp/*.sg.h                          ★ 纯 POD + inline 编解码，运行时零依赖

用法：
    python3 idl/codegen/sgidl_gen.py               # 生成 + 检查 + 更新编号表
    python3 idl/codegen/sgidl_gen.py --check       # 只检查，不写文件（CI 用）
    python3 idl/codegen/sgidl_gen.py --verify      # 检查生成物是否与 schema 同步（CI 用）

依据：11-decision-register.md §1.1（DR-TS1）、02-protocol.md §8。
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# ★★ 同 tools/check_shared_purity.py:Windows 控制台默认 cp936,而本脚本输出含
#   ✅ / ★ ⇒ print 抛 UnicodeEncodeError,退出码变 1,把「通过」读成「失败」。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

sys.path.insert(0, str(Path(__file__).parent))

from sgidl import checks, cpp, descriptor  # noqa: E402

IDL_DIR = Path(__file__).resolve().parent.parent
SCHEMA_DIR = IDL_DIR / "schema"
OUT_DIR = IDL_DIR / "generated" / "cpp"
SUPPORT_DIR = Path(__file__).resolve().parent / "support"
REGISTRY = Path(__file__).resolve().parent / "registry" / "msg_ids.json"

# 只提供 option 定义，不产出生成物
OPTION_FILE = "sg_options.proto"


def resolve_protoc() -> str:
    """定位 protoc。

    ★ DR-TS1 边界 ①:protoc **只在构建期**用于把 .proto 解析成 descriptor set,
      运行时不链接 libprotobuf ⇒ 它是构建工具,不是依赖。
      叠加 DR-TS2(生成物入库)⇒ **只消费生成物的人根本不需要装 protoc**。

    ⚠️ 因此「缺 protoc」是一种**正常状态**,必须给一句能照着做的诊断 ——
      不要让它表现为 subprocess 抛出的 `FileNotFoundError: [WinError 2]` traceback
      (2026-09-02 Windows 一次性验证实测:那份 traceback 读起来像脚本坏了,
       而真实情况只是这台机器没装构建期工具)。
    """
    exe = os.environ.get("PROTOC") or "protoc"
    found = shutil.which(exe)
    if not found:
        raise SystemExit(
            f"★ 找不到 protoc（{exe}）。\n"
            "  它只在构建期用于解析 .proto（DR-TS1 边界 ①），运行时不链接 libprotobuf；\n"
            "  生成物已入库（DR-TS2）⇒ 只**消费**生成物无需 protoc，\n"
            "  只有**改 schema 或校验生成物同步**时才需要它。\n"
            "  装法：winget install Google.Protobuf ／ choco install protoc ／ brew install protobuf\n"
            "  或用环境变量指定：PROTOC=<protoc 可执行文件路径>")
    return found


def run_protoc(schema_dir: Path) -> bytes:
    protos = sorted(p.relative_to(schema_dir).as_posix()
                    for p in schema_dir.rglob("*.proto"))
    if not protos:
        raise SystemExit(f"schema 目录下没有 .proto 文件：{schema_dir}")
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "descriptor.pb"
        cmd = [resolve_protoc(), f"--proto_path={schema_dir}",
               f"--descriptor_set_out={out}", "--include_imports", *protos]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr)
            raise SystemExit("protoc 解析失败")
        return out.read_bytes()


def generate(schema: descriptor.Schema) -> dict[str, str]:
    gen = cpp.CppGen(schema)
    files: dict[str, str] = {}
    for protofile in sorted(schema.files):
        files[gen.header_name(protofile)] = gen.emit_file(protofile)
    files["ids.h"] = gen.emit_ids()
    files["sg_idl_runtime.h"] = (SUPPORT_DIR / "sg_idl_runtime.h").read_text(
        encoding="utf-8")
    return files


def write_out(files: dict[str, str], out_dir: Path) -> None:
    if out_dir.exists():
        shutil.rmtree(out_dir)
    for rel, text in files.items():
        path = out_dir / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


def verify_out(files: dict[str, str], out_dir: Path) -> list[str]:
    errs = []
    existing = {p.relative_to(out_dir).as_posix()
                for p in out_dir.rglob("*") if p.is_file()} if out_dir.exists() else set()
    for rel, text in files.items():
        path = out_dir / rel
        if not path.exists():
            errs.append(f"生成物缺失：{rel}")
        elif path.read_text(encoding="utf-8") != text:
            errs.append(f"生成物与 schema 不同步：{rel}")
    for stale in sorted(existing - set(files)):
        errs.append(f"生成物目录有多余文件：{stale}")
    return errs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="只跑一致性检查，不写文件")
    ap.add_argument("--verify", action="store_true",
                    help="检查生成物是否与 schema 同步（不写文件）")
    args = ap.parse_args()

    raw = run_protoc(SCHEMA_DIR)
    schema = descriptor.load(raw, skip_files=(OPTION_FILE,))

    errs, current_registry = checks.run_all(schema, SCHEMA_DIR, REGISTRY)
    if errs:
        sys.stderr.write("\n★ schema 检查未通过：\n\n")
        for i, e in enumerate(errs, 1):
            sys.stderr.write(f"  [{i}] {e}\n\n")
        return 1

    files = generate(schema)

    if args.check:
        print(f"✅ schema 检查通过（{len(schema.messages)} 个消息 / "
              f"{len(schema.enums)} 个枚举）")
        return 0

    if args.verify:
        diffs = verify_out(files, OUT_DIR)
        if diffs:
            sys.stderr.write("\n★ 生成物与 schema 不同步，请重跑 sgidl_gen.py 并提交：\n\n")
            for d in diffs:
                sys.stderr.write(f"  - {d}\n")
            return 1
        print("✅ 生成物与 schema 同步")
        return 0

    write_out(files, OUT_DIR)
    REGISTRY.parent.mkdir(parents=True, exist_ok=True)
    REGISTRY.write_text(
        json.dumps(current_registry, indent=2, ensure_ascii=False, sort_keys=True)
        + "\n", encoding="utf-8")

    numbered = sum(1 for m in schema.messages.values() if m.msg_id is not None)
    print(f"✅ 生成完毕：{len(files)} 个文件 → {OUT_DIR}")
    print(f"   消息 {len(schema.messages)} 个（其中带 msg_id 的 {numbered} 个）/ "
          f"枚举 {len(schema.enums)} 个")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
