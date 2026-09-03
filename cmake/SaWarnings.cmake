# cmake/SaWarnings.cmake —— 统一的告警口径
#
# ★★ 本仓最容易踩的错误归为**三类静默错误**
#   (slot 别名合并 / 照抄 8.5 / 进程内捷径)—— 它们的共同点是**不报错、不崩溃**。
#   编译器能替我们抓住的那部分,一条都不该放过。

# ── ★ SA_WERROR:把告警当错误 ──────────────────────────────────────
#
# ⚠️★ **默认 OFF,CI 上 ON。这个不对称是有意的。**
#
# 文档里「0 告警」被当作凭据用了很多次(§9.0.5 / §9.0.6 / §9.0.7.1),
# 但至今**没有任何机制保证它继续为真** —— 告警只是滚过屏幕的文字,
# 一次构建几百行输出,新增一条不会有人发现。
# ⇒ 一次性验证记的是「那天 0 告警」;要让它明天还成立,只有让告警**中断构建**。
#
# 那为什么本地不默认开?因为开发中途的半成品代码(未使用的变量、
# 临时注释掉的分支)会被 -Werror 直接打断,而那时人正在想别的事。
# ⇒ **本地容忍告警、CI 不容忍**,两边的用途本就不同。
#
# ⚠️ 认下代价:GitHub runner 的编译器会随镜像升级(GCC 13→14→15),
# 新版本常带来新告警 ⇒ CI 可能因**编译器升级**而非代码改动变红。
# 这是接受的:那种红说明「新编译器发现了旧代码里的东西」,值得看一眼,
# 不是噪声。真正不可接受的是反过来 —— 告警悄悄堆积而没人知道。
option(SA_WERROR "把编译告警当作错误(CI 用;本地默认关)" OFF)

function(sa_apply_warnings target)
  if(MSVC)
    # ★★ `/utf-8` 不是风格选项,是**正确性**选项(2026-09-02 补,Windows 验证前置)。
    #
    # 本仓源码是 **UTF-8**,且中文**不止出现在注释里**:
    #   · doctest 用例名  TEST_CASE("相克矩阵:与 battle_event.c:922-949 逐项一致")
    #   · 断言消息        assert(false && "相克矩阵与 05-battle.md §3.4 原表不符")
    #   · printf 输出     "★ 相克矩阵重排错误 [攻%d][守%d]"
    #
    # ⚠️ MSVC 默认按**系统 ANSI 代码页**读源文件(简体中文 Windows = 936/GBK)
    #    ⇒ 轻则 C4819 刷屏 + 用例名与断言消息乱码,
    #      重则某个 UTF-8 字节序列在 GBK 下被读成尾随反斜杠一类,**直接编译错误**。
    #
    # ★ 客户端仓的 SaClientWarnings.cmake 早有这一行,本文件此前漏了 ——
    #   两端都是 clang 时永远不会暴露,与 d939247 那个缺口是同一类。
    target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
    if(SA_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
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
    if(SA_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
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
function(sa_enable_assertions target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()
