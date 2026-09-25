// shared/rules/ProfessionSkill.cpp —— 64 项职业技能静态映射表与参数纯函数计算 (批次 A-γ2)
//
// 1:1 映射原版 profession_skill.c / battle_event.c:7945-8200。

#include "rules/ProfessionSkill.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace SA::Rules
{

namespace
{

// 原版 BATTLE_COM_S_* 常量映射
constexpr std::int32_t kComVolcanoSprings = 301;
constexpr std::int32_t kComFireBall = 302;
constexpr std::int32_t kComFireSpear = 303;
constexpr std::int32_t kComSummonThunder = 304;
constexpr std::int32_t kComCurrent = 305;
constexpr std::int32_t kComStorm = 306;
constexpr std::int32_t kComIceArrow = 307;
constexpr std::int32_t kComIceCrack = 308;
constexpr std::int32_t kComIceMirror = 309;
constexpr std::int32_t kComDoom = 310;
constexpr std::int32_t kComBlood = 311;
constexpr std::int32_t kComBloodWorms = 312;
constexpr std::int32_t kComSign = 313;
constexpr std::int32_t kComFireEnclose = 314;
constexpr std::int32_t kComIceEnclose = 315;
constexpr std::int32_t kComThunderEnclose = 316;
constexpr std::int32_t kComEnclose = 317;
constexpr std::int32_t kComTranspose = 318;

constexpr std::int32_t kComChainAtk = 319;
constexpr std::int32_t kComReback = 320;
constexpr std::int32_t kComBrust = 321;
constexpr std::int32_t kComChainAtk2 = 322;
constexpr std::int32_t kComScapegoat = 323;
constexpr std::int32_t kComEnrage = 324;
constexpr std::int32_t kComEnergyCollect = 325;
constexpr std::int32_t kComFocus = 326;
constexpr std::int32_t kComShieldAttack = 327;
constexpr std::int32_t kComThroughAttack = 328;
constexpr std::int32_t kComCavalry = 329;
constexpr std::int32_t kComDeadAttack = 330;
constexpr std::int32_t kComConvolute = 331;
constexpr std::int32_t kComChaos = 332;

constexpr std::int32_t kComTrap = 333;
constexpr std::int32_t kComEnragePet = 334;
constexpr std::int32_t kComDragnet = 335;
constexpr std::int32_t kComEntwine = 336;
constexpr std::int32_t kComPlunder = 337;
constexpr std::int32_t kComResistFire = 338;
constexpr std::int32_t kComResistIce = 339;
constexpr std::int32_t kComResistThunder = 340;
constexpr std::int32_t kComResistFit = 341;
constexpr std::int32_t kComCallNature = 342;
constexpr std::int32_t kComBoundary = 343;
constexpr std::int32_t kComGResistFire = 344;
constexpr std::int32_t kComGResistIce = 345;
constexpr std::int32_t kComGResistThunder = 346;
constexpr std::int32_t kComAttackWeak = 347;
constexpr std::int32_t kComInstigate = 348;
constexpr std::int32_t kComOblivion = 349;

constexpr std::int32_t kComFullMp = 350;
constexpr std::int32_t kComStrongBack = 351;
constexpr std::int32_t kComStrengthen = 352;

// 64 项原版职业技能权威表
constexpr std::array<ProfessionSkillInfo, 64> kProfessionSkills = {{
    // ── 巫师 (21 项) ──
    {static_cast<std::uint32_t>(ProfessionSkillId::kVolcanoSprings), "PROFESSION_VOLCANO_SPRINGS", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComVolcanoSprings},
    {static_cast<std::uint32_t>(ProfessionSkillId::kFireBall), "PROFESSION_FIRE_BALL", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComFireBall},
    {static_cast<std::uint32_t>(ProfessionSkillId::kFireSpear), "PROFESSION_FIRE_SPEAR", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComFireSpear},
    {static_cast<std::uint32_t>(ProfessionSkillId::kSummonThunder), "PROFESSION_SUMMON_THUNDER", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComSummonThunder},
    {static_cast<std::uint32_t>(ProfessionSkillId::kCurrent), "PROFESSION_CURRENT", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComCurrent},
    {static_cast<std::uint32_t>(ProfessionSkillId::kStorm), "PROFESSION_STORM", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComStorm},
    {static_cast<std::uint32_t>(ProfessionSkillId::kIceArrow), "PROFESSION_ICE_ARROW", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComIceArrow},
    {static_cast<std::uint32_t>(ProfessionSkillId::kIceCrack), "PROFESSION_ICE_CRACK", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComIceCrack},
    {static_cast<std::uint32_t>(ProfessionSkillId::kIceMirror), "PROFESSION_ICE_MIRROR", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComIceMirror},
    {static_cast<std::uint32_t>(ProfessionSkillId::kDoom), "PROFESSION_DOOM", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComDoom},
    {static_cast<std::uint32_t>(ProfessionSkillId::kBlood), "PROFESSION_BLOOD", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComBlood},
    {static_cast<std::uint32_t>(ProfessionSkillId::kBloodWorms), "PROFESSION_BLOOD_WORMS", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComBloodWorms},
    {static_cast<std::uint32_t>(ProfessionSkillId::kSign), "PROFESSION_SIGN", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComSign},
    {static_cast<std::uint32_t>(ProfessionSkillId::kFireEnclose), "PROFESSION_FIRE_ENCLOSE", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComFireEnclose},
    {static_cast<std::uint32_t>(ProfessionSkillId::kIceEnclose), "PROFESSION_ICE_ENCLOSE", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComIceEnclose},
    {static_cast<std::uint32_t>(ProfessionSkillId::kThunderEnclose), "PROFESSION_THUNDER_ENCLOSE", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComThunderEnclose},
    {static_cast<std::uint32_t>(ProfessionSkillId::kFirePractice), "PROFESSION_FIRE_PRACTICE", ProfessionClass::kWizard, ProfessionSkillCategory::kPassive, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kIcePractice), "PROFESSION_ICE_PRACTICE", ProfessionClass::kWizard, ProfessionSkillCategory::kPassive, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kThunderPractice), "PROFESSION_THUNDER_PRACTICE", ProfessionClass::kWizard, ProfessionSkillCategory::kPassive, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kEnclose), "PROFESSION_ENCLOSE", ProfessionClass::kWizard, ProfessionSkillCategory::kMagic, kComEnclose},
    {static_cast<std::uint32_t>(ProfessionSkillId::kTranspose), "PROFESSION_TRANSPOSE", ProfessionClass::kWizard, ProfessionSkillCategory::kUtility, kComTranspose},

    // ── 勇士 (18 项) ──
    {static_cast<std::uint32_t>(ProfessionSkillId::kChainAtk), "PROFESSION_CHAIN_ATK", ProfessionClass::kFighter, ProfessionSkillCategory::kDirectAttack, kComChainAtk},
    {static_cast<std::uint32_t>(ProfessionSkillId::kAvoid), "PROFESSION_AVOID", ProfessionClass::kFighter, ProfessionSkillCategory::kPassive, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kWeaponFocus), "PROFESSION_WEAPON_FOCUS", ProfessionClass::kFighter, ProfessionSkillCategory::kPassive, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kReback), "PROFESSION_REBACK", ProfessionClass::kFighter, ProfessionSkillCategory::kUtility, kComReback},
    {static_cast<std::uint32_t>(ProfessionSkillId::kBrust), "PROFESSION_BRUST", ProfessionClass::kFighter, ProfessionSkillCategory::kDirectAttack, kComBrust},
    {static_cast<std::uint32_t>(ProfessionSkillId::kChainAtk2), "PROFESSION_CHAIN_ATK_2", ProfessionClass::kFighter, ProfessionSkillCategory::kDirectAttack, kComChainAtk2},
    {static_cast<std::uint32_t>(ProfessionSkillId::kScapegoat), "PROFESSION_SCAPEGOAT", ProfessionClass::kFighter, ProfessionSkillCategory::kUtility, kComScapegoat},
    {static_cast<std::uint32_t>(ProfessionSkillId::kEnrage), "PROFESSION_ENRAGE", ProfessionClass::kFighter, ProfessionSkillCategory::kUtility, kComEnrage},
    {static_cast<std::uint32_t>(ProfessionSkillId::kEnergyCollect), "PROFESSION_ENERGY_COLLECT", ProfessionClass::kFighter, ProfessionSkillCategory::kUtility, kComEnergyCollect},
    {static_cast<std::uint32_t>(ProfessionSkillId::kFocus), "PROFESSION_FOCUS", ProfessionClass::kFighter, ProfessionSkillCategory::kUtility, kComFocus},
    {static_cast<std::uint32_t>(ProfessionSkillId::kShieldAttack), "PROFESSION_SHIELD_ATTACK", ProfessionClass::kFighter, ProfessionSkillCategory::kDirectAttack, kComShieldAttack},
    {static_cast<std::uint32_t>(ProfessionSkillId::kDualWeapon), "PROFESSION_DUAL_WEAPON", ProfessionClass::kFighter, ProfessionSkillCategory::kPassive, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kDeflect), "PROFESSION_DEFLECT", ProfessionClass::kFighter, ProfessionSkillCategory::kPassive, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kThroughAttack), "PROFESSION_THROUGH_ATTACK", ProfessionClass::kFighter, ProfessionSkillCategory::kUtility, kComThroughAttack},
    {static_cast<std::uint32_t>(ProfessionSkillId::kCavalry), "PROFESSION_CAVALRY", ProfessionClass::kFighter, ProfessionSkillCategory::kDirectAttack, kComCavalry},
    {static_cast<std::uint32_t>(ProfessionSkillId::kDeadAttack), "PROFESSION_DEAD_ATTACK", ProfessionClass::kFighter, ProfessionSkillCategory::kDirectAttack, kComDeadAttack},
    {static_cast<std::uint32_t>(ProfessionSkillId::kConvolute), "PROFESSION_CONVOLUTE", ProfessionClass::kFighter, ProfessionSkillCategory::kUtility, kComConvolute},
    {static_cast<std::uint32_t>(ProfessionSkillId::kChaos), "PROFESSION_CHAOS", ProfessionClass::kFighter, ProfessionSkillCategory::kDirectAttack, kComChaos},

    // ── 猎人 (22 项) ──
    {static_cast<std::uint32_t>(ProfessionSkillId::kTrack), "PROFESSION_TRACK", ProfessionClass::kHunter, ProfessionSkillCategory::kNonCombat, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kEscape), "PROFESSION_ESCAPE", ProfessionClass::kHunter, ProfessionSkillCategory::kNonCombat, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kDocile), "PROFESSION_DOCILE", ProfessionClass::kHunter, ProfessionSkillCategory::kPassive, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kTrap), "PROFESSION_TRAP", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComTrap},
    {static_cast<std::uint32_t>(ProfessionSkillId::kEnragePet), "PROFESSION_ENRAGE_PET", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComEnragePet},
    {static_cast<std::uint32_t>(ProfessionSkillId::kDragnet), "PROFESSION_DRAGNET", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComDragnet},
    {static_cast<std::uint32_t>(ProfessionSkillId::kEntwine), "PROFESSION_ENTWINE", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComEntwine},
    {static_cast<std::uint32_t>(ProfessionSkillId::kAutarky), "PROFESSION_AUTARKY", ProfessionClass::kHunter, ProfessionSkillCategory::kPassive, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kPlunder), "PROFESSION_PLUNDER", ProfessionClass::kHunter, ProfessionSkillCategory::kDirectAttack, kComPlunder},
    {static_cast<std::uint32_t>(ProfessionSkillId::kToxinWeapon), "PROFESSION_TOXIN_WEAPON", ProfessionClass::kHunter, ProfessionSkillCategory::kPassive, 0},
    {static_cast<std::uint32_t>(ProfessionSkillId::kResistFire), "PROFESSION_RESIST_FIRE", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComResistFire},
    {static_cast<std::uint32_t>(ProfessionSkillId::kResistIce), "PROFESSION_RESIST_ICE", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComResistIce},
    {static_cast<std::uint32_t>(ProfessionSkillId::kResistThunder), "PROFESSION_RESIST_THUNDER", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComResistThunder},
    {static_cast<std::uint32_t>(ProfessionSkillId::kResistFit), "PROFESSION_RESIST_F_I_T", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComResistFit},
    {static_cast<std::uint32_t>(ProfessionSkillId::kCallNature), "PROFESSION_CALL_NATURE", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComCallNature},
    {static_cast<std::uint32_t>(ProfessionSkillId::kBoundary), "PROFESSION_BOUNDARY", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComBoundary},
    {static_cast<std::uint32_t>(ProfessionSkillId::kGResistFire), "PROFESSION_G_RESIST_FIRE", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComGResistFire},
    {static_cast<std::uint32_t>(ProfessionSkillId::kGResistIce), "PROFESSION_G_RESIST_ICE", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComGResistIce},
    {static_cast<std::uint32_t>(ProfessionSkillId::kGResistThunder), "PROFESSION_G_RESIST_THUNDER", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComGResistThunder},
    {static_cast<std::uint32_t>(ProfessionSkillId::kAttackWeak), "PROFESSION_ATTACK_WEAK", ProfessionClass::kHunter, ProfessionSkillCategory::kDirectAttack, kComAttackWeak},
    {static_cast<std::uint32_t>(ProfessionSkillId::kInstigate), "PROFESSION_INSTIGATE", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComInstigate},
    {static_cast<std::uint32_t>(ProfessionSkillId::kOblivion), "PROFESSION_OBLIVION", ProfessionClass::kHunter, ProfessionSkillCategory::kUtility, kComOblivion},

    // ── 共通 (3 项) ──
    {static_cast<std::uint32_t>(ProfessionSkillId::kFullMp), "PROFESSION_FULL_MP", ProfessionClass::kNone, ProfessionSkillCategory::kUtility, kComFullMp},
    {static_cast<std::uint32_t>(ProfessionSkillId::kStrongBack), "PROFESSION_STRONG_BACK", ProfessionClass::kNone, ProfessionSkillCategory::kUtility, kComStrongBack},
    {static_cast<std::uint32_t>(ProfessionSkillId::kStrengthen), "PROFESSION_STRENGTHEN", ProfessionClass::kNone, ProfessionSkillCategory::kUtility, kComStrengthen},
}};

} // namespace

