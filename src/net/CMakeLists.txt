# src/net/CMakeLists.txt —— L1 网络与会话
#
# ★★ 00 §3.1 的编译期隔离:只有 include/ 是 PUBLIC。

# ★★ framing.cpp 已于 2026-09-06 移到 `shared/wire/`(DR-TS9 乙案):
#    成帧与信封是双端同一语义,客户端 1.4 也要 ⇒ 共编一份,不写第二份。
#    ⇒ 本模块只留**宿主侧**:连接(socket / Loopback)与会话状态机。
add_library(sa_net STATIC
    loopback_transport.cpp
    session.cpp
    tcp_transport.cpp
)

target_include_directories(sa_net
    PUBLIC  "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

# ★★ winsock 要显式链接 —— 而且这是 **PRIVATE**:
#   TcpTransport 的声明里没有任何平台类型(net/api.h 卷首),
#   ⇒ 依赖它的 world / tests **不该**因此被拖进 winsock 的头与宏。
#   ⚠️ 写成 PUBLIC 不会报错,只会让那条依赖方向约束悄悄失效 ——
#     与 00 §10.4 三类静默错误同族:过宽的依赖不会失败,只会安静地多付代价。
if(WIN32)
  target_link_libraries(sa_net PRIVATE ws2_32)
endif()

# ★ 02 §9 那条边界 2026-09-06 随 DR-TS9 改为**按子树**:
#     shared/rules  ❌ 不得引 transport/     ← 不放宽
#     shared/wire   ✅ 可且只可引 transport/  ← 它的职责就是信封
#     src/net       ✅ 宿主侧,本来就可以
#   ⚠️ sa_idl 同时暴露 domain/ 与 transport/ 两组头,靠 include 路径分不开
#     —— 那条边界由 tools/check_shared_purity.py 按子树守着(SUBTREE_ALLOWED)。
#
# ★★ sa_wire 是 **PUBLIC**:`net/api.h` 用 `using sa::wire::FrameReader` 等把那批
#    类型引进 `sa::net`,消费者(world / tests)写的是 `sa::net::FrameReader`
#    ⇒ 它们需要看得见 `wire/framing.h`。写成 PRIVATE 会在消费者侧编译失败。
target_link_libraries(sa_net PUBLIC sa_idl sa_wire)

# ⚠️ 不链 sa_platform:见 session.cpp 里 Pong.server_time_ms 那处注释 ——
#    为了一个时间戳把 L1 绑到 L0 上不划算,时钟由宿主注入或上移到 world。

sa_apply_warnings(sa_net)
