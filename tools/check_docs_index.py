#!/usr/bin/env python3
"""tools/check_docs_index.py —— `docs/` 三分类与导航的可追溯性的执行者

★★ 这条检查存在的理由,和 `check_dr_table.py` 一模一样:**搬家之后没有执行者。**

2026-09-11 的文档重构把 `00-architecture.md` 的 §9.0.1–9.0.55(4,050 行 / 占全文 86%)
整体迁入 `docs/journal/`,并建了 `backlog/` 与 `deviations/` 两个派生视图。
那次重构立了一条**被反复引用的前提**:

    §9.0.x 编号一律沿用,搬家未改号 ⇒ 全仓约 600 处旧引用继续有效。

⇒ 这句话架在三个前提上:
   ① 每个编号在 `journal/` 下**恰好定义一次**(重复 ⇒ 引用指向二义;缺失 ⇒ 引用悬空);
   ② `journal/README.md` 的映射表**覆盖全部编号**(否则"按编号查文件"这条路断掉);
   ③ 瘦身后的 `00`/`01`/`11` 留下的**指针指向真实存在的文件**。

⚠️ 而这三条都没有执行者。**后果不是"导航不好看"**:
   · 若某节搬家时漏掉,那 601 处引用里指向它的那些会**静默悬空** ——
     读者只会看到"查不到",而不会知道它本来应该存在;
   · 若某节被复制到两个文件,两份会各自被后续批次追加、**无声分叉**
     (同 `00` §10.4 的第一类:不报错、不崩溃、内容烂掉);
   · 而 `ctest` 全绿,因为**文档不参与编译**。

★ 与 `shared_purity` / `module_boundaries` / `code_format` / `dr_table` 同族:
  它守的不是好看,是**一个已宣告完成的状态还成不成立**。
  ★ 本条守的对象与 `dr_table` 最近 —— 都是**文档自身的可追溯性**。

⇒ 判据(四条,都只读仓内文档,不需要任何外部工具):
   ① `journal/` 下 `### 9.0.N` 小节的编号**无重复**;
   ② 编号集合**连续覆盖** §9.0.1 … §9.0.<最大值>(允许 `N.M` 形式的子节如 9.0.7.1),
      ⚠️ 其中**有意缺失**的编号须在 KNOWN_GAPS 里显式登记 —— 逼迫"缺一个"变成一次决定;
   ③ `journal/README.md` 映射表覆盖全部编号,且指向的文件真实存在;
   ④ `00`/`01`/`11` 与各 README 里形如 `journal/x.md` 的指针全部指向真实文件。

⚠️ 本脚本**有意不检查**「journal 正文与 `00` 原文逐字一致」:
   搬家完成后 `00` 已不再持有那些正文 ⇒ 没有可比对的基准。
   那一关是**一次性的**,由重构当时的 `git diff` 逐行回查完成(见 `00` §9.0.56),
   不是常驻检查 —— 把它写成断言会让它永远通过而显得有保障。
"""

import re
import sys
from pathlib import Path

# ★ Windows 控制台的 cp936 / cp1252 编不出 ✅ ★ ⚠️ 这些字符(00 §9.0.12:
#   check_module_boundaries.py 漏了这一段 ⇒ 脚本**通过时也退出码 1**,
#   而 CI 会把那读成「这道检查失败了」)。
#   ⚠️ ci_verify.py §0 会断言本文件有这一段,别删。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):  # 被重定向到不支持 reconfigure 的对象
        pass

DOCS = Path(__file__).resolve().parent.parent / "docs"
JOURNAL = DOCS / "journal"

# ⚠️★ 有意缺失的编号:每一条都必须写明为什么。
#    ★ 这个字典的作用是**让"缺一个"变成一次决定**,而不是一次沉默 ——
#      空着它,漏搬一节和有意跳号就完全无法区分。
KNOWN_GAPS = {
    # 编号: 理由
}

# ⚠️★ 编号可带**字母后缀**(§9.0.69b / §9.0.69c —— 同一批次的补记)。
#   首版式样是 `9\.0\.[0-9.]+ `(编号后紧跟空格)⇒ 字母后缀**一个都匹配不上**,
#   于是 §9.0.69b/§9.0.69c 在 owner 里不存在 —— 而映射表那一侧的式样同样匹配不上,
#   两边**同时瞎**,`owner - listed` 与 `listed - owner` 都是空集 ⇒ 判据③一路绿。
#   ⇒ 这是"两个错误互相抵消"的典型:单看任何一条断言都通过,而它守的东西已经漏了。
#   (2026-09-18 修;当次实测旧式样 70 个编号 / 新式样 72 个 —— 差的正是两个字母后缀。)
SEC_RE = re.compile(r"^### (9\.0\.[0-9]+(?:\.[0-9]+)*[a-z]?) ", re.M)
# 指针:形如 `journal/xx.md` / `backlog/xx.md` / `deviations/xx.md`(可带 ../ 前缀)
PTR_RE = re.compile(r"(?:\.\./)?(journal|backlog|deviations)/([0-9A-Za-z_.-]+\.md)")

