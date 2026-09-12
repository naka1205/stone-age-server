#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "data/Json.h"
#include "data/RecordJson.h"
#include "domain/character_data.sa.h"
#include <limits>
#include <stdexcept>

using namespace SA::Data::Json;

TEST_CASE("JSON Unicode escapes preserve character names and supplementary characters")
{
	const auto parsed = parse(R"({"name":"\u77f3\u5668\ud83d\udc3e","nul":"\u0000"})");
	REQUIRE(parsed.ok);
	CHECK(parsed.value.find("name")->asString() == "石器🐾");
	CHECK(parsed.value.find("nul")->asString() == std::string(1, '\0'));
	CHECK_FALSE(parse(std::string("\"") + "\xc0\xaf" + "\"").ok);
	CHECK_FALSE(validUtf8("\xf4\x90\x80\x80"));
	CHECK_FALSE(validUtf8("\xed\xa0\x80"));
	CHECK(validUtf8("石器🐾"));
	for (const auto *bad : {R"("\ud800")", R"("\udc00")", R"("\ud800\u1234")", R"("\u12xz")", R"("\u123")"})
	{
		INFO(bad);
		CHECK_FALSE(parse(bad).ok);
	}
}

TEST_CASE("JSON numbers follow the grammar and reject non-finite values")
{
	CHECK(parse("-1.25e+2").value.asNumber() == -125.0);
	CHECK(parse("1E-3").value.asNumber() == doctest::Approx(0.001));
	for (const auto *bad : {"01", "-01", "1.", "1e", "1e+", "+1", "NaN", "Infinity", "1e9999", "1 2"})
	{
		INFO(bad);
		CHECK_FALSE(parse(bad).ok);
	}
	CHECK_THROWS_AS(stringify(Value::number(std::numeric_limits<double>::infinity())), std::invalid_argument);
}

TEST_CASE("JSON round trip retains 64-bit decimal identifiers and escaped content")
{
	const auto original = Value::obj({{"id", Value::str("18446744073709551615")},
	                                  {"name", Value::str("石器\n\"\\\t")},
	                                  {"items", Value::arr({Value(), Value::Bool(true), Value::number(0.125)})}});
	const auto encoded = stringify(original);
	const auto parsed = parse(encoded);
	REQUIRE(parsed.ok);
	CHECK(parsed.value.find("id")->asString() == "18446744073709551615");
	CHECK(parsed.value.find("name")->asString() == "石器\n\"\\\t");
	CHECK(stringify(parsed.value) == encoded);
	CHECK_FALSE(parse(R"({"id":"1","id":"2"})").ok);
	CHECK_FALSE(parse(std::string(100, '[') + "0" + std::string(100, ']')).ok);
}

TEST_CASE("IDL-generated persistence round trips typed records and rejects schema drift")
{
	SA::Domain::CharacterRecord record{};
	record.schema_ver = 1;
	record.char_id = std::numeric_limits<std::uint64_t>::max();
	(void)record.player.name.assign("石器");
	record.player.default_pet = -1;
	using Codec = SA::Data::JsonCodec<SA::Domain::CharacterRecord>;
	const auto encoded = Codec::encode(record);
	const auto decoded = Codec::decode(parse(stringify(encoded)).value);
	CHECK(decoded.char_id == record.char_id);
	CHECK(decoded.player.default_pet == -1);
	CHECK(std::string(decoded.player.name.c_str()) == "石器");
	auto bad = encoded.asObject();
	bad["char_id"] = Value::number(1);
	CHECK_THROWS_AS(Codec::decode(Value::obj(bad)), std::invalid_argument);
	bad = encoded.asObject();
	bad.erase("schema_ver");
	CHECK_THROWS_AS(Codec::decode(Value::obj(bad)), std::invalid_argument);
	bad = encoded.asObject();
	bad["unmodelled"] = Value::number(1);
	CHECK_THROWS_AS(Codec::decode(Value::obj(bad)), std::invalid_argument);
}
