# src/world/CMakeLists.txt —— 世界循环
#
# ★★ 00 §3.1 的编译期隔离:只有 include/ 是 PUBLIC。

add_library(sa_world STATIC
    world.cpp
)

target_include_directories(sa_world
    PUBLIC  "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

# world 是把三层缝在一起的地方:
#   sa_platform  L0 时钟 / 日志 / 配置 / 种子
#   sa_net       L1 会话与成帧
#   sa_shared    ★ L3 规则 —— D2 的落点,与客户端编译的是同一份源码
target_link_libraries(sa_world PUBLIC sa_platform sa_net sa_shared)

sa_apply_warnings(sa_world)