std::size_t getProfessionSkillTableSize() noexcept
{
	return kProfessionSkills.size();
}

const ProfessionSkillInfo *getProfessionSkillTable() noexcept
{
	return kProfessionSkills.data();
}

const ProfessionSkillInfo *findProfessionSkillById(std::uint32_t skill_id) noexcept
{
	for (const auto &info : kProfessionSkills)
	{
		if (info.skill_id == skill_id)
			return &info;
	}
	return nullptr;
}

const ProfessionSkillInfo *findProfessionSkillByName(std::string_view name) noexcept
{
	for (const auto &info : kProfessionSkills)
	{
		if (name == info.name)
			return &info;
	}
	return nullptr;
}

ProfSkillDirectParams computeProfSkillDirectParams(std::uint32_t skill_id,
                                                   int skill_level,
                                                   CombatantKind target_kind,
                                                   Random &rng) noexcept
{
	ProfSkillDirectParams params{};
	const auto *info = findProfessionSkillById(skill_id);
	if (info == nullptr || info->category != ProfessionSkillCategory::kDirectAttack)
		return params;

	params.is_direct = true;
	params.hits = 1;
	params.damage_percent = 100;
	params.attack_percent = 0;
	params.quick_percent = 0;
	params.apply_status = 0;
	params.status_turns = 0;

	const auto id = static_cast<ProfessionSkillId>(skill_id);
	switch (id)
	{
	case ProfessionSkillId::kBrust:
		// 爆击: 攻方力量增加 skill_level * 3 + 100% (battle_event.c:8012)
		params.attack_percent = skill_level * 3;
		break;

	case ProfessionSkillId::kChainAtk:
	{
		// 连环攻击: 概率 2 段击 (battle_event.c:8033)
		int eff_level = skill_level;
		if (eff_level % 10 != 0)
			eff_level += 1;
		const int hit = eff_level * 5 + 15;
		const int roll = rng.rand(1, 100);
		params.hits = (roll <= hit) ? 2 : 1;
		break;
	}

	case ProfessionSkillId::kChainAtk2:
		// 双重攻击: 固定 2 段击，攻击力提升 skill_level * 2% (battle_event.c:8062-8095)
		params.hits = 2;
		params.attack_percent = skill_level * 2;
		break;

	case ProfessionSkillId::kAttackWeak:
		// 弱点攻击: 目标若为宠物或敌人，攻击力提升 skill_level * 2 + 10%；自身敏捷下降 skill_level + 10% (battle_event.c:8127-8144)
		if (target_kind == CombatantKind::kPet || target_kind == CombatantKind::kEnemy)
		{
			params.attack_percent = skill_level * 2 + 10;
		}
		params.quick_percent = -(skill_level + 10);
		break;

	case ProfessionSkillId::kChaos:
		// 混乱攻击: 攻击力 70%，段数 3~5 段，附加混乱状态 3 回合 (battle_event.c:8018-8032)
		params.damage_percent = 70;
		if (skill_level >= 10)
			params.hits = 5;
		else if (skill_level >= 5)
			params.hits = 4;
		else
			params.hits = 3;
		params.apply_status = 7; // BATTLE_ST_CONFUSION
		params.status_turns = 3;
		break;

	default:
		// 其它直攻系技能 (CAVALRY, DEAD_ATTACK, SHIELD_ATTACK, PLUNDER 等)
		break;
	}

	return params;
}

