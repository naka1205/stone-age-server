#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
rescope_battle_port.py —— 0.1 四步改造的规模重算与批次切分

═══════════════════════════════════════════════════════════════════════════════
 为什么有这个脚本
═══════════════════════════════════════════════════════════════════════════════

`00-architecture.md` §9.0.1 / §1.3 与 `01-server-architecture.md` §13 欠债 1
三处都写着同一句话:

    「`07` §0.5 的四步改造估时是在 **26 个函数**上数出来的,
      至今没有按 §1.3 的实测量级重算 ⇒ **在排期之前那个数字不可用**。」

★ 但复核 `07` §0.5 / §11.4 后发现:**那里从来没有给过估时**,只给了「触及点数」。
  且 `stoneage-plan/00` §11.1 明写「人日绝对值无实测支撑,本轮取证由 AI 辅助完成,
  与人工基线不可直接换算」。

⇒ 本脚本**不产出人日**。它产出的是排期真正缺的那样东西:

    ① 1,047 处写里,**有多少在新架构下根本不产生 L3 移植工作**;
    ② 剩下的净规模按**可独立交付的批次**怎么分;
    ③ 1.4 demo 所需的最小批次(普攻链路)的确切边界。

`00` §11.1 自己给的方法论结论正是这条:
「先写『这个阶段结束时要能回答哪些问题』,再倒推任务,而不是先列任务再希望它们够用。」

═══════════════════════════════════════════════════════════════════════════════
 ★★ 两个改变排期的实测发现(本脚本量化它们)
═══════════════════════════════════════════════════════════════════════════════

`00` §1.3 把第②步的 1,047 处写解读为「**每个技能实现函数都自己写状态**」。
回源码核对后,该解读**部分失真**,且失真方向是**高估**:

★ 发现 1 —— **指令登记不是世界状态写。**

    `profession_common_fun` / `PETSKILL_*` 里大量的写是这个形状:

        CHAR_setWorkInt(charaindex, CHAR_WORKBATTLECOM1, com1);
        CHAR_setWorkInt(charaindex, CHAR_WORKBATTLECOM2, toNo);
        CHAR_setWorkInt(charaindex, CHAR_WORKBATTLEMODE, BATTLE_CHARMODE_C_OK);

    这是「**把本回合要执行的指令记在角色身上**」—— 原版用角色的 WORK 槽当
    指令传递通道。在新契约里它就是 `TurnCommands` / `domain::BattleCommand`,
    是 `ResolveTurn` 的**输入**,不是它的副作用。
    ⇒ 这批写**不产生第②步的移植工作**,它们塌缩成一次结构体赋值。

★ 发现 2 —— **62/64 个职业技能函数是同构薄包装,不是 64 份实现。**

        int PROFESSION_ice_crack(...) {
            if (toNo > 19) toNo = 0;
            profession_common_fun(charaindex, toNo, skill_level, array,
                                  BATTLE_COM_S_ICE_CRACK);
            return TRUE;
        }

    62 个函数逐字同构,只有 `BATTLE_COM_S_*` 常量不同,唯一实体是
    `profession_common_fun`(28 行)。真正的结算在 `BATTLE_COM_S_*` 的
    分发分支里(A 批次),不在这 64 个函数里。
    ⇒ 它们在新实现里是**一张 62 行的映射表**,不是 62 份移植。

⚠️ **这两条都不改变 D2 的裁定**(那由 `00` §1.1 的判据决定,已关闭),
   只改变**排期**。方向是**减负**,但减的是「份数」不是「难度」——
   A 批次(指令分发分支)的净规模一点没少,它才是真正的大头。

═══════════════════════════════════════════════════════════════════════════════
 口径
═══════════════════════════════════════════════════════════════════════════════

- 输入:`stoneage-plan/tools/battle_purity_full.json`(阶段 0.0 产物,340 函数)
        + 复用 `battle_purity_full.py` 的函数体索引回源码做二次分类。
- **不新增判据、不改门槛** —— 本脚本不做可行性判定,那是阶段 0.0 的事,已完成。
- 批次 0 的边界取 W1 的 `Q4` 逐函数名单(普攻链路),保证与 `05` §1.2 的口径一致。

用法:
  python3 tools/rescope_battle_port.py            # 写 rescope_battle_port.json
  python3 tools/rescope_battle_port.py --summary  # 只打摘要
