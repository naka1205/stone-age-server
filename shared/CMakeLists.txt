# shared/CMakeLists.txt —— D2 的物理体现
#
# ★★ 本目录被**客户端仓以 CMake FetchContent + 锁定 tag 引用**(DR-TS3),
#    两端编译同一份源码。这是 D2「L3 规则层双端共享」唯一的落地形式。
#
# ⚠️ 因此本文件里的每条依赖都要问一句:**客户端也能满足吗?**
#    答案为否的东西一律不能进来 —— 那正是 01 §4「shared/ 只依赖标准库」的由来。

# ── 双端标准的下限 ───────────────────────────────────────────────
#
# ⚠️★ 服务端是 C++20(D1),而 **GameStudio 是 C++17**(00 §1.2)。
#    ⇒ shared/ 的源码必须在 **C++17** 下也能编译,否则双端共享在客户端侧当场失败。
#    这里显式把 shared 目标降到 17,让不兼容在**服务端构建时**就暴露,
#    而不是等到客户端集成那天。
set(SG_SHARED_CXX_STANDARD 17 CACHE STRING "shared/ 的 C++ 标准下限（受 GameStudio 约束）")

# ── ★★ 独立可消费性(2026-09-02,客户端骨架落地时实测暴露)───────────
#
# 本文件原先依赖两样**只在服务端顶层存在**的东西:目标 `sg_idl` 与函数
# `sg_apply_warnings()`。⇒ 客户端按 DR-TS3 只引入 `shared/` 一个目录时,
# `add_subdirectory` **在配置期就失败**,D2 的物理前提根本走不到编译那一步。
#
# ⚠️ 这不是"客户端应该自己先定义 sg_idl" —— 那等于要求消费方知道本目录的内部依赖名,
#    与 DR-TS3 承诺的「FetchContent 直接可用」冲突。⇒ 缺口在本文件,补在本文件。
#
# 两处都写成「有就用、没有就就地补」,因此服务端顶层构建的行为**完全不变**。
if(NOT TARGET sg_idl)
    add_library(sg_idl INTERFACE)
    target_include_directories(sg_idl INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/../idl/generated/cpp")
endif()

add_library(sg_shared STATIC
    rules/battle.cpp
)

target_include_directories(sg_shared PUBLIC "${CMAKE_CURRENT_LIST_DIR}")

# ✅ 允许:IDL 的 domain/ 组(事件与领域值对象)
# ❌ 禁止:IDL 的 transport/ 组
#   —— 但 sg_idl 这个 target 同时暴露两组(生成物的 #include 从 generated/cpp 根算起,
#      靠 include 路径隔离不了)。⇒ 该边界由 tools/check_shared_purity.py 强制。
target_link_libraries(sg_shared PUBLIC sg_idl)

target_compile_features(sg_shared PUBLIC cxx_std_17)

# ── ★★ 浮点语义:禁止 FMA 合并(2026-09-02,跨编译器验证时固化)────────
#
# `00` §0 已认下第 ③ 层「规则与公式不可自证」,而 **L3 黄金用例集是唯一补偿手段**
# (§9.0.3 更把它升级为与移植同步的硬要求)。
# ⇒ 用例集的价值全部押在一件事上:**同样的输入,两端算出逐位相同的结果**。
#
# ⚠️ 但伤害公式是 `float`(移植期实测纠正了 `05` §3.1 的「整数除法」),
#    而 `a*b+c` 是否被合并成单条 FMA 指令**由编译器自行决定**:
#      GCC   默认 -ffp-contract=fast  ← 跨语句都能合并
#      clang 默认 on(仅语句内)
#      MSVC  默认 /fp:precise
#    三者结果可以逐位不同,且**不会有任何告警**。
#
# ★ 2026-09-02 实测:GCC 15 / libstdc++ 下 2,148 断言确实全绿 ——
#   但那是**当前公式恰好没踩到合并点**,不是这条性质成立。
#   ⇒ 显式关掉,让"逐位一致"从运气变成纪律。
#
# ⚠️ 这与本文件上方「不硬编码任何编译选项」不矛盾:那条针对的是**告警口径**
#    (风格,消费方各有一套);本条是**语义要求**,消费方换一套就会算错。
if(MSVC)
    target_compile_options(sg_shared PUBLIC /fp:precise)
else()
    target_compile_options(sg_shared PUBLIC -ffp-contract=off)
endif()

# ── ★★ 源码字符集:MSVC 必须显式 /utf-8(2026-09-02,Windows 验证前置)────
#
# `shared/rules/*.cpp|h` 是 **UTF-8**,带大量中文注释(battle.cpp 161 行 / constants.h 109 行)。
# ⚠️ MSVC 默认按**系统 ANSI 代码页**读源文件(简体中文 Windows = 936/GBK)⇒ C4819 刷屏,
#    而某些 UTF-8 字节序列在 GBK 下会被读成尾随反斜杠一类,可能直接编译错误。
#
# ★ 为什么写在**这里**而不是只写在消费方的告警口径里:
#   本文件上方那条「告警口径由消费方给」是对的,但 `/utf-8` 不是告警口径 ——
#   它决定**源文件怎么被解码**,是 DR-TS3「FetchContent 直接可用」的一部分。
#   ⚠️ 客户端只 `add_subdirectory(shared)` 时**不会**调用任何 `sg_apply_warnings`
#     ⇒ 若只补在两仓的告警函数里,`sg_shared` 这个目标本身仍然没有它。
#   这与本文件开头那个 `sg_idl` / `sg_apply_warnings` 缺口(d939247)是同一类问题:
#   **凡是「消费方不做就会坏」的东西,都得本目录自己保证。**
if(MSVC)
    target_compile_options(sg_shared PUBLIC /utf-8)
endif()

# ★ 阶段 0.1/1.1 起 sg_shared 是 STATIC(此前是 INTERFACE 的纯头契约层)。
#   ⚠️ 标准仍锁 17 —— 见上方 SG_SHARED_CXX_STANDARD。它要被 GameStudio(C++17)编译。
set_target_properties(sg_shared PROPERTIES
    CXX_STANDARD ${SG_SHARED_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# ★ 告警口径由**消费方**给:服务端有 sg_apply_warnings(cmake/SgWarnings.cmake),
#   客户端有它自己的一套(工具链不同,-Wconversion 这类开关不可能逐字通用)。
#   ⇒ 有则用,无则由消费方在自己那侧施加,不在此处硬编码任何编译选项。
if(COMMAND sg_apply_warnings)
    sg_apply_warnings(sg_shared)
endif()

# ⚠️ 这里**不**给 sg_shared 加任何其它 target_link_libraries(除 sg_idl):
#    一旦链上 pthread / 日志 / 网络,客户端侧的 FetchContent 就会被迫一起拉。
