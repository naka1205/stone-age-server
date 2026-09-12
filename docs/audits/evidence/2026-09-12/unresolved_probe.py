#!/usr/bin/env python3
"""Read initialized data using the existing COFF reader; never decode .text."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    sys.dont_write_bytecode = True
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[5])
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("unresolved-data.json"))
    args = parser.parse_args()
    reader_path = args.root / "stoneage-plan/tools/probe_data_values.py"
    spec = importlib.util.spec_from_file_location("original_data_reader", reader_path)
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    binary = args.root / "csa8.0/gmsv/gmsv.exe"
    data, sections, symbols = reader.load_pe(str(binary))
    digest = hashlib.sha256(data).hexdigest()
    assert digest == "6dc67e6f38cc9781cae071a8088fe313b383c1aeb5a2b770afdf2dd3a1aec85a"
    index = reader.index_data_symbols(sections, symbols)

    def read(name, count):
        entries = index[name]
        assert len(entries) == 1, (name, entries)
        entry = entries[0]
        assert entry["节"] in {".data", ".rdata", ".bss"}
        span = entry["span上界"]
        assert span is None or count * 4 <= span
        if entry["有初值"]:
            section = next(s for s in sections if s[0] == entry["节"])
            assert entry["节内偏移"] + count * 4 <= section[4]
        return {"location": entry, "values": reader.read_values(data, entry, "i", count)}

    family_source = args.root / "StoneAge/gmsv/src/char/family.c"
    source = family_source.read_text(encoding="utf-8")
    arrays = []
    for match in re.finditer(r"int\s+fmdplevelexp\[\]\s*=\s*\{(.*?)\};", source, re.S):
        body = re.sub(r"/\*.*?\*/|//[^\n]*", "", match.group(1), flags=re.S)
        arrays.append([int(value) for value in re.findall(r"\d+", body)])
    assert len(arrays) == 2 and all(len(a) == 11 for a in arrays)
    family = read("fmdplevelexp", 11)
    assert family["values"] == arrays[1]
    controls = {name: read(name, 1) for name in ("AC_WBSIZE", "EnemyMoveNum", "ConnectLen")}
    assert controls["AC_WBSIZE"]["values"] == [1048576]
    assert controls["EnemyMoveNum"]["values"] == [10]
    assert controls["ConnectLen"]["values"] is None
    level_source = reader.ssrc_levelup_tbl()
    assert len(level_source) == 141
    assert read("LevelUpTbl", 141)["values"] == level_source
    literals = []
    needle = b"./data/skillcode.txt\0"
    for name, _, _, raw, size in sections:
        if name not in {".data", ".rdata"}:
            continue
        offset = data.find(needle, raw, raw + size)
        if offset >= 0:
            literals.append({"section": name, "offset": offset})
    paths = [
        "stoneage-plan/tools/probe_data_values.py", "csa8.0/gmsv/setup.cf",
        "csa8.0/gmsv/data/skillcode.txt", "StoneAge/gmsv/src/char/family.c",
        "StoneAge/gmsv/src/battle/battle.c", "StoneAge/gmsv/src/configfile.c",
        "StoneAge/gmsv/src/include/char_base.h", "StoneAge/gmsv2.28_code.rar",
        "stoneage85/石器时代服务器端最新完整源代码/Serv.zip",
        "stoneage85/石器时代服务器端最新完整源代码/Serv/gmsv/battle/battle.c",
        "stoneage85/石器时代服务器端最新完整源代码/Serv/gmsv/configfile.c",
    ]
    result = {
        "binary": str(binary.relative_to(args.root)), "binary_sha256": digest,
        "scope": "Initialized data of the supplied B80 private-server rebuild; not an official runtime claim.",
        "method": "COFF symbol + data section offset; int32 type/count from source; no instruction decoding.",
        "family_exp": family, "family_matches_SSRC80_else": True,
        "controls": controls, "LevelUpTbl_141_values_match": True,
        "skillcode_literal_in_data_sections": literals,
        "literal_limit": "A missing or present literal does not establish the loader's behavior.",
        "sha256": {path: hashlib.sha256((args.root / path).read_bytes()).hexdigest() for path in paths},
    }
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"family_exp": family["values"], "controls_passed": True,
                      "output": str(args.output)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
