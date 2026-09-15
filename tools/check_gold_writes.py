#!/usr/bin/env python3
# tools/check_gold_writes.py —— GoldLedger 唯一入口的守卫(经济地基批,00 §8.4.3)
#
# ★★ 它挡的不是代码风格,是 `12-economy.md` §2.3 实测的那 62.5%:
#    原版 96 个石币写点里 60 处绕过唯一带上限与审计的 API(`CHAR_AddGold`/`DelGold`)
#    直接 `CHAR_setInt` 裸写 ⇒ 上限、溢出处置、审计三件事**不报错也不崩溃**,
#    只是悄悄少一次判定、少一条日志(00 §10.4 第一类静默错误)。
#    新实现把它们收进 `src/world/GoldLedger.{h,cpp}` 单入口后,旁路在**这里**被拦下。
#
# ⚠️ 与 check_shared_purity.py / check_module_boundaries.py 同一取向:
#    先前是注释里的一句纪律,现在是会失败的检查。那两条守 D2 与 §3.1,这条守 §8.4.3。
#
# ── 规则 ────────────────────────────────────────────────────────────────
#
# 扫描 **src/ 与 shared/** 的 C++ 源(*.cpp/*.cc/*.h/*.hpp),任何对名为 `gold`
# 的成员的**写**(`.` / `->` 后的 `= += -= *= /= %= ++ --`):
#     允许:src/world/GoldLedger.cpp   ← 唯一入口自己的实现(钳位/处置/落值)
#     其余一律违规。
#
# ⚠️ 三处**有意不扫**,各有理由,不是漏:
#   ① `idl/`(generated + schema)—— 那里写的是 `PlayerData`(存档记录),不是
#      `Model::Player`;编解码/拷贝必须直写字段。同理由,`CharacterRecord` 的
#      存档快照路径(`copyPlayerData`)不落扫描面 —— 快照是持久化层,不是账务。
#   ② `tests/` —— 运行期可达的 `Model::Player` 实体只活在 `src/world` 的池里,
#      测试拿不到非 const 引用(World 公开面没有"改实体"的方法,这是刻意的);
#      测试里的 `p.gold = …` 是**局部构造物**的摆位,不是旁路。⇒ 旁路只可能在
#      src/ 里被写出来,这正是本脚本拦的面。
#   ③ `build/`、`docs/` 等 —— 非源码。
#
# ⚠️ 读(如 `player.gold`)不拦:观察面与审计读数是合法的。
#
# ── 为什么必须用脚本而不是 C++ 的 private ───────────────────────────────
#
# `Model::Player::gold` 若改成 private,持久化层的生成模板
# `SA::Domain::copyPlayerData`(idl/generated,自由函数)就拿不到访问权;
# 给生成物挂 friend 前向声明会把 shared/model 与 codegen 的签名焊死 ——
# 代价与脆性都不值。⇒ 编译期收口不可行(登记在 journal/17-economy.md),
# 取「边界检查脚本 + 用例钉住」的组合,与本仓 purity/boundaries 两条守卫同款。
#
# 用法:
#     python3 tools/check_gold_writes.py          # 违规退出码 1

import re
import sys
from pathlib import Path

# ★★ 与 check_shared_purity.py 同一段,理由也同一条:Windows 上的输出编码
#    不是风格问题,是**退出码正确性**问题(简中 Windows 控制台默认 cp936,
#    print ✅/★ 会抛 UnicodeEncodeError ⇒ 通过时也退出码 1)。凡新增会被
#    ctest 注册的 Python 检查,都要带上它 —— 本脚本是第四个遵守的。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

ROOT = Path(__file__).resolve().parent.parent
SCAN_ROOTS = ("src", "shared")
SCAN_EXTS = {".cpp", ".cc", ".h", ".hpp"}

# 唯一入口的实现文件(前缀匹配,含同名头)。
ALLOWED_PREFIX = "src/world/GoldLedger"

# `p.gold =` / `p->gold +=` / `p.gold++` —— 成员名为 gold 的写。
# ⚠️ `==` 不是写;`gold = 0` 这种**字段声明**(无点无箭头)不匹配;
#    `record.player.gold` 若出现在 src/ 里同样命中(它就不该在 src/ 出现)。
WRITE_RE = re.compile(
    r"(?:\.|->)\s*gold\s*(?:=(?!=)|\+=|-=|\*=|/=|%=|\+\+(?!\+)|--(?!-))"
)


def main() -> int:
    offenders = []
    for root in SCAN_ROOTS:
        base = ROOT / root
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SCAN_EXTS or not path.is_file():
                continue
            rel = path.relative_to(ROOT).as_posix().replace("\\", "/")
            if rel.startswith(ALLOWED_PREFIX):
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                text = path.read_text(encoding="utf-8", errors="replace")
            for lineno, line in enumerate(text.splitlines(), start=1):
                if WRITE_RE.search(line):
                    offenders.append(f"{rel}:{lineno}: {line.strip()}")

    if offenders:
        print("★★ GoldLedger 唯一入口被绕过(00 §8.4.3):"
              "石币的写只允许发生在 src/world/GoldLedger.cpp 内部。\n"
              "   任何「改余额」必须走 GoldLedger::addGold / delGold"
              "(钳位 → 溢出处置 → 审计,三步不可拆);\n"
              "   直写会把上限判定与审计事件一起绕掉 ——"
              "这正是原版 12 §2.3 的 62.5% 绕过点在新实现里的重演。\n")
        for o in offenders:
            print(f"  ✗ {o}")
        print(f"\n共 {len(offenders)} 处。")
        return 1

    print("✅ gold 唯一入口守卫通过:src/ 与 shared/ 中对石币的写全部收在 GoldLedger 内。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
