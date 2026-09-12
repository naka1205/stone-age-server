#!/usr/bin/env python3
"""Read-only evidence extraction for the 2026-09-11 audit; never compile legacy code."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
SERVER = HERE.parents[3]
WORKSPACE = SERVER.parent
sys.path.insert(0, str(WORKSPACE / "stoneage-plan/tools"))
from char_sizeof import Preproc, strip_comments

pp = Preproc({})
root = WORKSPACE / "StoneAge/gmsv/src/include"
for name in ("version.h", "correct_bug.h", "version_pk.h", "char_base.h", "battle_event.h"):
    pp.feed(name, strip_comments((root / name).read_text(errors="replace")))
names = ("CHAR_WORKWEAKEN", "CHAR_WORKBARRIER", "CHAR_WORKNOCAST",
         "BATTLE_ST_WEAKEN", "BATTLE_ST_BARRIER", "BATTLE_ST_NOCAST", "BATTLE_ST_END")
result = {"ssrc80_enum_values": {name: pp.syms.get(name) for name in names},
          "unknown_if": sorted(pp.unknown_in_if)}
data = WORKSPACE / "csa8.0/gmsv/data"
limits = []
for line in (data / "encount.txt").read_bytes().decode("gb18030", errors="replace").splitlines():
    cols = line.split(",")
    if len(cols) >= 30:
        try:
            limits.append(int(cols[8]))
        except ValueError:
            pass
result["encount"] = {"rows": len(limits), "max_enemy_count": dict(sorted(Counter(limits).items())),
                     "rows_with_max_over_five": sum(v > 5 for v in limits)}
growth = []
for line in (data / "enemybase1.txt").read_bytes().decode("gb18030", errors="replace").splitlines():
    cols = line.split(",")
    if len(cols) > 9:
        try:
            growth.append(float(cols[8]))
        except ValueError:
            pass
result["growth"] = {"rows": len(growth), "fractional_rows": sum(v != int(v) for v in growth)}
powers = []
for line in (data / "itemset6.txt").read_bytes().decode("gb18030", errors="replace").splitlines():
    cols = line.split(",")
    if len(cols) > 10 and cols[10] == "ITEM_useRecovery":
        match = re.search(r"体\s*([+-]?\d+)", cols[3])
        if match and int(match.group(1)) > 0:
            powers.append(int(match.group(1)))
result["hp_recovery"] = {"rows": len(powers),
                         "powers_not_multiple_of_five": sum(p % 5 != 0 for p in powers)}
result["legacy_inverted_range_example"] = {
    "lo": 10, "hi": 5, "values_for_u_0_05_099": [10 + int(-4 * u) for u in (0, .5, .99)]
}
files = [
    "StoneAge/gmsv/src/battle/battle.c", "StoneAge/gmsv/src/battle/battle_event.c",
    "StoneAge/gmsv/src/include/char_base.h", "StoneAge/gmsv/src/include/battle_event.h",
    "StoneAge/gmsv/src/include/util.h", "StoneAge/gmsv/src/char/char_walk.c",
    "stoneage85/石器时代服务器端最新完整源代码/Serv/gmsv/battle/battle.c",
    "stoneage85/石器时代服务器端最新完整源代码/Serv/gmsv/battle/battle_event.c",
    "stone-age-server/shared/rules/Battle.cpp", "stone-age-server/shared/rules/Status.cpp",
    "stone-age-server/shared/rules/Combatant.h", "stone-age-server/shared/rules/Progression.cpp",
    "stone-age-server/src/world/World.cpp", "stone-age-client/src/net/ClientSession.cpp",
    "stone-age-client/src/battle/BattlePresenter.cpp", "stone-age-client/src/scenes/BattleScene.cpp",
    "stone-age-client/src/app/main.cpp", "csa8.0/gmsv/data/encount.txt",
    "csa8.0/gmsv/data/enemybase1.txt", "csa8.0/gmsv/data/itemset6.txt",
]
result["sha256"] = {name: hashlib.sha256((WORKSPACE / name).read_bytes()).hexdigest() for name in files}
print(json.dumps(result, ensure_ascii=False, indent=2))
