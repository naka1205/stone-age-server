// src/world/WorldEncounter.cpp —— 大世界暗雷/明雷遇敌链与敌人生成入场实现
//
// 对应原版 char/enemy.c (ENEMY_getEnemy, ENEMY_createEnemy), npc_npcenemy.c

#include "WorldImpl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace SA::World
{

//
// 详注见 world/Api.h 的声明处。移植来源 `ENCOUNT_getEncountAreaArray`
// (`char/encount.c:370-392`)· `GROUP_getGroupArray`(`char/enemy.c:745-755`)·
// `ENEMY_getEnemy` 的前两段(`char/enemy.c:1289-1355`)。

std::int32_t findEncountArea(const std::vector<EncountArea> &areas, std::int32_t floor,
                             std::int32_t x, std::int32_t y)
{
	std::int32_t index = -1;
	for (std::size_t i = 0; i < areas.size(); ++i)
	{
		const EncountArea &a = areas[i];
		if (a.floor != floor)
			continue;

		// 闭区间 —— `PointInRect`(`util.c:1363`)是 `x <= px && px <= x + width`,
		// 而载入期的 width 不 +1 ⇒ 两者配对后语义是「x1..x2 两端都含」。
		// ⚠️ 写成 `px < a.x + a.width` 会让单点区域(width==0)永不匹配。
		if (x < a.x || x > a.x + a.width)
			continue;
		if (y < a.y || y > a.y + a.height)
			continue;

		// ⚠️★★ `zorder <= 0` 整行跳过 —— 那一列兼任启用开关(源码 :378)。
		//    实测 1050 行全部 > 0 ⇒ 本判据一次都不触发,仍然移植(理由见 Api.h)。
		if (a.zorder <= 0)
			continue;

		// ★ 严格 `>` ⇒ zorder 相等时保留**先遇到**的(源码 :382)。
		//   ⚠️ 顺序敏感,而表的顺序就是文件行序 ⇒ D 线入库时**不得重排行**。
		if (index < 0 || a.zorder > areas[static_cast<std::size_t>(index)].zorder)
			index = static_cast<std::int32_t>(i);
	}
	return index;
}

std::int32_t findEnemyGroup(const std::vector<EnemyGroup> &groups, std::int32_t group_id)
{
	for (std::size_t i = 0; i < groups.size(); ++i)
		if (groups[i].group_id == group_id)
			return static_cast<std::int32_t>(i);
	return -1;
}

std::int32_t pickEnemyGroup(const EncountArea &area, const std::vector<EnemyGroup> &groups,
                            const std::vector<std::int32_t> &player_item_ids,
                            SA::Rules::Random &rng)
{
	// 候选三元组:编组槽号 / 该槽权重 / 已解析的 group 行下标。
	// ★ 缓存行下标是**有意偏离**源码:原版抽中后又调一次 `GROUP_getGroupArray`
	//   (`:1354`)⇒ 第二次线性扫 1220 行。缓存不改变任何行为,只省那一次扫。
	std::array<std::int32_t, kEncountGroupMaxNum> weight{};
	std::array<std::int32_t, kEncountGroupMaxNum> row{};
	int found = 0;
	std::int32_t total = 0;

	const auto holds = [&player_item_ids](std::int32_t item_id)
	{
		return std::find(player_item_ids.begin(), player_item_ids.end(), item_id) != player_item_ids.end();
	};

	for (std::size_t i = 0; i < static_cast<std::size_t>(kEncountGroupMaxNum); ++i)
	{
		const std::int32_t gid = area.group_id[i];
		if (gid == -1)
			continue;

		const std::int32_t g = findEnemyGroup(groups, gid);
		if (g < 0)
			continue; // ★ 不照抄原版"坏组仍入选"那条路径,理由见 Api.h 声明处 ③

		const EnemyGroup &grp = groups[static_cast<std::size_t>(g)];

		// 两道道具门，调用方从全部有效持有槽（含装备）提供实际 ID。
		if (grp.appear_by_item_id != -1 && !holds(grp.appear_by_item_id))
			continue;
		if (grp.not_appear_by_item_id != -1 && holds(grp.not_appear_by_item_id))
			continue;

		weight[static_cast<std::size_t>(found)] = area.group_prob[i];
		row[static_cast<std::size_t>(found)] = g;
		total += area.group_prob[i];
		++found;
	}

	// ★ 源码在 `RAND` **之前**就 `return NULL`(`:1342`)⇒ 无候选时**不消耗 rng**。
	if (found <= 0)
		return -1;

	// 抽签:`r = RAND(0, Σ − 1)`(源码 :1340 的 `r_max--` + :1346)。
	// ⚠️★ 本表实测权重和恒 ≥ 1(min=1)⇒ 不会走到 DR-BT23 的退化区间;
	//    ★ 而下一批的 group 侧**会**(4 行权重和为 0)—— 那是 R.1 排在本批前的理由。
	const std::int32_t r = rng.rand(0, total - 1);

	// ⚠️★★ 上界是 `found - 1`:最后一个候选不参与判定,落空即取它兜底(源码 :1347)。
	// ★★ **实测这两处细节都是「等价写法」而不是行为判据**(2026-09-09 穷举验证,
	//    1..4 个槽 × 权重 {−1,0,1,2,3} × r 遍历 [0, Σ−1],共 2,580 组):
	//      · 上界写 `found - 1` 还是 `found` ⇒ **差异 0 组**
	//        (因为 `r <= Σ−1 < acc(最后)` ⇒ 最后一个必然命中);
	//      · `weight != 0` 这半个条件 ⇒ **差异 0 组**(它是**冗余**的:权重 0 的槽
	//        不会让 acc 增长,而 `r < acc_prev` 若成立,前一轮就已经 break 了)。
	//    ⇒ ★ 两者**照抄源码**(它们是源码原文),但**不要声称它们要紧** ——
	//      ⚠️ 本注释初稿写的是「`weight != 0` 那半个条件要紧:权重 0 的槽不该被选中,
	//      少了它就会选中它」,**那句话是错的**,由反向验证 + 穷举当场揭穿(`00` §9.0.37 ⑥)。
	int pick = found - 1;
	std::int32_t acc = 0;
	for (int i = 0; i < found - 1; ++i)
	{
		acc += weight[static_cast<std::size_t>(i)];
		if (weight[static_cast<std::size_t>(i)] != 0 && r < acc)
		{
			pick = i;
			break;
		}
	}

	return row[static_cast<std::size_t>(pick)];
}

// ── 遇敌:编组 → 敌人列表(批次 M.7)──────────────────────────────────────
//
// 详注见 world/Api.h 的声明处。移植来源 `ENEMY_getEnemy` 第三、四段
// (`char/enemy.c:1356-1466`)· `ENEMY_getEnemyArrayFromId`(`:519-528`)·
// `ENEMYTEMP_getEnemyTempArrayFromTempNo`(`:337-347`)。

std::int32_t findEnemyEncounter(const std::vector<EnemyEncounter> &encounters,
                                std::int32_t enemy_id)
{
	for (std::size_t i = 0; i < encounters.size(); ++i)
		if (encounters[i].enemy_id == enemy_id)
			return static_cast<std::int32_t>(i);
	return -1;
}

std::int32_t findEnemyTemplate(const std::vector<EnemyTemplate> &templates,
                               std::int32_t temp_no)
{
	for (std::size_t i = 0; i < templates.size(); ++i)
		if (templates[i].temp_no == temp_no)
			return static_cast<std::int32_t>(i);
	return -1;
}

std::vector<std::int32_t> rollEnemyList(const EnemyGroup &group,
                                        const std::vector<EnemyEncounter> &encounters,
                                        const std::vector<EnemyTemplate> &templates,
                                        std::int32_t enemy_max_num, SA::Rules::Random &rng)
{
	// ── 第三段:收候选(源码 :1356-1401)──────────────────────────────
	// work[] = 候选敌人表行下标;wr[] = 各自权重(CREATEPROB);
	// createenemynum = Σ CREATEMAXNUM(出场数上界的一半)。
	// ⚠️ NPC 事件改组(:1367-1383)与 ENEMY_RandomEnemyArray(:1385)均不做,理由见 Api.h 卷首。
	std::array<std::int32_t, kEnemyGroupSlotMaxNum> work{};
	std::array<std::int32_t, kEnemyGroupSlotMaxNum> wr{};
	int found = 0;
	std::int32_t total = 0;
	std::int32_t createenemynum = 0;

	for (std::size_t s = 0; s < static_cast<std::size_t>(kEnemyGroupSlotMaxNum); ++s)
	{
		const std::int32_t eid = group.enemy_id[s];
		if (eid == -1)
			continue;
		// ENEMY_ID → 敌人表行下标(原版运行期读载入缓存,我们现扫)。找不到 ⇒ 跳过该槽,
		// 等价原版载入期把该槽置 -1(`enemy.c:690-710`)。
		const std::int32_t e = findEnemyEncounter(encounters, eid);
		if (e < 0)
			continue;
		work[static_cast<std::size_t>(found)] = e;
		wr[static_cast<std::size_t>(found)] = group.create_prob[s];
		total += group.create_prob[s];
		createenemynum += encounters[static_cast<std::size_t>(e)].create_max_num;
		++found;
	}

	// ★ 源码在 RAND 之前就 return(:1399)⇒ 无候选时不消耗 rng(同 pickEnemyGroup)。
	if (found <= 0)
		return {};

	// 出场数上界 = min(区域上限, Σ CREATEMAXNUM);出场数 = RAND(1, 上界)(源码 :1400-1401)。
	// ★ 实测 CREATEMAXNUM min=1 ⇒ createenemynum ≥ 1 ⇒ 上界 ≥ 1 ⇒ 不触发退化区间。
	const std::int32_t cap =
	    enemy_max_num < createenemynum ? enemy_max_num : createenemynum;
	std::int32_t entrymax = rng.rand(1, cap);

	// ── 第四段:逐只抽 + 同族上限门 + 大怪布阵(源码 :1402-1465)──────────────
	// ⚠️ 产出保留定长 16 槽(kEnemyIndexTableMaxSize)+ -1 空位 —— 大怪换位要按位置
	//    读写(见 Api.h),vector 一路 push 做不到。末尾再裁成紧凑序列。
	std::array<std::int32_t, kEnemyIndexTableMaxSize> indextable{};
	indextable.fill(-1);
	const std::int32_t r_max = total - 1; // 源码 :1397 的 r_max--
	int bigcnt = 0;
	int i = 0;
	for (int loopcounter = 0; i < entrymax && loopcounter < 100; ++loopcounter)
	{
		// 权重抽签 —— 与 pickEnemyGroup 同构(found-1 兜底 + wr!=0,等价/冗余见 DR-DT13 ④)。
		const std::int32_t r = rng.rand(0, r_max);
		int pick = found - 1;
		std::int32_t acc = 0;
		for (int j = 0; j < found - 1; ++j)
		{
			acc += wr[static_cast<std::size_t>(j)];
			if (wr[static_cast<std::size_t>(j)] != 0 && r < acc)
			{
				pick = j;
				break;
			}
		}
		const std::int32_t row = work[static_cast<std::size_t>(pick)];

		// 同族上限门(源码 :1418-1428):
		//   cnt       = 索引表里已放入几只该行;
		//   samecount = 候选 work[] 里该行出现几次。
		//   cnt >= CREATEMAXNUM * samecount ⇒ 不再放它(i 不推进,loopcounter 推进)。
		int cnt = 0;
		for (int j = 0;
		     j < kEnemyIndexTableMaxSize && indextable[static_cast<std::size_t>(j)] != -1; ++j)
			if (indextable[static_cast<std::size_t>(j)] == row)
				++cnt;
		int samecount = 0;
		for (int k = 0; k < found; ++k)
			if (work[static_cast<std::size_t>(k)] == row)
				++samecount;
		if (cnt >= encounters[static_cast<std::size_t>(row)].create_max_num * samecount)
			continue;

		// 大怪布阵(源码 :1430-1464):查模板 E_T_SIZE。
		// ★ 模板查不到 ⇒ 整只不放(源码 i++ 在 ENEMYTEMP_CHECKINDEX 块内 ⇒ 此处 continue)。
		const std::int32_t t = findEnemyTemplate(
		    templates, encounters[static_cast<std::size_t>(row)].temp_no);
		if (t < 0)
			continue;

		if (templates[static_cast<std::size_t>(t)].size == kEnemySizeBig)
		{
			if (bigcnt >= 5)
			{
				// 前 5 位已满大怪 ⇒ 减少总出场数并跳过(源码 :1433-1436)。
				--entrymax;
				continue;
			}
			if (i > 4)
			{
				// 要放到第 6 位起 ⇒ 去前 5 位找第一只 NORMAL,与之交换(源码 :1437-1454)。
				int j = 0;
				bool swap_ready = false;
				for (; j < 5; ++j)
				{
					const std::int32_t front = indextable[static_cast<std::size_t>(j)];
					if (front == -1)
						break; // 对应 ENEMY_CHECKINDEX 失败
					const std::int32_t ft = findEnemyTemplate(
					    templates, encounters[static_cast<std::size_t>(front)].temp_no);
					if (ft < 0)
						break; // 对应 ENEMYTEMP_CHECKINDEX 失败
					if (templates[static_cast<std::size_t>(ft)].size == kEnemySizeNormal)
					{
						swap_ready = true;
						break;
					}
				}
				if (!swap_ready)
					continue; // 前 5 位无 NORMAL 可换出 ⇒ 本轮不放
				indextable[static_cast<std::size_t>(i)] = indextable[static_cast<std::size_t>(j)];
				indextable[static_cast<std::size_t>(j)] = row;
			}
			else
			{
				indextable[static_cast<std::size_t>(i)] = row;
			}
			++bigcnt;
		}
		else
		{
			indextable[static_cast<std::size_t>(i)] = row;
		}

		++i; // ★ 只在模板有效(t >= 0)时推进 —— 源码 :1463 在 CHECKINDEX 块内。
	}

	// 裁成实际长度(原版返回 int* + -1 结尾,我们返回紧凑 vector)。
	std::vector<std::int32_t> out;
	out.reserve(static_cast<std::size_t>(i));
	for (int k = 0; k < i; ++k)
		out.push_back(indextable[static_cast<std::size_t>(k)]);
	return out;
}

// ── 敌人生成与入场(批次 M.4b · 等级摇号 M.5)──────────────────────────
//
// 详注见 world/Api.h 的声明处。移植来源 `ENEMY_createEnemy`(展开视图
// `char/enemy.c:994-1180`),建 / 不建逐条见 `shared/model/Enemy.h` 文末。

// 等级摇号 —— 源码 :1034 的 `RAND(LV_MIN, LV_MAX)` + 载入期归一 :479-486。
//
// ★ 归一在此而非载入期的三条等价判据(幂等 / 不耗 rng / 只碰这两列)见 Api.h 声明处。
std::int32_t rollEncounterLevel(const EnemyEncounter &enc, SA::Rules::Random &rng)
{
	// ── 归一 ①(源码 :483):`lv_min == 0` ⇒ **取 lv_max**,不是"从 0 级起" ────
	//
	// ⚠️★ 这一条最容易被漏掉,而漏掉它的表现是**整批低等级怪变弱**而不报错:
	//    配 `0,18` 的行原版给固定 18 级,漏了归一就变成 `RAND(0,18)`。
	//    ★ 实测 `enemy1.txt` 2154 行里这条**一次都不触发**(`lv_min == 0` 为 0 行)——
	//      正因为如此,它只能靠手造数据的用例钉住;而"真数据跑得过"证明不了它。
	std::int32_t lo = enc.lv_min;
	const std::int32_t hi_raw = enc.lv_max;
	if (lo == 0)
		lo = hi_raw;

	// ── 归一 ②(源码 :484-485):写反了自动纠正 ──────────────────────────
	//
	// ⚠️★ **这一步的理由在 2026-09-09 换过一次(DR-BT23)**:原先写的是
	//    「它是 `Rules::Random::rand` 的 `lo <= hi` 前提的唯一保证者」——
	//    而退化区间现在**有定义**(返回 `lo` 且照常消耗一次)⇒ 那个理由失效。
	// ✅ 现判据:**原版在载入期就做了这两条归一**(`enemy.c:479-486`)
	//    ⇒ 删掉它就不是"引入 UB",而是**与原版行为不等价** —— 后者一样不可接受,
	//      且它是源码事实,不会再随接口口径变化(`00` §9.0.36 ④)。
	const std::int32_t lv_min = lo < hi_raw ? lo : hi_raw;
	const std::int32_t lv_max = lo < hi_raw ? hi_raw : lo;

	// ★ 闭区间 —— 原版 `RAND(x,y)` 展开后是 `x + (int)((y-x+1)*rand()/(RAND_MAX+1))`
	//   (`include/util.h:79`)⇒ 取得到 y。`Random::rand` 同语义,不必换算。
	// ⚠️ `lv_min == lv_max` 时**照样摇一次**(实测 1142/2154 行是这种):
	//    结果恒等于它,但**rng 被消耗了一次** ⇒ 不能"优化"成直接 return,
	//    那会让固定等级的怪与区间等级的怪走出不同长度的随机序列 ⇒ 回放对不上。
	return rng.rand(lv_min, lv_max);
}

// ── 敌人基础经验表(源码 `include/enemyexptbl.h` 的 `enemybaseexptbl[]`)──────────
//
// ★ 200 个硬编码值,下标 = level − 1(`kEnemyBaseExpTbl[0]` = 1 级基础经验)。
//   `enemyExp()` 里 `level--` 后取它,level < 1 或 > 200 越界 ⇒ 返 0(源码 :780)。
// ⚠️★★ **74 级是一处递减异常**:73 级 959 → 74 级 **956**(比前一个小),75 级又跳到 1012。
//    这是**原版数据的毛刺**(否则整表单调递增),照抄不修 —— 改成插值(如 985)是把猜测
//    固化,同「实测异常照抄」纪律。将来这表若走 D 线入库,导入器**不得**顺手纠正它。
constexpr std::array<std::int32_t, 200> kEnemyBaseExpTbl = {
    1,
    2,
    3,
    4,
    5,
    6,
    9,
    12,
    15,
    18, // level   1-10
    22,
    26,
    30,
    35,
    40,
    46,
    52,
    58,
    65,
    72, //        11-20
    79,
    87,
    95,
    104,
    113,
    122,
    131,
    141,
    151,
    162, //        21-30
    173,
    184,
    196,
    208,
    220,
    233,
    246,
    260,
    274,
    288, //        31-40
    303,
    318,
    333,
    348,
    365,
    381,
    398,
    415,
    432,
    450, //        41-50
    468,
    486,
    506,
    525,
    545,
    564,
    585,
    606,
    627,
    648, //        51-60
    670,
    692,
    714,
    737,
    760,
    784,
    808,
    832,
    857,
    882, //        61-70
    907,
    933,
    959,
    956, // ★★ 74 级递减异常:比上一个(73 级 959)小 —— 原版数据毛刺,照抄不修
    1012,
    1040,
    1067,
    1095,
    1123,
    1152, //        71-80
    1181,
    1210,
    1240,
    1270,
    1300,
    1331,
    1362,
    1394,
    1426,
    1458, //        81-90
    1490,
    1524,
    1557,
    1590,
    1625,
    1659,
    1694,
    1729,
    1764,
    1800, //       91-100
    1836,
    1872,
    1909,
    1946,
    1983,
    2021,
    2059,
    2097,
    2136,
    2175, //      101-110
    2214,
    2254,
    2294,
    2334,
    2374,
    2414,
    2455,
    2496,
    2537,
    2578, //      111-120
    2619,
    2661,
    2703,
    2745,
    2787,
    2829,
    2872,
    2915,
    2958,
    3000, //      121-130
    3043,
    3088,
    3132,
    3176,
    3220,
    3264,
    3309,
    3354,
    3399,
    3444, //      131-140
    3489,
    3535,
    3581,
    3627,
    3673,
    3719,
    3765,
    3812,
    3859,
    3906, //      141-150
    3953,
    4000,
    4047,
    4095,
    4143,
    4191,
    4239,
    4287,
    4335,
    4384, //      151-160
    4433,
    4482,
    4531,
    4580,
    4629,
    4679,
    4729,
    4779,
    4829,
    4879, //      161-170
    4929,
    4980,
    5031,
    5082,
    5133,
    5184,
    5235,
    5287,
    5339,
    5391, //      171-180
    5443,
    5495,
    5547,
    5599,
    5652,
    5705,
    5758,
    5811,
    5864,
    5917, //      181-190
    5970,
    6024,
    6078,
    6132,
    6186,
    6240,
    6295,
    6350,
    6405,
    6460, //      191-200
};

// 敌人身上的经验值 —— 1:1 移植 `ENEMY_getExp`(展开视图 `char/enemy.c:761-799`)。
//
// ★★ **签名不收 `EnemyEncounter`**:生效行(:796)只读模板 `tp` + 入参 level / rank,
//    敌人表行 `p` 只出现在 :795 那条被注释掉的旧式里(校正见 `Enemy.h` 文末 ⑥)。
// 公式(源码逐行):
//   :779  level--;                              ← 下标是「等级 − 1」
//   :780  越界(含 level<=0)返 0;
//   :786  rank<0||rank>5 ⇒ 归 0 档;
//   :787  rankBonus = ranktbl[rank].rank;        ← {2.5,2.0,1.5,1.0,0.5,0.0}(num 列不用,不建)
//   :789  alpha = (critical+counter+GET+poison+paralysis+sleep+stone+drunk+confusion)
//               / 100.0 + rare;                  ← ★ GET = capture_difficulty(一列两用)
//   :796  ret  = base[level] + (rankBonus + alpha) * (level+1);  ← level 已--,+1 复原成原等级
//   :797  return ret < 1 ? 1 : ret;              ← 保底 1
// ⚠️ 浮点类型贴源码:`/100.0` 是 double 除;最终整段在 float 域求值后截断成 int。
//    逐位一致依赖 sa_world 的 `-ffp-contract=off`(见 CMakeLists,与 sa_shared 同源)。
std::int32_t enemyExp(const EnemyTemplate &tmpl, std::int32_t level, std::int32_t rank)
{
	level -= 1;
	if (level < 0 || level >= static_cast<std::int32_t>(kEnemyBaseExpTbl.size()))
		return 0;

	static constexpr float kRankBonus[6] = {2.5f, 2.0f, 1.5f, 1.0f, 0.5f, 0.0f};
	if (rank < 0 || rank > 5)
		rank = 0;
	const float rank_bonus = kRankBonus[static_cast<std::size_t>(rank)];

	// ★ E_T_GET 项就是 `capture_difficulty`(Api.h 的 EnemyTemplate 已一列两用)。
	const std::int32_t resist_sum =
	    tmpl.critical + tmpl.counter + tmpl.capture_difficulty + tmpl.poison + tmpl.paralysis + tmpl.sleep + tmpl.stone + tmpl.drunk + tmpl.confusion;
	const float alpha =
	    static_cast<float>(static_cast<double>(resist_sum) / 100.0 + tmpl.rare);

	const float raw =
	    static_cast<float>(kEnemyBaseExpTbl[static_cast<std::size_t>(level)]) + (rank_bonus + alpha) * static_cast<float>(level + 1);
	const std::int32_t ret = static_cast<std::int32_t>(raw);
	return ret < 1 ? 1 : ret;
}

SA::Model::Enemy spawnEnemy(const EnemyTemplate &tmpl, const EnemyEncounter &enc,
                            std::int32_t baselevel, SA::Rules::Random &rng,
                            const SA::Rules::RulesConfig &cfg)
{
	SA::Model::Enemy out{};

	// ── 图号(源码 :1023-1024):两个槽同值 ──────────────────────────
	out.origin_image = tmpl.image;
	out.base_image = tmpl.image;

	// ── 等级(源码 :1030-1035)★ 两个分支都是原版 ─────────────────────
	//
	// ⚠️★★ **摇号必须在 `rollSpawnStats` 之前**(源码 :1034 早于 :1045 的 ±2 扰动)——
	//    顺序即语义:调换会让同种子下的四维整体变化,而**没有一处会报错**
	//    (同 M.4b 成长率取"扰动后、撒点前"那一刻的理由)。
	// ★ 因此这一段放在这里而不是函数开头:它必须在四维之前、图号之后无所谓。
	out.level = baselevel > 0 ? baselevel : rollEncounterLevel(enc, rng);

	// ── 四维 + 成长率(源码 :1045-1070)= DR-DT10 的 `rollSpawnStats` ───
	//
	// ★★ **这一行是欠债 25 的关闭点**:公式自 M.4a 起就在 `shared/rules`,
	//    但在此之前**没有任何调用方**。
	// ⚠️ 四步顺序(±2 → 打包成长率 → 撒 10 点 → PARAM_CAL)整个封在那个纯函数里,
	//    这里不得拆开或重排 —— 详见 `Progression.cpp` 的四步说明。
	// ⚠️★ 喂给它的是 `out.level`(**已决定的**等级)而不是 `baselevel` ——
	//    ★★ 传后者的后果**不是"算出 0 或负数"而是"算小一个数量级"**:
	//      coef = (level − 1) × lvup + init ⇒ level 0 时 = −4.5 + 10 = **5.5**(仍为正)
	//      ⇒ 乌力的 vital 会是 165 而不是 840。⚠️ 因此「四维 > 0」这种量级断言
	//      **抓不到这个错**,必须逐值 —— 这一条是 M.5 反向验证逼出来的
	//      (注入它时七条断言一条都没红,详见 `WorldTickTest` 同名用例)。
	const SA::Rules::SpawnStats rolled =
	    SA::Rules::rollSpawnStats(tmpl.stats, out.level, rng, cfg);
	out.vital = rolled.vital;
	out.str = rolled.str;
	out.tough = rolled.tough;
	out.dex = rolled.dex;
	out.growth_vital = rolled.growth_vital;
	out.growth_str = rolled.growth_str;
	out.growth_tough = rolled.growth_tough;
	out.growth_dex = rolled.growth_dex;

	// ── 四属性(源码 :1071-1074)────────────────────────────────────
	// ★ 模板侧已按 **地水火风** 具名(见 EnemyTemplate),此处逐字段对拷 ⇒
	//   顺序陷阱在类型层面就没有发生的余地。
	out.earth = tmpl.earth;
	out.water = tmpl.water;
	out.fire = tmpl.fire;
	out.wind = tmpl.wind;

	// ── AI(源码 :1075-1076)────────────────────────────────────────
	out.mod_ai = tmpl.mod_ai;
	// ★ `VARIABLEAI = 0` 照抄。⚠️ 它与"幸运"是同一个物理槽 —— 捕获会以幸运的名义
	//   把这个 0 拷进宠物(见 `Enemy.h` 卷首与 `createPetFromCapture` 同处)。
	out.variable_ai = 0;

	// ── 评级(源码 :1096-1097)──────────────────────────────────────
	// ★ 判据是**模板原始基数之和**,与本次摇号无关 ⇒ 传 `tmpl.stats` 而不是 `rolled`。
	//   ⚠️ 传 rolled 会让同模板摇出不同 rank 而没有一处报错,详见 `enemyRank` 声明处。
	out.pet_rank = SA::Rules::enemyRank(tmpl.stats);

	// ── 名字(源码 :1108-1110)──────────────────────────────────────
	out.name = tmpl.name;

	// ── 宠技槽(源码 :1092-1094;原始 8.5 树 `enemy.c:1204-1206`,批次 B2a)──
	// ★ 1:1 那个整组拷循环:`for(i) CharNew.unionTable.indexOfPetskill[i] =
	//   *(tp + E_T_PETSKILL1 + i)` —— 一处夹取 / 清洗都没有(0 = 无技能、-1 = 空槽、
	//   表外死引用全部照存,取值域讨论见 EnemyTemplate::pet_skills)。
	// ⚠️ 消费方:① 捕获时整组拷给宠物(pet.c:375-377 同款);② 敌人侧本批**不用**
	//   (fillEnemyCommands 只填普攻,敌人 AI 属后续批)。
	for (std::size_t i = 0; i < SA::Model::Enemy::kPetSkillSlots; ++i)
		out.pet_skills[i] = tmpl.pet_skills[i];

	// ── 模板号(源码 :1200 `CHAR_PETID = *(tp + E_T_TEMPNO)`)──────────
	// ★ `CHAR_PETID` 的值 = 模板号,捕获扣道具 `IsNeedCaptureItem` 据它查 `NeedEnemy[]` 表。
	//   ⚠️ 别与 `ENEMY_ID`(遇敌表,归 D 线)混,见 `Enemy.h` 的 `pet_id` 注释。
	out.pet_id = tmpl.temp_no;

	// ── 捕获相关(源码 :1165-1166)★ 两个 WORK 字段,来源两张表 ─────────
	//
	// ⚠️★★ **左右两边的来源不同,这一行是那个区分的落点**(M.5 把它接对了):
	//      `capturable`         ← 敌人表 `ENEMY_PETFLG`(c14,源码 :1165)
	//      `capture_difficulty` ← 模板表 `E_T_GET`     (源码 :1166)
	//    ⇒ 同一只怪在不同敌人表配置下可捕 / 不可捕,而难度跟着模板走。
	//    ★ M.4b 时 `capturable` 权宜地挂在 `EnemyTemplate` 上(敌人表未移植),
	//      现在归位。⚠️ 别把 `enc` 换成 `tmpl` —— 模板表 c38 也有个叫 `E_T_PETFLG`
	//      的列,而 `ENEMY_createEnemy` **从不读它**(见 Api.h 那条)。
	out.capturable = enc.capturable;
	out.capture_difficulty = tmpl.capture_difficulty;

	// ── 生命(源码 :1153 推导 → :1159 满血)──────────────────────────
	//
	// ★ `hp = deriveBaseStats(四维).max_hp` —— **不存 max_hp**(不造第二真源,
	//   同 `Model::Pet`);投影到战场时再推一次(`enterEnemyToField`)。
	// ⚠️★ 推导只吃四维、不吃等级(DR-DT9:公式里没有 level)⇒ 等级的作用**全部**
	//    发生在上面 `rollSpawnStats` 那一步。这条已在 M.3 纠正过一次文档分叉,别再写反。
	out.hp = SA::Rules::deriveBaseStats(out.vital, out.str, out.tough, out.dex).max_hp;

	// ⚠️ `mp` / `max_mp` 留 0 —— 源码从不写它们,默认模板里也是 0(见 `Enemy.h`)。

	// ── 战果:经验值 / 决斗点判定树(源码 :1028 + :1101-1107)★ 三值一棵树,不拆开 ────
	//
	// `duelpoint` 无条件写(源码 :1101);仅当 `duelpoint <= 0` 才给 `exp`(源码 :1102):
	//   `enc.exp != -1` 用敌人表值 · `enc.exp == -1` 哨兵 ⇒ 走 `enemyExp()`(源码 :1103-1107)。
	// ⚠️★ `duelpoint > 0` 的「决斗点怪」`exp` 保持默认 0 —— 那场结算走决斗点、不走经验
	//    (`battle.c:2267` 的 `dpbattle`),而决斗点分配本批未做(登记残缺,见 `Enemy.h`)。
	// ★ `enemyExp` 的 rank 传刚算好的 `out.pet_rank`(与源码 :1106 传 `enemyrank` 同一个值)。
	out.duelpoint = enc.duelpoint;
	if (enc.duelpoint <= 0)
	{
		out.exp = enc.exp != -1 ? enc.exp : enemyExp(tmpl, out.level, out.pet_rank);
	}

	// ── 预掉落道具(源码 :1210-1224)★ 千分率摇进敌人预掉落槽,道具域第三批 I.3 ──────
	//
	// ⚠️★ **必须在 `rollSpawnStats`(上方四维)之后摇** —— 源码顺序四维 :1067 → 掉落 :1210,
	//    其间无 rng 消耗 ⇒ 放这里(rank/name/hp/exp 之后)与源码同种子下逐位一致。
	// ★ 只在 `item_prob != 0` 的槽摇(源码 :1211 `if(ITEMPROB != 0)`)⇒ prob=0 不耗 rng
	//   ⇒ 未配掉落的敌人 rng 序列与本批之前一致(现有用例不受影响,同 I.1「默认恒 0」)。
	// ★ 千分率 `RAND(0,999) < prob`(源码 :1213,`_FIX_ITEMPROB` ON);紧凑存 item_id、
	//   保持摇号顺序(见 `Enemy.h` dropped_items 注释)。
	for (int i = 0; i < SA::Model::Enemy::kMaxDrops; ++i)
	{
		if (enc.item_prob[i] == 0)
			continue;
		if (rng.rand(0, 999) < enc.item_prob[i])
		{
			out.dropped_items[static_cast<std::size_t>(out.drop_count)] = enc.item[i];
			++out.drop_count;
		}
	}

	return out;
}

bool enterEnemyToField(SA::Rules::BattleField &field, int field_slot,
                       const SA::Model::Enemy &enemy)
{
	// ── 门 ①:敌人可使用任一侧完整的 10 格；只拒绝越界 ────────────
	if (field_slot < 0 || field_slot >= SA::Rules::kSlotCount)
		return false;

	// ── 门 ②:目标槽未被占(源码 `NewEntry:975` ⇒ ENTRYMAX)────────────
	SA::Rules::Combatant &dst = field.at(field_slot);
	if (dst.occupied)
		return false;

	// ── 投影 Enemy → Combatant ────────────────────────────────────────
	// ★ 先清成干净单位,不留前一个占据该槽者的脏值(同 enterPetToField)。
	dst = SA::Rules::Combatant{};
	dst.occupied = true;
	dst.kind = SA::Rules::CombatantKind::kEnemy;
	dst.slot = static_cast<std::uint8_t>(field_slot);
	dst.pet_id = enemy.pet_id;
	dst.level = enemy.level;
	dst.hp = enemy.hp;
	dst.mp = enemy.mp;
	dst.max_mp = enemy.max_mp;

	// ⚠️★ `luck` **留 0** —— 敌人没有幸运这个属性(同槽异义,`Enemy.h` 卷首)。
	//    ★ 不写 `dst.luck = 0;` 这一行:结构默认就是 0,写出来反而像"我们决定填 0"。
	//    ⚠️ 后果是可观察的:`luck` 参与 DR-BT1 的量化前提(上限 25)⇒ 敌人在那些
	//      公式里恒取幸运 0。这是原版行为,不是我们省事。

	// ⚠️★★ 四属**按具名下标写,绝不按位置拷**(三套顺序两两不同,已栽过两次)。
	dst.elements[static_cast<int>(SA::Rules::Element::kEarth)] = enemy.earth;
	dst.elements[static_cast<int>(SA::Rules::Element::kWater)] = enemy.water;
	dst.elements[static_cast<int>(SA::Rules::Element::kFire)] = enemy.fire;
	dst.elements[static_cast<int>(SA::Rules::Element::kWind)] = enemy.wind;

	// ── 属性推导:四维 → 基础三围 + max_hp(DR-DT9)────────────────────
	const SA::Rules::DerivedStats stats =
	    SA::Rules::deriveBaseStats(enemy.vital, enemy.str, enemy.tough, enemy.dex);

	// ── ★★ 原始四维直拷(批次 L4.1,理由同 enterPetToField)──────────────
	dst.vital = enemy.vital;
	dst.str = enemy.str;
	dst.tough = enemy.tough;
	dst.dex = enemy.dex;

	dst.attack = stats.attack;
	dst.defense = stats.defense;
	dst.quick = stats.quick;
	dst.fix_dex = stats.quick;
	dst.max_hp = stats.max_hp;
	// ⚠️ HP 不夹取 —— 理由**与 enterPetToField 不同**,见 Api.h 声明处:
	//    夹取属回合准备阶段的 complianceParameter(`BATTLE_TurnParam`,未移植)。

	// ── ★★ 捕获修正:两个字段第一次有了真数据(源码 :1165-1166)──────────
	//
	// `Combatant.h` 里写着「1.5 无敌人数值表 ⇒ 调用方按 30 兜底 / 一律 false」,
	// 本函数就是那个"将来的调用方"。⇒ 兜底值从此不该再出现在这条路径上。
	dst.mods.capturable = enemy.capturable;
	dst.mods.capture_difficulty = enemy.capture_difficulty;

	// ⚠️ `immune_critical` / `immune_knockback`(DR-BT11 的数据驱动标志)**仍留 false**:
	//    它们的来源是敌人数值表里的免疫标记,而那属 L4 内容导入(D 线)——
	//    ★ 与 `capturable` 不同,后者在 `enemy.txt` / `enemybase1.txt` 里有明确的列
	//    (`ENEMY_PETFLG` / `E_T_GET`),前者在原版**根本没有列**(原版硬编码图号)
	//    ⇒ 那一列是 DR-BT11 要求**新造**的,得等内容表定型,不是从模板里读出来的。
	return true;
}

// ══ 敌人生成入场(批次 M.4b)══════════════════════════════════════
//
// 详注见 world/Api.h 的声明处。★ 三道门,顺序 = 预留 → 提交:
//   两个可失败的动作(池 / 入场)都排在任何不可回退的写之前
//   ⇒ 失败时世界状态一个字节都没动(同 `createPetFromCapture` 的形状)。
bool World::spawnEnemyToField(BattleId battle, std::uint8_t slot,
                              const EnemyTemplate &tmpl, const EnemyEncounter &enc,
                              std::int32_t baselevel)
{
	Impl &s = *_impl;

	// ── 门 ①:战斗与槽号 ──────────────────────────────────────────
	const auto bit = s.battles.find(battle);
	if (bit == s.battles.end())
		return false;
	if (slot >= SA::Rules::kSlotCount)
		return false;
	BattleInstance &b = bit->second;

	// ⚠️★ 该槽已有敌人实体 ⇒ 拒绝。**不是**因为槽被占(那是门 ③ 的事),
	//    而是因为覆盖掉旧句柄就等于泄漏一个池槽 —— 与 M.1 那条漏释放同族。
	if (b.enemy_of_slot[slot].valid())
		return false;

	// ── 门 ②:敌人池 ──────────────────────────────────────────────
	const SA::Model::EntityHandle eh = s.enemies.allocate();
	if (!eh.valid())
	{
		// ⚠️ 池满必须报出来(同 M.1 的 Player 池):容量是硬上限,`allocate` 不会扩容。
		s.logger.log(SA::Platform::LogLevel::kError,
		             SA::Platform::LogEvent::kEntityPoolExhausted,
		             {{"battle_id", battle},
		              {"pool", std::string_view("enemy")},
		              {"capacity", static_cast<std::uint64_t>(kMaxEnemies)}});
		return false;
	}
	SA::Model::Enemy *enemy = s.enemies.resolve(eh);
	if (enemy == nullptr)
	{
		// ★ 走不到(刚 allocate 成功)。守它零成本,理由同 createPetFromCapture。
		return false;
	}

	// ★ 生成:消耗**该场战斗的 rng**(可回放的凭据是战斗种子,见 Api.h 声明处)。
	// ⚠️★ 消耗次数**取决于分支**:`baselevel > 0` ⇒ 14 次;`<= 0` ⇒ 15 次
	//    (多的那次是等级摇号,且在最前面)。改动这里的调用序会改变回放。
	*enemy = spawnEnemy(tmpl, enc, baselevel, b.rng, s.rules_config);

	// ── 门 ③:入场投影 ────────────────────────────────────────────
	if (!enterEnemyToField(b.field, static_cast<int>(slot), *enemy))
	{
		// ⚠️★ **失败要把刚分配的实体还回去** —— 否则每次入场失败都泄漏一个槽,
		//    而"入场失败"是完全正常的(槽被占 / 槽在宠位)⇒ 泄漏会累积得很快。
		//    ★ 这一步就是"预留 → 提交"里的**回滚**:门 ② 的预留可撤销,所以能这样写。
		(void)s.enemies.release(eh);
		return false;
	}

	// ── 提交:记下「槽 → 敌人实体」的映射 ────────────────────────────
	// ★ 到这里没有可失败的动作了。捕获要靠这条映射找到四维的源头。
	b.enemy_of_slot[slot] = eh;
	b.dp_battle = b.dp_battle || enemy->duelpoint > 0;
	s.pushBattleSnapshot(b);

	// ⚠️★ 记的是 `enemy->level`(**实际生效**的等级)而不是入参 `baselevel` ——
	//    摇号分支下入参是 0,记它等于什么都没记。★ 同时记 `enemy_id`,
	//    否则"这只怪是哪一行配出来的"在日志里无从追溯(敌人表 44 处引用都靠它)。
	s.logger.log(SA::Platform::LogLevel::kDebug,
	             SA::Platform::LogEvent::kBattleJoined,
	             {{"battle_id", battle},
	              {"slot", static_cast<std::uint64_t>(slot)},
	              {"level", static_cast<std::uint64_t>(enemy->level)},
	              {"enemy_id", static_cast<std::uint64_t>(enc.enemy_id)},
	              {"kind", std::string_view("enemy")}});
	return true;
}

void World::loadEncounterTables(std::vector<EncountArea> areas,
                                std::vector<EnemyGroup> groups,
                                std::vector<EnemyEncounter> encounters,
                                std::vector<EnemyTemplate> templates)
{
	Impl &s = *_impl;
	s.encount_areas = std::move(areas);
	s.enemy_groups = std::move(groups);
	s.encounters = std::move(encounters);
	s.enemy_templates = std::move(templates);
}

// 遇敌命中后的开战组装(批次 W.4)——移植 `EN_recv`(`callfromcli.c:1249`)清走路串 +
//   `BATTLE_CreateVsEnemy(charaindex,0,-1)` 净核(`battle.c:2528`):
//   遇敌链(`pickEnemyGroup`→`rollEnemyList`)→ 建场 → 玩家入场 → 逐只敌人入场。
// ⚠️★ 遇敌链的 rng 用**世界 rng**(`s.random`)——原版 `ENEMY_getEnemy` 在建 battle **之前**、
//    用全局 `rand()`,不是战斗 rng(战斗此刻还没建;敌人四维生成才用战斗 rng,见 spawnEnemyToField)。
bool World::triggerEncounter(SA::Net::SessionId session, std::int32_t area_row)
{
	Impl &s = *_impl;
	if (s.inBattle(session))
		return false;
	if (s.partyModeOf(session) == PartyMode::kMember)
		return false;
	if (area_row < 0 ||
	    static_cast<std::size_t>(area_row) >= s.encount_areas.size())
		return false;
	const EncountArea &area = s.encount_areas[static_cast<std::size_t>(area_row)];

	// SSRC80 enemy.c:1421–1445 扫全部持有槽，包含装备位（不是仅背包段）。
	std::vector<std::int32_t> inventory;
	if (const auto *player = s.players.resolve(s.player_of_session.find(session)))
		for (auto handle : player->items)
			if (const auto *item = s.items.resolve(handle))
				inventory.push_back(item->item_id);
	const std::int32_t grow =
	    pickEnemyGroup(area, s.enemy_groups, inventory, s.world_rng);
	if (grow < 0)
		return false;

	// ── 选敌人列表(编组 → 敌人表行下标序列,含大怪布阵顺序)────────────────
	const std::vector<std::int32_t> rows =
	    rollEnemyList(s.enemy_groups[static_cast<std::size_t>(grow)], s.encounters,
	                  s.enemy_templates, area.enemy_max_num, s.world_rng);
	if (rows.empty())
		return false; // 无候选 ⇒ 本次不遇敌

	// ── 建场 + 玩家与队伍入场 (Side[0]) ──────────────────────────────
	SA::Rules::BattleField field{};
	std::vector<SA::Net::SessionId> battle_party_members{};
	if (s.partyModeOf(session) == PartyMode::kLeader)
	{
		battle_party_members = s.partyMembersOf(session);
	}
	else
	{
		battle_party_members = {session};
	}

	for (std::size_t idx = 0; idx < battle_party_members.size() && idx < SA::Rules::kBattlePlayerMax; ++idx)
	{
		const auto mid = battle_party_members[idx];
		field.at(static_cast<int>(idx)) = makePlayerCombatant(s.players.resolve(s.player_of_session.find(mid)),
		                                                      playerEquipModifiers(mid),
		                                                      s.getRidingPet(mid));
	}

	const BattleId battle = startBattle(field);
	for (std::size_t idx = 0; idx < battle_party_members.size() && idx < SA::Rules::kBattlePlayerMax; ++idx)
	{
		const auto mid = battle_party_members[idx];
		if (!joinBattle(battle, mid, static_cast<std::uint8_t>(idx)))
		{
			s.logger.log(SA::Platform::LogLevel::kError,
			             SA::Platform::LogEvent::kBattleJoinFailed,
			             {{"battle_id", battle},
			              {"session_id", mid},
			              {"reason", std::string_view("encounter_party_join_failed")}});
		}
	}

	// ── 逐只敌人入场(Side[1] 起,baselevel=-1 野外摇号)──────────────────
	//   ★ `rollEnemyList` 已含大怪布阵顺序 ⇒ 第 i 只落敌方槽 `kSideOffset + i`。
	//   敌人按类型可用完整 10 格；玩家的 5 格上限不适用于敌人（F14）。
	int placed = 0;
	for (std::size_t i = 0;
	     i < rows.size() && placed < SA::Rules::kSideOffset; ++i)
	{
		const std::int32_t erow = rows[i];
		if (erow < 0 || static_cast<std::size_t>(erow) >= s.encounters.size())
			continue;
		const EnemyEncounter &enc = s.encounters[static_cast<std::size_t>(erow)];
		const std::int32_t trow = findEnemyTemplate(s.enemy_templates, enc.temp_no);
		if (trow < 0)
			continue; // 模板查不到 ⇒ 整只不放(同 rollEnemyList 内大怪布阵的处置)
		const EnemyTemplate &tmpl = s.enemy_templates[static_cast<std::size_t>(trow)];
		const std::uint8_t slot =
		    static_cast<std::uint8_t>(SA::Rules::kSideOffset + placed);
		if (spawnEnemyToField(battle, slot, tmpl, enc, /*baselevel=*/-1))
			++placed;
	}

	if (placed == 0)
	{
		// ⚠️ 选出了怪却一只都没落地(全被模板/槽门挡)⇒ 空战斗;tick 会判空侧结束,
		//    但这是异常路径,先记一笔(同 §10.4 那族"看起来做了、其实没写")。
		s.logger.log(SA::Platform::LogLevel::kWarn,
		             SA::Platform::LogEvent::kBattleJoinFailed,
		             {{"battle_id", battle},
		              {"session_id", session},
		              {"reason", std::string_view("encounter_no_enemy_placed")}});
	}
	// ★ 成功路径不额外 log:startBattle(kBattleStarted+kBattleSeed)/joinBattle/
	//   spawnEnemyToField 已各自记账,遇敌只是它们的调用者。
	return placed > 0;
}

// ══ 明雷开战(批次 W.5)═══════════════════════════════════════════════
//   移植 EV 事件链 EVENT_main → NPC_NPCEnemy_Encount → NPC_NPCEnemy_BattleIn →
//   BATTLE_CreateVsEnemy(player,_,enemy)(npc_npcenemy.c:672/674)的净核。
//   ★ 与暗雷 triggerEncounter 的关键区别见 Api.h 声明处:用世界态**已存在**的敌人实体,
//     转移 handle 所有权,不 allocate / 不 spawnEnemy / 不耗战斗 rng。
bool World::triggerNpcEnemyBattle(SA::Net::SessionId session, std::size_t world_enemy_idx)
{
	Impl &s = *_impl;
	if (s.inBattle(session))
		return false;
	if (s.partyModeOf(session) == PartyMode::kMember)
		return false;
	if (world_enemy_idx >= s.world_enemies.size())
		return false;
	// ★ 拷一份 WorldEnemy:下面要 erase(world_enemies),持有引用会失效。
	const Impl::WorldEnemy we = s.world_enemies[world_enemy_idx];
	SA::Model::Enemy *enemy = s.enemies.resolve(we.handle);
	if (enemy == nullptr)
		return false;
	// 记世界坐标:广播消失要在 erase 前用它(enterEnemyToField 只改战场投影,不动 Enemy 的 x/y)。
	const std::int32_t ex = enemy->x;
	const std::int32_t ey = enemy->y;

	// ── 建场 + 玩家与队伍入场 (Side[0], 同 triggerEncounter) ──────────────────
	SA::Rules::BattleField field{};
	std::vector<SA::Net::SessionId> battle_party_members{};
	if (s.partyModeOf(session) == PartyMode::kLeader)
	{
		battle_party_members = s.partyMembersOf(session);
	}
	else
	{
		battle_party_members = {session};
	}

	for (std::size_t idx = 0; idx < battle_party_members.size() && idx < SA::Rules::kBattlePlayerMax; ++idx)
	{
		const auto mid = battle_party_members[idx];
		field.at(static_cast<int>(idx)) = makePlayerCombatant(s.players.resolve(s.player_of_session.find(mid)),
		                                                      playerEquipModifiers(mid),
		                                                      s.getRidingPet(mid));
	}

	const BattleId battle = startBattle(field);
	for (std::size_t idx = 0; idx < battle_party_members.size() && idx < SA::Rules::kBattlePlayerMax; ++idx)
	{
		const auto mid = battle_party_members[idx];
		if (!joinBattle(battle, mid, static_cast<std::uint8_t>(idx)))
		{
			s.logger.log(SA::Platform::LogLevel::kError,
			             SA::Platform::LogEvent::kBattleJoinFailed,
			             {{"battle_id", battle},
			              {"session_id", mid},
			              {"reason", std::string_view("npcenemy_party_join_failed")}});
		}
	}

	// ── 明雷入场(敌方首槽 kSideOffset)——★ 用**已存在**的敌人实体,转移所有权 ──────────
	const auto bit = s.battles.find(battle);
	if (bit == s.battles.end())
		return false; // 走不到(startBattle 刚建),守它零成本(同 spawnEnemyToField)。
	BattleInstance &b = bit->second;
	const std::uint8_t slot = static_cast<std::uint8_t>(SA::Rules::kSideOffset);
	if (b.enemy_of_slot[slot].valid())
		return false; // 敌方首槽被占(空场刚建,不该发生)——守它。
	if (!enterEnemyToField(b.field, static_cast<int>(slot), *enemy))
		return false; // 入场门(槽越界/被占):空战斗留 tick 收尾,同 triggerEncounter。
	// ★★ 所有权转移:同一 EntityHandle 从世界态挪到战斗态(enemy_of_slot),不 allocate/不 release
	//    ⇒ 战斗结束按暗雷同一路径回池;enemyCount() 守恒(不是新建一只)。
	b.enemy_of_slot[slot] = we.handle;
	b.dp_battle = b.dp_battle || enemy->duelpoint > 0;
	s.pushBattleSnapshot(b);

	// ── 从世界态移除 + 广播消失(原版明雷进战斗态即从地图消失)───────────────────
	//   ⚠️ 先广播(用移除前的世界坐标)再 erase;broadcastEnemyDespawn 单向发给视野内玩家。
	s.broadcastEnemyDespawn(enemy->floor, ex, ey, encodeHandle(we.handle));
	s.world_enemies.erase(s.world_enemies.begin() +
	                      static_cast<std::ptrdiff_t>(world_enemy_idx));
	return true;
}

} // namespace SA::World
