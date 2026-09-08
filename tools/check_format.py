#!/usr/bin/env python3
"""tools/check_format.py —— .clang-format 纪律的执行者

★★ 这条检查存在的理由,是它**此前不存在**。

00-architecture.md §9.0.23(命名改造 P6)宣告了「五仓字节一致、CI --Werror 强制」,
⚠️ 但 tools/ci_verify.py 的六项里没有格式项,.github/workflows/ 也不跑 clang-format
⇒ **那句「强制」从来没有执行者**。实测代价(2026-09-08 批次 M.3 顺手核出,
01 §13 欠债 24):HEAD 上已经漂了,而 ctest 全程 13/13 全绿。

  ⚠️★ 且漂移量比欠债 24 记的更大:该条目记「4 处」(WorldTickTest.cpp 3 + World.cpp 1),
     本脚本落地时全仓实测是 **10 处 / 5 个文件** —— 多出的 6 处在 ModelPoolTest.cpp(4)、
     EntityPool.h(1)、RulesBattleTest.cpp(1)。⇒ 当时那个数是**按批次改动面**数的,
     不是按全仓数的。这本身就是本脚本要消灭的东西:**没有全仓扫描,就没有全仓结论。**

★ 与 01 §13 欠债 20 同族的形态:**立了纪律,但没有任何东西会失败。**
  处置一律走这个仓的老路 —— 变成一条会失败的断言。

── ★★ 判据:退出码,不是输出文本 ────────────────────────────────────

clang-format --dry-run --Werror 会把每处违规打成一行诊断并以非 0 退出。
本脚本**以退出码定胜负**,把解析出的诊断只当作**给人看的明细**,并且
**断言两者一致**(退出码非 0 ⟺ 至少解析到一条诊断)。

⚠️★ 为什么要多这一道:2026-09-08 写本脚本时真的踩了一次 ——
   手写验证命令里 grep 的是 `warning: THIS-WORDING-NO-LONGER-MATCHES`,
   而 --Werror 把它提升成了 **`error:`** ⇒ 匹配 0 条 ⇒ 屏幕上印出「0 违规」,
   **而那一趟其实有 10 处**。⇒ 与 00 §9.0.12 A 型完全同族:
   **报告印的是一个看起来像凭据、实际来自别处的值。**
   ★ 若将来某个 clang-format 版本改了这句措辞,一致性断言会当场报「解析失效」,
     而不是安静地把红读成绿。

── ⚠️ 扫描面:codegen 产物不在其列,这是有判据的排除 ──────────────────

idl/generated/ 与 idl/codegen/support/ **有意排除**:

  ① 它们是 codegen 的产物,格式由 idl/codegen/saidl_gen.py 决定;
  ② ★★ idl_verify 那道关是**逐字节**比对 schema 与生成物 ——
     若本脚本把生成物格式化了,两道守卫就会**互相打架**:
     格式化一次 ⇒ idl_verify 红;重跑 codegen ⇒ 本脚本红。
  ③ 实测量级(2026-09-08):battle_events.sa.h 单文件 1,300 处、
     sa_idl_runtime.h 290 处 ⇒ 不是「顺手补一下」的规模。

⇒ ★ 这是一笔**显式登记的残留**,不是"已覆盖":要把生成物也纳入,
  正确做法是让 codegen 自己输出符合 .clang-format 的代码(改 cpp.py),
  而不是在这里事后格式化。⚠️ 别把它挪进 EXCLUDED 就当解决了。

★ EXCLUDED 是**排除**清单而不是**收录**清单,是有意的:新加的手写目录
  默认就被扫到,不需要有人记得来这里登记一行 ——
  与 ci_verify.py 的 check_script_encoding()「故意做到不需要维护任何清单」同一取向。

用法:
    python3 tools/check_format.py              # 违规则退出码 1,并列出 file:line:col
    python3 tools/check_format.py --fix        # 就地格式化(本地用;CI 永不传它)
    SA_CLANG_FORMAT=/path/to/clang-format python3 tools/check_format.py
"""

# ★ `X | None` 注解在 Python < 3.10 上会在**函数定义时**就抛 TypeError。
#   ⚠️ 这条不是洁癖:CI 用 setup-python 钉了 3.12,而本机(macOS 自带)是 3.9
#   ⇒ 少了这一行,本脚本会**只在本地炸、CI 全绿**,恰是 00 §9.0.6 那类
#     「在一套环境上验过 ≠ 都成立」的盲区,只是方向相反。
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# ★★ Windows 上的输出编码不是风格问题,是**退出码正确性**问题 ——
#   与 check_shared_purity.py 卷首同一条(简中 Windows 的 cp936 遇到 ✅ / ★ / ⇒
#   会抛 UnicodeEncodeError,于是脚本**通过时也退出码 1**)。
#   ⚠️ ci_verify.py §0 会断言本文件有这一段,别删。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):   # 被重定向到不支持 reconfigure 的对象
        pass