# ── ★★ 判据⑤(2026-09-18 补)的式样:per-file 表的「节数」列 ──────────────
#
# ⚠️★ 为什么补这一条:该列是**手写的**,而判据③只认「全量映射表」那张按编号的表
#   ⇒ 「节数」列**从来没有任何东西检查**。实测它已经烂了:
#      · `90-push-windows.md` 列写 10,实际 15(五个窗口没算进去);
#      · `10-battle-core.md` 写 6,实际 7;`14-item.md` 写 4,实际 5;
#      · `18-petskill.md` **整行都不存在**(3 节没登记)。
#   ★ 烂法值得记:每次窗口都有人往那个文件里加一节,而**加的人不会去看这张表** ——
#     于是它单调落后,且因为没有任何断言而永不报错。与本日 §9.0.69c 记的
#     「注释里复述每次都会变的值」是同一族:**手抄的派生数据 = 注定要烂的第二真源**。
#   ⇒ 处置不是"记得改",是让它**可被推导**并被断言:下面从各文件真实标题数出节数,
#     与表里写的比。数字不再靠人维护。
FILE_ROW_RE = re.compile(
    r"^\|\s*\[`?([0-9A-Za-z_.-]+\.md)`?\]\([^)]*\)\s*\|[^|]*\|\s*(\d+)\s*\|\s*$", re.M)
TOTAL_ROW_RE = re.compile(r"^\|\s*\*\*合计\*\*\s*\|[^|]*\|\s*\*\*(\d+)\*\*\s*\|\s*$", re.M)


def base_num(num: str) -> str:
    """§9.0.69b → 9.0.69:字母后缀是**同一编号的补记**,不参与连续性判据。

    ⚠️ 必要性:判据②把编号按 `.` 切开取 int ⇒ `int("69b")` 直接抛 ValueError。
       加了字母后缀式样而不加这个,脚本会**崩溃**而不是报红 —— 那比漏报更坏。
    """
    m = re.match(r"(9\.0\.[0-9]+(?:\.[0-9]+)*)[a-z]?$", num)
    return m.group(1) if m else num


def sort_key(num: str):
    # ★ 排序键要能容忍字母后缀(错误信息里要按序打印编号)。
    return tuple(int(x) for x in base_num(num).split("."))


