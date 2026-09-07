// src/platform/json.cpp —— 最小 JSON 子集解析器
//
// 递归下降。★ 深度有硬上限:JSON 是递归结构,不设限就是一条
//   「构造一个两万层嵌套的配置文件把服务端栈爆掉」的路径。
//   配置文件的真实深度是 2(顶层对象 + tempo 子对象)。

#include "internal/Json.h"

#include <cstdlib>
#include <utility>

namespace SA::Platform::json
{
namespace
{

// 真实需求是 2 层。给到 32 是留余量,不是留给"以后可能很深"。
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
				// 见头文件:有意不支持。报错优于解错。
				_error = "不支持 \\u 转义";
				return false;
			default:
				_error = "无法识别的转义";
				return false;
			}
		}
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
		bool fractional = false;
		if (!eof() && peek() == '.')
		{
			fractional = true;
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
			// 见头文件:有意不支持。
			_error = "不支持指数写法";
			return false;
		}
		(void)fractional;
		const std::string token(_text.substr(start, _pos - start));
		out = Value::number(std::strtod(token.c_str(), nullptr));
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

} // namespace SA::Platform::json
