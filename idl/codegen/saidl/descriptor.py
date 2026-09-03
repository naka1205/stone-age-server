"""descriptor set → 内部 IR。

只覆盖本项目 schema 允许使用的构造（DR-TS1 边界 ④：不得使用 oneof 以外的高级特性）。
遇到不允许的构造一律报错，而不是静默忽略 —— 静默忽略正是「两端漂移」的温床。
"""

from __future__ import annotations

from dataclasses import dataclass, field as dc_field

from . import wire

# ── descriptor.proto 的字段号（本模块唯一的「魔数」来源） ──────────
F_SET_FILE = 1

F_FILE_NAME = 1
F_FILE_PACKAGE = 2
F_FILE_DEPENDENCY = 3
F_FILE_MESSAGE_TYPE = 4
F_FILE_ENUM_TYPE = 5
F_FILE_SYNTAX = 12

F_MSG_NAME = 1
F_MSG_FIELD = 2
F_MSG_NESTED_TYPE = 3
F_MSG_ENUM_TYPE = 4
F_MSG_EXTENSION = 6
F_MSG_OPTIONS = 7
F_MSG_ONEOF_DECL = 8

F_FLD_NAME = 1
F_FLD_NUMBER = 3
F_FLD_LABEL = 4
F_FLD_TYPE = 5
F_FLD_TYPE_NAME = 6
F_FLD_OPTIONS = 8
F_FLD_ONEOF_INDEX = 9
F_FLD_PROTO3_OPTIONAL = 17

F_ENUM_NAME = 1
F_ENUM_VALUE = 2
F_ENUM_OPTIONS = 3
F_ENUMVAL_NAME = 1
F_ENUMVAL_NUMBER = 2

F_ONEOF_NAME = 1

# ── sa_options.proto 的扩展字段号 ────────────────────────────────
OPT_MSG_ID = 50001
OPT_TRANSPORT_ONLY = 50002
OPT_DEPRECATED_ID = 50003
OPT_MAX_LEN = 50011
OPT_MAX_COUNT = 50012
OPT_ENUM_WIDTH = 50021

LABEL_OPTIONAL = 1
LABEL_REQUIRED = 2
LABEL_REPEATED = 3

# FieldDescriptorProto.Type
T_DOUBLE, T_FLOAT, T_INT64, T_UINT64, T_INT32 = 1, 2, 3, 4, 5
T_FIXED64, T_FIXED32, T_BOOL, T_STRING, T_GROUP = 6, 7, 8, 9, 10
T_MESSAGE, T_BYTES, T_UINT32, T_ENUM = 11, 12, 13, 14
T_SFIXED32, T_SFIXED64, T_SINT32, T_SINT64 = 15, 16, 17, 18

SCALARS = {
    T_DOUBLE, T_FLOAT, T_INT64, T_UINT64, T_INT32, T_FIXED64, T_FIXED32,
    T_BOOL, T_UINT32, T_SFIXED32, T_SFIXED64, T_SINT32, T_SINT64,
}

# ⚠️ 禁用清单：这些构造要么破坏 POD（边界 ②），要么引入 protobuf 语义（边界 ④）。
BANNED_TYPES = {
    T_GROUP: "group",
    T_BYTES: "bytes（变长且无天然上限；需要二进制负载请用 repeated uint32 + max_count 或独立帧）",
    T_DOUBLE: "double（POD 布局要求定宽整型与 float32；浮点只允许 float）",
}


class SchemaError(Exception):
    pass


@dataclass
class Field:
    name: str
    number: int
    ptype: int
    type_name: str | None      # message / enum 的全限定名（带前导点，已去除）
    repeated: bool
    oneof_index: int | None
    max_len: int | None
    max_count: int | None
    file: str


@dataclass
class Oneof:
    name: str
    fields: list[Field] = dc_field(default_factory=list)


@dataclass
class Message:
    fqname: str                # sa.domain.Vec2
    fields: list[Field]
    oneofs: list[Oneof]
    msg_id: int | None
    transport_only: bool
    deprecated_id: bool
    file: str
    package: str

    @property
    def cpp_name(self) -> str:
        return self.fqname.split(".")[-1] if "." not in self.fqname else \
            "".join(p[0].upper() + p[1:] for p in self.fqname.split(".")[2:])


