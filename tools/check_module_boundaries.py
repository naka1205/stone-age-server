#!/usr/bin/env python3
# tools/check_module_boundaries.py —— 00 §3.1 的守卫
#
# ★★ 它挡的不是代码风格,是 00 §10.4 三类静默错误里最难查的那一类:
#    **「进程内捷径」—— 单容器跑通、生产分布式挂。**
#
# §3.1 的原话:「一旦有人在单容器形态下写了『反正同进程,直接读 social 的 map』,
# 生产分布式形态就会以最难查的方式崩掉。」
# ⇒ 各模块编译成独立静态库、只暴露 `include/<module>/api.h`,由 CMake 的
#   PUBLIC/PRIVATE include 路径落实。
#
# ⚠️ 但 CMake 有两个挡不住的缺口,本脚本补的正是这两个:
#   ① `#include "../net/session.h"` —— 相对路径绕过 include 搜索路径;
#   ② 模块把第二个头放进 `include/<module>/` —— 暴露面从"一个 api.h"悄悄变宽。
#
# ★ 与 check_shared_purity.py 同一取向:先前是口头纪律,现在是会失败的检查。
#   (那一条守 D2,这一条守 §3.1;两者都是"违反了不报错、不崩溃"的类型。)

import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"

# ★★ 声明式依赖表 —— 这才是本脚本的核心资产。
#    改动它等于改动架构,应当被 review;而"顺手加个 include"不该能绕过它。
#
# ⚠️ 顺序即分层(01 §4 的 L0 → L1 → L2):下层不得认识上层。
ALLOWED_DEPS = {
    # platform 是 L0,依赖面必须为空 —— 它才可能在阶段 2 被 storage / lock 复用。
    "platform": set(),
    # net 是 L1。★ 它是**唯一**该看见 transport/ 组 IDL 的模块(02 §9)。
    #   ⚠️ 不依赖 platform:见 session.cpp 里 Pong.server_time_ms 那处注释。
    "net": set(),
    # world 把三层缝在一起。
    "world": {"platform", "net"},
}

# 每个模块允许对外暴露的头。★ 恰好一个 —— 见 00 §3.1「只暴露接口头」。
PUBLIC_HEADER = "api.h"

# ★ src/ 根下的文件是**装配层**(01 §4 的 `main.cpp`:「单一入口,按配置装载模块」)。
#   它按定义要认识所有模块 —— 那正是它的职责。⇒ 依赖表对它不设限,
#   但 ①(相对路径)与 ②(只能引 api.h)两条**仍然适用**:
#   装配层也不许伸手进别人的内部头。
ENTRY_MODULE = "<entry>"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')


def module_of(path: Path) -> str:
    parts = path.relative_to(SRC).parts
    return parts[0] if len(parts) > 1 else ENTRY_MODULE


def main() -> int:
    if not SRC.is_dir():
        print(f"❌ 找不到 {SRC}", file=sys.stderr)
        return 1

    modules = sorted(
        p.name for p in SRC.iterdir() if p.is_dir() and (p / "CMakeLists.txt").exists()
    )
    problems = []

    # ── 检查 1:声明式依赖表必须覆盖全部模块 ──────────────────────
    #
    # ⚠️ 新建一个模块却忘了登记,后果是它的依赖**完全不受检查** ——
    #    那正是「静默消失的检查」,与 ci_verify.py 里 EXPECTED_TESTS 同一类问题。
    for m in modules:
        if m not in ALLOWED_DEPS:
            problems.append(
                f"[未登记模块] src/{m}/ 存在但不在 ALLOWED_DEPS 里。\n"
                f"    ⇒ 新增模块必须同时在本脚本登记它的依赖面,"
                f"否则它的依赖不受任何检查。这一步是**故意**要求人确认的。"
            )
    for m in ALLOWED_DEPS:
        if m not in modules:
            problems.append(f"[登记了不存在的模块] ALLOWED_DEPS 里有 {m},但 src/{m}/ 不存在")

    # ── 检查 2:暴露面恰好一个 api.h ──────────────────────────────
    for m in modules:
        pub = SRC / m / "include" / m
        if not pub.is_dir():
            problems.append(
                f"[缺少暴露面] src/{m}/include/{m}/ 不存在 ⇒ 该模块没有对外接口头"
            )
            continue
        headers = sorted(p.name for p in pub.iterdir() if p.is_file())
        if headers != [PUBLIC_HEADER]:
            problems.append(
                f"[暴露面变宽] src/{m}/include/{m}/ 里有 {headers},"
                f"应当只有 {PUBLIC_HEADER}。\n"
                f"    ⇒ 00 §3.1 要求「只暴露接口头」。多一个头就多一条"
                f"绕过接口直接读内部状态的路。"
            )

    # ── 检查 3:逐条 #include 比对依赖表 ──────────────────────────
    sources = sorted(
        p for p in SRC.rglob("*") if p.suffix in (".h", ".hpp", ".cpp", ".cc")
    )
    for path in sources:
        mod = module_of(path)
        rel = path.relative_to(SRC.parent)
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            inc = match.group(1)

            # ① 相对路径一律禁止 —— 它绕过 include 搜索路径,
            #    是 CMake 的 PRIVATE/PUBLIC 划分挡不住的那个缺口。
            if ".." in inc.split("/"):
                problems.append(
                    f"[相对路径逃逸] {rel}:{lineno}\n"
                    f"    #include \"{inc}\"\n"
                    f"    ⇒ CMake 的 include 路径隔离对相对路径无效。"
                    f"要用别的模块就走它的 <模块/api.h>。"
                )
                continue

            parts = inc.split("/")
            if len(parts) < 2:
                continue  # 标准库或本模块内的平铺头

            target = parts[0]
            if target not in ALLOWED_DEPS or target == mod:
                continue  # 不是跨模块引用(可能是 IDL 的 domain/ transport/)

            # ② 跨模块只能引 api.h
            if parts[-1] != PUBLIC_HEADER or len(parts) != 2:
                problems.append(
                    f"[越过接口头] {rel}:{lineno} 引了 {target} 的非接口头 \"{inc}\"\n"
                    f"    ⇒ 只能 #include \"{target}/{PUBLIC_HEADER}\"。"
                )
                continue

            # ③ 依赖方向必须在表里(装配层不设限,见 ENTRY_MODULE)
            if mod == ENTRY_MODULE:
                continue
            if target not in ALLOWED_DEPS[mod]:
                problems.append(
                    f"[未声明的依赖] {rel}:{lineno} 里 {mod} → {target}\n"
                    f"    ⇒ ALLOWED_DEPS[\"{mod}\"] = {sorted(ALLOWED_DEPS[mod])}。\n"
                    f"    ⚠️ 若这条依赖确实该有,请在本脚本里显式加上并说明理由 ——"
                    f"依赖面是架构,不该被一次顺手的 include 悄悄改掉。"
                )

    if problems:
        print("❌ 模块边界检查失败(00 §3.1)\n", file=sys.stderr)
        for p in problems:
            print("  " + p + "\n", file=sys.stderr)
        print(
            "⚠️ 违反本约束的后果见 00 §10.4:**单容器跑通、生产分布式挂**,"
            "且是三类静默错误里最难查的那种。",
            file=sys.stderr,
        )
        return 1

    print(
        f"✅ 模块边界检查通过 —— {len(modules)} 个模块 "
        f"({', '.join(modules)})、{len(sources)} 个源文件"
    )
    for m in modules:
        deps = sorted(ALLOWED_DEPS[m])
        print(f"   {m:10s} → {', '.join(deps) if deps else '(无)'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
