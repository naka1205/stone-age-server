#!/usr/bin/env python3
"""tools/ci_verify.py —— 一趟走完本仓的全部验收,CI 与本地共用同一份逻辑

★★ 为什么是一个脚本而不是直接把命令写进 workflow YAML:

  ① CI 平台会换(gitee → GitHub → 将来可能自建),而**验收内容不该跟着换**。
     YAML 里那套命令一旦成为唯一真源,本地就再也无法复现 CI 的判定,
     "在我机器上是绿的" 会变成常态。
  ② ★ 本脚本有两项**不是跑命令、而是做判断**的检查(§2 与 §5),
     它们在 YAML 里写不下,也不该写。

⚠️★ 归因顺序是有纪律的(承自 tools/win_validate.ps1 的实测教训):
    先看 §0 入口脚本编码 —— 若某个检查脚本会在 cp1252 下把"通过"印成退出码 1,
                            它给出的红与绿都不含信息;
    再看 §2 测试注册清单 —— 若某条检查根本没注册,后面所有绿色都不完整;
    再看 §5 断言防线     —— 若断言被编译掉,前面所有"通过"都不算数;
    才轮到逐条测试结果。
  ⇒ 报告按这个顺序打印,不按执行顺序。

用法:
    python3 tools/ci_verify.py                     # 默认 build/ci · RelWithDebInfo
    python3 tools/ci_verify.py --build-dir build/x --config Release
    python3 tools/ci_verify.py --generator "Visual Studio 18 2026"
    python3 tools/ci_verify.py --skip-negative     # 跳过 §5(不建议;只在调试本脚本时用)

退出码:0 = 全部通过;1 = 有任何一项未通过。
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

# ★★ Windows 上的输出编码不是风格问题,是**退出码正确性**问题 ——
#   与 check_shared_purity.py 卷首同一条(2026-09-02 实测:简中 Windows 的
#   cp936 控制台遇到 ✅ / ★ / ⇒ 会抛 UnicodeEncodeError,于是脚本
#   **通过时也退出码 1**,CI 会把它读成"检查失败")。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):   # 被重定向到不支持 reconfigure 的对象
        pass

REPO = Path(__file__).resolve().parent.parent

# ── ★★ §2 的期望集合 ────────────────────────────────────────────────
#
# ⚠️★ **这个清单写死在这里是有意的,不是偷懒。**
#
# tests/CMakeLists.txt 里有两条检查是**条件注册**的:
#     shared_purity   需要 Python3
#     idl_verify      需要 protoc
# 缺依赖时它们**不注册**(而不是常红 —— 常红会训练人忽略红色,00 §10.4)。
#
# ⇒ 于是有一个危险的失效:CI 镜像哪天不带 protoc 了,idl_verify 悄悄消失,
#   ctest 照样打印 "100% tests passed",而**守 schema 漂移的那道关已经没了**。
#   01 §13 欠债 9 的原话:「CI 上必须有 protoc,否则这道关会静默消失 ——
#   那比常红更坏。」
#
# ⇒ 本清单把"应该有几条"变成一个**会失败的断言**。
#   将来新增测试时必须同步改这里 —— 那正是想要的:让测试集合的变化
#   经过一次人的确认,而不是被环境悄悄决定。
EXPECTED_TESTS = {
    "contract_smoke",   # 契约层冒烟(含相克矩阵重排保护,§5 的探针也打在它身上)
    "rules_battle",     # ★ L3 黄金用例集 —— ③ 层不可自证的唯一补偿手段
    "idl_smoke",        # IDL 生成物体积与编解码
    "shared_purity",    # ★★ D2 的守卫:shared/ 只依赖标准库
    "idl_verify",       # ★ schema 与生成物同步(需 protoc)
    # ── 阶段 1.5(2026-09-04 接入构建时补齐)──────────────────────
    "net_framing",        # 帧层 / 信封层 / 会话状态机
    "net_tcp",            # ★ TcpTransport —— 1.5 收尾项,真 socket 上跑
    "platform_config",    # 配置装载与快速失败
    "world_tick",         # ★ 最小 tick 与「战斗速度 ≠ tick 频率」
    "module_boundaries",  # ★★ 00 §3.1 的守卫:进程内捷径(需 Python3)
    # ── 1.5 收尾(2026-09-05,TcpTransport 接入入口)────────────────
    "server_self_test",   # ★ 入口自检:启动 → 绑端口 → tick → 关闭,退出码即判据
}


def check_script_encoding():
    """★★ §0 凡入口脚本都必须显式设 UTF-8 输出 —— 把「各自记得」变成会失败的断言。

    ⚠️★ 起因(2026-09-04 实测,00 §9.0.12):check_module_boundaries.py 漏了
      卷首那一段 ⇒ windows runner 的 cp1252 stdout 遇到 print("✅ …") 抛
      UnicodeEncodeError ⇒ 退出码 1 ⇒ CI 报「00 §3.1 的守卫失败」,
      **而边界检查本身是通过的**。

    ★ 值得记的不是这个错,是它**为什么会发生**:ci_verify.py /
      check_shared_purity.py / saidl_gen.py 三处早就都有这一段,
      check_shared_purity.py 卷首还专门写了半屏理由(2026-09-02 实测)。
      惯例存在、理由写清楚了,新加的第四个脚本照样漏 ——
      因为它是「每个文件各自记得」的纪律,而不是结构性保证。
      ⇒ 与 §2 同族:让「应该都有」变成一条**会失败**的断言。

    ★ 判据只有两条,故意做到不需要维护任何清单:
        ① 有 `if __name__ == ...` 守卫 ⇒ 它会被当作入口执行,退出码有人读;
        ② 源码含非 ASCII               ⇒ 它可能 print 出 cp936/cp1252 编不出的字符。
      被 import 的模块**不在其列**(idl/codegen/saidl/*.py)—— 它们共用入口
      已经设好的 sys.stdout,各自再设一遍是噪音。

    ⚠️ 判据故意**不**写成「在 CI 上跑的脚本」:那需要一份清单,而清单本身
       就是下一个「各自记得」。宁可对一次性分析工具也过严 ——
       补一行的成本是 5 行,漏一个的成本是一次难以归因的红。
    """
    entry = re.compile(r"^\s*if\s+__name__\s*==", re.M)
    offenders = []
    for path in sorted(REPO.rglob("*.py")):
        rel = path.relative_to(REPO)
        if {"build", "_deps", ".git"} & set(rel.parts):
            continue
        src = path.read_text(encoding="utf-8")
        if not entry.search(src) or src.isascii():
            continue
        if "reconfigure(encoding" not in src:
            offenders.append(rel.as_posix())
    if offenders:
        return ("★★ 入口脚本输出编码", False,
                f"缺 sys.stdout.reconfigure(encoding=\"utf-8\") 的入口脚本:"
                f"{', '.join(offenders)} ⇒ 在 cp936 / cp1252 控制台上 print "
                f"非 ASCII 会抛 UnicodeEncodeError,**通过时也退出码 1** ⇒ "
                f"CI 会把它读成「这道检查失败了」,而检查本身可能是通过的")
    return ("★★ 入口脚本输出编码", True, "全部入口脚本已显式设 UTF-8")


class Report:
    """逐项记录,末尾按归因顺序汇总。"""

    def __init__(self):
        self.items = []          # [(order, name, ok, detail)]

    def record(self, order, name, ok, detail=""):
        self.items.append((order, name, ok, detail))
        print(f"{'✅' if ok else '❌'} {name}" + (f" —— {detail}" if detail else ""),
              flush=True)

    def summarize(self):
        print("\n" + "═" * 70)
        print("汇总(按归因顺序,不按执行顺序)")
        print("═" * 70)
        for _, name, ok, detail in sorted(self.items, key=lambda x: x[0]):
            print(f"  {'✅' if ok else '❌'} {name}")
            if detail:
                print(f"       {detail}")
        failed = [n for _, n, ok, _ in self.items if not ok]
        print("═" * 70)
        if failed:
            print(f"❌ {len(failed)} 项未通过:{', '.join(failed)}")
            print("\n⚠️ 读法:先看「测试注册清单」—— 少一条则后面的绿色都不完整;")
            print("        再看「断言防线」—— 它失效则所有『通过』都不算数;")
            print("        才是逐条测试。")
            return 1
        print(f"✅ 全部 {len(self.items)} 项通过")
        return 0


def run(cmd, cwd=None, capture=True):
    """跑一条命令,返回 (returncode, 合并后的输出)。"""
    print(f"\n$ {' '.join(str(c) for c in cmd)}", flush=True)
    proc = subprocess.run(
        cmd, cwd=cwd or REPO,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
        text=True, encoding="utf-8", errors="replace")
    if capture and proc.stdout:
        print(proc.stdout, flush=True)
    return proc.returncode, (proc.stdout or "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default="build/ci")
    ap.add_argument("--config", default="RelWithDebInfo")
    ap.add_argument("--generator", default=None)
    ap.add_argument("--skip-negative", action="store_true")
    args = ap.parse_args()

    build = (REPO / args.build_dir).resolve()
    rep = Report()

    # ── §0 ★★ 入口脚本输出编码(最先判,且不需要构建)────────────────
    #
    # ⚠️ 排在配置之前是有理由的:它判的是「后面那些检查脚本的**退出码**
    #    本身可不可信」。一个会在 cp1252 下把"通过"印成退出码 1 的脚本,
    #    它给出的红与绿都不含信息 ⇒ 必须比它们先判。
    rep.record(0, *check_script_encoding())

    # ── §1 配置 ────────────────────────────────────────────────────
    #
    # ★ SA_WERROR=ON:文档里「0 告警」被当凭据用了很多次,但至今没有任何机制
    #   保证它继续为真。CI 上让告警**中断构建**,那句话才是可持续的。
    #   (cmake/SaWarnings.cmake 卷首有为什么本地默认 OFF 的理由。)
    cfg = ["cmake", "-S", str(REPO), "-B", str(build),
           f"-DCMAKE_BUILD_TYPE={args.config}", "-DSA_WERROR=ON"]
    if args.generator:
        cfg += ["-G", args.generator]
    rc, out = run(cfg)
    if rc != 0:
        rep.record(1, "配置", False, "cmake 配置失败 ⇒ 后续全部无法进行")
        return rep.summarize()

    # ⚠️ 配置期的 message(WARNING) 是「某条检查不会注册」的**唯一预警**。
    #    它在 §2 会变成硬失败,这里先把它显式摘出来,让日志里能一眼看到原因。
    for pat, why in (("未找到 protoc", "idl_verify 不会注册"),
                     ("未找到 Python3", "shared_purity 与 idl_verify 都不会注册")):
        if pat in out:
            print(f"\n⚠️★ 配置期告警:{pat} ⇒ {why}", flush=True)
    # ⚠️★ **unused-cli 必须是硬失败,不能只是 CMake 的一句黄字**(2026-09-04 兑现)。
    #
    #   改名 sg_ → sa_ 时本行漏改成了 `-DSG_WERROR=ON`。CMake 不认识这个变量,
    #   于是只印一句 "Manually-specified variables were not used",配置照样成功,
    #   下面第 1 项照样报告「SA_WERROR=ON」—— 而 -Werror **整整一批提交都没开过**。
    #   三平台全绿,绿得毫无意义。
    #
    #   ⇒ 这一族错误的共性:传错开关名不会失败,只会让开关静默失效。
    #     凡「我们靠某个 -D 才成立的结论」,都要先证明那个 -D 真的被项目收下了。
    #     (同族先例:RelWithDebInfo 定义 NDEBUG ⇒ assert 被编译掉,测试照样打印 OK。)
    if "Manually-specified variables were not used" in out:
        junk = re.findall(r"^\s{4}(\w+)\s*$", out, re.M)
        rep.record(1, "配置", False,
                   "★ CMake 收到了它不认识的 -D 变量:" + ", ".join(junk) +
                   " ⇒ 对应开关静默失效,本次运行的所有『通过』都不可信")
        return rep.summarize()

    rep.record(1, "配置", True, f"{args.config} · SA_WERROR=ON")

    # ── §2 ★★ 测试注册清单(最先判,理由见 EXPECTED_TESTS 卷首)────────
    rc, out = run(["ctest", "--test-dir", str(build), "-C", args.config, "-N"])
    listed = set(re.findall(r"Test\s+#\d+:\s+(\S+)", out))
    missing = EXPECTED_TESTS - listed
    extra = listed - EXPECTED_TESTS
    if missing:
        rep.record(2, "★★ 测试注册清单", False,
                   f"**少了** {sorted(missing)} ⇒ 这些检查在本次运行中"
                   f"根本不存在,而 ctest 仍会打印 100% passed。"
                   f"CI 镜像缺 protoc / Python3 是最常见原因。")
    elif extra:
        rep.record(2, "★★ 测试注册清单", False,
                   f"多了 {sorted(extra)} ⇒ 新增测试须同步更新 "
                   f"tools/ci_verify.py 的 EXPECTED_TESTS(这一步是**故意**"
                   f"要求人确认的,不是脚本没跟上)")
    else:
        rep.record(2, "★★ 测试注册清单", True,
                   f"{len(listed)} 条全部注册:{', '.join(sorted(listed))}")

    # ── §3 清洁构建 ────────────────────────────────────────────────
    #
    # ⚠️★ **必须清洁**:增量构建下「0 告警」是假证据 —— 没重新编译的 TU
    #    当然不会再报一次告警。这条是 2026-09-02 Windows 验证时踩出来的。
    rc, _ = run(["cmake", "--build", str(build), "--config", args.config,
                 "--clean-first", "--parallel"])
    rep.record(3, "清洁构建(SA_WERROR ⇒ 告警即失败)", rc == 0,
               "" if rc == 0 else "构建失败或存在告警")
    if rc != 0:
        return rep.summarize()

    # ── §4 ctest ──────────────────────────────────────────────────
    rc, out = run(["ctest", "--test-dir", str(build), "-C", args.config,
                   "--output-on-failure"])
    rep.record(4, "ctest", rc == 0,
               (re.search(r"\d+% tests passed.*", out) or [""])[0]
               if rc == 0 else "有测试未通过")

    # ── §5 ★ 断言防线反向验证 ──────────────────────────────────────
    if args.skip_negative:
        rep.record(5, "★ 断言防线反向验证", True, "按 --skip-negative 跳过")
    else:
        rep.record(5, *negative_check(build, args.config))

    return rep.summarize()


def negative_check(build, config):
    """★ 故意把相克矩阵改错一格,期望 contract_smoke **失败**。不失败才是问题。

    防的是 00 §10.4 那一族里最危险的一种:CMake 的 Release / RelWithDebInfo /
    MinSizeRel 都定义 NDEBUG,而 NDEBUG 把 `assert` **整个编译掉** ⇒ 测试照常
    链接、照常退出码 0、照常打印 OK,但一条都没验证。
    cmake/SaWarnings.cmake 的 sa_enable_assertions() 就是为此存在的,
    而**它自己没有任何东西守着**。

    ⚠️★ 探针必须打在 contract_smoke 上,不能打在黄金用例集上 ——
        这是 2026-09-02 首次执行 win_validate.ps1 时纠正的一处错误:
        黄金用例集用 doctest 的 CHECK,**它与 NDEBUG 无关**,不管 assert
        有没有被编译掉,改错矩阵它都会红 ⇒ 那样得到的"失败"证明不了
        sa_enable_assertions() 有效,反会把假阳性读成"断言防线成立"。
        ★★ 本节要防的错误,恰恰会发生在本节自己身上。

    ★ contract_smoke 的 CheckElementMatrix() 是**先 printf 再 assert**
      ⇒ 四种结果可分辨,不只看退出码。

    ⚠️★★ 还原不只是"把文件写回去"(2026-09-05 实测修正,00 §9.0.14):
        初版用 shutil.copy2 还原 ⇒ 备份的 **mtime 一并被还原** ⇒ 还原后的 constants.h
        比注入期间编出的 .o **更旧** ⇒ make / ninja 判定产物是新的、不再重编
        ⇒ sa_shared 里改坏的 battle.cpp.o 原样留在 build 目录里。
        而那句「还原后重建,避免留下改坏的产物」的 --target 只指了 sa_contract_smoke,
        对 rules_battle 链的那份 sa_shared 无能为力。
        ⇒ 下一个在同一目录做增量构建的人,会看到 rules_battle 报
          「相克 [攻0][守1] 得 9.9 应 1.5」—— 一条与他的改动毫无关系的红。
        ★ 这与 00 §9.0.12 / §9.0.13 是同一族:动作做了(还原),没有任何东西**观测**
          它的结果(产物干净了没)。⇒ 见 _restore_and_recheck():还原 → 重建整个目录
          → 复跑受影响的两条用例 → **断言它们回到绿**。
    """
    target = REPO / "shared" / "rules" / "constants.h"
    backup = target.with_suffix(".h.ci_verify_backup")
    orig = target.read_text(encoding="utf-8")

    # 打在 kElementMatrix 初始化列表里的**第一格 1.5**(攻地 / 守水)。
    # ★ 模式绑在标识符 `kElementMatrix` 上,不绑在中文注释上 —— 注释会被重排,代码不会。
    # ⚠️ 它是 double,不是 float:初版 win_validate.ps1 找 `1.5f` ⇒ 那一节从未真正执行过。
    pat = re.compile(r"(kElementMatrix.*?=\s*\{.*?)1\.5", re.S)
    if not pat.search(orig):
        return ("★ 断言防线反向验证", False,
                "未匹配到 kElementMatrix 的初始化列表 ⇒ 注入模式已过期,"
                "须同步修本脚本(**勿改测试去迁就脚本**)")

    # 备份文件仍然留着:脚本若被中途杀掉,工作树里要有一份能手工还原的原件。
    shutil.copy2(target, backup)
    try:
        verdict = _probe_with_bad_matrix(build, config, target,
                                         pat.sub(r"\g<1>9.9", orig, count=1))
    finally:
        restore_problem = _restore_and_recheck(build, config, target, backup)

    if restore_problem:
        # ⚠️ 即便注入那一半通过了,也要判失败:build 目录此刻是脏的,
        #    留给后面的人一个"看起来验过、实际被污染"的目录,比本项红更坏。
        return ("★ 断言防线反向验证", False, restore_problem)
    return verdict


def _probe_with_bad_matrix(build, config, target, bad_text):
    """注入改错的矩阵,只重建探针目标,读四态。(不负责还原 —— 见调用方的 finally。)"""
    target.write_text(bad_text, encoding="utf-8")
    rc_build, _ = run(["cmake", "--build", str(build), "--config", config,
                       "--target", "sa_contract_smoke"])
    if rc_build != 0:
        return ("★ 断言防线反向验证", False,
                "注入后**构建失败** ⇒ 本项不结论(是注入方式的问题,"
                "不是断言的问题)")

    # ⚠️ --timeout 60:MSVC 的 assert 走 _wassert,控制台程序理论上写 stderr 后
    #    abort,但别把整趟验证押在这个假设上 —— 万一弹窗,超时也算失败,不会挂死。
    rc_test, out = run(["ctest", "--test-dir", str(build), "-C", config,
                        "-R", "contract_smoke", "--output-on-failure",
                        "--timeout", "60"])
    saw_assert = bool(re.search(r"[Aa]ssertion", out))
    saw_printf = "相克矩阵重排错误" in out

    if rc_test != 0 and saw_assert:
        return ("★ 断言防线反向验证", True,
                "改错矩阵后 assert 确实触发并使测试失败 ⇒ sa_enable_assertions() 有效")
    if rc_test != 0:
        return ("★ 断言防线反向验证", True,
                "测试确实失败,但输出未见 assert 字样(可能被 abort 截断)"
                "⇒ 防线成立,证据偏弱")
    if saw_printf:
        return ("★ 断言防线反向验证", False,
                "⚠️★★ 已打印「相克矩阵重排错误」却仍然通过 ⇒ "
                "**assert 被 NDEBUG 编译掉了**,前面所有绿色都不可信!")
    return ("★ 断言防线反向验证", False,
            "⚠️★ 改错后仍通过、且连 printf 都没出现 ⇒ 注入没进到编译产物,本项不成立")


def _restore_and_recheck(build, config, target, backup):
    """还原源文件、重建**整个**目录、复跑受影响用例。返回问题描述;空串 = 干净。

    ★ 三步缺一不可,每步对应一种曾经真实发生或必然会发生的失效:
      ① write_bytes 而非 copy2 —— 让 mtime 是"现在",构建系统才看得见文件变了;
      ② 重建整个目录而非 --target sa_contract_smoke —— 注入期间被重编的是 sa_shared,
         它被 contract_smoke **和** rules_battle 两条链共享;
      ③ 复跑那两条并断言通过 —— 「还原了」是动作,「产物干净了」才是结论,
         中间要有一次观测(00 §9.0.13 的原则:观测值还要证明是对的)。
    """
    target.write_bytes(backup.read_bytes())
    backup.unlink()
    rc_build, _ = run(["cmake", "--build", str(build), "--config", config,
                       "--parallel"])
    if rc_build != 0:
        return ("⚠️★★ 注入已还原,但**重建失败** ⇒ build 目录状态不明,"
                "请删掉它重跑;在此之前它给出的任何绿色都不可信")
    rc_test, _ = run(["ctest", "--test-dir", str(build), "-C", config,
                      "-R", "contract_smoke|rules_battle", "--output-on-failure"])
    if rc_test != 0:
        return ("⚠️★★ 注入已还原并重建,但 contract_smoke / rules_battle **仍不通过**"
                " ⇒ 改错矩阵的产物残留在 build 目录里(还原没有让构建系统看见文件变了)"
                " ⇒ 请删掉 build 目录重跑,并检查本函数的还原方式")
    return ""


if __name__ == "__main__":
    sys.exit(main())
