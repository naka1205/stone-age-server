// tests/WorldLeaderboardTest.cpp —— 阶段 14: 尼斯大陆排行榜与全服荣誉殿堂测试

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <string>

#include "rules/LeaderboardRank.h"
#include "world/Api.h"

using namespace SA::Rules;
using namespace SA::World;

namespace
{

SA::Platform::ServerConfig makeTestConfig()
{
	const SA::Platform::ConfigResult r = SA::Platform::parseConfig(R"({
    "protocol_version": 1, "log_level": "error",
    "tempo": { "tick_hz": 100, "battle_turn_interval_ms": 1000 }
  })");
	REQUIRE(r.ok);
	return r.config;
}

struct LeaderboardFixture
{
	SA::Platform::ServerConfig config = makeTestConfig();
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random_source{0x123456};
	SA::Net::LoopbackTransport transport{};
	SA::World::World world{config, clock, logger, random_source, transport};

	SA::Net::SessionId createPlayer(const std::string &name, int level = 50, std::int32_t gold = 100000)
	{
		const auto session = transport.connect();
		world.onSessionReady(session);
		auto *p = world.playerForTest(session);
		REQUIRE(p != nullptr);
		p->name.assign(name.c_str());
		p->level = level;
		p->hp = 500;
		p->gold = gold;
		p->floor = 1000;
		p->x = 20;
		p->y = 20;
		return session;
	}
};

} // namespace

