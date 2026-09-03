# src/platform/CMakeLists.txt —— L0 平台设施
#
# ★★ 00 §3.1 的编译期隔离在这里落地:
#    · include/        PUBLIC  —— 只有 platform/api.h 一个头对外
#    · 其余源码与 internal/  PRIVATE —— 别的模块**编译期**就看不见
#    ⇒ 「不得有共享内存捷径」从口头纪律变成 include 路径事实。

add_library(sa_platform STATIC
    clock.cpp
    config.cpp
    json.cpp
    log.cpp
    random.cpp
)

target_include_directories(sa_platform
    PUBLIC  "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

# ⚠️ 这里**没有** target_link_libraries(sa_platform ... sa_shared):
#    platform 是 L0,不该认识 L3 的规则类型。种子交给谁用是 world 的事。
#    ⇒ 保持 platform 的依赖面为空,它才可能在阶段 2 被 storage / lock 复用。

sa_apply_warnings(sa_platform)