def main() -> int:
    if not JOURNAL.is_dir():
        print(f"❌ 找不到 {JOURNAL}", file=sys.stderr)
        return 2

    failures = []

    # ── 判据 ①:编号无重复 ────────────────────────────────────────
    owner: dict[str, list[str]] = {}
    for f in sorted(JOURNAL.glob("*.md")):
        if f.name == "README.md":
            continue
        for num in SEC_RE.findall(f.read_text(encoding="utf-8")):
            owner.setdefault(num, []).append(f.name)

    dups = {n: fs for n, fs in owner.items() if len(fs) > 1}
    if dups:
        failures.append(
            "同一编号在多个文件里定义:\n"
            + "".join(f"    · §{n} ← {fs}\n" for n, fs in sorted(dups.items()))
            + "  ⇒ 两份会各自被后续批次追加、**无声分叉**(00 §10.4 第一类)。\n"
            "  修:只保留一处;跨模块的批次择其主模块,另一处改成一行指针。"
        )

    if not owner:
        print("❌ journal/ 下没有任何 §9.0.x 小节", file=sys.stderr)
        return 2

    # ── 判据 ②:编号连续覆盖 ──────────────────────────────────────
    #
    # ⚠️★ 用 base_num 取主编号:§9.0.69b 与 §9.0.69 是**同一编号**,不该各占一格。
    tops = sorted({int(base_num(n).split(".")[2]) for n in owner}, key=int)
    gaps = [i for i in range(1, max(tops) + 1) if i not in tops]
    undeclared = [f"9.0.{i}" for i in gaps if f"9.0.{i}" not in KNOWN_GAPS]
    if undeclared:
        failures.append(
            f"编号不连续,且这些缺口没有在 KNOWN_GAPS 里登记:{undeclared}\n"
            "  ⇒ 全仓约 600 处 `§9.0.x` 引用里指向它的那些会**静默悬空** ——\n"
            "    读者只看到「查不到」,不会知道它本来应该存在。\n"
            "  修:漏搬的补搬;有意跳号的写进本脚本的 KNOWN_GAPS 并注明理由。"
        )

    # ── 判据 ③:README 映射表覆盖全部编号 ─────────────────────────
    readme = JOURNAL / "README.md"
    if not readme.exists():
        failures.append("缺 journal/README.md —— 「按编号查文件」这条路整个断掉。")
    else:
        rtext = readme.read_text(encoding="utf-8")
        # ⚠️★ 这一侧的式样必须与 SEC_RE **同步放宽**,否则字母后缀两边一起瞎 ⇒
        #   两个错误互相抵消,判据③恒绿(2026-09-18 实测:差集两边都是空集)。
        listed = set(re.findall(r"\|\s*§(9\.0\.[0-9]+(?:\.[0-9]+)*[a-z]?)\s*\|", rtext))
        miss = sorted(set(owner) - listed, key=sort_key)
        if miss:
            failures.append(
                f"映射表漏了 {len(miss)} 个编号:{['§' + m for m in miss[:12]]}"
                f"{' …' if len(miss) > 12 else ''}\n"
                "  ⇒ 旧引用按编号查不到文件。修:重跑生成或手工补行。"
            )
        ghost = sorted(listed - set(owner), key=sort_key)
        if ghost:
            failures.append(
                f"映射表列了但 journal/ 下不存在的编号:{['§' + g for g in ghost]}\n"
                "  ⇒ 导航指向空气,比漏登记更坏(读者会以为自己看漏了)。"
            )

        # ── 判据 ⑤:per-file 表的「节数」列与真实标题数一致 ──────────
        #
        # ★ 判据③守的是「按编号查得到文件」,守不到这张表 —— 它是**另一张表**,
        #   列的是每个文件收了几节,而该列纯手写、此前无任何断言(理由见 FILE_ROW_RE 上方)。
        #   ⚠️ 只对表里**已列出的行**比对;表里整个漏掉一个文件(如曾经的 18-petskill.md)
        #     由下面的"表里没列出的 journal 文件"一并报出。
        real_counts: dict[str, int] = {}
        for f in sorted(JOURNAL.glob("*.md")):
            if f.name == "README.md":
                continue
            real_counts[f.name] = len(SEC_RE.findall(f.read_text(encoding="utf-8")))
        bad_rows, unlisted = [], []
        for name, claimed in FILE_ROW_RE.findall(rtext):
            if name not in real_counts:
                continue  # 指向非 journal 文件的行(本表内不该有,判据④另管指针)
            if int(claimed) != real_counts[name]:
                bad_rows.append(f"    · {name}: 表里写 {claimed},实际 {real_counts[name]}\n")
        for name, n in sorted(real_counts.items()):
            if not re.search(r"\[`?" + re.escape(name) + r"`?\]", rtext):
                unlisted.append(f"    · {name}(实际 {n} 节)\n")
        if bad_rows or unlisted:
            msg = "per-file 表的「节数」列与真实标题数不符:\n" + "".join(bad_rows)
            if unlisted:
                msg += "  这些文件在表里**整行缺失**:\n" + "".join(unlisted)
            msg += (
                "  ⇒ 该列是手写的派生数据,而派生数据**注定要烂** ——\n"
                "    加节的人不会来看这张表,而此前没有任何断言会因此报错。\n"
                "  修:把表里的数字改成上面报出的实际值(别再靠人记)。"
            )
            failures.append(msg)
        else:
            tm = TOTAL_ROW_RE.search(rtext)
            total_real = sum(real_counts.values())
            if tm and int(tm.group(1)) != total_real:
                failures.append(
                    f"per-file 表的「合计」写 {tm.group(1)},实际 {total_real}\n"
                    "  ⇒ 同「节数」列:手写的派生数据。修:改成实际值。"
                )

    # ── 判据 ④:目录指针全部有效 ──────────────────────────────────
    scan = [DOCS / "README.md", DOCS / "00-architecture.md",
            DOCS / "01-server-architecture.md", DOCS / "11-decision-register.md"]
    scan += [p for d in ("journal", "backlog", "deviations")
             for p in sorted((DOCS / d).glob("*.md"))]
    broken = {}
    for f in scan:
        if not f.exists():
            continue
        for sub, name in PTR_RE.findall(f.read_text(encoding="utf-8")):
            if not (DOCS / sub / name).exists():
                broken.setdefault(f"{sub}/{name}", []).append(f.name)
    if broken:
        failures.append(
            "指针指向不存在的文件:\n"
            + "".join(f"    · {t} ← 被 {srcs} 引用\n" for t, srcs in sorted(broken.items()))
        )

    if failures:
        print("❌★★ docs 三分类与导航的可追溯性检查失败\n")
        for f in failures:
            print("  " + f + "\n")
        print(f"  实测:journal/ {len(owner)} 个编号 / {len({f for fs in owner.values() for f in fs})} 个文件")
        return 1

    print(
        f"✅ docs 可追溯性:journal/ {len(owner)} 个编号(§9.0.1–9.0.{max(tops)},"
        f"{len({f for fs in owner.values() for f in fs})} 个文件)编号唯一且连续 · "
        f"映射表全覆盖 · 目录指针全部有效"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
