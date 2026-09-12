#ifndef __SA_RecordJson_H__
#define __SA_RecordJson_H__
#include "data/Json.h"
#include "sa_idl_runtime.h"
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace SA::Data
{
// Field names come from IDL-generated visitors, not a second persistence schema.
template <typename T, typename Enable = void>
struct JsonCodec
{
	static Json::Value encode(const T &value)
	{
		Json::Object out;
		visitFields(value, [&](const char *name, const auto &field)
		            { out.emplace(name, JsonCodec<std::decay_t<decltype(field)>>::encode(field)); });
		return Json::Value::obj(std::move(out));
	}
	static T decode(const Json::Value &value)
	{
		if (!value.isObject())
			throw std::invalid_argument("record must be an object");
		T out{};
		std::size_t fields = 0;
		visitFields(out, [&](const char *name, auto &field)
		            {
			            const auto *item = value.find(name);
			            if (!item) throw std::invalid_argument(std::string("missing field: ") + name);
			            field = JsonCodec<std::decay_t<decltype(field)>>::decode(*item);
			            ++fields; });
		if (fields != value.asObject().size())
			throw std::invalid_argument("unknown record field");
		return out;
	}
};

template <typename T>
struct JsonCodec<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
{
	static Json::Value encode(T value)
	{
		if constexpr (sizeof(T) > 4)
			return Json::Value::str(std::to_string(value));
		else
			return Json::Value::number(static_cast<double>(value));
	}
	static T decode(const Json::Value &value)
	{
		if constexpr (sizeof(T) > 4)
		{
			if (!value.isString())
				throw std::invalid_argument("64-bit integer must be a decimal string");
			T out{};
			const auto &text = value.asString();
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
			if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || std::to_string(out) != text)
				throw std::invalid_argument("invalid decimal identifier");
			return out;
		}
		else
		{
			const double n = value.asNumber();
			if (!value.isNumber() || !std::isfinite(n) || std::trunc(n) != n ||
			    n < static_cast<double>(std::numeric_limits<T>::min()) || n > static_cast<double>(std::numeric_limits<T>::max()))
				throw std::invalid_argument("integer out of range");
			return static_cast<T>(n);
		}
	}
};

template <>
struct JsonCodec<bool>
{
	static Json::Value encode(bool value) { return Json::Value::Bool(value); }
	static bool decode(const Json::Value &value)
	{
		if (!value.isBool())
			throw std::invalid_argument("boolean required");
		return value.asBool();
	}
};

template <std::size_t N>
struct JsonCodec<SA::IDL::FixedStr<N>>
{
	static Json::Value encode(const SA::IDL::FixedStr<N> &value)
	{
		if (value.size() > N)
			throw std::invalid_argument("invalid bounded string size");
		return Json::Value::str(std::string(value.data, value.size()));
	}
	static SA::IDL::FixedStr<N> decode(const Json::Value &value)
	{
		SA::IDL::FixedStr<N> out{};
		if (!value.isString() || value.asString().size() > N || value.asString().find('\0') != std::string::npos || !Json::validUtf8(value.asString()))
			throw std::invalid_argument("invalid bounded string");
		(void)out.assign(value.asString().data(), value.asString().size());
		return out;
	}
};

template <typename T, std::size_t N>
struct JsonCodec<SA::IDL::FixedVec<T, N>>
{
	static Json::Value encode(const SA::IDL::FixedVec<T, N> &value)
	{
		if (value.size() > N)
			throw std::invalid_argument("invalid bounded array size");
		Json::Array out;
		for (const auto &item : value)
			out.push_back(JsonCodec<T>::encode(item));
		return Json::Value::arr(std::move(out));
	}
	static SA::IDL::FixedVec<T, N> decode(const Json::Value &value)
	{
		if (!value.isArray() || value.asArray().size() > N)
			throw std::invalid_argument("array out of bounds");
		SA::IDL::FixedVec<T, N> out{};
		for (const auto &item : value.asArray())
			(void)out.push_back(JsonCodec<T>::decode(item));
		return out;
	}
};
} // namespace SA::Data
#endif
