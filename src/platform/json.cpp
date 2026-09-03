// src/platform/json.cpp —— 最小 JSON 子集解析器
//
// 递归下降。★ 深度有硬上限:JSON 是递归结构,不设限就是一条
//   「构造一个两万层嵌套的配置文件把服务端栈爆掉」的路径。
//   配置文件的真实深度是 2(顶层对象 + tempo 子对象)。

#include "internal/json.h"

#include <cstdlib>
#include <utility>

namespace sa::platform::json {
namespace {

// 真实需求是 2 层。给到 32 是留余量,不是留给"以后可能很深"。
constexpr int kMaxDepth = 32;

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  ParseOutcome Run() {
    SkipWs();
    Value v;
    if (!ParseValue(v, 0)) return Fail();
    SkipWs();
    if (pos_ != text_.size()) {
      error_ = "顶层值之后还有多余内容";
      return Fail();
    }
    ParseOutcome out;
    out.ok = true;
    out.value = std::move(v);
    return out;
  }

 private:
  ParseOutcome Fail() {
    ParseOutcome out;
    out.ok = false;
    out.error = error_.empty() ? std::string("解析失败") : error_;
    out.offset = pos_;
    out.line = LineAt(pos_);
    return out;
  }

  int LineAt(std::size_t off) const {
    int line = 1;
    const std::size_t n = off < text_.size() ? off : text_.size();
    for (std::size_t i = 0; i < n; ++i) {
      if (text_[i] == '\n') ++line;
    }
    return line;
  }

  bool Eof() const { return pos_ >= text_.size(); }
  char Peek() const { return text_[pos_]; }

  void SkipWs() {
    while (!Eof()) {
      const char c = Peek();
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else if (c == '/') {
        // ★ 明确拒绝而不是跳过:JSON 没有注释,静默容忍会让人以为它是标准的,
        //   然后某天换一个解析器就全线报错。
        error_ = "JSON 不支持注释";
        return;
      } else {
        return;
      }
    }
  }

  bool Literal(std::string_view lit) {
    if (text_.size() - pos_ < lit.size()) return false;
    if (text_.compare(pos_, lit.size(), lit) != 0) return false;
    pos_ += lit.size();
    return true;
  }

  bool ParseValue(Value& out, int depth) {
    if (depth > kMaxDepth) {
      error_ = "嵌套层数超过上限";
      return false;
    }
    if (Eof()) {
      error_ = "内容意外结束";
      return false;
    }
    switch (Peek()) {
      case '{': return ParseObject(out, depth);
      case '[': return ParseArray(out, depth);
      case '"': {
        std::string s;
        if (!ParseString(s)) return false;
        out = Value::Str(std::move(s));
        return true;
      }
      case 't':
        if (!Literal("true")) { error_ = "无法识别的字面量"; return false; }
        out = Value::Bool(true);
        return true;
      case 'f':
        if (!Literal("false")) { error_ = "无法识别的字面量"; return false; }
        out = Value::Bool(false);
        return true;
      case 'n':
        if (!Literal("null")) { error_ = "无法识别的字面量"; return false; }
        out = Value();
        return true;
      default: return ParseNumber(out);
    }
  }

  bool ParseObject(Value& out, int depth) {
    ++pos_;  // '{'
    Object obj;
    SkipWs();
    if (!error_.empty()) return false;
    if (!Eof() && Peek() == '}') {
      ++pos_;
      out = Value::Obj(std::move(obj));
      return true;
    }
    for (;;) {
      SkipWs();
      if (!error_.empty()) return false;
      if (Eof() || Peek() != '"') {
        error_ = "对象的键必须是字符串";
        return false;
      }
      std::string key;
      if (!ParseString(key)) return false;
      SkipWs();
      if (!error_.empty()) return false;
      if (Eof() || Peek() != ':') {
        error_ = "键之后缺少冒号";
        return false;
      }
      ++pos_;
      SkipWs();
      if (!error_.empty()) return false;
      Value v;
      if (!ParseValue(v, depth + 1)) return false;
      // ★ 重复键报错,不是"后者覆盖前者"。配置文件里写重了两次
      //   listen_port 是人为错误,静默取其一正是 00 §10.4 那类静默错误。
      if (obj.find(key) != obj.end()) {
        error_ = "重复的键:" + key;
        return false;
      }
      obj.emplace(std::move(key), std::move(v));
      SkipWs();
      if (!error_.empty()) return false;
      if (Eof()) {
        error_ = "对象未闭合";
        return false;
      }
      if (Peek() == ',') {
        ++pos_;
        continue;
      }
      if (Peek() == '}') {
        ++pos_;
        out = Value::Obj(std::move(obj));
        return true;
      }
      error_ = "对象里缺少逗号或右花括号";
      return false;
    }
  }

