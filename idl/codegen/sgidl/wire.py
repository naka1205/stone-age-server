"""protobuf wire format 的最小解析器。

★ 为什么自己写:DR-TS1 边界 ① 要求 protoc 只在构建期用于解析。
若 codegen 依赖 `pip install protobuf`,构建前置就从「装 protoc」变成
「装 protoc + 配 Python 环境」。本模块 ~150 行,换来 `protoc + python3` 即可构建。

本模块只做「字节 → 按字段号分组的原始值」,不认识任何 .proto 语义;
descriptor.proto 的字段号映射在 descriptor.py 里。
"""

from __future__ import annotations

WIRE_VARINT = 0
WIRE_FIXED64 = 1
WIRE_LEN = 2
WIRE_SGROUP = 3
WIRE_EGROUP = 4
WIRE_FIXED32 = 5


class WireError(Exception):
    pass


def _read_varint(buf: bytes, pos: int) -> tuple[int, int]:
    result = 0
    shift = 0
    while True:
        if pos >= len(buf):
            raise WireError("varint 越过缓冲末尾")
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, pos
        shift += 7
        if shift > 63:
            raise WireError("varint 超过 64 位")


def parse(buf: bytes) -> dict[int, list]:
    """解析一条 message，返回 {字段号: [原始值, ...]}。

    原始值类型：varint/fixed → int，length-delimited → bytes。
    重复字段与「同号出现多次」一律进列表，调用方决定取第一个还是全部。
    """
    out: dict[int, list] = {}
    pos = 0
    n = len(buf)
    while pos < n:
        tag, pos = _read_varint(buf, pos)
        field_no = tag >> 3
        wire_type = tag & 0x07
        if field_no == 0:
            raise WireError("字段号 0 非法")

        if wire_type == WIRE_VARINT:
            value, pos = _read_varint(buf, pos)
        elif wire_type == WIRE_FIXED64:
            if pos + 8 > n:
                raise WireError("fixed64 越界")
            value = int.from_bytes(buf[pos : pos + 8], "little")
            pos += 8
        elif wire_type == WIRE_FIXED32:
            if pos + 4 > n:
                raise WireError("fixed32 越界")
            value = int.from_bytes(buf[pos : pos + 4], "little")
            pos += 4
        elif wire_type == WIRE_LEN:
            length, pos = _read_varint(buf, pos)
            if pos + length > n:
                raise WireError("length-delimited 越界")
            value = buf[pos : pos + length]
            pos += length
        elif wire_type == WIRE_SGROUP:
            # descriptor set 里不会出现 group，出现即说明解析已经跑偏
            raise WireError("不支持 group（wire type 3/4）")
        else:
            raise WireError(f"未知 wire type {wire_type}")

        out.setdefault(field_no, []).append(value)
    return out


# ── 取值辅助 ────────────────────────────────────────────────────
# descriptor.proto 里 optional 字段缺省时整个字段不出现，故一律给 default。


def get_int(fields: dict[int, list], no: int, default: int | None = None) -> int | None:
    vals = fields.get(no)
    if not vals:
        return default
    v = vals[-1]
    if not isinstance(v, int):
        raise WireError(f"字段 {no} 期望整数，实得 {type(v).__name__}")
    return v


def get_bool(fields: dict[int, list], no: int, default: bool = False) -> bool:
    v = get_int(fields, no, None)
    return default if v is None else bool(v)


def get_str(fields: dict[int, list], no: int, default: str | None = None) -> str | None:
    vals = fields.get(no)
    if not vals:
        return default
    v = vals[-1]
    if not isinstance(v, bytes):
        raise WireError(f"字段 {no} 期望字节串，实得 {type(v).__name__}")
    return v.decode("utf-8")


def get_bytes(fields: dict[int, list], no: int) -> bytes | None:
    vals = fields.get(no)
    if not vals:
        return None
    v = vals[-1]
    if not isinstance(v, bytes):
        raise WireError(f"字段 {no} 期望字节串")
    return v


def get_msgs(fields: dict[int, list], no: int) -> list[dict[int, list]]:
    """把 repeated 的嵌套 message 字段解析成一串 fields 字典。"""
    out = []
    for v in fields.get(no, []):
        if not isinstance(v, bytes):
            raise WireError(f"字段 {no} 期望嵌套 message")
        out.append(parse(v))
    return out


def get_msg(fields: dict[int, list], no: int) -> dict[int, list] | None:
    msgs = get_msgs(fields, no)
    return msgs[-1] if msgs else None


def get_strs(fields: dict[int, list], no: int) -> list[str]:
    out = []
    for v in fields.get(no, []):
        if not isinstance(v, bytes):
            raise WireError(f"字段 {no} 期望字节串")
        out.append(v.decode("utf-8"))
    return out
