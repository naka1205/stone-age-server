#!/usr/bin/env python3
"""tools/check_dr_table.py —— `11-decision-register.md` 可数性的执行者

★★ 这条检查存在的理由,和 `check_format.py` 一模一样:**它此前不存在。**

`11` §14 的口径明写:

    要问"还有几条没定",**数 §1–§12 里的 ⚠️ 与 ⏳ 标记**,别数总数。

⇒ 这句话把「待拍板 0 条」这个**被反复引用的结论**架在一个前提上:
  **每条 DR 都在 §1–§12 的主表里有一行。**
⚠️ 而那个前提从来没有执行者。实测(2026-09-09,批次 M.5,`00` §9.0.35 ⑦)三条 DR 只有展开小节、
   主表无行:`DR-DT11` · `DR-BT22`(均 M.4b 新增)· `DR-DT12`(M.5 新增)。
   ⇒ M.4b 变更记录写的「119 → 121 行」与实测 **120 行**不符,
     ★ **这是这个数第三次记错**(前两次见 §14 计数口径 ①②)。

★★ **后果不是"格式不整齐"**:若某条新 DR 是 ⚠️ 待拍板却只写在小节里,
   那么「待拍板 0 条」**会是错的,而且不会有任何东西发现** ——
   开工判断("没有决策挡住开工")正是建立在那个数上的。

★ 与 `shared_purity` / `module_boundaries` / `code_format` 同族:
  它守的不是好看,是**一个已宣告完成的状态还成不成立**;
  而本条守的对象比它们特殊 —— 是**文档自身的可数性**。

⇒ 判据(两条,都只用文档自身,不需要外部工具):
   ① 凡有 `### N.N DR-XXn …` 展开小节的编号,**必须**在 §1–§12 的某个表格行首出现;
   ② §1–§12 的表格行首编号**不得重复**(一行一个唯一编号,§14 的「只数行」口径)。

⚠️ 本脚本**有意不检查**「§14 里的计数文字是否等于实测行数」:
   §14 已明确保留「96 条」作为**历史延续值且不再作为判断依据**
   ⇒ 把它钉成断言会让一条已声明作废的数字重新变成硬约束。
"""

import re
import sys
from pathlib import Path

# ★ Windows 控制台的 cp936 / cp1252 编不出 ✅ ★ ⚠️ 这些字符(00 §9.0.12:
#   check_module_boundaries.py 漏了这一段 ⇒ 脚本**通过时也退出码 1**,
#   而 CI 会把那读成「这道检查失败了」)。
#   ⚠️ ci_verify.py §0 会断言本文件有这一段,别删。
#   ★ 那条断言在本文件身上当场兑现了一次 —— 它立案时写的正是
#     「惯例存在、理由写清楚了,新加的第四个脚本照样漏」,而本文件是第五个。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):  # 被重定向到不支持 reconfigure 的对象
        pass

DOC = Path(__file__).resolve().parent.parent / "docs" / "11-decision-register.md"

# §1–§12 的边界。★ 用标题文本而不是行号 —— 行号会随每次编辑漂移,
#   而"§1 开始 / §13 开始"这两个锚点是结构性的。
BEGIN = "\n## 1. "
END = "\n## 13."

ROW_RE = re.compile(r"^\|\s*\*{0,2}(DR-[A-Z]+\d+)", re.M)
SUB_RE = re.compile(r"^###\s+[\d.]+\s+(DR-[A-Z]+\d+)", re.M)


def main() -> int:
    if not DOC.exists():
        print(f"❌ 找不到 {DOC}", file=sys.stderr)
        return 2
    text = DOC.read_text(encoding="utf-8")

    try:
        body = text[text.index(BEGIN) : text.index(END)]
    except ValueError:
        # ⚠️ 锚点没了就必须硬失败,不能"跳过检查" ——
        #    静默跳过等于把这条守卫悄悄关掉(00 §10.4 的第三类)。
        print(
            "❌ 定位不到 §1–§12 的边界锚点。\n"
            f"   需要文档里同时存在 {BEGIN.strip()!r} 与 {END.strip()!r} 两个标题。\n"
            "   ⚠️ 改了章节标题就要同步改本脚本 —— 不要让它变成一条永远通过的检查。",
            file=sys.stderr,
        )
        return 2

    rows = ROW_RE.findall(body)
    subs = SUB_RE.findall(text)

    failures = []

    # ── 判据 ①:有小节必须有主表行 ──────────────────────────────────
    orphans = sorted(set(subs) - set(rows))
    if orphans:
        failures.append(
            "有展开小节但 §1–§12 主表里没有对应行:\n"
            + "".join(f"    · {d}\n" for d in orphans)
            + "  ⇒ §14 的口径是「数 §1–§12 里的 ⚠️ 与 ⏳ 标记」,\n"
            "    不进主表的 DR **数不到** ⇒「待拍板 N 条」这个结论会失去凭据。\n"
            "  修:在对应章节的主表末尾补一行(裁定 + 依据指向那个小节)。"
        )

    # ── 判据 ②:主表行首编号不得重复 ────────────────────────────────
    dups = sorted({d for d in rows if rows.count(d) > 1})
    if dups:
        failures.append(
            "§1–§12 主表里有重复的 DR 编号:\n"
            + "".join(f"    · {d}(出现 {rows.count(d)} 次)\n" for d in dups)
            + "  ⇒ 违反 §14「只维护『行』这一个可数口径」——\n"
            "    重复行会让行数与决策数分叉,而那正是此前记错三次的根因。"
        )

    if failures:
        print("❌★★ DR 登记表可数性检查失败\n")
        for f in failures:
            print("  " + f + "\n")
        print(
            f"  实测:§1–§12 主表 {len(rows)} 行 / 唯一编号 {len(set(rows))} 个 · "
            f"有展开小节的编号 {len(set(subs))} 个"
        )
        return 1

    print(
        f"✅ DR 登记表可数性:§1–§12 主表 {len(rows)} 行(编号唯一)· "
        f"{len(set(subs))} 个展开小节全部在表内"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