"""

import json
import os
import re
import sys
from collections import Counter, defaultdict

# ★★ 输出编码不是风格问题,是**退出码正确性**问题(与 check_shared_purity.py
#   卷首同一条):简中 Windows 的 cp936 控制台遇到 ✅ / ★ / ⇒ 会抛
#   UnicodeEncodeError,于是脚本**通过时也退出码 1**。
# ⚠️ 本脚本是一次性分析工具,不在 CI 上跑 —— 这一行仍然加,因为
#   ci_verify.py §0 的判据故意不带「哪些脚本在 CI 上跑」这种例外清单:
#   例外清单本身就是下一个「各自记得」(00 §9.0.12)。
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):   # 被重定向到不支持 reconfigure 的对象
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
PLAN_TOOLS = os.path.normpath(os.path.join(REPO, "..", "stoneage-plan", "tools"))

if not os.path.isdir(PLAN_TOOLS):
    sys.exit(f"找不到证据仓工具目录:{PLAN_TOOLS}\n"
             "本脚本消费 stoneage-plan 的阶段 0.0 产物,须与本仓平级检出。")

sys.path.insert(0, PLAN_TOOLS)

import battle_purity_full as BP          # noqa: E402
from battle_string_scan import BUF_PAT   # noqa: E402

FULL_JSON = os.path.join(PLAN_TOOLS, "battle_purity_full.json")
W1_JSON = os.path.join(PLAN_TOOLS, "battle_strings.json")
OUT = os.path.join(HERE, "rescope_battle_port.json")

# ═══════════════════════════════════════════════════════════════════════════
#  1. 写点分类 —— 指令登记 vs 真世界状态
# ═══════════════════════════════════════════════════════════════════════════
#
# ★ 判据:写的**目标槽位**是不是「本回合指令」的载体。
#   原版没有 TurnCommands 这个类型,它把指令暂存在角色的 WORK 槽里,
#   靠 BATTLE_Battling 稍后读回来分发。⇒ 这是**传参**,不是状态变更。
#
# ⚠️ 口径故意取窄:只列**确定**是指令通道的槽位。
#    像 CHAR_WORKBATTLEINDEX(战场归属)、CHAR_WORKBATTLEFLG(战斗标志)
#    虽也带 WORKBATTLE 前缀,但它们是真状态 ⇒ **不列入**。
#    宁可把指令槽算成状态写(高估工作量),不可反过来。

COMMAND_SLOTS = {
    # BATTLE_Battling 读回来做指令分发的三个槽 + 模式位
    "CHAR_WORKBATTLECOM1",      # 指令码(BATTLE_COM_*)
    "CHAR_WORKBATTLECOM2",      # 目标位次
    "CHAR_WORKBATTLECOM3",      # 高低位打包 skill_level / array
    "CHAR_WORKBATTLEMODE",      # 该角色本回合是否已下达指令
    # _PROFESSION_ADDSKILL 的集气延迟通道(profession_common_fun 内)
    "CHAR_WORK_com1",
    "CHAR_WORK_toNo",
    "CHAR_WORK_mode",
    "CHAR_WORK_skill_level",
    "CHAR_WORK_array",
}

# 与 battle_purity_full.WRITE_WIDE_PAT 同源。★ 必须保持一致,
# 否则本脚本的「1,047」与阶段 0.0 的「1,047」对不上,分类就失去意义。
WRITE_WIDE_CALL = re.compile(
    r"\b((?:CHAR|ITEM|PET|PETSKILL|MAGIC|PROFESSION|MAP|NPC)_\w*"
    r"(?:[sS]et|[dD]el|[aA]dd|[sS]ub|[rR]emove|[cC]hange|[cC]lear|[rR]eset"
    r"|[uU]pdate|[wW]rite|[mM]ove|[cC]reate|[dD]estroy|[eE]nd)\w*)\s*\(")


def call_args(text, lparen):
    """取 text[lparen] 处 '(' 的顶层实参列表。括号/引号不平衡时返回已解析的部分。"""
    depth, cur, out = 0, "", []
    i = lparen
    while i < len(text):
        ch = text[i]
        if ch == '"':                       # 跳过字符串字面量
            j = i + 1
            while j < len(text) and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            cur += text[i:j + 1]
            i = j + 1
            continue
        if ch == "(":
            depth += 1
            if depth > 1:
                cur += ch
        elif ch == ")":
            depth -= 1
            if depth == 0:
                out.append(cur.strip())
                return out
            cur += ch
        elif ch == "," and depth == 1:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
        i += 1
    out.append(cur.strip())
    return out


def classify_writes(clean):
    """把一个函数体内的宽口径写点分成「指令登记」与「真状态写」。"""
    cmd, state, slots = 0, 0, Counter()
    for m in WRITE_WIDE_CALL.finditer(clean):
        args = call_args(clean, m.end() - 1)
        slot = args[1] if len(args) > 1 else ""
        slot = slot.split("[")[0].strip()
        if slot in COMMAND_SLOTS:
            cmd += 1
            slots[slot] += 1
        else:
            state += 1
    return cmd, state, slots


# ═══════════════════════════════════════════════════════════════════════════
#  2. 塌缩型函数识别 —— 薄包装 与 空壳
# ═══════════════════════════════════════════════════════════════════════════
#
# 两类函数在新实现里都**不产生逐行移植工作**,但理由不同,必须分开记:
#
# ★ 类 1「同构薄包装」—— 塌缩为**映射表的一行**。判据四条同时成立:
#   ① 函数体去注释后 ≤ THIN_MAX_LINES 行;
#   ② 体内只调用 1 个函数,且该函数**在 battle/ 内有定义**
#      (即它是分派器,不是 CHAR_setWorkInt 这类跨模块访问器);
#   ③ 该分派器被 ≥ SHARED_MIN 个函数共享(**同构**的实质:一群函数共用一个实体);
#   ④ 不含随机点、不含串缓冲写入。
#
#   ⚠️ 判据 ②③ 是 v2 收紧的。首跑时只要求「唯一调用」,于是把 12 个
#      「函数体只有一句 CHAR_setWorkInt」的**真实现**误判成了薄包装 ——
#      那些是短,不是同构,仍要逐个移植。
#
# ★ 类 2「空壳」—— 塌缩为**零**。函数体只有 return,没有任何调用。
#   实测 9 个职业技能是 `{ return TRUE; }`(自给自足 / 双刀 / 火焰修炼 / 满 MP …)
#   ⇒ 指令入口存在但不做事,效果若有则在别处。**不是待补,是原版就没有。**

THIN_MAX_LINES = 12
SHARED_MIN = 3
CALL_ANY = re.compile(r"\b([A-Za-z_]\w*)\s*\(")


def body_calls(defn):
    """函数体内的调用名(去掉 C 关键字与自递归)。"""
    return [c for c in CALL_ANY.findall(defn["clean"])
            if c not in BP.C_KEYWORDS and c != defn.get("name")]


def is_empty_shell(defn):
    """空壳:零调用、零串缓冲。"""
    clean = defn["clean"]
    if BUF_PAT.search(clean):
        return False
    if body_calls(defn):
        return False
    stmts = [l.strip() for l in clean.split("\n") if l.strip()]
    # 只剩签名 / 花括号 / return
    return all(s in "{}" or s.startswith("return") or s.endswith("(")
               or "(" in s and s.endswith(")") for s in stmts) and len(stmts) <= 6


def thin_wrapper_target(defn, index):
    """若是同构薄包装,返回它包装的分派器名;否则 None。判据见本节卷首。"""
    clean = defn["clean"]
    if len([l for l in clean.split("\n") if l.strip()]) > THIN_MAX_LINES:
        return None
    if BUF_PAT.search(clean):
        return None
    uniq = sorted(set(body_calls(defn)))
    if len(uniq) != 1:
        return None
    target = uniq[0]
    if target not in index:          # ② 必须是 battle/ 内的分派器
        return None
    return target


# ═══════════════════════════════════════════════════════════════════════════
#  3. 批次划分
# ═══════════════════════════════════════════════════════════════════════════
#
# 批次 0 = W1 的普攻链路 —— **1.4 demo 的最小切面**。
#   `02` §10 欠债 5 已证现有 IDL 的 `Hit` 字段「够 MELEE 链路(即 1.4 demo)用」。
# 其余按阶段 0.0 的入口角色分批,因为那正是它们在原版里的自然边界。

BATCH_ORDER = ["0 普攻链路(demo)", "A 指令分发分支", "B 宠技表",
               "C 职技表", "D 闭包内被调"]

METRICS = ["函数数", "行数", "写-指令登记", "写-真状态", "写-合计",
           "串缓冲", "随机点", "g* 引用", "I/O(玩法+审计)"]


def batch_of(row, w1_names):
    if row["函数"] in w1_names:
        return BATCH_ORDER[0]
    roles = row.get("入口角色") or []
    kinds = {r.split(":")[0] for r in roles}
    if kinds == {"A"}:
        return BATCH_ORDER[1]
    if kinds == {"B"}:
        return BATCH_ORDER[2]
    if kinds == {"C"}:
        return BATCH_ORDER[3]
    if not roles:
        return BATCH_ORDER[4]
    return BATCH_ORDER[1]        # 多重角色(A 与技能表都能到达)归 A,不重复计


def main():
    summary_only = "--summary" in sys.argv

    full = json.load(open(FULL_JSON, encoding="utf-8"))
    rows = full["逐函数度量"]
    w1 = json.load(open(W1_JSON, encoding="utf-8"))
    w1_names = {f["函数"] for f in w1["Q4 纯函数化度量(§3.5 步骤 6)"]["逐函数"]}

    index, _dups = BP.build_index()

    # 技能表入口(B 宠技表 / C 职技表)的函数名 —— 空壳分类要用。
    skill_entry_names = {
        r["函数"].split("#")[0] for r in rows
        if r.get("入口角色")
        and {x.split(":")[0] for x in r["入口角色"]} & {"B", "C"}
    }

    # ── 回源码做二次分类 ──────────────────────────────────────────
    by_batch = defaultdict(lambda: Counter())
    thin_by_target = defaultdict(list)
    empty_shells = []
    slot_hist = Counter()
    detail = []
    missing = []

    for row in rows:
        # ⚠️ 阶段 0.0 对重名函数用 `名字#序号` 登记每份定义(build_index 的 dups)。
        #    匹配源码时须剥掉后缀,否则 6 个重名函数会全部丢失。
        name = row["函数"].split("#")[0]
        file_, line = row["出处"].split(":")
        defs = [d for d in index.get(name, [])
                if d["file"] == file_ and str(d["line"]) == line]
        if not defs:
            missing.append(row["出处"] + " " + row["函数"])
            continue
        defn = dict(defs[0], name=name)

        cmd, state, slots = classify_writes(defn["clean"])
        slot_hist.update(slots)
        thin = thin_wrapper_target(defn, index)
        empty = (not thin) and is_empty_shell(defn)
        if thin:
            thin_by_target[thin].append(name)
        if empty:
            empty_shells.append(name)

        b = batch_of(row, w1_names)
        agg = by_batch[b]
        agg["函数数"] += 1
        agg["行数"] += row["行数"]
        agg["写-指令登记"] += cmd
        agg["写-真状态"] += state
        agg["写-合计"] += row["★ 写-宽(判定口径)"]
        agg["串缓冲"] += row["写串缓冲"]
        agg["随机点"] += row["随机点"]
        agg["g* 引用"] += row["g* 引用次数"]
        agg["I/O(玩法+审计)"] += row["★ I/O-判定合计"]
        if thin:
            agg["同构薄包装"] += 1
        if empty:
            agg["空壳"] += 1

        detail.append({
            "函数": row["函数"], "出处": row["出处"], "批次": b,
            "行数": row["行数"],
            "写-指令登记": cmd, "写-真状态": state,
            "写-宽(0.0 口径)": row["★ 写-宽(判定口径)"],
            "同构薄包装": thin or "",
            "空壳": empty,
            "串缓冲": row["写串缓冲"], "随机点": row["随机点"],
            "g* 引用": row["g* 引用次数"],
            "I/O(玩法+审计)": row["★ I/O-判定合计"],
        })

    # ★ 薄包装的判据 ③:分派器须被 ≥ SHARED_MIN 个函数共享。
    #   共享数不足的,退回「短的真实现」—— 短不等于同构,仍要逐个移植。
    for target in [t for t, v in list(thin_by_target.items())
                   if len(v) < SHARED_MIN]:
        for name in thin_by_target.pop(target):
            for r in detail:
                if r["函数"].split("#")[0] == name and r["同构薄包装"] == target:
                    r["同构薄包装"] = ""
                    by_batch[r["批次"]]["同构薄包装"] -= 1

    # ★ 空壳分两类,后果不同,不可合并统计:
    #   · 技能指令空实现(入口角色 B/C)—— 原版该指令**就是不做事**(被动技能,
    #     效果在攻击序列里而非指令里,如 PROFESSION_weapon_focus ↔
    #     PROFESSION_SKILL_WEAPON_FOCUS_LVEVEL_UP 66 行)。⇒ 新实现同样不做事。
    #   · 表访问器包装(其余)—— `return XXX_getInt(...)` 之流,新实现里仍然存在,
    #     只是很小。**不是塌缩,只是短。**
    skill_empty = sorted(n for n in empty_shells if n in skill_entry_names)
    accessor_empty = sorted(n for n in empty_shells if n not in skill_entry_names)

    # ⚠️ 分类的自洽性:cmd + state 必须等于 0.0 口径的写-宽合计。
    #    对不上说明本脚本的 WRITE_WIDE_CALL 与 0.0 的 WRITE_WIDE_PAT 已漂移。
    tot_cmd = sum(b["写-指令登记"] for b in by_batch.values())
    tot_state = sum(b["写-真状态"] for b in by_batch.values())
    tot_wide = sum(b["写-合计"] for b in by_batch.values())
    consistent = (tot_cmd + tot_state == tot_wide)

    # ── 塌缩后的净规模 ────────────────────────────────────────────
    #
    # 「净」= 真正要一行行移植的量:
    #   · 指令登记写 → 塌缩为 TurnCommands 赋值,不计入
    #   · 同构薄包装 → 塌缩为映射表的一行,其函数数不计入
    thin_total = sum(len(v) for v in thin_by_target.values())

    result = {
        "产物": "rescope_battle_port.json",
        "生成者": "tools/rescope_battle_port.py",
        "用途": "0.1 四步改造的规模重算与批次切分(00-architecture.md §9.0.1 的排期空白)",
        "★ 权威边界": [
            "本脚本**不产出人日**。00 §11.1 已明写人日绝对值无实测支撑。",
            "本脚本**不做可行性判定** —— 那是阶段 0.0 的事,已完成且 D2 存续。",
            "本脚本改变的是**排期与批次**,不改变任何已裁定的架构决策。",
        ],
        "输入": {
            "阶段 0.0 度量": os.path.relpath(FULL_JSON, REPO),
            "W1 普攻链路名单": os.path.relpath(W1_JSON, REPO),
            "函数体索引": "复用 battle_purity_full.build_index()",
        },
        "自检": {
            "分类自洽(指令登记 + 真状态 == 0.0 写-宽合计)": consistent,
            "指令登记": tot_cmd, "真状态": tot_state, "0.0 写-宽合计": tot_wide,
            "未能在源码中定位的函数": missing,
        },
        "★★ 发现 1 —— 指令登记不是世界状态写": {
            "实测": f"{tot_cmd} / {tot_wide} = {tot_cmd / tot_wide:.1%} 的写是往「本回合指令」槽位写",
            "槽位分布": dict(slot_hist.most_common()),
            "为什么不计入第②步": (
                "原版没有 TurnCommands 类型,它把指令暂存在角色 WORK 槽里,"
                "由 BATTLE_Battling 稍后读回分发 ⇒ 这是**传参**不是状态变更。"
                "新契约里它就是 rules::TurnCommands / domain::BattleCommand,"
                "是 ResolveTurn 的**输入**。⇒ 塌缩为一次结构体赋值,不产生移植工作。"),
            "口径取窄": sorted(COMMAND_SLOTS),
        },
        "★★ 发现 2 —— 塌缩型函数不产生逐行移植": {
            "同构薄包装": {
                "实测": f"{thin_total} 个(≤{THIN_MAX_LINES} 行、唯一调用是 battle/ 内分派器、"
                        f"该分派器被 ≥{SHARED_MIN} 个函数共享、无随机无串缓冲)",
                "按分派器": {k: {"个数": len(v), "样例": sorted(v)[:5]}
                             for k, v in sorted(thin_by_target.items(),
                                                key=lambda kv: -len(kv[1]))},
                "为什么不计入函数数": (
                    "它们逐字同构,只有指令码常量不同 ⇒ 新实现里是一张映射表的一行,"
                    "不是一份移植。真正的结算在 BATTLE_COM_S_* 分发分支(A 批次)。"),
            },
            "★ 技能指令空实现": {
                "实测": f"{len(skill_empty)} 个技能入口的函数体只有 return,零调用",
                "清单": skill_empty,
                "★ 这不是待补": (
                    "指令入口存在但不做事 —— **原版就没有实现**。它们是被动技能:"
                    "效果在攻击序列里生效,不在指令里(如 PROFESSION_weapon_focus 空,"
                    "而 PROFESSION_SKILL_WEAPON_FOCUS_LVEVEL_UP 有 66 行)。"
                    "⇒ 新实现同样不做事,或直接不注册该指令。"
                    "⚠️ 勿把它们当成「8.0 缺失、需要补齐」的功能项。"),
            },
            "表访问器包装(不塌缩,只是短)": {
                "实测": f"{len(accessor_empty)} 个",
                "清单": accessor_empty,
                "说明": "`return XXX_getInt(...)` 之流,新实现里仍然存在。列出只为避免"
                        "与上一类混淆 —— 它们**不减少**移植工作。",
            },
        },
        "★ 批次表": {
            b: dict(by_batch[b]) for b in BATCH_ORDER if b in by_batch
        },
        "★ 四步改造按批次的触及点数": {},
        "逐函数": sorted(detail, key=lambda r: (BATCH_ORDER.index(r["批次"]), r["函数"])),
    }

    # 四步 × 批次
    steps = {
        "① g* → 显式参数结构体": "g* 引用",
        "② 写世界状态 → 返回事件": "写-真状态",
        "③ 串拼装 + 发包 → 事件列表": "串缓冲",
        "④ RAND() → 注入序列": "随机点",
    }
    for step, key in steps.items():
        result["★ 四步改造按批次的触及点数"][step] = {
            b: by_batch[b][key] for b in BATCH_ORDER if b in by_batch
        }
        result["★ 四步改造按批次的触及点数"][step]["合计"] = sum(
            by_batch[b][key] for b in by_batch)

    # ── 摘要 ──────────────────────────────────────────────────────
    print("═" * 78)
    print(" 0.1 四步改造 —— 规模重算(rescope_battle_port.py)")
    print("═" * 78)
    if not consistent:
        print(f"⚠️ 分类不自洽:{tot_cmd} + {tot_state} != {tot_wide} —— "
              "本脚本的写正则与阶段 0.0 已漂移,结论不可用。")
    if missing:
        print(f"⚠️ {len(missing)} 个函数未能在源码中定位:{missing[:3]}")

    print(f"\n★ 发现 1:{tot_cmd}/{tot_wide} = {tot_cmd / tot_wide:.1%} 的写是**指令登记**,"
          f"不是世界状态写\n  ⇒ 第②步的净规模 = {tot_state} 处(不是 {tot_wide} 处)")
    print(f"\n★ 发现 2:{thin_total} 个同构薄包装塌缩为映射表,"
          f"{len(skill_empty)} 个技能指令空实现塌缩为零")
    for k, v in sorted(thin_by_target.items(), key=lambda kv: -len(kv[1])):
        print(f"    {len(v):3d} 个 → {k}")
    if skill_empty:
        print(f"    空实现:{', '.join(skill_empty[:5])}"
              f"{' …' if len(skill_empty) > 5 else ''}")
    if accessor_empty:
        print(f"    (另有 {len(accessor_empty)} 个表访问器包装,**不减少**移植工作)")

    print("\n★ 批次表")
    hdr = f"  {'批次':<18}" + "".join(f"{m:>10}" for m in METRICS[:6])
    print(hdr)
    print("  " + "─" * (len(hdr) - 2))
    for b in BATCH_ORDER:
        if b not in by_batch:
            continue
        a = by_batch[b]
        print(f"  {b:<18}" + "".join(f"{a[m]:>10}" for m in METRICS[:6]))
    tot = Counter()
    for a in by_batch.values():
        tot.update(a)
    print(f"  {'合计':<18}" + "".join(f"{tot[m]:>10}" for m in METRICS[:6]))

    print("\n★ 四步 × 批次(触及点数)")
    for step, per in result["★ 四步改造按批次的触及点数"].items():
        parts = "  ".join(f"{b.split()[0]}={per[b]}"
                          for b in BATCH_ORDER if b in per)
        print(f"  {step:<28} {parts}  合计={per['合计']}")

    b0 = by_batch[BATCH_ORDER[0]]
    print(f"\n★ demo 最小切面(批次 0 普攻链路):"
          f"{b0['函数数']} 函数 / {b0['行数']} 行 / "
          f"第①步 {b0['g* 引用']} 处({b0['g* 引用'] / max(tot['g* 引用'], 1):.0%} 的 g*)/ "
          f"第②步 {b0['写-真状态']} 处 / 第③步 {b0['串缓冲']} 处 / 第④步 {b0['随机点']} 处")

    if not summary_only:
        with open(OUT, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=1)
        print(f"\n已写出 {os.path.relpath(OUT, REPO)}")

    return 0 if consistent and not missing else 1


if __name__ == "__main__":
    sys.exit(main())