@dataclass
class EnumValue:
    name: str
    number: int


@dataclass
class Enum:
    fqname: str
    values: list[EnumValue]
    width: int
    file: str
    package: str


@dataclass
class Schema:
    messages: dict[str, Message]
    enums: dict[str, Enum]
    files: list[str]

    def order(self) -> list[Message]:
        """按依赖拓扑序返回消息，保证 C++ 结构体先定义后使用。"""
        seen: dict[str, int] = {}
        out: list[Message] = []

        def visit(fq: str, stack: tuple[str, ...]) -> None:
            if fq in seen:
                if seen[fq] == 1:
                    raise SchemaError(
                        "消息定义存在循环嵌套：" + " → ".join(stack + (fq,))
                        + "\n  POD 结构体不能自引用（无指针、无堆成员）")
                return
            seen[fq] = 1
            msg = self.messages[fq]
            for f in msg.fields:
                if f.ptype == T_MESSAGE:
                    visit(f.type_name, stack + (fq,))
            seen[fq] = 2
            out.append(msg)

        for fq in self.messages:
            visit(fq, ())
        return out


def _opt_uint(options, no):
    if options is None:
        return None
    return wire.get_int(options, no, None)


def _opt_bool(options, no):
    if options is None:
        return False
    return wire.get_bool(options, no, False)


def _parse_enum(ed, package: str, prefix: str, filename: str) -> Enum:
    name = wire.get_str(ed, F_ENUM_NAME)
    fq = f"{package}.{prefix}{name}" if package else f"{prefix}{name}"
    values = []
    for vd in wire.get_msgs(ed, F_ENUM_VALUE):
        values.append(EnumValue(
            name=wire.get_str(vd, F_ENUMVAL_NAME),
            number=wire.get_int(vd, F_ENUMVAL_NUMBER, 0),
        ))
    width = _opt_uint(wire.get_msg(ed, F_ENUM_OPTIONS), OPT_ENUM_WIDTH) or 32
    if width not in (8, 16, 32):
        raise SchemaError(f"枚举 {fq} 的 (sa.width) = {width}，只允许 8 / 16 / 32")
    return Enum(fqname=fq, values=values, width=width, file=filename, package=package)


