#!/usr/bin/env python3
"""tools/check_shared_purity.py —— shared/ 的依赖纯度检查

★★ 这条检查挡的是 D2 失效,不是代码风格。

01-server-architecture.md §4:
    「shared/ 是 D2 的物理体现 —— 该目录被客户端以 CMake FetchContent 方式引用,
      两端编译同一份源码。因此它只能依赖标准库:不得出现 socket、MySQL、日志、
      以及任何 #include 服务端头文件的代码。」

02-protocol.md §9:
    「shared/rules 可以依赖 IDL 生成的**事件类型**,但不得依赖任何**传输类型**
      (Envelope、连接、会话)。否则 01 §4 的『shared/ 只依赖标准库』就破了。」

⚠️ 为什么必须用脚本而不是 CMake 的 include 路径隔离:
   IDL 生成物的 #include 一律从 generated/cpp 根算起(如 "domain/common.sg.h"),
   ⇒ 只要把 generated/cpp 加进 include path,domain/ 与 transport/ 就同时可见,
     无法只暴露其一。而且 CMake 也管不住 `#include <sys/socket.h>` 这类。

用法:
    python3 tools/check_shared_purity.py          # 违规则退出码 1
"""

import re
import sys
from pathlib import Path

# ★★ Windows 上的输出编码不是风格问题,是**退出码正确性**问题(2026-09-02 实测)。
#
#   简中 Windows 控制台默认 cp936(GBK),而本脚本的输出含 ✅ / ★ / ⇒ ——
#   `print` 会抛 UnicodeEncodeError,于是两种情况**都读不出真相**:
#     · 纯度通过时:崩在成功那一行 ⇒ 退出码 1 ⇒ CI 报「D2 的守卫失败」,而它其实通过了;
#     · 纯度失败时:崩在打印违规清单那一行 ⇒ 看到的是 traceback,不是「D2 被破了」。
#
#   ⇒ 与 shared/ 自己给 `/utf-8` 同理:凡「消费方不做就会坏」的,脚本自己保证。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):   # 被重定向到不支持 reconfigure 的对象
        pass

ROOT = Path(__file__).resolve().parent.parent
SHARED = ROOT / "shared"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

# C++ 标准库头(含 C 兼容头)。shared/ 只允许这些 + 项目内 rules/ model/ domain/。
STD_HEADERS = {
    # 容器与工具
    "array", "bitset", "deque", "forward_list", "list", "map", "queue", "set",
    "span", "stack", "unordered_map", "unordered_set", "vector",
    "algorithm", "functional", "iterator", "memory", "numeric", "optional",
    "ranges", "tuple", "utility", "variant", "concepts", "compare", "bit",
    # 数值与字符
    "cmath", "cstdint", "cstddef", "cstring", "cstdlib", "cassert", "climits",
    "cfloat", "cctype", "cstdio", "limits", "type_traits", "initializer_list",
    "string", "string_view", "charconv", "cstdarg",
    # 例外与诊断
    "exception", "stdexcept", "system_error", "source_location",
}

# 明确禁止的依赖面。★ 每条都对应一个具体后果,不是泛泛的"保持干净"。
BANNED = [
    (re.compile(r"^transport/"),
     "传输类型(02 §9):shared/ 依赖 Envelope/握手/错误面会破坏「只依赖标准库」"),
    (re.compile(r"^(sys/|netinet/|arpa/|winsock|ws2_)"),
     "socket —— shared/ 要在客户端编译,不得含网络代码(01 §4)"),
    (re.compile(r"(mysql|mariadb|sqlite|hiredis|redis\+\+|sw/redis)"),
     "存储/缓存客户端 —— 属 L0,不属 L3"),
    (re.compile(r"(spdlog|prometheus|fmt/)"),
     "日志/指标 —— 属 L0;L3 是纯函数,不记日志"),
    (re.compile(r"(asio|boost)"),
     "asio/boost —— 属 sg_net(L0)"),
    (re.compile(r"^\.\./src/|^src/"),
     "服务端源码 —— shared/ 不得反向依赖 src/"),
    (re.compile(r"^(chrono|ctime)$"),
     "时钟 —— L3 必须是纯函数,读时钟会破坏可回放性(05 §1.5)"),
    (re.compile(r"^random$"),
     "★ <random> —— 随机源必须经注入的 IRandom(07 §11.3 判据 ④);"
     "且 std::uniform_int_distribution 的取数方式不由标准规定,跨实现序列不同"),
    (re.compile(r"^(iostream|fstream|sstream|filesystem)$"),
     "I/O —— L3 不做任何 I/O(07 §11.3 判据 ③)"),
    (re.compile(r"^(thread|mutex|atomic|condition_variable|future)$"),
     "并发原语 —— L3 是纯函数,不该有共享状态"),
]

# 允许的项目内前缀。
ALLOWED_PROJECT_PREFIXES = ("rules/", "model/", "domain/", "sg_idl_runtime.h", "ids.h")


def main() -> int:
    if not SHARED.is_dir():
        print(f"★ 找不到 {SHARED}", file=sys.stderr)
        return 1

    violations: list[str] = []
    scanned = 0

    for path in sorted(SHARED.rglob("*")):
        if path.suffix not in {".h", ".hpp", ".cpp", ".cc", ".inl"}:
            continue
        scanned += 1
        rel = path.relative_to(ROOT)
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            m = INCLUDE_RE.match(line)
            if not m:
                continue
            header = m.group(1)

            hit = next((why for pat, why in BANNED if pat.search(header)), None)
            if hit:
                violations.append(f"{rel}:{lineno}  #include \"{header}\"\n    ⇒ {hit}")
                continue

            if header in STD_HEADERS:
                continue
            if header.startswith(ALLOWED_PROJECT_PREFIXES):
                continue

            violations.append(
                f"{rel}:{lineno}  #include \"{header}\"\n"
                f"    ⇒ 既不是标准库头,也不在允许的项目内前缀 "
                f"{ALLOWED_PROJECT_PREFIXES} 之列。\n"
                f"      若确属标准库,把它加进本脚本的 STD_HEADERS;"
                f"否则它不该出现在 shared/。")

    if violations:
        print("★★ shared/ 依赖纯度检查失败 —— D2「一份规则两端编译」会因此失效:\n")
        for v in violations:
            print("  " + v.replace("\n", "\n  "))
        print(f"\n共 {len(violations)} 处。")
        return 1

    print(f"✅ shared/ 依赖纯度检查通过（扫描 {scanned} 个文件）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
