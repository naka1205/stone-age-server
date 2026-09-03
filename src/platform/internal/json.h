// src/platform/internal/json.h —— 最小 JSON 子集解析器(**私有头**)
//
// ⚠️ 本文件不在 include/ 下 ⇒ 只有 sa_platform 自己看得见(00 §3.1)。
//
// ★ 为什么自己写而不是拉一个库(2026-09-03 用户裁定「先不做 schema 层」):
//   它会是服务端 src/ 的**第一个运行时第三方依赖**,而需求只是
//   「读一个十来项的配置文件」。与 DR-TS1 选「自写 codegen、运行时零依赖」同一取向。
//
// ── 有意不支持的东西(不是漏了)────────────────────────────────
//   · \u 转义 —— 配置里不会出现;真需要时报错比悄悄解错好
//   · 浮点指数写法 —— 配置项目前全是整数与布尔
//   · 注释 —— JSON 本就没有;要写说明请用相邻的键
//   ⇒ 上述任一出现都会**解析失败并给出位置**,不会被静默忽略。
//     这与 00 §10.4「三类静默错误」的取向一致:宁可拒绝,不可装作看懂了。

#ifndef SA_PLATFORM_INTERNAL_JSON_H
#define SA_PLATFORM_INTERNAL_JSON_H

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sa::platform::json {

class Value;

using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

enum class Type : std::uint8_t { kNull, kBool, kNumber, kString, kObject, kArray };

class Value {
 public:
  Value() = default;

  static Value Bool(bool v);
  static Value Number(double v);
  static Value Str(std::string v);
  static Value Obj(Object v);
  static Value Arr(Array v);

  Type type() const noexcept { return type_; }
  bool is_null() const noexcept { return type_ == Type::kNull; }
  bool is_bool() const noexcept { return type_ == Type::kBool; }
  bool is_number() const noexcept { return type_ == Type::kNumber; }
  bool is_string() const noexcept { return type_ == Type::kString; }
  bool is_object() const noexcept { return type_ == Type::kObject; }
  bool is_array() const noexcept { return type_ == Type::kArray; }

  bool as_bool() const noexcept { return bool_; }
  double as_number() const noexcept { return number_; }
  const std::string& as_string() const noexcept { return string_; }
  const Object& as_object() const noexcept { return object_; }
  const Array& as_array() const noexcept { return array_; }

  // 找不到返回 nullptr。★ 不提供「找不到给默认值」的重载 ——
  //   那会让「键写错了」和「键没写」变成同一件事,而前者是配置错误。
  const Value* Find(std::string_view key) const;

 private:
  Type type_ = Type::kNull;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  Object object_;
  Array array_;
};

struct ParseOutcome {
  bool ok = false;
  Value value{};
  std::string error;      // 人读的原因
  std::size_t offset = 0; // 出错的字节位置
  int line = 1;           // 1 起
};

ParseOutcome Parse(std::string_view text);

}  // namespace sa::platform::json

#endif  // SA_PLATFORM_INTERNAL_JSON_H
