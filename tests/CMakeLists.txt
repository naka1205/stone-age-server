# tests/CMakeLists.txt
#
# ⚠️ 现在用标准 <cassert>,不是 doctest。
#    DR-TS4 已裁定 doctest,但接入点在阶段 1.1 ——
#    契约层的用例数很少,而 shared/rules 有实现后黄金用例集才会成批出现。
#    过早引入会给 shared/ 的客户端集成加一个此刻不必要的 FetchContent。

# ── 契约层冒烟 ───────────────────────────────────────────────────
#
# ★ 故意用 **C++17** 编译:GameStudio 是 C++17,shared/ 必须在它下面能编译,
#   否则 D2「一份规则两端编译」在客户端侧当场失败。
#   ⇒ 让不兼容在服务端构建时就暴露,而不是等到客户端集成那天。
add_executable(sg_contract_smoke contract_smoke.cpp)
target_link_libraries(sg_contract_smoke PRIVATE sg_shared)
set_target_properties(sg_contract_smoke PROPERTIES
    CXX_STANDARD ${SG_SHARED_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
sg_apply_warnings(sg_contract_smoke)
sg_enable_assertions(sg_contract_smoke)   # ★ 见 cmake/SgWarnings.cmake:防「绿色的假测试」
add_test(NAME contract_smoke COMMAND sg_contract_smoke)

# ── IDL 生成物冒烟(与 idl/tests/run.sh 同源)──────────────────────
#
# 这个用 C++20:生成物只被服务端与客户端的**网络层**消费,不进 shared/。
add_executable(sg_idl_smoke ${CMAKE_SOURCE_DIR}/idl/tests/smoke.cpp)
target_link_libraries(sg_idl_smoke PRIVATE sg_idl)
sg_apply_warnings(sg_idl_smoke)
sg_enable_assertions(sg_idl_smoke)        # ★ 同上
add_test(NAME idl_smoke COMMAND sg_idl_smoke)

# ── ★★ shared/ 依赖纯度(D2 的守卫)───────────────────────────────
#
# 它挡的是 D2 失效,不是代码风格 —— 见 tools/check_shared_purity.py 卷首。
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME shared_purity
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_SOURCE_DIR}/tools/check_shared_purity.py)
  # IDL schema 与生成物同步(02 §8 检查 1–3 + 生成物入库)
  add_test(NAME idl_verify
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_SOURCE_DIR}/idl/codegen/sgidl_gen.py --verify)
else()
  message(WARNING
      "未找到 Python3 —— shared/ 依赖纯度与 IDL 同步检查将不会运行。"
      "⚠️ 这两条不是可选的风格检查,CI 上必须有 Python3。")
endif()
