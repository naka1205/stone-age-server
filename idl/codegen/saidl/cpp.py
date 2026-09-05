"""C++ 后端：IR → 纯 POD struct + inline 编解码（header-only）。

生成物形状（DR-TS1 §1.1）：
    struct 是聚合体（无用户构造/析构/虚函数）⇒ 可 memcpy、可放进 union；
    string → sa::idl::FixedStr<max_len>；repeated → sa::idl::FixedVec<T, max_count>；
    oneof  → tag + union，不生成继承（边界 ④）。

编码格式（本项目自定义，不是 protobuf wire）：
    schema 是唯一真源且版本不匹配在握手阶段直接拒绝（02 §2.1）
    ⇒ 报文里不需要字段标签，按字段号升序裸排即可。
    标量 = 定长小端；string = u16 len + 字节；repeated = u16 count + 元素；
    嵌套消息 = 内联展开（无长度前缀）；oneof = u16 字段号 + 选中成员。
"""

from __future__ import annotations

from . import descriptor as D

# ptype → (C++ 类型, Writer 方法, Reader 方法)
SCALAR_MAP = {
    D.T_INT32:    ("std::int32_t",  "i32", "i32"),
    D.T_SINT32:   ("std::int32_t",  "i32", "i32"),
    D.T_SFIXED32: ("std::int32_t",  "i32", "i32"),
    D.T_UINT32:   ("std::uint32_t", "u32", "u32"),
    D.T_FIXED32:  ("std::uint32_t", "u32", "u32"),
    D.T_INT64:    ("std::int64_t",  "i64", "i64"),
    D.T_SINT64:   ("std::int64_t",  "i64", "i64"),
    D.T_SFIXED64: ("std::int64_t",  "i64", "i64"),
    D.T_UINT64:   ("std::uint64_t", "u64", "u64"),
    D.T_FIXED64:  ("std::uint64_t", "u64", "u64"),
    D.T_BOOL:     ("bool",          "b",   "b"),
    D.T_FLOAT:    ("float",         "f32", "f32"),
}

ENUM_UNDERLYING = {8: "std::uint8_t", 16: "std::uint16_t", 32: "std::uint32_t"}


def _split_ns(fqname: str, package: str) -> tuple[str, str]:
    """sa.domain.Outer.Inner + 包 sa.domain → ('sa::domain', 'Outer_Inner')。"""
    rest = fqname[len(package) + 1:] if package else fqname
    return package.replace(".", "::"), rest.replace(".", "_")


def _snake_to_upper(name: str) -> str:
    return name.upper()


def _pascal(name: str) -> str:
    return "".join(p[:1].upper() + p[1:] for p in name.split("_"))


