# src/CMakeLists.txt —— 服务端模块
#
# ★★ 00 §3.1:「进程内形态下,服务之间只能通过上述接口通信,不得有共享内存捷径。
#    ⇒ 做成编译期约束:各模块互相不可见,只暴露接口头,链接期换实现。」
#
# 落地方式:每个模块一个静态库,只有 `include/` 是 PUBLIC ⇒ 别的模块**编译期**
# 就看不见它的内部头。⚠️ 00 §9.0.4 明写这件事「必须在 1.5 建目录时就立起来 ——
# 事后拆比一开始就分贵得多」。
#
# ⚠️ CMake 挡得住"include 别人的私有头",挡不住"写 #include \"../net/session.h\"" ——
#    那条由 tools/check_module_boundaries.py 守着(ctest 必跑项)。
#
# ── 已建 / 未建(01 §4)────────────────────────────────────────────
#   ✅ platform   1.5:配置 · 日志 · 单调时钟 · 随机源
#   ✅ net        1.5:ITransport · 长度前缀成帧 · IDL 编解码 · 握手会话
#   ✅ world      1.5:最小 tick · 一场战斗的生命周期
#   ⬜ storage · lock · gateway · session_storage · social —— 阶段 2

add_subdirectory(platform)
add_subdirectory(net)
add_subdirectory(world)

add_executable(stone_age_server main.cpp)
# ⚠️ 只链 sa_world:它已 PUBLIC 依赖 sa_platform,再显式写一遍会让链接器
#    报 "ignoring duplicate libraries" —— 而本仓把「0 告警」当凭据用(00 §9.0.9 ③)。
target_link_libraries(stone_age_server PRIVATE sa_world)
sa_apply_warnings(stone_age_server)

# ⚠️ 不给 stone_age_server 开 sa_enable_assertions:它是**产物**,不是测试。
#    断言防线针对的是"测试在 NDEBUG 下静默失效"那件事(cmake/SaWarnings.cmake)。
