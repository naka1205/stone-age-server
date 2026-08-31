# cmake/SgWarnings.cmake —— 统一的告警口径
#
# ★ 为什么这不是"风格偏好":00 §10.4 把本项目最容易踩的错误归为**三类静默错误**
#   (slot 别名合并 / 照抄 8.5 / 进程内捷径)—— 它们的共同点是**不报错、不崩溃**。
#   编译器能替我们抓住的那部分,一条都不该放过。

function(sg_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        # ⚠️ 与本项目直接相关的几条,不是通用清单:
        -Wconversion          # 原版大量 int/float 混算(伤害公式第 3 步的整数除法窄缝)
        -Wsign-conversion     # 原版 slot 下标与 -1 哨兵混用(objindex = -1)
        -Wshadow              # 原版 g* 全局与局部同名普遍存在,四步改造第①步正要消灭它
        -Wold-style-cast
        -Wnon-virtual-dtor
    )
  endif()
endfunction()

# ★★ 测试目标必须开断言 —— 这不是可选的严格性,是防「绿色的假测试」。
#
# CMake 的 Release / RelWithDebInfo / MinSizeRel 三种构型都会定义 **NDEBUG**,
# 而 NDEBUG 会把 `assert` **整个编译掉**。
# ⇒ 后果:测试照常链接、照常退出码 0、照常打印 "OK",但**一条都没验证**。
#
# 实测(2026-08-31):首次配置 RelWithDebInfo 构建契约冒烟测试时正是如此 ——
# 编译器报了 7 个「变量已赋值但未使用」,那正是断言被剥掉后留下的痕迹。
# 若不是开了 -Wall/-Wextra,这个假绿色不会有任何外部表现。
#
# ⚠️ 这与 00 §10.4 的「三类静默错误」同源:不报错、不崩溃,只是悄悄什么都不做。
function(sg_enable_assertions target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()
