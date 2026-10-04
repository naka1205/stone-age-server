// tests/WorldManorTest.cpp —— 阶段 13: 四大庄园骑乘进阶认证考核、金库税收分红与特权技能树体系测试

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <string>

#include "rules/ManorPrivilege.h"
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

struct ManorFixture
{
	SA::Platform::ServerConfig config = makeTestConfig();
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random_source{0x123456};
	SA::Net::LoopbackTransport transport{};
	SA::World::World world{config, clock, logger, random_source, transport};

	SA::Net::SessionId createPlayer(const std::string &name, int level = 50, std::int32_t gold = 1000000)
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

TEST_CASE("四大庄园骑乘进阶认证考核、金库税收分红与特权技能树体系 (Phase 13)")
{
	SUBCASE("庄园特权光环纯函数与大世界属性即时生效")
	{
		// 1. 无庄园成员无光环
		auto a_none = computeManorAura(ManorKind::kNone, true);
		CHECK(a_none.attack_percent == doctest::Approx(0.0f));
		CHECK(a_none.escape_bonus_rate == 0);

		// 2. 萨姆吉尔庄园成员身处属地 (in_domain = true)
		auto a_samo_dom = computeManorAura(ManorKind::kSamo, true);
		CHECK(a_samo_dom.attack_percent == doctest::Approx(5.0f));
		CHECK(a_samo_dom.defense_percent == doctest::Approx(5.0f));
		CHECK(a_samo_dom.quick_percent == doctest::Approx(5.0f));
		CHECK(a_samo_dom.escape_bonus_rate == 10);
		CHECK(a_samo_dom.damage_reduction == doctest::Approx(0.05f));

		// 3. 萨姆吉尔庄园成员身处非属地大世界 (in_domain = false)
		auto a_samo_wild = computeManorAura(ManorKind::kSamo, false);
		CHECK(a_samo_wild.attack_percent == doctest::Approx(2.0f));
		CHECK(a_samo_wild.escape_bonus_rate == 5);
		CHECK(a_samo_wild.damage_reduction == doctest::Approx(0.02f));
	}

	SUBCASE("庄园金库资金注资、取款与防溢出夹紧")
	{
		ManorFixture f;
		CHECK(f.world.manorTreasury(FamilyManor::kSamo) == 0);

		// 注资 100 万
		CHECK(f.world.depositManorTreasury(FamilyManor::kSamo, 1000000));
		CHECK(f.world.manorTreasury(FamilyManor::kSamo) == 1000000);

		// 取款 40 万
		CHECK(f.world.withdrawManorTreasury(FamilyManor::kSamo, 400000));
		CHECK(f.world.manorTreasury(FamilyManor::kSamo) == 600000);

		// 取款超过现有金库失败
		CHECK_FALSE(f.world.withdrawManorTreasury(FamilyManor::kSamo, 9999999));
		CHECK(f.world.manorTreasury(FamilyManor::kSamo) == 600000);

		// 硬上限防溢出 (50,000,000)
		CHECK(f.world.depositManorTreasury(FamilyManor::kSamo, 60000000));
		CHECK(f.world.manorTreasury(FamilyManor::kSamo) == kMaxManorTreasury);

		// 交易税率设置
		CHECK(f.world.manorTaxRate(FamilyManor::kSamo) == kDefaultTaxRatePercent);
		CHECK(f.world.setManorTaxRate(FamilyManor::kSamo, 8));
		CHECK(f.world.manorTaxRate(FamilyManor::kSamo) == 8);
		// 超过范围设置失败
		CHECK_FALSE(f.world.setManorTaxRate(FamilyManor::kSamo, 25));
		CHECK(f.world.manorTaxRate(FamilyManor::kSamo) == 8);
	}

	SUBCASE("庄园金库税收分红结算与多角色阶梯分配")
	{
		ManorFixture f;
		const auto s_leader = f.createPlayer("萨姆族长", 80, 500000);
		const auto s_elder = f.createPlayer("萨姆长老", 75, 500000);
		const auto s_member = f.createPlayer("萨姆战士", 70, 500000);

		// 创建家族并入驻萨姆吉尔庄园
		const std::uint32_t fid = f.world.createFamily(s_leader, "白狼战团", "誓死守卫萨姆吉尔");
		REQUIRE(fid != 0);

		// 招收长老与成员
		CHECK(f.world.applyJoinFamily(s_elder, fid));
		CHECK(f.world.acceptFamilyMember(s_leader, fid, "萨姆长老", true));
		CHECK(f.world.setFamilyMemberRole(s_leader, fid, "萨姆长老", FamilyRole::kElder));

		CHECK(f.world.applyJoinFamily(s_member, fid));
		CHECK(f.world.acceptFamilyMember(s_leader, fid, "萨姆战士", true));

		// 家族占领庄园
		CHECK(f.world.occupyManor(fid, FamilyManor::kSamo));
		CHECK(f.world.familyManor(fid) == FamilyManor::kSamo);

		// 玩家身处萨姆吉尔村 (floor 1000) 享受属地光环
		auto aura = f.world.playerManorAura(s_leader);
		CHECK(aura.attack_percent == doctest::Approx(5.0f));

		// 注资庄园金库 100 万石币
		f.world.depositManorTreasury(FamilyManor::kSamo, 1000000);
		CHECK(f.world.manorTreasury(FamilyManor::kSamo) == 1000000);

		// 执行分红 (分红池为 50 万，分发给 3 名成员)
		const std::uint32_t distributed = f.world.distributeManorDividends(FamilyManor::kSamo);
		CHECK(distributed > 0);
		CHECK(distributed <= 500000);

		// 金库扣除发放的分红总额
		CHECK(f.world.manorTreasury(FamilyManor::kSamo) == 1000000 - distributed);

		// 检查在线成员钱包石币增量
		auto *p_leader = f.world.playerForTest(s_leader);
		REQUIRE(p_leader != nullptr);
		CHECK(p_leader->gold > 500000); // 族长获得 25% 基础分红
	}

	SUBCASE("庄园骑乘认证考题理论验证与免考特权通道")
	{
		ManorFixture f;
		const auto s_leader = f.createPlayer("雷龙族长", 80, 500000);
		const auto s_manor_mem = f.createPlayer("庄园特权骑士", 10, 0); // 10级、0石币
		const auto s_outsider = f.createPlayer("外来流浪者", 85, 500000);

		// 1. 考题理论问答验证
		CHECK(f.world.verifyRideExamQuiz(1, 1));       // 萨姆吉尔红暴 (选项1)
		CHECK_FALSE(f.world.verifyRideExamQuiz(1, 0)); // 错误答案
		CHECK(f.world.verifyRideExamQuiz(2, 0));       // 玛丽娜斯渔村 (选项0)

		// 2. 占领庄园成员免试通道
		const std::uint32_t fid = f.world.createFamily(s_leader, "雷龙军团", "卡鲁它那");
		REQUIRE(fid != 0);
		CHECK(f.world.applyJoinFamily(s_manor_mem, fid));
		CHECK(f.world.acceptFamilyMember(s_leader, fid, "庄园特权骑士", true));
		CHECK(f.world.occupyManor(fid, FamilyManor::kKarutana));

		// 庄园成员免考骑乘认证 (零等级、零石币要求，直发证书)
		CHECK(f.world.canBypassRideExam(s_manor_mem, RideCertType::kKarutana));
		CHECK(f.world.takeRideExam(s_manor_mem, RideCertType::kKarutana) == RideExamResultCode::kSuccess);
		CHECK(f.world.hasRideCert(s_manor_mem, RideCertType::kKarutana));

		// 3. 外来成员常规考核与考费划拨
		CHECK_FALSE(f.world.canBypassRideExam(s_outsider, RideCertType::kKarutana));
		f.world.setPlayerFame(s_outsider, 500);
		const std::uint32_t treasury_before = f.world.manorTreasury(FamilyManor::kKarutana);

		CHECK(f.world.takeRideExam(s_outsider, RideCertType::kKarutana) == RideExamResultCode::kSuccess);
		CHECK(f.world.hasRideCert(s_outsider, RideCertType::kKarutana));

		// 验证 20,000 学费中的 20% (4,000) 自动注资庄园金库
		const std::uint32_t treasury_after = f.world.manorTreasury(FamilyManor::kKarutana);
		CHECK(treasury_after == treasury_before + 4000);
	}

	SUBCASE("庄园召集令全员集结感知")
	{
		ManorFixture f;
		const auto s_leader = f.createPlayer("族长", 80);
		const auto s_member = f.createPlayer("队员", 80);

		const std::uint32_t fid = f.world.createFamily(s_leader, "召唤战队", "全员集结");
		REQUIRE(fid != 0);
		CHECK(f.world.applyJoinFamily(s_member, fid));
		CHECK(f.world.acceptFamilyMember(s_leader, fid, "队员", true));

		// 族长发起集结
		CHECK(f.world.summonFamilyMembers(s_leader));

		// 普通成员无权发起集结
		CHECK_FALSE(f.world.summonFamilyMembers(s_member));
	}

	SUBCASE("反向变异实证 —— 越权免试防御与金库超额操作防御 (RV-Manor-1)")
	{
		ManorFixture f;
		const auto s_attacker = f.createPlayer("非法考生", 10, 0);

		// 1. 越权免试防御：非庄园成员请求免考必然失败
		CHECK_FALSE(f.world.canBypassRideExam(s_attacker, RideCertType::kManorSamo));
		CHECK(f.world.takeRideExam(s_attacker, RideCertType::kManorSamo) == RideExamResultCode::kInsufficientLevel);

		// 2. 金库空仓分红安全防御 (金库为 0 分红返回 0，不产生负数或非法转账)
		CHECK(f.world.distributeManorDividends(FamilyManor::kSamo) == 0);

		// 3. 非法考题与答案越界防御
		CHECK_FALSE(f.world.verifyRideExamQuiz(-1, 0));
		CHECK_FALSE(f.world.verifyRideExamQuiz(999, 0));
		CHECK_FALSE(f.world.verifyRideExamQuiz(1, 99));

		// 4. 税率越界夹紧防御
		CHECK(calculateTradeTax(10000, 0) == 100);    // clamp 至最低 1%
		CHECK(calculateTradeTax(10000, 100) == 1000); // clamp 至最高 10%
	}
}
