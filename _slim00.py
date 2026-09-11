#!/usr/bin/env python3
"""第 4 步 —— 00-architecture.md 瘦身(一次性,用完即删)

删 §9.0.1–9.0.55 + §10.1–10.3 + §12;把四张阶段表上移到 §9.0 之后;插指针块。
★ 只做"删"与"移",不改写任何保留下来的行。
"""
import re
from pathlib import Path

P = Path("docs/00-architecture.md")
L = P.read_text(encoding="utf-8").split("\n")
HEAD = re.compile(r"^### (9\.0\.[0-9.]+|阶段 [0-9]) ")


def marks():
    out = []
    for i, l in enumerate(L):
        m = HEAD.match(l)
        if m:
            out.append((i, m.group(1)))
        elif l.startswith("## ") or l.startswith("### 10."):
            out.append((i, None))
    return out


M = marks()
span = {}
for n, (i, k) in enumerate(M):
    if k is None:
        continue
    span[k] = (i, M[n + 1][0] if n + 1 < len(M) else len(L))

# ① 摘出四张阶段表(保留,要上移)
stages = []
for s in ("阶段 0", "阶段 1", "阶段 2", "阶段 3"):
    a, b = span[s]
    stages.extend(L[a:b])
while stages and not stages[-1].strip():
    stages.pop()

# ② 要删除的行区间:所有 9.0.x 小节 + 四张阶段表(原位)
kill = set()
for k, (a, b) in span.items():
    kill.update(range(a, b))

# ③ §10.1–10.3 迁出(§10.4 保留)
i101 = next(i for i, l in enumerate(L) if l.startswith("### 10.1 "))
i104 = next(i for i, l in enumerate(L) if l.startswith("### 10.4 "))
kill.update(range(i101, i104))

# ④ §12 变更记录整节迁出
i12 = next(i for i, l in enumerate(L) if l.startswith("## 12. "))
kill.update(range(i12, len(L)))

# ⑤ 指针块:插在 §9.0 那一节的末尾(= 第一个 9.0.x 小节之前)
first = span["9.0.1"][0]
PTR = """### 9.0.x 逐批次执行记录 → 已迁至 `journal/`

> ★★ **原 §9.0.1–§9.0.55(4,050 行)已于 2026-09-11 整体迁入 [`journal/`](journal/)**,
> 按功能模块分 11 个文件。**编号一律沿用,搬家未改号** —— 全仓约 600 处 `§9.0.x` 引用继续有效。
>
> **查一条旧引用**(如 `00 §9.0.35`):到 [`journal/README.md`](journal/README.md)
> 的全量映射表按编号找文件,文件内小节标题仍是 `### 9.0.35 …`。

| 去处 | 收录 |
|---|---|
| [`journal/01-infra-bootstrap.md`](journal/01-infra-bootstrap.md) | 工程骨架 · 阶段 0/1 起步 · `shared/wire/` · 1.4 服务端侧 |
| [`journal/02-infra-d2-crosscompile.md`](journal/02-infra-d2-crosscompile.md) | D2 两端编译实证 · 跨编译器出清 · Windows 验证 · CI 覆盖 GCC/MSVC |
| [`journal/03-infra-ci-guards.md`](journal/03-infra-ci-guards.md) | CI 挂载 · 五个守卫脚本的立案与它们抓到的真问题 |
| [`journal/04-infra-naming.md`](journal/04-infra-naming.md) | 项目定位澄清 · SA/SG 前缀 · P0–P6 命名改造 |
| [`journal/10-battle-core.md`](journal/10-battle-core.md) | L3 战斗:批次 0.5 调度 · A.1 逃跑 · A.2 捕获 · A.3 暴击 · A.4 打飞 |
| [`journal/11-model-attr.md`](journal/11-model-attr.md) | L2 实体池与实体族 · M.1–M.4b 属性推导与四维公式 · 换宠 |
| [`journal/12-encounter.md`](journal/12-encounter.md) | M.5 敌人表 · R.1 随机源 · M.6/M.7 遇敌链 · 战果结算 |
| [`journal/13-world.md`](journal/13-world.md) | W.1 移动视野 · W.2/W.3 刷怪游荡 · W.4 暗雷 · W.5 明雷 |
| [`journal/14-item.md`](journal/14-item.md) | 道具域 I.1 背包 · I.2 扣道具 · I.3 掉落 · I.4 使用 |
| [`journal/15-status.md`](journal/15-status.md) | L4.1 状态异常系统 |
| [`journal/90-push-windows.md`](journal/90-push-windows.md) | 九次 `shared-v0.x.0` 推送窗口的闭合核实 |

⚠️ **新批次的执行记录写进 `journal/` 对应模块文件,不再追加到本文。**
编号继续用 `§9.0.N`(N 接续),并在 [`journal/README.md`](journal/README.md) 的映射表补一行 ——
★ `tools/check_docs_index.py`(ctest 项 `docs_index`)会检查编号唯一、连续、且映射表覆盖。

---

""".split("\n")

# ⑥ 组装(★ 四张阶段表按原样放在指针块之前 —— 计划在前、已做的记录指针在后;
#    不加任何杜撰的新标题,它们本就是 ### 级小节)
out = []
for i, l in enumerate(L):
    if i == first:
        out.extend(stages)
        out.append("")
        out.append("---")
        out.append("")
        out.extend(PTR)
    if i in kill:
        continue
    out.append(l)

# ⑦ §10.1–10.3 与 §12 的位置补指针
txt = "\n".join(out)
txt = txt.replace(
    "### 10.4 ",
    """### 10.1–10.3 → 已迁至 `backlog/`

| 原节 | 去处 |
|---|---|
| §10.1 R-b:无解的结构性事实 · §10.2 六项永久不可判定 · §10.3 实现期待补清单 | [`backlog/04-undecidable.md`](backlog/04-undecidable.md) |

★ §10.4 留在本文 —— 它是**架构级告诫**,不是遗留项。

### 10.4 """,
    1,
)
txt = txt.rstrip("\n") + """

---

## 12. 变更记录 → 已迁至 `journal/99-changelog.md`

> ★ 原 §12 的逐条变更记录已迁入 [`journal/99-changelog.md`](journal/99-changelog.md)。
> ⚠️ **新批次仍须补一行变更记录**,写到那里。
"""

# 收敛连续空行(≥3 → 2)
txt = re.sub(r"\n{4,}", "\n\n\n", txt)
P.write_text(txt + "\n", encoding="utf-8")
print(f"00-architecture.md: {len(L)} → {len(txt.split(chr(10)))} 行")
