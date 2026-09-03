# src/net/CMakeLists.txt —— L1 网络与会话
#
# ★★ 00 §3.1 的编译期隔离:只有 include/ 是 PUBLIC。

add_library(sa_net STATIC
    framing.cpp
    loopback_transport.cpp
    session.cpp
)

target_include_directories(sa_net
    PUBLIC  "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

# ★ net 是**唯一**该看见 transport/ 组 IDL 的模块(02 §9:
#   「shared/ 不得引用 transport/,只有 src/net 依赖」)。
#   ⚠️ sa_idl 同时暴露 domain/ 与 transport/ 两组头,靠 include 路径分不开
#     —— 那条边界由 tools/check_shared_purity.py 在 shared/ 侧守着,
#       这里是它合法的另一侧。
target_link_libraries(sa_net PUBLIC sa_idl)

# ⚠️ 不链 sa_platform:见 session.cpp 里 Pong.server_time_ms 那处注释 ——
#    为了一个时间戳把 L1 绑到 L0 上不划算,时钟由宿主注入或上移到 world。

sa_apply_warnings(sa_net)