  bool ParseArray(Value& out, int depth) {
    ++pos_;  // '['
    Array arr;
    SkipWs();
    if (!error_.empty()) return false;
    if (!Eof() && Peek() == ']') {
      ++pos_;
      out = Value::Arr(std::move(arr));
      return true;
    }
    for (;;) {
      SkipWs();
      if (!error_.empty()) return false;
      Value v;
      if (!ParseValue(v, depth + 1)) return false;
      arr.push_back(std::move(v));
      SkipWs();
      if (!error_.empty()) return false;
      if (Eof()) {
        error_ = "数组未闭合";
        return false;
      }
      if (Peek() == ',') {
        ++pos_;
        continue;
      }
      if (Peek() == ']') {
        ++pos_;
        out = Value::Arr(std::move(arr));
        return true;
      }
      error_ = "数组里缺少逗号或右方括号";
      return false;
    }
  }

  bool ParseString(std::string& out) {
    ++pos_;  // 开头的引号
    std::string s;
    for (;;) {
      if (Eof()) {
        error_ = "字符串未闭合";
        return false;
      }
      const char c = text_[pos_++];
      if (c == '"') {
        out = std::move(s);
        return true;
      }
      if (c != '\\') {
        // 控制字符按 JSON 规范必须转义。放行会让配置里一个误入的换行
        // 变成"看起来正常但值不对"。
        if (static_cast<unsigned char>(c) < 0x20) {
          error_ = "字符串里出现未转义的控制字符";
          return false;
        }
        s.push_back(c);
        continue;
      }
      if (Eof()) {
        error_ = "转义符之后内容结束";
        return false;
      }
      const char e = text_[pos_++];
      switch (e) {
        case '"': s.push_back('"'); break;
        case '\\': s.push_back('\\'); break;
        case '/': s.push_back('/'); break;
        case 'b': s.push_back('\b'); break;
        case 'f': s.push_back('\f'); break;
        case 'n': s.push_back('\n'); break;
        case 'r': s.push_back('\r'); break;
        case 't': s.push_back('\t'); break;
        case 'u':
          // 见头文件:有意不支持。报错优于解错。
          error_ = "不支持 \\u 转义";
          return false;
        default:
          error_ = "无法识别的转义";
          return false;
      }
    }
  }

  bool ParseNumber(Value& out) {
    const std::size_t start = pos_;
    if (!Eof() && Peek() == '-') ++pos_;
    std::size_t digits = 0;
    while (!Eof() && Peek() >= '0' && Peek() <= '9') {
      ++pos_;
      ++digits;
    }
    if (digits == 0) {
      error_ = "不是合法的值";
      return false;
    }
    bool fractional = false;
    if (!Eof() && Peek() == '.') {
      fractional = true;
      ++pos_;
      std::size_t frac = 0;
      while (!Eof() && Peek() >= '0' && Peek() <= '9') {
        ++pos_;
        ++frac;
      }
      if (frac == 0) {
        error_ = "小数点后缺少数字";
        return false;
      }
    }
    if (!Eof() && (Peek() == 'e' || Peek() == 'E')) {
      // 见头文件:有意不支持。
      error_ = "不支持指数写法";
      return false;
    }
    (void)fractional;
    const std::string token(text_.substr(start, pos_ - start));
    out = Value::Number(std::strtod(token.c_str(), nullptr));
    return true;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::string error_;
};

}  // namespace

Value Value::Bool(bool v) {
  Value out;
  out.type_ = Type::kBool;
  out.bool_ = v;
  return out;
}

Value Value::Number(double v) {
  Value out;
  out.type_ = Type::kNumber;
  out.number_ = v;
  return out;
}

Value Value::Str(std::string v) {
  Value out;
  out.type_ = Type::kString;
  out.string_ = std::move(v);
  return out;
}

Value Value::Obj(Object v) {
  Value out;
  out.type_ = Type::kObject;
  out.object_ = std::move(v);
  return out;
}

Value Value::Arr(Array v) {
  Value out;
  out.type_ = Type::kArray;
  out.array_ = std::move(v);
  return out;
}

const Value* Value::Find(std::string_view key) const {
  if (type_ != Type::kObject) return nullptr;
  const auto it = object_.find(std::string(key));
  return it == object_.end() ? nullptr : &it->second;
}

ParseOutcome Parse(std::string_view text) {
  Parser p(text);
  return p.Run();
}

}  // namespace sa::platform::json
