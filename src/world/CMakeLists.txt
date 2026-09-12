# src/world/CMakeLists.txt —— 世界循环
#
# ★★ 00 §3.1 的编译期隔离:只有 include/ 是 PUBLIC。

add_library(sa_world STATIC
    World.cpp
    Map.cpp
)

target_include_directories(sa_world
    PUBLIC  "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

# world 是把三层缝在一起的地方:
#   sa_platform  L0 时钟 / 日志 / 配置 / 种子
#   sa_net       L1 会话与成帧
#   sa_shared    ★ L3 规则 —— D2 的落点,与客户端编译的是同一份源码
target_link_libraries(sa_world PUBLIC sa_platform sa_net sa_shared)
target_link_libraries(sa_world PUBLIC sa_session_storage)

sa_apply_warnings(sa_world)

# ── ★ 浮点合并禁止(战果结算批次:enemyExp 是 world 第一个本地浮点公式)──────────
#
# ⚠️ `enemyExp()`(World.cpp)的 `base + (rankBonus + alpha) * (level + 1)` 是 `x*y+z`,
#    GCC / clang 会合并成单条 FMA、MSVC(/fp:precise)不合并 ⇒ 三平台 CI 逐位不同,
#    而**战果经验进黄金用例集**(可回放是 ③ 层唯一补偿,00 §0)⇒ 表现为最难诊断的偶发红。
# ★ 理由与 shared/CMakeLists.txt 那条完全同源(那里是伤害公式,这里是经验公式):
#   让「逐位一致」从运气变成纪律。⇒ 显式关掉,与 sa_shared 对齐。
# ⚠️ world 此前所有浮点都是**调 sa_shared 的函数**(deriveBaseStats,已在那边 off);
#    enemyExp 是第一个**在 world 本地**求值的浮点公式 ⇒ 约束必须落到 sa_world 自己。
#    ★ 用 PRIVATE:world 是服务端顶层库,不被别的目标消费其浮点(与 sa_shared 的 PUBLIC 不同)。
if(MSVC)
    target_compile_options(sa_world PRIVATE /fp:precise)
else()
    target_compile_options(sa_world PRIVATE -ffp-contract=off)
endif()