bool isAbnormalStatusForReback(int status) noexcept
{
	// 源码 SSRC80 battle.c:9331-9339
	// status_table[9] = { 2(麻痹), 3(睡眠), 4(石化), 12(晕眩), 13(缠绕), 14(天罗), 15(冰暴), 17(冰箭), 23(雷附体) }
	return status == 2 || status == 3 || status == 4 ||
	       status == 12 || status == 13 || status == 14 ||
	       status == 15 || status == 17 || status == 23;
}

std::int32_t computeProfessionRebackHeal(std::int32_t max_hp,
                                         std::int32_t current_hp,
                                         int skill_level) noexcept
{
	if (skill_level <= 0 || max_hp <= 0 || current_hp <= 0)
		return 0;
	int pct = skill_level * 2;
	if (pct > 20)
		pct = 20;
	std::int32_t heal = (max_hp * pct) / 100;
	if (current_hp + heal > max_hp)
		heal = max_hp - current_hp;
	if (heal < 0)
		heal = 0;
	return heal;
}

int computeProfessionAvoidBonus(int skill_level) noexcept
{
	// 源码 battle.c:9236-9240
	if (skill_level <= 0)
		return 0;
	int val = (skill_level <= 5) ? (skill_level * 2) : ((skill_level - 5) * 3);
	return (val > 25) ? 25 : val;
}

int computeProfessionWeaponFocusBonus(int skill_level) noexcept
{
	// 源码 battle.c:9305-9310
	if (skill_level <= 0)
		return 0;
	int val = (skill_level <= 5) ? (skill_level * 2) : ((skill_level - 5) * 3 + 10);
	return (val > 25) ? 25 : val;
}

int computeProfessionDeflectBonus(int skill_level) noexcept
{
	// 源码 battle.c:9259
	if (skill_level <= 0)
		return 0;
	return skill_level + 10;
}

int computeProfessionPracticeBonus(int skill_level) noexcept
{
	// 源码 battle.c:9175-9177
	if (skill_level <= 0)
		return 0;
	int val = (skill_level >= 6) ? ((skill_level - 5) * 3 + 10) : (skill_level * 2);
	return (val > 25) ? 25 : val;
}

} // namespace SA::Rules
