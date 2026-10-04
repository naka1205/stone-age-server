// tests/WorldProfessionTest.cpp —— 阶段 12: 职业进阶系统与武器专精熟练度体系测试

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <string>

#include "rules/WeaponMastery.h"
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

struct ProfessionFixture
{
	SA::Platform::ServerConfig config = makeTestConfig();
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0x123456};
	SA::Net::LoopbackTransport transport{};
	World world{config, clock, logger, random, transport};

	SA::Net::ConnectionId spawnPlayer(int level = 35, int fame = 100)
	{
		const auto id = transport.connect();
		world.onSessionReady(id);
		auto *p = world.playerForTest(id);
		if (p)
		{
			p->level = level;
			p->hp = 100;
		}
		world.setPlayerFame(id, fame);
		return id;
	}
};

} // namespace

TEST_CASE("职业进阶系统与武器精通专精熟练度体系 (Phase 12)")
{
	SUBCASE("职业进阶门禁与三转宗师状态机 (checkProfessionPromotion)")
	{
		// 1. 见习晋升一转初职门禁
		// 等级不足 30
		auto r1 = checkProfessionPromotion(25, 0, 100, ProfessionClass::kNone, ProfessionRank::kNovice, ProfessionClass::kFighter, ProfessionRank::kFirst);
		CHECK_FALSE(r1.eligible);
		CHECK(std::string(r1.reason).find("等级不足") != std::string::npos);

		// 声望不足 50
		auto r2 = checkProfessionPromotion(35, 0, 20, ProfessionClass::kNone, ProfessionRank::kNovice, ProfessionClass::kFighter, ProfessionRank::kFirst);
		CHECK_FALSE(r2.eligible);
		CHECK(std::string(r2.reason).find("声望不足") != std::string::npos);

		// 满足条件一转成功
		auto r3 = checkProfessionPromotion(35, 0, 60, ProfessionClass::kNone, ProfessionRank::kNovice, ProfessionClass::kFighter, ProfessionRank::kFirst);
		CHECK(r3.eligible);

		// 2. 一转晋升二转进阶门禁
		// 跨职业二转拦截 (勇士转暗灵大魔导)
		auto r4 = checkProfessionPromotion(105, 1, 600, ProfessionClass::kFighter, ProfessionRank::kFirst, ProfessionClass::kWizard, ProfessionRank::kSecond);
		CHECK_FALSE(r4.eligible);
		CHECK(std::string(r4.reason).find("不可跨系") != std::string::npos);

		// 未转生拦截
		auto r5 = checkProfessionPromotion(105, 0, 600, ProfessionClass::kFighter, ProfessionRank::kFirst, ProfessionClass::kFighter, ProfessionRank::kSecond);
		CHECK_FALSE(r5.eligible);
		CHECK(std::string(r5.reason).find("转生次数不足") != std::string::npos);

		// 满足条件二转白狼战狂
		auto r6 = checkProfessionPromotion(105, 1, 600, ProfessionClass::kFighter, ProfessionRank::kFirst, ProfessionClass::kFighter, ProfessionRank::kSecond);
		CHECK(r6.eligible);

		// 3. 二转晋升三转宗师门禁
		// 等级不足 130
		auto r7 = checkProfessionPromotion(125, 5, 3000, ProfessionClass::kFighter, ProfessionRank::kSecond, ProfessionClass::kFighter, ProfessionRank::kMaster);
		CHECK_FALSE(r7.eligible);

		// 转生不足 5 转
		auto r8 = checkProfessionPromotion(135, 4, 3000, ProfessionClass::kFighter, ProfessionRank::kSecond, ProfessionClass::kFighter, ProfessionRank::kMaster);
		CHECK_FALSE(r8.eligible);

		// 达成三转宗师 (极意战神)
		auto r9 = checkProfessionPromotion(135, 5, 2500, ProfessionClass::kFighter, ProfessionRank::kSecond, ProfessionClass::kFighter, ProfessionRank::kMaster);
		CHECK(r9.eligible);

		// 4. 称号与头衔文本验证
		CHECK(std::string(getProfessionTitle(ProfessionClass::kFighter, ProfessionRank::kFirst)) == "白狼勇士");
		CHECK(std::string(getProfessionTitle(ProfessionClass::kFighter, ProfessionRank::kSecond)) == "白狼战狂");
		CHECK(std::string(getProfessionTitle(ProfessionClass::kFighter, ProfessionRank::kMaster)) == "极意战神");
		CHECK(std::string(getProfessionTitle(ProfessionClass::kHunter, ProfessionRank::kFirst)) == "追猎者");
		CHECK(std::string(getProfessionTitle(ProfessionClass::kHunter, ProfessionRank::kSecond)) == "神射手");
		CHECK(std::string(getProfessionTitle(ProfessionClass::kHunter, ProfessionRank::kMaster)) == "逐风巡林");
		CHECK(std::string(getProfessionTitle(ProfessionClass::kWizard, ProfessionRank::kFirst)) == "暗灵法师");
		CHECK(std::string(getProfessionTitle(ProfessionClass::kWizard, ProfessionRank::kSecond)) == "大魔导士");
		CHECK(std::string(getProfessionTitle(ProfessionClass::kWizard, ProfessionRank::kMaster)) == "元素使徒");
	}

	SUBCASE("武器熟练度上限与升级公式 (expForNextMasteryLevel & getWeaponMasteryCap)")
	{
		// 1. 无职业冒险者上限 20
		CHECK(getWeaponMasteryCap(ProfessionClass::kNone, ProfessionRank::kNovice, WeaponClass::kAxe) == 20);
		CHECK(getWeaponMasteryCap(ProfessionClass::kNone, ProfessionRank::kNovice, WeaponClass::kBow) == 20);

		// 2. 勇士职业武器阶梯 (斧/枪: 60/80/100, 弓: 30, 杖: 10)
		CHECK(getWeaponMasteryCap(ProfessionClass::kFighter, ProfessionRank::kFirst, WeaponClass::kAxe) == 60);
		CHECK(getWeaponMasteryCap(ProfessionClass::kFighter, ProfessionRank::kSecond, WeaponClass::kAxe) == 80);
		CHECK(getWeaponMasteryCap(ProfessionClass::kFighter, ProfessionRank::kMaster, WeaponClass::kAxe) == 100);
		CHECK(getWeaponMasteryCap(ProfessionClass::kFighter, ProfessionRank::kMaster, WeaponClass::kBow) == 30);
		CHECK(getWeaponMasteryCap(ProfessionClass::kFighter, ProfessionRank::kMaster, WeaponClass::kOther) == 10);

		// 3. 猎人职业武器阶梯 (弓/投掷: 60/80/100, 斧: 20)
		CHECK(getWeaponMasteryCap(ProfessionClass::kHunter, ProfessionRank::kFirst, WeaponClass::kBow) == 60);
		CHECK(getWeaponMasteryCap(ProfessionClass::kHunter, ProfessionRank::kMaster, WeaponClass::kBow) == 100);
		CHECK(getWeaponMasteryCap(ProfessionClass::kHunter, ProfessionRank::kMaster, WeaponClass::kAxe) == 20);

		// 4. 巫师职业武器阶梯 (杖: 60/80/100)
		CHECK(getWeaponMasteryCap(ProfessionClass::kWizard, ProfessionRank::kMaster, WeaponClass::kRod) == 100);

		// 5. 经验累加与升级推导
		auto r1 = applyMasteryExp(0, 0, 50, 60); // 0级升1级需50经验
		CHECK(r1.first == 1);
		CHECK(r1.second == 0);

		auto r2 = applyMasteryExp(1, 0, 100, 60); // 1级升2级需75经验，余25
		CHECK(r2.first == 2);
		CHECK(r2.second == 25);

		// 达到上限封顶
		auto r3 = applyMasteryExp(59, 0, 99999, 60);
		CHECK(r3.first == 60);
		CHECK(r3.second == 0);
	}

	SUBCASE("武器专精属性加成与职业相性共鸣 (computeWeaponMasteryBonus)")
	{
		// 1. 基础加成 (20 级熟练度: +5.0% 攻击力, +4 命中, 20% 负面减轻)
		auto b_base = computeWeaponMasteryBonus(ProfessionClass::kNone, ProfessionRank::kNovice, WeaponClass::kClaw, 20);
		CHECK(b_base.attack_percent == doctest::Approx(5.0f)); // 20 * 0.25%
		CHECK(b_base.hit_bonus == 4);                          // 20 / 5
		CHECK(b_base.penalty_mitigation == doctest::Approx(0.20f));

		// 2. 勇士持战斧相性共鸣 (狂暴增伤 +10%, 爆击率 +5%)
		auto b_warrior = computeWeaponMasteryBonus(ProfessionClass::kFighter, ProfessionRank::kMaster, WeaponClass::kAxe, 80);
		CHECK(b_warrior.attack_percent == doctest::Approx(80 * 0.25f + 10.0f)); // 20% + 10% = 30%
		CHECK(b_warrior.crit_bonus_percent == doctest::Approx(5.0f));
		CHECK(b_warrior.penalty_mitigation == doctest::Approx(0.80f));

		// 3. 猎人持长弓相性共鸣 (先攻敏捷 +15, 命中额外 +10)
		auto b_hunter = computeWeaponMasteryBonus(ProfessionClass::kHunter, ProfessionRank::kMaster, WeaponClass::kBow, 60);
		CHECK(b_hunter.initiative_bonus == 15);
		CHECK(b_hunter.hit_bonus == 60 / 5 + 10); // 12 + 10 = 22

		// 4. 巫师持法杖相性共鸣 (魔法增伤 +15%, 气力消耗减少 10%)
		auto b_mage = computeWeaponMasteryBonus(ProfessionClass::kWizard, ProfessionRank::kMaster, WeaponClass::kRod, 100);
		CHECK(b_mage.magic_bonus_percent == doctest::Approx(15.0f));
		CHECK(b_mage.mp_cost_reduction == doctest::Approx(0.10f));
		CHECK(b_mage.penalty_mitigation == doctest::Approx(1.0f)); // 100% 抵消装备负面
	}

	SUBCASE("大世界世界循环 API 集成 (promotePlayerProfession & addPlayerWeaponMasteryExp)")
	{
		ProfessionFixture fix;
		auto &world = fix.world;
		auto session = fix.spawnPlayer(35, 100);

		// 初始状态为见习无职业
		CHECK(world.playerProfessionClass(session) == ProfessionClass::kNone);
		CHECK(world.playerProfessionRank(session) == ProfessionRank::kNovice);
		CHECK(world.playerWeaponMasteryLevel(session, WeaponClass::kAxe) == 0);

		// 执行一转
		auto promo1 = world.promotePlayerProfession(session, ProfessionClass::kFighter, ProfessionRank::kFirst);
		REQUIRE(promo1.eligible);
		CHECK(world.playerProfessionClass(session) == ProfessionClass::kFighter);
		CHECK(world.playerProfessionRank(session) == ProfessionRank::kFirst);

		// 增加巨斧武器熟练度经验
		CHECK(world.addPlayerWeaponMasteryExp(session, WeaponClass::kAxe, 300));
		CHECK(world.playerWeaponMasteryLevel(session, WeaponClass::kAxe) > 0);
		const auto bonus = world.playerWeaponMasteryBonus(session, WeaponClass::kAxe);
		CHECK(bonus.attack_percent > 10.0f); // 含勇士相性共鸣 +10%

		// 直接设置熟练度等级 (受职业阶位上限约束)
		world.setPlayerWeaponMasteryLevel(session, WeaponClass::kAxe, 999);
		CHECK(world.playerWeaponMasteryLevel(session, WeaponClass::kAxe) == 60); // 一转勇士巨斧上限为 60
	}

	SUBCASE("反向变异实证 —— 进阶门禁越权防御与跨职业上限溢出防御 (RV-Profession-1)")
	{
		// 1. 越权跨系进阶防御
		auto r_cross = checkProfessionPromotion(120, 2, 1000, ProfessionClass::kHunter, ProfessionRank::kFirst, ProfessionClass::kFighter, ProfessionRank::kSecond);
		CHECK_FALSE(r_cross.eligible);

		// 2. 跨职业上限溢出防御 (勇士持法杖上限仅为 10，绝不可获得高阶熟练度)
		CHECK(getWeaponMasteryCap(ProfessionClass::kFighter, ProfessionRank::kMaster, WeaponClass::kRod) == 10);
		auto r_mage_cap = applyMasteryExp(9, 0, 99999, getWeaponMasteryCap(ProfessionClass::kFighter, ProfessionRank::kMaster, WeaponClass::kRod));
		CHECK(r_mage_cap.first == 10); // 严格被钳位于 10，不溢出

		// 3. 非相性武器不触发专属共鸣 (勇士手持长弓绝不触发狂暴加成与爆击率)
		auto b_wrong = computeWeaponMasteryBonus(ProfessionClass::kFighter, ProfessionRank::kMaster, WeaponClass::kBow, 30);
		CHECK(b_wrong.crit_bonus_percent == doctest::Approx(0.0f));
		CHECK(b_wrong.attack_percent == doctest::Approx(30 * 0.25f)); // 仅 7.5%，无 +10% 狂暴
	}
}
