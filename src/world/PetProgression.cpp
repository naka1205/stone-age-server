// src/world/PetProgression.cpp —— 宠物融合与转生系统纯函数算力与规则引擎 (批次 §9.0.96)
//
// 依据:
// - docs/06-progression.md §4 (宠物融合三表投影 / DR-DT4 历史 off-by-one 决策 / 资质继承)
// - docs/06-progression.md §6 (宠物转生公式 NPC_PetTransManGetAns / 五次方算力 / Fx档位)
// - 原版 char_base.c:509-567 (PetTable[29][29], PropertyTable[4][4], FusionTable[11][16])
// - 原版 char_data.c:1550-1618 (PETFUSION_FusionPetMain / PETFUSION_FusionPetSub 资质继承与技能过滤)
// - 原版 char_data.c:1701-1804 (PETTRANS_getPetBase / NPC_PetTransManGetAns / PETTRANS_PetTransManStatus)

#include "world/Api.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace SA::World
{

namespace
{

// 29x29 融合码对投影矩阵 (原版 PetTable[29][29], char_base.c:509-545)
// sub1_code x main_code -> base2 (取值 0..10)
constexpr int kPetTable[29][29] = {
    // 0           4              9             14             19             24
    {1, 2, 5, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 5, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5},
    {2, 5, 1, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1},
    {5, 1, 2, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2},
    {1, 2, 5, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5},
    {2, 5, 1, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1},

    {2, 1, 2, 1, 2, 2, 2, 5, 5, 1, 2, 5, 2, 1, 2, 1, 2, 5, 2, 1, 2, 1, 2, 5, 2, 5, 2, 1, 2},
    {1, 2, 5, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 1},
    {2, 5, 1, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 2},
    {10, 3, 10, 3, 10, 8, 10, 8, 10, 3, 10, 3, 10, 8, 10, 8, 10, 3, 10, 3, 10, 8, 10, 8, 10, 8, 10, 3, 10},
    {3, 8, 3, 8, 3, 10, 3, 10, 8, 3, 3, 8, 3, 8, 3, 10, 3, 10, 3, 8, 3, 8, 3, 10, 3, 10, 3, 8, 3},

    {3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8},
    {5, 1, 5, 1, 5, 2, 5, 2, 5, 1, 5, 5, 5, 1, 5, 2, 5, 2, 5, 1, 5, 1, 5, 2, 5, 2, 5, 1, 5},
    {8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10},
    {10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3},
    {3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8},

    {5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1},
    {9, 0, 9, 0, 9, 4, 9, 4, 9, 0, 9, 0, 9, 4, 9, 4, 9, 0, 9, 0, 9, 4, 9, 4, 9, 0, 9, 4, 9},
    {1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2},
    {6, 0, 6, 0, 6, 9, 6, 9, 6, 0, 6, 0, 6, 9, 6, 9, 6, 0, 6, 0, 6, 9, 6, 9, 6, 0, 6, 9, 6},
    {4, 6, 4, 6, 4, 9, 4, 9, 4, 6, 4, 6, 4, 9, 4, 9, 4, 6, 4, 6, 4, 9, 4, 9, 4, 6, 4, 9, 4},

    {8, 3, 8, 3, 8, 10, 8, 10, 8, 3, 8, 3, 8, 10, 8, 10, 8, 3, 8, 3, 8, 10, 8, 10, 8, 3, 8, 10, 8},
    {8, 10, 3, 8, 10, 3, 8, 10, 10, 3, 8, 10, 3, 8, 10, 3, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10},
    {1, 2, 5, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5},
    {0, 4, 0, 4, 0, 6, 0, 6, 0, 4, 0, 4, 0, 6, 0, 6, 0, 4, 0, 4, 0, 6, 0, 4, 0, 6, 0, 4, 0},
    {1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2, 5, 1, 2},

    {3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8},
    {10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3, 8, 10, 3},
    {2, 1, 2, 1, 5, 1, 5, 1, 2, 1, 2, 1, 5, 1, 5, 1, 2, 1, 2, 1, 5, 1, 5, 1, 2, 1, 2, 5, 1},
    {6, 9, 6, 9, 6, 0, 6, 0, 6, 9, 6, 9, 6, 0, 6, 0, 6, 9, 6, 9, 6, 0, 6, 0, 6, 9, 6, 0, 6}};

// 4x4 属性投影矩阵 (原版 PropertyTable[4][4], char_base.c:547-552)
// sub1_elem x main_elem -> base1 (取值 0..15)
// 属性顺序: 地(0) 水(1) 火(2) 风(3)
constexpr int kPropertyTable[4][4] = {
    {0, 4, 5, 6},
    {7, 1, 8, 9},
    {10, 11, 2, 12},
    {13, 14, 15, 3}};

// 11x16 融合最终结果表 (原版 FusionTable[11][16] 及 oldfusion.txt, char_base.c:555-567)
// base2 x base1 -> 目标宠物模板 ID (989..1033)
constexpr int kFusionTable[11][16] = {
    {989, 990, 991, 992, 989, 992, 989, 990, 990, 990, 991, 991, 991, 992, 989, 992},
    {1001, 1002, 1003, 1004, 1001, 1001, 1004, 1001, 1002, 1002, 1003, 1003, 1003, 1004, 1004, 1003},
    {1005, 1006, 1007, 1008, 1005, 1005, 1005, 1006, 1006, 1006, 1007, 1006, 1007, 1008, 1008, 1008},
    {1021, 1025, 1023, 1024, 1025, 1021, 1021, 1022, 1022, 1022, 1023, 1023, 1023, 1021, 1024, 1024},
    {1030, 1031, 1032, 1033, 1030, 1030, 1030, 1031, 1031, 1031, 1032, 1031, 1032, 1030, 1033, 1033},
    {1017, 1018, 1019, 1020, 1018, 1017, 1017, 1018, 1019, 1018, 1019, 1019, 1020, 1017, 1020, 1020},
    {1009, 1010, 1011, 1012, 1010, 1009, 1009, 1010, 1010, 1010, 1011, 1011, 1011, 1012, 1012, 1011},
    {993, 994, 995, 996, 994, 993, 993, 994, 995, 994, 995, 993, 996, 993, 996, 996},
    {1026, 1027, 1028, 1029, 1026, 1026, 1026, 1026, 1028, 1027, 1028, 1028, 1029, 1029, 1029, 1029},
    {997, 998, 999, 999, 1000, 997, 997, 1000, 998, 998, 1000, 998, 999, 999, 999, 999},
    {1013, 1014, 1015, 1016, 1013, 1013, 1016, 1013, 1015, 1014, 1015, 1015, 1015, 1016, 1016, 1016}};

// 不可遗传的宠物技能表 (原版 illegalpetskill[15], enemy.c:1937)
constexpr std::int32_t kIllegalPetSkills[] = {
    41, 52, 600, 601, 602, 603, 604, 614, 617, 628, 630, 631, 635, 638, 641};

bool isSkillInheritable(std::int32_t skill_id) noexcept
{
	if (skill_id <= 0)
		return false;
	for (auto illegal : kIllegalPetSkills)
	{
		if (skill_id == illegal)
			return false;
	}
	return true;
}

} // namespace

int getPetFusionBase2(int sub_code, int main_code) noexcept
{
	if (sub_code < 0 || sub_code >= 29 || main_code < 0 || main_code >= 29)
		return -1;
	return kPetTable[sub_code][main_code];
}

int getPetFusionBase1(int sub_elem, int main_elem) noexcept
{
	if (sub_elem < 0 || sub_elem >= 4 || main_elem < 0 || main_elem >= 4)
		return -1;
	return kPropertyTable[sub_elem][main_elem];
}

int lookupPetFusionTarget(int base2, int base1) noexcept
{
	if (base2 < 0 || base2 >= 11 || base1 < 0 || base1 >= 16)
		return -1;
	return kFusionTable[base2][base1];
}

int resolvePetFusionResultId(int sub_code, int main_code, int sub_elem, int main_elem) noexcept
{
	const int base2 = getPetFusionBase2(sub_code, main_code);
	if (base2 < 0)
		return -1;
	const int base1 = getPetFusionBase1(sub_elem, main_elem);
	if (base1 < 0)
		return -1;
	return lookupPetFusionTarget(base2, base1);
}

PetGrowth calculateFusionGrowth(PetGrowth main_growth, int main_level,
                                PetGrowth sub1_growth, int sub1_level,
                                const PetGrowth *sub2_growth, int sub2_level) noexcept
{
	// 原版 PETFUSION_FusionPetMain / PETFUSION_FusionPetSub 算法 (char_data.c:1550-1618):
	// 等级低于 80 级时削弱: base[i] = base[i] * 0.8
	auto scale = [](PetGrowth g, int lv)
	{
		if (lv < 80)
		{
			g.vital = static_cast<int>(g.vital * 0.8);
			g.str = static_cast<int>(g.str * 0.8);
			g.tough = static_cast<int>(g.tough * 0.8);
			g.dex = static_cast<int>(g.dex * 0.8);
		}
		return g;
	};

	const PetGrowth m = scale(main_growth, main_level);
	const PetGrowth s1 = scale(sub1_growth, sub1_level);

	// 副宠贡献 work:
	// 单副宠: work[i] = s1[i] * 0.4
	// 双副宠: work[i] = ((s1[i] + s2[i]) / 2) * 0.4
	double sub_v = 0.0, sub_s = 0.0, sub_t = 0.0, sub_d = 0.0;
	if (sub2_growth != nullptr)
	{
		const PetGrowth s2 = scale(*sub2_growth, sub2_level);
		sub_v = ((s1.vital + s2.vital) / 2.0) * 0.4;
		sub_s = ((s1.str + s2.str) / 2.0) * 0.4;
		sub_t = ((s1.tough + s2.tough) / 2.0) * 0.4;
		sub_d = ((s1.dex + s2.dex) / 2.0) * 0.4;
	}
	else
	{
		sub_v = s1.vital * 0.4;
		sub_s = s1.str * 0.4;
		sub_t = s1.tough * 0.4;
		sub_d = s1.dex * 0.4;
	}

	// 主宠贡献: m[i] * 0.6
	const double res_v = sub_v + (m.vital * 0.6);
	const double res_s = sub_s + (m.str * 0.6);
	const double res_t = sub_t + (m.tough * 0.6);
	const double res_d = sub_d + (m.dex * 0.6);

	PetGrowth out{};
	out.vital = std::clamp(static_cast<int>(std::round(res_v)), 5, 60);
	out.str = std::clamp(static_cast<int>(std::round(res_s)), 5, 60);
	out.tough = std::clamp(static_cast<int>(std::round(res_t)), 5, 60);
	out.dex = std::clamp(static_cast<int>(std::round(res_d)), 5, 60);
	return out;
}

std::array<std::int32_t, 7> calculateFusionSkills(const std::int32_t main_skills[7],
                                                  const std::int32_t sub1_skills[7],
                                                  const std::int32_t *sub2_skills) noexcept
{
	std::array<std::int32_t, 7> out{};
	std::size_t out_idx = 0;

	auto add_skill = [&](std::int32_t sk)
	{
		if (out_idx >= 7)
			return;
		if (!isSkillInheritable(sk))
			return;
		for (std::size_t i = 0; i < out_idx; ++i)
		{
			if (out[i] == sk)
				return;
		}
		out[out_idx++] = sk;
	};

	// 1. 主宠技能优先
	if (main_skills != nullptr)
	{
		for (std::size_t i = 0; i < 7; ++i)
			add_skill(main_skills[i]);
	}

	// 2. 副宠 1 技能继承
	if (sub1_skills != nullptr)
	{
		for (std::size_t i = 0; i < 7; ++i)
			add_skill(sub1_skills[i]);
	}

	// 3. 副宠 2 技能继承
	if (sub2_skills != nullptr)
	{
		for (std::size_t i = 0; i < 7; ++i)
			add_skill(sub2_skills[i]);
	}

	// 剩余槽位填 0
	while (out_idx < 7)
	{
		out[out_idx++] = 0;
	}

	return out;
}

int calculatePetTransAns(int total1, int total2, int pet_level, int pet_rank, int current_trans) noexcept
{
	// 原版 NPC_PetTransManGetAns (char_data.c:1726-1761)
	int lv = pet_level;
	if (lv > 130)
		lv = 130;
	constexpr int kTransLv = 100;

	// total1 辅助宠成长总和 (通常玛蕾菲雅可达 100~200)
	float total = static_cast<float>(total1) / 100.0f;
	total = total * total * total * total * total; // 五次方
	if (total < 1.0f)
		total = 0.0f;
	else
		total = total * 1.3f; // 最大约 41.6

	// Fx 档位衰减: rank=0~6 时 Fx 约在 4~11
	const int fx = static_cast<int>(static_cast<float>(5 - pet_rank) * 1.2f) + 5;
	const int effective_fx = std::max(1, fx);

	int ans = static_cast<int>(total) + total2 + ((lv - kTransLv) / effective_fx);

	// 转生上限门限: 0 转升 1 转上限 150，1 转升 2 转上限 200
	if (current_trans == 0)
	{
		if (ans > 150)
			ans = 150;
	}
	else
	{
		if (ans > 200)
			ans = 200;
	}
	return ans;
}

PetGrowth calculatePetTransStats(PetGrowth base, PetGrowth work, int pet_level, int pet_rank, int current_trans) noexcept
{
	// 原版 PETTRANS_PetTransManStatus (char_data.c:1763-1804)
	const int total2 = base.vital + base.str + base.tough + base.dex;
	const int total1 = work.vital + work.str + work.tough + work.dex;

	const int ans = calculatePetTransAns(total1, total2, pet_level, pet_rank, current_trans);

	// 加权总和: total1 + work_total * 4
	const int work_total = (work.vital + work.str + work.tough + work.dex) * 4;
	const int weighted_total = total1 + work_total;

	PetGrowth out{};
	if (weighted_total <= 0)
	{
		out.vital = std::max(1, base.vital);
		out.str = std::max(1, base.str);
		out.tough = std::max(1, base.tough);
		out.dex = std::max(1, base.dex);
		return out;
	}

	out.vital = std::max(1, (ans * (base.vital + work.vital * 4)) / weighted_total);
	out.str = std::max(1, (ans * (base.str + work.str * 4)) / weighted_total);
	out.tough = std::max(1, (ans * (base.tough + work.tough * 4)) / weighted_total);
	out.dex = std::max(1, (ans * (base.dex + work.dex * 4)) / weighted_total);
	return out;
}

} // namespace SA::World