class CppGen:
    def __init__(self, schema: D.Schema):
        self.s = schema
        # fqname → (命名空间, C++ 名)
        self.names: dict[str, tuple[str, str]] = {}
        for m in schema.messages.values():
            self.names[m.fqname] = _split_ns(m.fqname, m.package)
        for e in schema.enums.values():
            self.names[e.fqname] = _split_ns(e.fqname, e.package)
        self.file_of: dict[str, str] = {}
        for m in schema.messages.values():
            self.file_of[m.fqname] = m.file
        for e in schema.enums.values():
            self.file_of[e.fqname] = e.file

    # ── 名字与类型 ────────────────────────────────────────────
    def qual(self, fqname: str) -> str:
        ns, name = self.names[fqname]
        return f"{ns}::{name}" if ns else name

    def elem_type(self, f: D.Field) -> str:
        """字段的元素类型（不含 repeated 包装）。"""
        if f.ptype in SCALAR_MAP:
            return SCALAR_MAP[f.ptype][0]
        if f.ptype == D.T_STRING:
            return f"sa::idl::FixedStr<{f.max_len}>"
        if f.ptype in (D.T_MESSAGE, D.T_ENUM):
            return self.qual(f.type_name)
        raise D.SchemaError(f"字段 {f.name} 的类型 {f.ptype} 无 C++ 映射")

    def field_type(self, f: D.Field) -> str:
        t = self.elem_type(f)
        if f.repeated:
            return f"sa::idl::FixedVec<{t}, {f.max_count}>"
        return t

    # ── 元素级编解码语句 ──────────────────────────────────────
    #
    # ★ `w` / `r` 是流参数名。之所以做成参数而不是硬编码:repeated 字段会把语句
    #   包进一个 lambda,而 lambda 的形参若也叫 `w`/`r`,就会**遮蔽外层同名形参**。
    #   ⚠️ GCC 的 -Wshadow 会报,Apple clang 不报(2026-09-02 跨编译器验证实测,
    #      GCC 15 / libstdc++ 下 36 处)。⇒ lambda 内改用 `we` / `re`。
    def write_elem(self, f: D.Field, expr: str, w: str = "w") -> str:
        if f.ptype in SCALAR_MAP:
            return f"{w}.{SCALAR_MAP[f.ptype][1]}({expr});"
        if f.ptype == D.T_STRING:
            return f"sa::idl::write_str({w}, {expr});"
        if f.ptype == D.T_ENUM:
            u = ENUM_UNDERLYING[self.s.enums[f.type_name].width]
            width = self.s.enums[f.type_name].width
            m = {8: "u8", 16: "u16", 32: "u32"}[width]
            return f"{w}.{m}(static_cast<{u}>({expr}));"
        return f"encode({w}, {expr});"

    def read_elem(self, f: D.Field, expr: str, r: str = "r") -> str:
        if f.ptype in SCALAR_MAP:
            return f"{expr} = {r}.{SCALAR_MAP[f.ptype][2]}();"
        if f.ptype == D.T_STRING:
            return f"sa::idl::read_str({r}, {expr});"
        if f.ptype == D.T_ENUM:
            width = self.s.enums[f.type_name].width
            m = {8: "u8", 16: "u16", 32: "u32"}[width]
            return f"{expr} = static_cast<{self.qual(f.type_name)}>({r}.{m}());"
        return f"decode({r}, {expr});"

    def write_field(self, f: D.Field, owner: str, indent: str) -> list[str]:
        acc = f"{owner}.{f.name}"
        if not f.repeated:
            return [indent + self.write_elem(f, acc)]
        et = self.elem_type(f)
        body = self.write_elem(f, "e", "we")
        return [
            f"{indent}sa::idl::write_vec(w, {acc},",
            f"{indent}    [](sa::idl::Writer& we, const {et}& e) {{ {body} }});",
        ]

    def read_field(self, f: D.Field, owner: str, indent: str) -> list[str]:
        acc = f"{owner}.{f.name}"
        if not f.repeated:
            return [indent + self.read_elem(f, acc)]
        et = self.elem_type(f)
        body = self.read_elem(f, "e", "re")
        return [
            f"{indent}sa::idl::read_vec(r, {acc},",
            f"{indent}    [](sa::idl::Reader& re, {et}& e) {{ {body} }});",
        ]

    # ── 消息体 ────────────────────────────────────────────────
    def _layout(self, msg: D.Message):
        """按字段号升序排出成员布局；oneof 整体落在其最小字段号处。

        返回 [('field', Field) | ('oneof', Oneof)]。
        """
        emitted: set[int] = set()
        items = []
        for f in sorted(msg.fields, key=lambda x: x.number):
            if f.oneof_index is None:
                items.append(("field", f))
                continue
            if f.oneof_index in emitted:
                continue
            emitted.add(f.oneof_index)
            items.append(("oneof", msg.oneofs[f.oneof_index]))
        return items

    def oneof_names(self, o: D.Oneof) -> tuple[str, str, str]:
        """(枚举类型名, tag 成员名, union 成员名)"""
        base = _pascal(o.name)
        return f"{base}Kind", f"{o.name}_kind", o.name

    def emit_message(self, msg: D.Message) -> list[str]:
        _, name = self.names[msg.fqname]
        L = [f"struct {name} {{"]
        items = self._layout(msg)

        for kind, item in items:
            if kind == "field":
                f = item
                L.append(f"  {self.field_type(f)} {f.name};")
            else:
                o = item
                ename, tagname, uname = self.oneof_names(o)
                u = "std::uint16_t"
                L.append(f"  enum class {ename} : {u} {{")
                L.append("    NONE = 0,")
                for f in sorted(o.fields, key=lambda x: x.number):
                    L.append(f"    {_snake_to_upper(f.name)} = {f.number},")
                L.append("  };")
                L.append(f"  {ename} {tagname};")
                L.append(f"  union {_pascal(o.name)}Union {{")
                for f in sorted(o.fields, key=lambda x: x.number):
                    L.append(f"    {self.field_type(f)} {f.name};")
                L.append(f"  }} {uname};")
        L.append("};")
        L.append("")

        # ── encode ──
        L.append(f"inline void encode(sa::idl::Writer& w, const {name}& m) {{")
        if not items:
            L.append("  (void)w; (void)m;")
        for kind, item in items:
            if kind == "field":
                L += self.write_field(item, "m", "  ")
            else:
                o = item
                ename, tagname, uname = self.oneof_names(o)
                L.append(f"  w.u16(static_cast<std::uint16_t>(m.{tagname}));")
                L.append(f"  switch (m.{tagname}) {{")
                for f in sorted(o.fields, key=lambda x: x.number):
                    L.append(f"    case {name}::{ename}::{_snake_to_upper(f.name)}:")
                    L.append("      " + self.write_elem(f, f"m.{uname}.{f.name}"))
                    L.append("      break;")
                L.append(f"    case {name}::{ename}::NONE:")
                L.append("    default:")
                L.append("      break;")
                L.append("  }")
        L.append("}")
        L.append("")

        # ── decode ──
        #
        # ★★ **decode 无条件写满整个 struct,中途一次也不提前返回**(2026-09-06)。
        #
        # 原先每个字段后跟一句 `if (!r.ok()) return;`。⚠️ 那与 sa_idl_runtime.h
        #   卷首那句承诺**正好相反**:「任一次越界读都会置错误位并返回零值;
        #   后续读取全部短路 ⇒ 解码失败是整条消息作废,不存在部分成功的中间态」。
        #   ⇒ 早退让"作废"的消息里**尾部字段保持未初始化**,而不是零 ——
        #     语义承诺是"全零",生成物的形状给的是"一半真值一半垃圾"。
        #
        # ★ 它是怎么被抓到的:GCC 15 在 `-O2 -Wmaybe-uninitialized` 下看穿了
        #   inline decode 的早退路径,报 `b.turn may be used uninitialized`
        #   (2026-09-06 CI,三平台里**只有 Linux/GCC 红** —— clang 与 MSVC
        #   都不报,不是它们对,是它们的数据流分析没走到这一步)。
        #   ⚠️ 只报第二个字段而不报第一个,恰恰印证了根因:第一个字段在
        #     首次早退**之前**赋值,必然被写;从第二个起才有"不写"的路径。
        #
        # ⇒ 去掉早退后:失败时每次读都短路返回 0 ⇒ **全部标量字段无条件被赋值**。
        #   ★ 这比给 79 个 struct 加默认初始化器(NSDMI)好三处:
        #     ① 零内存成本 —— NSDMI 会让每次声明零初始化整个
        #        `FixedVec<BattleEvent, 256>`(≈10 KB);
        #     ② `is_trivial` 不变 —— NSDMI 会让默认构造非 trivial,削弱 DR-TS1
        #        边界 ② 的「纯 POD」;
        #     ③ 修的是**根因**(早退违背了 runtime 的短路设计),不是给每个消费点补一层。
        #   ⚠️ 失败路径上会多走完剩余字段的短路读,代价是每字段一个分支;
        #     repeated 更便宜 —— count 短路读回 0 ⇒ 循环零次(read_vec)。
        L.append(f"inline void decode(sa::idl::Reader& r, {name}& m) {{")
        if not items:
            L.append("  (void)r; (void)m;")
        for kind, item in items:
            if kind == "field":
                L += self.read_field(item, "m", "  ")
            else:
                o = item
                ename, tagname, uname = self.oneof_names(o)
                L.append("  {")
                # ★ tag 先置 NONE,再按读到的值改写 —— 未知 tag 那条路径
                #   (default)也就不会留下一个未初始化的 tag。
                L.append(f"    m.{tagname} = {name}::{ename}::NONE;")
                L.append("    const std::uint16_t tag = r.u16();")
                L.append("    switch (tag) {")
                for f in sorted(o.fields, key=lambda x: x.number):
                    L.append(f"      case {f.number}:")
                    L.append("        " + self.read_elem(f, f"m.{uname}.{f.name}"))
                    L.append(f"        m.{tagname} = "
                             f"{name}::{ename}::{_snake_to_upper(f.name)};")
                    L.append("        break;")
                L.append("      case 0:")
                L.append("        break;")
                L.append("      default:")
                # ★ 未知 tag 不静默跳过 —— 原版 switch 无 default 导致三个子命令被
                #   静默丢弃(07 §8.4 / DR-CP4)。这里一律判为解码失败。
                # ⚠️ `r.fail()` 之后**不再 return** —— 意图由 fail() 兑现(调用方
                #   检 ok() 得 false),而 return 会让 oneof 之后的字段不被写,
                #   正是本次要修掉的那个形状。
                #   ★ 诚实标注:**当前 schema 下这一改行为不可区分** —— 4 个含
                #     oneof 的消息(BattleEvent / BattleCommand / WindowOpen /
                #     WindowReply)里 oneof 都排在最后。⇒ 它防的是将来往 oneof
                #     之后加字段的那一天,而那时不会有任何东西报错
                #     (idl/tests/smoke.cpp ④ 已把这条标注写在用例里)。
                L.append("        r.fail();")
                L.append("        break;")
                L.append("    }")
                L.append("  }")
        L.append("}")
        L.append("")
        return L

    def emit_enum(self, e: D.Enum) -> list[str]:
        _, name = self.names[e.fqname]
        u = ENUM_UNDERLYING[e.width]
        L = [f"enum class {name} : {u} {{"]
        for v in e.values:
            L.append(f"  {v.name} = {v.number},")
        L.append("};")
        L.append("")
        return L

    # ── 文件级 ────────────────────────────────────────────────
    def deps_of(self, filename: str) -> list[str]:
        deps = set()
        for m in self.s.messages.values():
            if m.file != filename:
                continue
            for f in m.fields:
                if f.ptype in (D.T_MESSAGE, D.T_ENUM):
                    dep = self.file_of[f.type_name]
                    if dep != filename:
                        deps.add(dep)
        return sorted(deps)

    def header_name(self, protofile: str) -> str:
        return protofile[:-len(".proto")] + ".sa.h"

    def emit_file(self, filename: str) -> str:
        guard = ("SA_IDL_" + self.header_name(filename)
                 .replace("/", "_").replace(".", "_").upper())
        L = [
            "// ★ 本文件由 idl/codegen 从 schema 生成，请勿手工编辑。",
            f"// 来源：{filename}",
            "//",
            "// 修改方式：改 schema → 重跑 `python3 idl/codegen/saidl_gen.py` → 提交生成物",
            "// （DR-TS2：生成产物入库，客户端连 protoc 都不需要）。",
            "",
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            '#include "sa_idl_runtime.h"',
        ]
        for dep in self.deps_of(filename):
            L.append(f'#include "{self.header_name(dep)}"')
        L.append("")

        # 本文件内的类型按拓扑序输出；同文件的枚举先于消息
        pkgs = {}
        for e in self.s.enums.values():
            if e.file == filename:
                pkgs.setdefault(e.package, {"enums": [], "msgs": []})["enums"].append(e)
        for m in self.s.order():
            if m.file == filename:
                pkgs.setdefault(m.package, {"enums": [], "msgs": []})["msgs"].append(m)

        for package, group in pkgs.items():
            parts = package.split(".") if package else []
            for p in parts:
                L.append(f"namespace {p} {{")
            L.append("")
            for e in group["enums"]:
                L += self.emit_enum(e)
            for m in group["msgs"]:
                L += self.emit_message(m)
            for p in reversed(parts):
                L.append(f"}}  // namespace {p}")
            L.append("")

        L.append(f"#endif  // {guard}")
        L.append("")
        return "\n".join(L)

    # ── ids.h：编号常量表 ─────────────────────────────────────
    def emit_ids(self) -> str:
        L = [
            "// ★ 本文件由 idl/codegen 生成，请勿手工编辑。",
            "//",
            "// 02-protocol.md §1.1：每个 message 的编号在 IDL 里写死数字，",
            "// 不依赖声明顺序；编号一经发布不得复用。",
            "",
            "#ifndef SA_IDL_IDS_H",
            "#define SA_IDL_IDS_H",
            "",
            "#include <cstdint>",
            "",
            "namespace sa {",
            "namespace idl {",
            "",
            "enum class MsgId : std::uint32_t {",
        ]
        numbered = sorted(
            [m for m in self.s.messages.values() if m.msg_id is not None],
            key=lambda m: m.msg_id)
        for m in numbered:
            _, name = self.names[m.fqname]
            mark = "  // deprecated：占位，不得复用" if m.deprecated_id else ""
            L.append(f"  {name} = 0x{m.msg_id:04X},{mark}")
        L += [
            "};",
            "",
            "// 编译期把消息类型映射到编号：msg_id_of<sa::domain::Foo>()",
            "template <typename T>",
            "struct MsgTraits;",
            "",
            "template <typename T>",
            "constexpr std::uint32_t msg_id_of() {",
            "  return static_cast<std::uint32_t>(MsgTraits<T>::kId);",
            "}",
            "",
            "}  // namespace idl",
            "}  // namespace sa",
            "",
        ]
        seen_headers: set[str] = set()
        for m in numbered:
            if m.deprecated_id:
                continue
            hdr = self.header_name(m.file)
            if hdr in seen_headers:
                continue
            seen_headers.add(hdr)
            L.append(f'#include "{hdr}"')
        L.append("")
        L += ["namespace sa {", "namespace idl {", ""]
        for m in numbered:
            if m.deprecated_id:
                continue
            _, name = self.names[m.fqname]
            L.append("template <>")
            L.append(f"struct MsgTraits<{self.qual(m.fqname)}> {{")
            L.append(f"  static constexpr MsgId kId = MsgId::{name};")
            L.append(f'  static constexpr const char* kName = "{m.fqname}";')
            L.append("};")
            L.append("")
        L += ["}  // namespace idl", "}  // namespace sa", "",
              "#endif  // SA_IDL_IDS_H", ""]
        return "\n".join(L)
