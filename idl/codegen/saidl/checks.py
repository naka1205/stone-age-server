"""schema 一致性检查。

02-protocol.md §8 要求 CI 必须有的三条：
  1. 编号唯一性 —— 挡住「客户端头文件自身同号重复」（10 §6.3 实测 2 处）
  2. 编号稳定性 —— 与上次发布的编号表 diff，已发布编号只允许新增不允许改动
     ★ 第 2 条比第 1 条更重要：编号冲突第一天暴露，编号漂移半年后暴露
  3. 无条件编译 —— schema 里出现任何 #if / 特性开关即失败（§1.1 落地规则 4）

本项目另加两条，同属「结构上消灭漂移」：
  4. 号段归属 —— 每个 msg_id 必须落在 §3 已声明号段内
  5. 分层依赖 —— schema/domain/ 不得引用 transport_only 消息（§9，shared/ 只依赖标准库）
"""

from __future__ import annotations

import json
import re
from pathlib import Path

from . import descriptor as D

# 02-protocol.md §3 号段划分
SEGMENTS = [
    (0x0001, 0x00FF, "握手 / 会话 / 心跳"),
    (0x0100, 0x01FF, "世界与移动"),
    (0x0200, 0x02FF, "战斗"),
    (0x0300, 0x03FF, "角色与成长"),
    (0x0400, 0x04FF, "道具与装备"),
    (0x0500, 0x05FF, "宠物"),
    (0x0600, 0x06FF, "NPC 与窗口"),
    (0x0700, 0x07FF, "社交与聊天"),
    (0x0800, 0x08FF, "家族与庄园"),
    (0x0F00, 0x0FFF, "GM 与运维"),
]

# 禁止出现在 schema 源文件里的条件编译 / 特性开关痕迹
BANNED_PATTERNS = [
    (re.compile(r"^\s*#\s*(if|ifdef|ifndef|else|elif|endif)\b"), "C 预处理指令"),
    (re.compile(r"^\s*//\s*@?(ifdef|feature)\b", re.I), "注释式特性开关"),
]


class CheckError(Exception):
    pass


def segment_of(msg_id: int) -> str | None:
    for lo, hi, name in SEGMENTS:
        if lo <= msg_id <= hi:
            return name
    return None


def check_no_conditionals(schema_dir: Path) -> list[str]:
    """检查 3：schema 源文件里不得有条件编译。"""
    errs = []
    for path in sorted(schema_dir.rglob("*.proto")):
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for pat, what in BANNED_PATTERNS:
                if pat.search(line):
                    errs.append(
                        f"{path}:{lineno} 出现{what}：{line.strip()}\n"
                        "  02 §1.1 规则 4：特性差异走运行时配置（D7），不走 schema 分支。")
    return errs


def check_unique_ids(schema: D.Schema) -> list[str]:
    """检查 1：msg_id 与枚举值的唯一性。"""
    errs = []
    seen: dict[int, str] = {}
    for m in schema.messages.values():
        if m.msg_id is None:
            continue
        if m.msg_id in seen:
            errs.append(
                f"msg_id 0x{m.msg_id:04X} 被重复使用："
                f"{seen[m.msg_id]} 与 {m.fqname}")
        seen[m.msg_id] = m.fqname

    for e in schema.enums.values():
        vals: dict[int, str] = {}
        for v in e.values:
            if v.number in vals:
                errs.append(
                    f"枚举 {e.fqname} 的值 {v.number} 重复："
                    f"{vals[v.number]} 与 {v.name}")
            vals[v.number] = v.name

    # 消息内字段号唯一（protoc 已保证，这里防御 descriptor 解析出错）
    for m in schema.messages.values():
        nums: dict[int, str] = {}
        for f in m.fields:
            if f.number in nums:
                errs.append(f"{m.fqname} 字段号 {f.number} 重复：{nums[f.number]} 与 {f.name}")
            nums[f.number] = f.name
    return errs


def check_segments(schema: D.Schema) -> list[str]:
    """检查 4：msg_id 必须落在已声明号段内。"""
    errs = []
    for m in schema.messages.values():
        if m.msg_id is None:
            continue
        if segment_of(m.msg_id) is None:
            errs.append(
                f"{m.fqname} 的 msg_id 0x{m.msg_id:04X} 不在 02 §3 的任何号段内。\n"
                "  号段可以扩，但必须先在 02-protocol.md §3 与 checks.py 里同步声明。")
    return errs


def check_layering(schema: D.Schema) -> list[str]:
    """检查 5：domain/ 不得引用 transport_only 消息。"""
    errs = []
    for m in schema.messages.values():
        if m.file.startswith("transport/"):
            continue
        for f in m.fields:
            if f.ptype != D.T_MESSAGE:
                continue
            target = schema.messages[f.type_name]
            if target.transport_only or target.file.startswith("transport/"):
                errs.append(
                    f"{m.fqname}.{f.name} 引用了传输层类型 {target.fqname}。\n"
                    "  02 §9：shared/rules 可依赖 IDL 事件类型，但不得依赖任何传输类型，"
                    "否则「shared/ 只依赖标准库」即破。")
    return errs


def check_stability(schema: D.Schema, registry_path: Path) -> tuple[list[str], dict]:
    """检查 2：与已发布编号表 diff。返回 (错误列表, 新的编号表)。

    编号表格式：{"messages": {"全限定名": msg_id}, "enums": {"枚举全名": {"值名": 值}}}
    """
    current = {
        "messages": {m.fqname: m.msg_id
                     for m in schema.messages.values() if m.msg_id is not None},
        "enums": {e.fqname: {v.name: v.number for v in e.values}
                  for e in schema.enums.values()},
    }
    if not registry_path.exists():
        return [], current

    published = json.loads(registry_path.read_text(encoding="utf-8"))
    errs = []

    for name, old_id in published.get("messages", {}).items():
        new_id = current["messages"].get(name)
        if new_id is None:
            errs.append(
                f"已发布消息 {name}（0x{old_id:04X}）在当前 schema 中消失。\n"
                "  编号一经发布不得复用：请保留消息并标注 [(sg.deprecated_id) = true]。")
        elif new_id != old_id:
            errs.append(
                f"已发布消息 {name} 的编号从 0x{old_id:04X} 改成了 0x{new_id:04X}。\n"
                "  ★ 编号漂移会在半年后暴露，不允许改动。")

    for ename, old_vals in published.get("enums", {}).items():
        new_vals = current["enums"].get(ename)
        if new_vals is None:
            errs.append(f"已发布枚举 {ename} 在当前 schema 中消失。")
            continue
        for vname, old_num in old_vals.items():
            new_num = new_vals.get(vname)
            if new_num is None:
                errs.append(f"已发布枚举值 {ename}.{vname}（{old_num}）消失。")
            elif new_num != old_num:
                errs.append(
                    f"已发布枚举值 {ename}.{vname} 从 {old_num} 改成了 {new_num}。")

    return errs, current


def run_all(schema: D.Schema, schema_dir: Path,
            registry_path: Path) -> tuple[list[str], dict]:
    errs: list[str] = []
    errs += check_no_conditionals(schema_dir)
    errs += check_unique_ids(schema)
    errs += check_segments(schema)
    errs += check_layering(schema)
    stab_errs, current = check_stability(schema, registry_path)
    errs += stab_errs
    return errs, current
