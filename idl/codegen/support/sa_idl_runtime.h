// sg_idl_runtime.h —— IDL 生成代码的运行时支撑
//
// ★ 本文件是**手写**的,不是生成物;codegen 只负责把它复制进 idl/generated/cpp/。
//
// 三条约束(DR-TS1 §1.1 边界):
//   ① 运行时不链接 libprotobuf —— 本文件只 include <cstdint>/<cstddef>/<cstring>;
//   ② 生成产物必须是 POD —— FixedStr/FixedVec 均为聚合体,无用户构造/析构/虚函数,
//      trivially copyable,可直接 memcpy、可放进 union;
//   ③ shared/ 只依赖标准库 —— 本文件满足,故 shared/rules 可依赖生成的事件类型(02 §9)。
//
// ⚠️ 解码路径上的每一处长度都必须校验。原版 `szAllBattleString` 的 1024 B 缓冲
//    用 strncat 第三参写错、等价无上界 strcat,余量仅 56 字节(07 §10.4)。
//    本文件的 Reader 不提供任何「不校验」的读取入口。

#ifndef SG_IDL_RUNTIME_H
#define SG_IDL_RUNTIME_H

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sg {
namespace idl {

// ── 定长字符串 ────────────────────────────────────────────────
// N = 最大字节数(不含结尾 NUL)。存储多留 1 字节,保证 c_str() 永远可用。
template <std::size_t N>
struct FixedStr {
  std::uint16_t len;
  char data[N + 1];

  static constexpr std::size_t capacity() { return N; }

  const char* c_str() const { return data; }
  std::size_t size() const { return len; }
  bool empty() const { return len == 0; }

  // 超长即失败,不截断。截断会让「数据看起来正常但内容错了」——正是静默错误。
  bool assign(const char* s, std::size_t n) {
    if (n > N) return false;
    std::memcpy(data, s, n);
    data[n] = '\0';
    len = static_cast<std::uint16_t>(n);
    return true;
  }
  bool assign(const char* s) { return assign(s, std::strlen(s)); }
};

// ── 定长向量 ──────────────────────────────────────────────────
template <typename T, std::size_t N>
struct FixedVec {
  std::uint16_t count;
  T data[N];

  static constexpr std::size_t capacity() { return N; }

  std::size_t size() const { return count; }
  bool empty() const { return count == 0; }
  bool full() const { return count >= N; }

  T& operator[](std::size_t i) { return data[i]; }
  const T& operator[](std::size_t i) const { return data[i]; }

  T* begin() { return data; }
  T* end() { return data + count; }
  const T* begin() const { return data; }
  const T* end() const { return data + count; }

  // 返回新元素指针;满则返回 nullptr。调用方必须检查。
  T* push_back() {
    if (count >= N) return nullptr;
    return &data[count++];
  }
  bool push_back(const T& v) {
    T* slot = push_back();
    if (!slot) return false;
    *slot = v;
    return true;
  }
  void clear() { count = 0; }
};

// ── 写入器 ────────────────────────────────────────────────────
// 写满即置错误位并停止写入,不越界。调用方在收尾时检查 ok()。
class Writer {
 public:
  Writer(std::uint8_t* buf, std::size_t cap) : buf_(buf), cap_(cap) {}

  bool ok() const { return ok_; }
  std::size_t size() const { return pos_; }
  std::uint8_t* data() { return buf_; }

  void raw(const void* p, std::size_t n) {
    if (!ok_) return;
    if (pos_ + n > cap_) { ok_ = false; return; }
    std::memcpy(buf_ + pos_, p, n);
    pos_ += n;
  }

  void u8(std::uint8_t v) { raw(&v, 1); }
  void u16(std::uint16_t v) { le(v, 2); }
  void u32(std::uint32_t v) { le(v, 4); }
  void u64(std::uint64_t v) { le(v, 8); }
  void i8(std::int8_t v) { u8(static_cast<std::uint8_t>(v)); }
  void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void b(bool v) { u8(v ? 1 : 0); }
  void f32(float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    u32(bits);
  }

 private:
  void le(std::uint64_t v, std::size_t n) {
    if (!ok_) return;
    if (pos_ + n > cap_) { ok_ = false; return; }
    for (std::size_t i = 0; i < n; ++i)
      buf_[pos_ + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
    pos_ += n;
  }

  std::uint8_t* buf_;
  std::size_t cap_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

// ── 读取器 ────────────────────────────────────────────────────
// 任一次越界读都会置错误位并返回零值;后续读取全部短路。
// ⇒ 解码失败是「整条消息作废」,不存在「部分成功」的中间态。
class Reader {
 public:
  Reader(const std::uint8_t* buf, std::size_t len) : buf_(buf), len_(len) {}

  bool ok() const { return ok_; }
  std::size_t remaining() const { return ok_ ? len_ - pos_ : 0; }
  std::size_t consumed() const { return pos_; }
  void fail() { ok_ = false; }

  bool raw(void* out, std::size_t n) {
    if (!ok_) return false;
    if (pos_ + n > len_) { ok_ = false; return false; }
    std::memcpy(out, buf_ + pos_, n);
    pos_ += n;
    return true;
  }
  const std::uint8_t* take(std::size_t n) {
    if (!ok_) return nullptr;
    if (pos_ + n > len_) { ok_ = false; return nullptr; }
    const std::uint8_t* p = buf_ + pos_;
    pos_ += n;
    return p;
  }

  std::uint8_t u8() {
    std::uint8_t v = 0;
    raw(&v, 1);
    return v;
  }
  std::uint16_t u16() { return static_cast<std::uint16_t>(le(2)); }
  std::uint32_t u32() { return static_cast<std::uint32_t>(le(4)); }
  std::uint64_t u64() { return le(8); }
  std::int8_t i8() { return static_cast<std::int8_t>(u8()); }
  std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  bool b() { return u8() != 0; }
  float f32() {
    std::uint32_t bits = u32();
    float v = 0.0f;
    std::memcpy(&v, &bits, 4);
    return v;
  }

 private:
  std::uint64_t le(std::size_t n) {
    if (!ok_) return 0;
    if (pos_ + n > len_) { ok_ = false; return 0; }
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < n; ++i)
      v |= static_cast<std::uint64_t>(buf_[pos_ + i]) << (8 * i);
    pos_ += n;
    return v;
  }

  const std::uint8_t* buf_;
  std::size_t len_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

// ── 生成代码调用的字符串 / 向量编解码 ──────────────────────────

template <std::size_t N>
inline void write_str(Writer& w, const FixedStr<N>& s) {
  w.u16(s.len);
  w.raw(s.data, s.len);
}

template <std::size_t N>
inline void read_str(Reader& r, FixedStr<N>& s) {
  std::uint16_t n = r.u16();
  if (!r.ok()) return;
  if (n > N) {  // ★ 上限校验:超限即整条消息作废,不截断
    r.fail();
    return;
  }
  const std::uint8_t* p = r.take(n);
  if (!p) return;
  std::memcpy(s.data, p, n);
  s.data[n] = '\0';
  s.len = n;
}

// 元素编解码由生成代码以 lambda 形式传入,避免为标量与消息各写一套。
template <typename T, std::size_t N, typename F>
inline void write_vec(Writer& w, const FixedVec<T, N>& v, F write_elem) {
  w.u16(v.count);
  for (std::size_t i = 0; i < v.count; ++i) write_elem(w, v.data[i]);
}

template <typename T, std::size_t N, typename F>
inline void read_vec(Reader& r, FixedVec<T, N>& v, F read_elem) {
  std::uint16_t n = r.u16();
  if (!r.ok()) return;
  if (n > N) {  // ★ 上限校验
    r.fail();
    return;
  }
  for (std::size_t i = 0; i < n; ++i) {
    read_elem(r, v.data[i]);
    if (!r.ok()) return;
  }
  v.count = n;
}

}  // namespace idl
}  // namespace sg

#endif  // SG_IDL_RUNTIME_H