TEST_CASE("尼斯大陆全域排行榜与全服荣誉殿堂体系 (Phase 14)")
{
	SUBCASE("排行榜条目确定性排序与同分打破僵局纯函数 (sortLeaderboardRecords)")
	{
		LeaderboardRecord records[4] = {
		    {1002, "勇者乙", 0, 8000, "巫师"},
		    {1001, "勇者甲", 0, 9500, "狂战士"},
		    {1004, "勇者丁", 0, 7200, "猎人"},
		    {1003, "勇者丙", 0, 8000, "游侠"}, // 与乙同分 8000，但 entity_id 1003 > 1002
		};

		sortLeaderboardRecords(records, 4);

		// 1. 降序排名验证
		CHECK(records[0].entity_id == 1001);
		CHECK(records[0].rank == 1);
		CHECK(records[0].score == 9500);

		// 2. 同分打破僵局: entity_id 较小者排前
		CHECK(records[1].entity_id == 1002);
		CHECK(records[1].rank == 2);
		CHECK(records[1].score == 8000);

		CHECK(records[2].entity_id == 1003);
		CHECK(records[2].rank == 3);
		CHECK(records[2].score == 8000);

		// 3. 末位
		CHECK(records[3].entity_id == 1004);
		CHECK(records[3].rank == 4);
		CHECK(records[3].score == 7200);
	}

	SUBCASE("荣誉殿堂专属称号与四维属性加成纯函数 (computeHallOfFameBonus)")
	{
		// 1. 等级榜状元 (rank 1)
		auto b_lvl1 = computeHallOfFameBonus(LeaderboardKind::kLevel, 1);
		CHECK(b_lvl1.attack_bonus_percent == doctest::Approx(5.0f));
		CHECK(b_lvl1.defense_bonus_percent == doctest::Approx(5.0f));
		CHECK(b_lvl1.title_id == 1001);
		CHECK(std::string(b_lvl1.title_name) == "尼斯传奇怪兽猎人");

		// 2. 等级榜榜眼 (rank 2)
		auto b_lvl2 = computeHallOfFameBonus(LeaderboardKind::kLevel, 2);
		CHECK(b_lvl2.attack_bonus_percent == doctest::Approx(3.0f));
		CHECK(b_lvl2.title_id == 1002);

		// 3. 等级榜探花 (rank 3)
		auto b_lvl3 = computeHallOfFameBonus(LeaderboardKind::kLevel, 3);
		CHECK(b_lvl3.attack_bonus_percent == doctest::Approx(2.0f));
		CHECK(b_lvl3.title_id == 1003);

		// 4. 声望榜状元
		auto b_fame1 = computeHallOfFameBonus(LeaderboardKind::kFame, 1);
		CHECK(b_fame1.title_id == 1011);
		CHECK(std::string(b_fame1.title_name) == "天下名扬四海之主");

		// 5. 庄园战榜状元
		auto b_manor1 = computeHallOfFameBonus(LeaderboardKind::kManorWins, 1);
		CHECK(b_manor1.title_id == 1031);
		CHECK(std::string(b_manor1.title_name) == "四大庄园永恒守护者");

		// 6. 第 4 名及未上榜人员无加成
		auto b_rank4 = computeHallOfFameBonus(LeaderboardKind::kLevel, 4);
		CHECK(b_rank4.title_id == 0);
		CHECK(b_rank4.attack_bonus_percent == doctest::Approx(0.0f));
	}

	SUBCASE("大世界排行榜数据聚合、更新与截断限制 (World::updateLeaderboardEntry & getLeaderboard)")
	{
		LeaderboardFixture fix;

		// 插入声望榜
		fix.world.updateLeaderboardEntry(LeaderboardKind::kFame, 1001, "萨姆族长", 5000, "萨姆吉尔");
		fix.world.updateLeaderboardEntry(LeaderboardKind::kFame, 1002, "渔村长老", 8500, "玛丽娜斯");
		fix.world.updateLeaderboardEntry(LeaderboardKind::kFame, 1003, "飞龙勇士", 6200, "加加");

		auto lb = fix.world.getLeaderboard(LeaderboardKind::kFame, 10);
		REQUIRE(lb.size() == 3);
		CHECK(lb[0].entity_id == 1002);
		CHECK(lb[0].rank == 1);
		CHECK(lb[0].score == 8500);

		CHECK(lb[1].entity_id == 1003);
		CHECK(lb[1].rank == 2);
		CHECK(lb[1].score == 6200);

		CHECK(lb[2].entity_id == 1001);
		CHECK(lb[2].rank == 3);
		CHECK(lb[2].score == 5000);

		// 更新萨姆族长声望反超为第一名
		fix.world.updateLeaderboardEntry(LeaderboardKind::kFame, 1001, "萨姆族长", 12000, "萨姆吉尔");
		auto lb_updated = fix.world.getLeaderboard(LeaderboardKind::kFame, 10);
		REQUIRE(lb_updated.size() == 3);
		CHECK(lb_updated[0].entity_id == 1001);
		CHECK(lb_updated[0].rank == 1);
		CHECK(lb_updated[0].score == 12000);

		// 批量插入 110 条，验证 100 条硬上限截断
		for (std::uint32_t i = 1; i <= 110; ++i)
		{
			fix.world.updateLeaderboardEntry(LeaderboardKind::kLevel, i + 2000, "批量玩家", i * 10);
		}
		auto lb_level = fix.world.getLeaderboard(LeaderboardKind::kLevel, 200);
		CHECK(lb_level.size() == kMaxLeaderboardEntries);
		CHECK(lb_level.front().rank == 1);
		CHECK(lb_level.back().rank == 100);
	}

	SUBCASE("荣誉殿堂每日膜拜领赏与每日一次门禁限制 (World::worshipHallOfFame)")
	{
		LeaderboardFixture fix;
		auto s_worshipper = fix.createPlayer("平民膜拜者", 60, 50000);
		auto *p_worshipper = fix.world.playerForTest(s_worshipper);
		REQUIRE(p_worshipper != nullptr);

		// 设立等级榜第一名
		fix.world.updateLeaderboardEntry(LeaderboardKind::kLevel, 9999, "全服第一人", 140, "极意战神");

		// 1. 成功膜拜第一名
		bool ok = fix.world.worshipHallOfFame(s_worshipper, LeaderboardKind::kLevel, 1, 20261004);
		CHECK(ok);
		// 奖励石币: lvl 60 * 100 = 6000
		CHECK(p_worshipper->gold == 56000);

		// 2. 同一天重复膜拜被拦截
		bool repeat = fix.world.worshipHallOfFame(s_worshipper, LeaderboardKind::kLevel, 1, 20261004);
		CHECK_FALSE(repeat);
		CHECK(p_worshipper->gold == 56000);

		// 3. 次日可再次膜拜
		bool next_day = fix.world.worshipHallOfFame(s_worshipper, LeaderboardKind::kLevel, 1, 20261005);
		CHECK(next_day);
		CHECK(p_worshipper->gold == 62000);

		// 4. 膜拜非第一名被拦截
		bool wrong_rank = fix.world.worshipHallOfFame(s_worshipper, LeaderboardKind::kLevel, 2, 20261006);
		CHECK_FALSE(wrong_rank);
	}

	SUBCASE("反向变异实证 —— 空榜膜拜、越界名次与殿堂加成防御 (RV-Leaderboard-1)")
	{
		LeaderboardFixture fix;
		auto s_player = fix.createPlayer("测试玩家", 30, 10000);

		// 1. 空榜膜拜防御
		CHECK_FALSE(fix.world.worshipHallOfFame(s_player, LeaderboardKind::kDuelScore, 1, 20261004));

		// 2. 非法榜单更新防御
		fix.world.updateLeaderboardEntry(LeaderboardKind::kNone, 888, "非法", 100);
		CHECK(fix.world.getLeaderboard(LeaderboardKind::kNone).empty());

		// 3. 未上榜玩家荣誉殿堂加成为空防御
		auto bonus = fix.world.playerHallOfFameBonus(s_player);
		CHECK(bonus.title_id == 0);
		CHECK(bonus.attack_bonus_percent == doctest::Approx(0.0f));

		// 4. 将测试玩家列入决斗榜状元，即时获得殿堂加成
		auto *p = fix.world.playerForTest(s_player);
		REQUIRE(p != nullptr);
		fix.world.updateLeaderboardEntry(LeaderboardKind::kDuelScore, static_cast<std::uint32_t>(s_player), p->name.c_str(), 2500);

		auto top_bonus = fix.world.playerHallOfFameBonus(s_player);
		CHECK(top_bonus.title_id == 1041);
		CHECK(top_bonus.attack_bonus_percent == doctest::Approx(5.0f));
		CHECK(std::string(top_bonus.title_name) == "天下第一武斗魁首");
	}
}
