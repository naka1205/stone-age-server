// 双端 JSON 内存编解码；独立 sa_data，无文件、网络或数据库 I/O。
// 64 位标识使用十进制字符串，避免 double 超过 2^53 丢精度。
#ifndef __SA_Json_H__
#define __SA_Json_H__

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace SA::Data::Json
{

class Value;

using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

enum class Type : std::uint8_t
{
	kNull,
	kBool,
	kNumber,
	kString,
	kObject,
	kArray
};

class Value
{
  public:
	Value() = default;

	static Value Bool(bool v);
	static Value number(double v);
	static Value str(std::string v);
	static Value obj(Object v);
	static Value arr(Array v);

	Type type() const noexcept { return _type; }
	bool isNull() const noexcept { return _type == Type::kNull; }
	bool isBool() const noexcept { return _type == Type::kBool; }
	bool isNumber() const noexcept { return _type == Type::kNumber; }
	bool isString() const noexcept { return _type == Type::kString; }
	bool isObject() const noexcept { return _type == Type::kObject; }
	bool isArray() const noexcept { return _type == Type::kArray; }

	bool asBool() const noexcept { return _bool; }
	double asNumber() const noexcept { return _number; }
	const std::string &asString() const noexcept { return _string; }
	const Object &asObject() const noexcept { return _object; }
	const Array &asArray() const noexcept { return _array; }

	// 找不到返回 nullptr。★ 不提供「找不到给默认值」的重载 ——
	//   那会让「键写错了」和「键没写」变成同一件事,而前者是配置错误。
	const Value *find(std::string_view key) const;

  private:
	Type _type = Type::kNull;
	bool _bool = false;
	double _number = 0.0;
	std::string _string;
	Object _object;
	Array _array;
};

struct ParseOutcome
{
	bool ok = false;
	Value value{};
	std::string error;      // 人读的原因
	std::size_t offset = 0; // 出错的字节位置
	int line = 1;           // 1 起
};

ParseOutcome parse(std::string_view text);
bool validUtf8(std::string_view text) noexcept;
std::string stringify(const Value &value);

} // namespace SA::Data::Json

#endif // __SA_Json_H__