def _parse_message(md, package: str, prefix: str, filename: str,
                   messages: dict, enums: dict) -> None:
    name = wire.get_str(md, F_MSG_NAME)
    fq = f"{package}.{prefix}{name}" if package else f"{prefix}{name}"

    if wire.get_msgs(md, F_MSG_EXTENSION):
        # extend 只允许出现在 sa_options.proto（那份文件不参与生成）
        raise SchemaError(f"消息 {fq} 内出现 extend —— 业务 schema 不得使用扩展")

    options = wire.get_msg(md, F_MSG_OPTIONS)
    oneof_names = [wire.get_str(od, F_ONEOF_NAME)
                   for od in wire.get_msgs(md, F_MSG_ONEOF_DECL)]
    oneofs = [Oneof(name=n) for n in oneof_names]

    fields: list[Field] = []
    for fd in wire.get_msgs(md, F_MSG_FIELD):
        fname = wire.get_str(fd, F_FLD_NAME)
        ptype = wire.get_int(fd, F_FLD_TYPE, 0)
        label = wire.get_int(fd, F_FLD_LABEL, LABEL_OPTIONAL)
        fopts = wire.get_msg(fd, F_FLD_OPTIONS)

        if ptype in BANNED_TYPES:
            raise SchemaError(
                f"{fq}.{fname} 使用了禁用类型 {BANNED_TYPES[ptype]}")
        if label == LABEL_REQUIRED:
            raise SchemaError(f"{fq}.{fname} 使用了 proto2 的 required")
        if wire.get_bool(fd, F_FLD_PROTO3_OPTIONAL):
            raise SchemaError(
                f"{fq}.{fname} 使用了 proto3 optional —— "
                "它会生成合成 oneof 并引入 has 位，与 POD 布局冲突")

        repeated = label == LABEL_REPEATED
        type_name = wire.get_str(fd, F_FLD_TYPE_NAME)
        if type_name:
            type_name = type_name.lstrip(".")

        max_len = _opt_uint(fopts, OPT_MAX_LEN)
        max_count = _opt_uint(fopts, OPT_MAX_COUNT)

        if ptype == T_STRING and max_len is None:
            raise SchemaError(
                f"{fq}.{fname} 是 string 但未标注 [(sa.max_len) = N]。\n"
                "  DR-TS1 边界 ②：变长字段必须给出显式定长上限。")
        if repeated and max_count is None:
            raise SchemaError(
                f"{fq}.{fname} 是 repeated 但未标注 [(sa.max_count) = N]。\n"
                "  DR-TS1 边界 ②：变长字段必须给出显式定长上限。")

        oneof_index = wire.get_int(fd, F_FLD_ONEOF_INDEX, None)
        if oneof_index is not None and repeated:
            raise SchemaError(f"{fq}.{fname} 在 oneof 内使用了 repeated —— proto 不允许")

        f = Field(name=fname, number=wire.get_int(fd, F_FLD_NUMBER, 0),
                  ptype=ptype, type_name=type_name, repeated=repeated,
                  oneof_index=oneof_index, max_len=max_len,
                  max_count=max_count, file=filename)
        fields.append(f)
        if oneof_index is not None:
            oneofs[oneof_index].fields.append(f)

    messages[fq] = Message(
        fqname=fq, fields=fields, oneofs=oneofs,
        msg_id=_opt_uint(options, OPT_MSG_ID),
        transport_only=_opt_bool(options, OPT_TRANSPORT_ONLY),
        deprecated_id=_opt_bool(options, OPT_DEPRECATED_ID),
        file=filename, package=package,
    )

    nested_prefix = f"{prefix}{name}."
    for nd in wire.get_msgs(md, F_MSG_NESTED_TYPE):
        _parse_message(nd, package, nested_prefix, filename, messages, enums)
    for ed in wire.get_msgs(md, F_MSG_ENUM_TYPE):
        e = _parse_enum(ed, package, nested_prefix, filename)
        enums[e.fqname] = e


def load(descriptor_set: bytes, skip_files: tuple[str, ...] = ()) -> Schema:
    """解析 protoc --descriptor_set_out 的产物。

    skip_files 里的文件只参与解析（提供 option 定义），不产出生成物。
    """
    root = wire.parse(descriptor_set)
    messages: dict[str, Message] = {}
    enums: dict[str, Enum] = {}
    files: list[str] = []

    for fdp in wire.get_msgs(root, F_SET_FILE):
        filename = wire.get_str(fdp, F_FILE_NAME)
        if filename in skip_files or filename.startswith("google/protobuf/"):
            continue
        syntax = wire.get_str(fdp, F_FILE_SYNTAX, "proto2")
        if syntax != "proto3":
            raise SchemaError(f"{filename} 的 syntax 是 {syntax}，只允许 proto3")
        package = wire.get_str(fdp, F_FILE_PACKAGE, "")
        files.append(filename)

        for md in wire.get_msgs(fdp, F_FILE_MESSAGE_TYPE):
            _parse_message(md, package, "", filename, messages, enums)
        for ed in wire.get_msgs(fdp, F_FILE_ENUM_TYPE):
            e = _parse_enum(ed, package, "", filename)
            enums[e.fqname] = e

    # 引用完整性：所有 message/enum 引用必须落在本次解析范围内
    for msg in messages.values():
        for f in msg.fields:
            if f.ptype == T_MESSAGE and f.type_name not in messages:
                raise SchemaError(f"{msg.fqname}.{f.name} 引用了未知消息 {f.type_name}")
            if f.ptype == T_ENUM and f.type_name not in enums:
                raise SchemaError(f"{msg.fqname}.{f.name} 引用了未知枚举 {f.type_name}")

    return Schema(messages=messages, enums=enums, files=files)