ROOT = Path(__file__).resolve().parent.parent

SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".inl"}

# 构建目录与 VCS 目录 —— 与 ci_verify.py check_script_encoding() 同一组。
SKIP_PARTS = {"build", "_deps", ".git"}

# ⚠️ 见卷首「扫描面」:codegen 产物有意排除,判据是「两道守卫会互相打架」。
EXCLUDED_PREFIXES = ("idl/generated/", "idl/codegen/")

# ★ 版本下限。--dry-run / --Werror 需要 >= 10;这里取 15 只是留一段余量,
#   本项目的 .clang-format 只用 BasedOnStyle + 6 个键,不依赖新版特性。
#
# ⚠️★ **下限不等于"任何版本都会给出同样判定"**。2026-09-08 三版本交叉实测
#    (Apple clang-format 21.0.0 / pip 21.1.8 / Homebrew LLVM 22.1.4):
#    同一份代码 **10 处违规、file:line:col 逐位一致** ⇒ 跨版本漂移在本
#    .clang-format 下不是现实风险(ColumnLimit: 0 消掉了折行这个最大分歧源)。
#    ★ 但那是**实测结论,不是保证** ⇒ CI 侧 pin 了确切版本(见 ci.yml),
#      让"换版本"成为一次显式的升级动作,而不是某天 runner 镜像自己变了。
#    ⇒ 本脚本**报告实际版本**(观测,不是硬编码文本 —— 00 §9.0.12 的教训),
#      平台间若真有分歧,诊断里一眼能看出是版本差异还是代码问题。
MIN_MAJOR = 15

# clang-format `--dry-run --Werror` 的诊断行:
#     <file>:<line>:<col>: error: code should be clang-formatted [-Wclang-format-violations]
#
# ⚠️★ 措辞里的 `error:` **不是** `warning:`(欠债 24 落地时踩过:按 `warning:` grep
#    会印出「0 违规」而那趟其实有 10 处)⇒ 两个都接,判据仍取退出码。
#
# ⚠️★★ **本行在 2026-09-09 被修复过一次,值得记**:欠债 24(`b6c718c`)做反向验证时
#    把匹配串换成了一个故意不匹配的哨兵(`THIS-WORDING-NO-LONGER-MATCHES`)以确认
#    下面那条一致性断言会转红 —— ★ **然后忘了改回来,并且连哨兵一起提交了**。
#    后果:凡真有违规时,退出码非 0 而解析到 0 条 ⇒ 一致性断言抢先触发,
#    打印的是「判据自相矛盾」而**不是违规清单**。
#    ★ 为什么两天没人发现:那条一致性断言在**零违规**状态下(退出码 0 + 解析 0 条)
#      两边同向 ⇒ 检查照常绿。⇒ **只有"该红的时候"才会暴露的缺陷,绿色证明不了它。**
#    ⇒ 教训:反向验证的改动必须与恢复动作成对,而且**恢复后要再跑一次"该红"的场景**
#      (本次即:先造一处违规确认打印出清单,再修掉它)。
DIAG_RE = re.compile(
    r"^(?P<loc>[^\n:]+:\d+:\d+):\s*(?:error|warning):\s*code should be clang-formatted",
    re.M)


def find_clang_format() -> str | None:
    """定位 clang-format。★ 顺序:显式指定 > PATH > 平台常见位置。

    ⚠️ HINTS 那一段不是"贴心",是为了让**本机默认能跑起来** ——
       macOS 上 clang-format 装在 Xcode 工具链里但**不在 PATH 上**,
       而一条跑不起来的检查很快就会被当成不存在。
    """
    explicit = os.environ.get("SA_CLANG_FORMAT")
    if explicit:
        return explicit if Path(explicit).exists() else None
    found = shutil.which("clang-format")
    if found:
        return found
    for cand in (
        "/Applications/Xcode.app/Contents/Developer/Toolchains/"
        "XcodeDefault.xctoolchain/usr/bin/clang-format",
        "/opt/homebrew/opt/llvm/bin/clang-format",
        "/usr/local/opt/llvm/bin/clang-format",
    ):
        if Path(cand).exists():
            return cand
    return None


def clang_format_version(exe: str) -> tuple[str, int | None]:
    """返回 (原样版本串, major)。major 为 None = 解析不出。"""
    try:
        out = subprocess.run([exe, "--version"], capture_output=True, text=True,
                             encoding="utf-8", errors="replace").stdout.strip()
    except OSError as e:
        return (f"<无法执行:{e}>", None)
    m = re.search(r"version\s+(\d+)\.", out)
    return (out, int(m.group(1)) if m else None)


