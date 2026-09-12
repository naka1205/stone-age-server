// JSON 内存编解码；Unicode 转义、有限数值与重复键校验。
// 深度上限同时约束读取和写出，供配置、内容与版本化存档使用。

#include "data/Json.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace SA::Data::Json
{
namespace
{

constexpr int kMaxDepth = 32;

class Parser
{
  public:
	explicit Parser(std::string_view text) : _text(text) {}

	ParseOutcome run()
	{
		skipWs();
		Value v;
		if (!parseValue(v, 0))
			return fail();
		skipWs();
		if (_pos != _text.size())
		{
			_error = "顶层值之后还有多余内容";
			return fail();
		}
		ParseOutcome out;
		out.ok = true;
		out.value = std::move(v);
		return out;
	}

  private:
	ParseOutcome fail()
	{
		ParseOutcome out;
		out.ok = false;
		out.error = _error.empty() ? std::string("解析失败") : _error;
		out.offset = _pos;
		out.line = lineAt(_pos);
		return out;
	}

	int lineAt(std::size_t off) const
	{
		int line = 1;
		const std::size_t n = off < _text.size() ? off : _text.size();
		for (std::size_t i = 0; i < n; ++i)
		{
			if (_text[i] == '\n')
				++line;
		}
		return line;
	}

	bool eof() const { return _pos >= _text.size(); }
	char peek() const { return _text[_pos]; }

	void skipWs()
	{
		while (!eof())
		{
			const char c = peek();
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			{
				++_pos;
			}
			else if (c == '/')
			{
				// ★ 明确拒绝而不是跳过:JSON 没有注释,静默容忍会让人以为它是标准的,
				//   然后某天换一个解析器就全线报错。
				_error = "JSON 不支持注释";
				return;
			}
			else
			{
				return;
			}
		}
	}

	bool literal(std::string_view lit)
	{
		if (_text.size() - _pos < lit.size())
			return false;
		if (_text.compare(_pos, lit.size(), lit) != 0)
			return false;
		_pos += lit.size();
		return true;
	}

	bool parseValue(Value &out, int depth)
	{
		if (depth > kMaxDepth)
		{
			_error = "嵌套层数超过上限";
			return false;
		}
		if (eof())
		{
			_error = "内容意外结束";
			return false;
		}
		switch (peek())
		{
		case '{':
			return parseObject(out, depth);
		case '[':
			return parseArray(out, depth);
		case '"':
		{
			std::string s;
			if (!parseString(s))
				return false;
			out = Value::str(std::move(s));
			return true;
		}
		case 't':
			if (!literal("true"))
			{
				_error = "无法识别的字面量";
				return false;
			}
			out = Value::Bool(true);
			return true;
		case 'f':
			if (!literal("false"))
			{
				_error = "无法识别的字面量";
				return false;
			}
			out = Value::Bool(false);
			return true;
		case 'n':
			if (!literal("null"))
			{
				_error = "无法识别的字面量";
				return false;
			}
			out = Value();
			return true;
		default:
			return parseNumber(out);
		}
	}

	bool parseObject(Value &out, int depth)
	{
		++_pos; // '{'
		Object obj;
		skipWs();
		if (!_error.empty())
			return false;
		if (!eof() && peek() == '}')
		{
			++_pos;
			out = Value::obj(std::move(obj));
			return true;
		}
		for (;;)
		{
			skipWs();
			if (!_error.empty())
				return false;
			if (eof() || peek() != '"')
			{
				_error = "对象的键必须是字符串";
				return false;
			}
			std::string key;
			if (!parseString(key))
				return false;
			skipWs();
			if (!_error.empty())
				return false;
			if (eof() || peek() != ':')
			{
				_error = "键之后缺少冒号";
				return false;
			}
			++_pos;
			skipWs();
			if (!_error.empty())
				return false;
			Value v;
			if (!parseValue(v, depth + 1))
				return false;
			// ★ 重复键报错,不是"后者覆盖前者"。配置文件里写重了两次
			//   listen_port 是人为错误,静默取其一正是 00 §10.4 那类静默错误。
			if (obj.find(key) != obj.end())
			{
				_error = "重复的键:" + key;
				return false;
			}
			obj.emplace(std::move(key), std::move(v));
			skipWs();
			if (!_error.empty())
				return false;
			if (eof())
			{
				_error = "对象未闭合";
				return false;
			}
			if (peek() == ',')
			{
				++_pos;
				continue;
			}
			if (peek() == '}')
			{
				++_pos;
				out = Value::obj(std::move(obj));
				return true;
			}
			_error = "对象里缺少逗号或右花括号";
			return false;
		}
	}

	bool parseArray(Value &out, int depth)
	{
		++_pos; // '['
		Array arr;
		skipWs();
		if (!_error.empty())
			return false;
		if (!eof() && peek() == ']')
		{
			++_pos;
			out = Value::arr(std::move(arr));
			return true;
		}
		for (;;)
		{
			skipWs();
			if (!_error.empty())
				return false;
			Value v;
			if (!parseValue(v, depth + 1))
				return false;
			arr.push_back(std::move(v));
			skipWs();
			if (!_error.empty())
				return false;
			if (eof())
			{
				_error = "数组未闭合";
				return false;
			}
			if (peek() == ',')
			{
				++_pos;
				continue;
			}
			if (peek() == ']')
			{
				++_pos;
				out = Value::arr(std::move(arr));
				return true;
			}
			_error = "数组里缺少逗号或右方括号";
			return false;
		}
	}

	bool parseString(std::string &out)
	{
		++_pos; // 开头的引号
		std::string s;
		for (;;)
		{
			if (eof())
			{
				_error = "字符串未闭合";
				return false;
			}
			const char c = _text[_pos++];
			if (c == '"')
			{
				if (!validUtf8(s))
				{
					_error = "字符串不是有效 UTF-8";
					return false;
				}
				out = std::move(s);
				return true;
			}
			if (c != '\\')
			{
				// 控制字符按 JSON 规范必须转义。放行会让配置里一个误入的换行
				// 变成"看起来正常但值不对"。
				if (static_cast<unsigned char>(c) < 0x20)
				{
					_error = "字符串里出现未转义的控制字符";
					return false;
				}
				s.push_back(c);
				continue;
			}
			if (eof())
			{
				_error = "转义符之后内容结束";
				return false;
			}
			const char e = _text[_pos++];
			switch (e)
			{
			case '"':
				s.push_back('"');
				break;
			case '\\':
				s.push_back('\\');
				break;
			case '/':
				s.push_back('/');
				break;
			case 'b':
				s.push_back('\b');
				break;
			case 'f':
				s.push_back('\f');
				break;
			case 'n':
				s.push_back('\n');
				break;
			case 'r':
				s.push_back('\r');
				break;
			case 't':
				s.push_back('\t');
				break;
			case 'u':
			{
				std::uint32_t code = 0;
				if (!hex4(code))
					return false;
				if (code >= 0xd800 && code <= 0xdbff)
				{
					if (_text.size() - _pos < 2 || _text[_pos] != '\\' || _text[_pos + 1] != 'u')
					{
						_error = "UTF-16 高代理项缺少低代理项";
						return false;
					}
					_pos += 2;
					std::uint32_t low = 0;
					if (!hex4(low))
						return false;
					if (low < 0xdc00 || low > 0xdfff)
					{
						_error = "非法 UTF-16 低代理项";
						return false;
					}
					code = 0x10000u + ((code - 0xd800u) << 10u) + low - 0xdc00u;
				}
				else if (code >= 0xdc00 && code <= 0xdfff)
				{
					_error = "孤立 UTF-16 低代理项";
					return false;
				}
				if (code < 0x80)
					s.push_back(static_cast<char>(code));
				else if (code < 0x800)
				{
					s.push_back(static_cast<char>(0xc0u | (code >> 6u)));
					s.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
				}
				else if (code < 0x10000)
				{
					s.push_back(static_cast<char>(0xe0u | (code >> 12u)));
					s.push_back(static_cast<char>(0x80u | ((code >> 6u) & 0x3fu)));
					s.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
				}
				else
				{
					s.push_back(static_cast<char>(0xf0u | (code >> 18u)));
					s.push_back(static_cast<char>(0x80u | ((code >> 12u) & 0x3fu)));
					s.push_back(static_cast<char>(0x80u | ((code >> 6u) & 0x3fu)));
					s.push_back(static_cast<char>(0x80u | (code & 0x3fu)));
				}
				break;
			}
			default:
				_error = "无法识别的转义";
				return false;
			}
		}
	}

	bool hex4(std::uint32_t &out)
	{
		out = 0;
		for (int i = 0; i < 4; ++i)
		{
			if (eof())
			{
				_error = "Unicode 转义不完整";
				return false;
			}
			const char c = _text[_pos++];
			const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
			                                               : c >= 'A' && c <= 'F'   ? c - 'A' + 10
			                                                                        : -1;
			if (digit < 0)
			{
				_error = "Unicode 转义含非十六进制字符";
				return false;
			}
			out = (out << 4u) | static_cast<std::uint32_t>(digit);
		}
		return true;
	}

	bool parseNumber(Value &out)
	{
		const std::size_t start = _pos;
		if (!eof() && peek() == '-')
			++_pos;
		std::size_t digits = 0;
		while (!eof() && peek() >= '0' && peek() <= '9')
		{
			++_pos;
			++digits;
		}
		if (digits == 0)
		{
			_error = "不是合法的值";
			return false;
		}
		if (digits > 1 && _text[start + (_text[start] == '-' ? 1u : 0u)] == '0')
		{
			_error = "数字不能有前导零";
			return false;
		}
		if (!eof() && peek() == '.')
		{
			++_pos;
			std::size_t frac = 0;
			while (!eof() && peek() >= '0' && peek() <= '9')
			{
				++_pos;
				++frac;
			}
			if (frac == 0)
			{
				_error = "小数点后缺少数字";
				return false;
			}
		}
		if (!eof() && (peek() == 'e' || peek() == 'E'))
		{
			++_pos;
			if (!eof() && (peek() == '+' || peek() == '-'))
				++_pos;
			const std::size_t exponent = _pos;
			while (!eof() && peek() >= '0' && peek() <= '9')
				++_pos;
			if (_pos == exponent)
			{
				_error = "指数部分缺少数字";
				return false;
			}
		}
		const std::string token(_text.substr(start, _pos - start));
		std::istringstream stream(token);
		stream.imbue(std::locale::classic());
		double number = 0;
		stream >> number;
		if (stream.fail() || !std::isfinite(number))
		{
			_error = "数字超出有限值范围";
			return false;
		}
		out = Value::number(number);
		return true;
	}

	std::string_view _text;
	std::size_t _pos = 0;
	std::string _error;
};

} // namespace

Value Value::Bool(bool v)
{
	Value out;
	out._type = Type::kBool;
	out._bool = v;
	return out;
}

Value Value::number(double v)
{
	Value out;
	out._type = Type::kNumber;
	out._number = v;
	return out;
}

Value Value::str(std::string v)
{
	Value out;
	out._type = Type::kString;
	out._string = std::move(v);
	return out;
}

Value Value::obj(Object v)
{
	Value out;
	out._type = Type::kObject;
	out._object = std::move(v);
	return out;
}

Value Value::arr(Array v)
{
	Value out;
	out._type = Type::kArray;
	out._array = std::move(v);
	return out;
}

const Value *Value::find(std::string_view key) const
{
	if (_type != Type::kObject)
		return nullptr;
	const auto it = _object.find(std::string(key));
	return it == _object.end() ? nullptr : &it->second;
}

ParseOutcome parse(std::string_view text)
{
	Parser p(text);
	return p.run();
}

bool validUtf8(std::string_view text) noexcept
{
	for (std::size_t offset = 0; offset < text.size();)
	{
		const auto first = static_cast<unsigned char>(text[offset++]);
		if (first < 0x80)
			continue;
		const int length = first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3
		                                                    : first >= 0xf0 && first <= 0xf4   ? 4
		                                                                                       : 0;
		if (!length || text.size() - offset < static_cast<std::size_t>(length - 1))
			return false;
		std::uint32_t point = first & (length == 2 ? 0x1fu : length == 3 ? 0x0fu
		                                                                 : 0x07u);
		for (int i = 1; i < length; ++i)
		{
			const auto next = static_cast<unsigned char>(text[offset++]);
			if ((next & 0xc0u) != 0x80u)
				return false;
			point = (point << 6u) | (next & 0x3fu);
		}
		if ((length == 3 && point < 0x800) || (length == 4 && point < 0x10000) ||
		    (point >= 0xd800 && point <= 0xdfff) || point > 0x10ffff)
			return false;
	}
	return true;
}

namespace
{
void quote(std::string &out, std::string_view text)
{
	if (!validUtf8(text))
		throw std::invalid_argument("JSON invalid UTF-8");
	constexpr char hex[] = "0123456789abcdef";
	out.push_back('"');
	for (char raw : text)
	{
		const auto c = static_cast<unsigned char>(raw);
		if (c == '"' || c == '\\')
		{
			out.push_back('\\');
			out.push_back(raw);
		}
		else if (c < 0x20)
		{
			out += "\\u00";
			out.push_back(hex[c >> 4u]);
			out.push_back(hex[c & 15u]);
		}
		else
			out.push_back(raw);
	}
	out.push_back('"');
}

void encode(const Value &value, std::string &out, int depth)
{
	if (depth > kMaxDepth)
		throw std::invalid_argument("JSON nesting limit");
	switch (value.type())
	{
	case Type::kNull:
		out += "null";
		break;
	case Type::kBool:
		out += value.asBool() ? "true" : "false";
		break;
	case Type::kString:
		quote(out, value.asString());
		break;
	case Type::kNumber:
	{
		if (!std::isfinite(value.asNumber()))
			throw std::invalid_argument("JSON non-finite number");
		std::ostringstream stream;
		stream.imbue(std::locale::classic());
		stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value.asNumber();
		out += stream.str();
		break;
	}
	case Type::kArray:
	{
		out.push_back('[');
		bool first = true;
		for (const auto &item : value.asArray())
		{
			if (!first)
				out.push_back(',');
			first = false;
			encode(item, out, depth + 1);
		}
		out.push_back(']');
		break;
	}
	case Type::kObject:
	{
		out.push_back('{');
		bool first = true;
		for (const auto &item : value.asObject())
		{
			if (!first)
				out.push_back(',');
			first = false;
			quote(out, item.first);
			out.push_back(':');
			encode(item.second, out, depth + 1);
		}
		out.push_back('}');
		break;
	}
	}
}
} // namespace

std::string stringify(const Value &value)
{
	std::string out;
	encode(value, out, 0);
	return out;
}

} // namespace SA::Data::Json