def collect_files() -> list[Path]:
    """全仓手写 C++ 源码。★ 排除清单见 EXCLUDED_PREFIXES 的判据。"""
    files = []
    for path in sorted(ROOT.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if SKIP_PARTS & set(rel.parts):
            continue
        if rel.as_posix().startswith(EXCLUDED_PREFIXES):
            continue
        files.append(path)
    return files


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fix", action="store_true",
                    help="就地格式化而不是只检查(CI 永不传它)")
    args = ap.parse_args()

    exe = find_clang_format()
    if exe is None:
        # ⚠️ 这里返回 1 而不是 0。「本机没装」在 tests/CMakeLists.txt 那一层
        #    表现为**不注册**(那是给本机开的口子);一旦这条检查真被跑起来,
        #    "找不到工具"就是失败 —— 不能让它以"跳过"的面目印一个绿。
        print("❌ 找不到 clang-format。\n"
              "   本机安装(任一):brew install llvm · "
              "python3 -m pip install clang-format==21.1.8\n"
              "   或显式指定:SA_CLANG_FORMAT=/path/to/clang-format", file=sys.stderr)
        return 1

    version_text, major = clang_format_version(exe)
    if major is None:
        print(f"❌ 无法解析 clang-format 版本:{version_text}", file=sys.stderr)
        return 1
    if major < MIN_MAJOR:
        print(f"❌ clang-format 版本过低:{version_text} ⇒ 需要 >= {MIN_MAJOR}",
              file=sys.stderr)
        return 1

    files = collect_files()
    if not files:
        print("❌ 扫描面为空 —— 排除规则或目录布局有问题,不该是 0 个文件",
              file=sys.stderr)
        return 1

    rel_names = [str(f.relative_to(ROOT)) for f in files]

    if args.fix:
        subprocess.run([exe, "-i", *rel_names], cwd=ROOT, check=True)
        print(f"✅ 已就地格式化 {len(files)} 个文件({version_text})")
        return 0

    proc = subprocess.run([exe, "--dry-run", "--Werror", *rel_names],
                          cwd=ROOT, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    diagnostics = DIAG_RE.findall(proc.stdout + proc.stderr)

    # ── ★★ 一致性断言:退出码与解析出的明细必须同向 ────────────────
    #
    # 见卷首:退出码是判据,诊断是明细。两者分叉只有两种可能,都必须大声失败:
    #   · 退出码非 0 而解析到 0 条 ⇒ 诊断措辞变了(本脚本的 DIAG_RE 过期),
    #     若不管它,下面就会印出"0 处违规"并返回 1,读者会以为是别的原因;
    #   · 退出码 0 而解析到若干条 ⇒ 更怪,说明 --Werror 没生效。
    if (proc.returncode != 0) != bool(diagnostics):
        print(f"❌★★ 格式检查的**判据自相矛盾**,本项不结论:\n"
              f"   clang-format 退出码 = {proc.returncode},"
              f"而解析到的违规明细 = {len(diagnostics)} 条\n"
              f"   ⇒ 多半是诊断措辞变了(DIAG_RE 过期)或 --Werror 未生效。\n"
              f"   ⚠️ **勿放宽本断言去迁就它** —— 它存在的理由正是"
              f"「按文本判定会把红读成绿」(见本文件卷首)。\n"
              f"   版本:{version_text}\n"
              f"── clang-format 原样输出 ──\n{proc.stdout}{proc.stderr}",
              file=sys.stderr)
        return 1

    if proc.returncode != 0:
        by_file: dict[str, list[str]] = {}
        for loc in diagnostics:
            f, line, col = loc.rsplit(":", 2)
            by_file.setdefault(f, []).append(f"{line}:{col}")
        print(f"❌★★ .clang-format 检查失败 —— {len(diagnostics)} 处违规,"
              f"分布在 {len(by_file)} 个文件:\n")
        for f in sorted(by_file):
            print(f"  {f}  ({len(by_file[f])} 处):{', '.join(by_file[f])}")
        print(f"\n  版本:{version_text}")
        print("\n  修:python3 tools/check_format.py --fix"
              "(或 clang-format -i <文件>)")
        print("  ⚠️ 不要改 .clang-format 去迁就代码 —— 它与引擎 GameStudio "
              "逐字一致(P6 / DR-TS7),改它就把「三仓单一格式体系」这条放掉了。")
        return 1

    print(f"✅ .clang-format 检查通过(扫描 {len(files)} 个文件,{version_text})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
