// shared/model/Pet.h —— Pet 族实体(L2 领域模型,批次 M.1)
//
// ★★ 本文件的字段清单**不是自拟的**,取自原版 `PET_createPetFromCharaIndex`
//    (展开视图 `char/pet.c:325-405`)实际拷贝的那 30 项 —— 它是"一只宠物由哪些
//    字段构成"在源码里唯一成文的地方。⇒ 建 / 不建**逐条记明**(见文末),不静默省略。
//
// ⚠️ 行号基准 = `stoneage-plan/tools/unifdef_80/` **展开视图**,不是原始 8.5 树
//    (两套差约 340 行,00 §9.0.25 ⑤)。
//
// ── M1 / §4.2:为什么 Pet 必须是独立类型,不是 Player 的一组 flag ──────────
//   `CHAR_PET` 段 19 条别名是**「玩家 vs 宠物」的整体同槽异义**,危险度高于
//   NPCWORKINT 那批:同一个 Char 实例只要 CHAR_TYPE 判错就读到完全不相干的值。
//   ★ 本文件里有三个字段正是那 19 条中的成员,注释逐个点名:
//       mod_ai        = CHAR_CHARM        (宠物 AI 模式 ← 魅力)
//       variable_ai   = CHAR_LUCK         (宠物 AI 变量 ← 幸运)
//       capture_level = CHAR_CHATVOLUME   (捕获等级   ← 音量)
//   ⇒ 合并 Pet 与 Player 会让"魅力"写进"AI 模式";而它**不报错、不崩溃、数据烂掉**
//     (00 §10.4 第一类静默错误)。
//
// ⚠️ shared/ 只依赖标准库 + 项目内 rules/ model/ domain/(01 §4)。

#ifndef __SA_Pet_H__
#define __SA_Pet_H__

#include <cstdint>

#include "model/EntityKind.h"
#include "model/Handle.h"
#include "sa_idl_runtime.h"

namespace SA::Model
{

// 名字长度上限 = 31 字节(DR-TS5,2026-09-07 用户拍板)。
// ★ 三个字段(角色名 / 称号 / 宠名)统一这个上限,故取同一个常量而不是各写 31。
inline constexpr std::size_t kNameMaxBytes = 31;

using NameStr = SA::IDL::FixedStr<kNameMaxBytes>;

// Pet 族实体。
//
// ★ POD:三根支柱要求池是 std::array 且运行期零分配(15 §9.1)⇒ 本结构不含
//   任何自管内存的成员(NameStr 是定长内联串)。
struct Pet
{
	std::uint64_t uid = 0; // Durable identity, never an EntityHandle.
	// ★ 类型与族的对应在编译期可查 —— 和类型的分派不靠运行期 tag 字段(M2)。
	static constexpr EntityKind kKind = EntityKind::kPet;

	// ── 主人(源码 :390-395)────────────────────────────────────────
	//
	// ★ 原版存三样:`CHAR_WORKPLAYERINDEX`(运行期下标,:390)· `CHAR_OWNERCDKEY`
	//   (:392)· `CHAR_OWNERCHARANAME`(:394)。本批把第一样换成**句柄**
	//   (带 generation ⇒ 主人下线后 resolve 返 nullptr,不脏读复用槽,M10),第三样照留。
	// ⚠️ `owner_cdkey` **有意不建**:它是**持久化冗余**(宠物存档要能找回主人),
	//    而本批不落盘。★ 更要紧的是它会撞出一条新的长度决策 —— cdkey 的字节上限
	//    从未被裁定过,与 DR-TS5 由写代码触发是同一现象(11 §14 已预告"0.2 继续推进
	//    时还会撞出同类条目")⇒ 留到落盘时一并登记,不在这里拍一个数。
	EntityHandle owner{};
	NameStr owner_char_name{};

	// ── 基本数值(源码 :340-342)────────────────────────────────────
	//
	// ⚠️ **没有 max_hp** —— 源码只拷 HP/MP/MAXMP,不拷 MAXHP,因为它是
	//    `CHAR_complianceParameter` 从 vital / 等级 / 装备**推导**出的 `CHAR_WORKMAXHP`。
	//    ⇒ 建一个存储字段会造出第二个真源。推导已移植(DR-DT9),见文末 ②。
	std::int32_t hp = 0;
	std::int32_t mp = 0;
	std::int32_t max_mp = 0;

	// ── 原始四维 + 幸运(源码 :343-347)─────────────────────────────
	//
	// ✅ **四维在批次 M.4b 起有了非 0 来源** —— 捕获时从**敌人 L2 实体**逐个拷
	//    (`Model::Enemy::vital` 等,DR-DT10 的 `rollSpawnStats` 产物)。
	//    ⇒ 欠债 23「战场态宠物三围恒 0」的根在此关闭。
	//
	// ⚠️★ 历史(M.1,2026-09-07)记的缺口在这里,原话保留以便对照 ——
	//    被捕目标在战场里只是 `Rules::Combatant`,那是**战斗输入子集**,存的是
	//    「本回合已重算完毕」的 `attack` / `defense` / `quick`,**没有 vital / str /
	//    tough / dex 这四个原始属性**,也没有名字;而源码是从 `enemyindex`
	//    (一个完整的 Char 实体)拷。⇒ 当时缺的是**敌人侧的 L2 实体**,
	//    M.4b 建了 `Model::Enemy` 族 + `EntityPool<Enemy>` 之后这一半补齐。
	//    ★ 现在的分工是:**四维 / 成长率 / 名字 / 评级 / 图号从 L2 `Enemy` 拷,
	//      HP / MP 从战场 `Combatant` 拷** —— 原版两者是同一个 `Char`,我们分成
	//      「L2 实体」与「战场投影」两半 ⇒ 各拷各自权威的那一半,
	//      HP 必须取战场当前值(原版战斗中就地改 Char 的 HP)。
	//
	// ⚠️★★ **`luck` 恒 0,且这不是缺口 —— 是原版的同槽异义**:
	//    `CHAR_VARIABLEAI == CHAR_LUCK`(`char_base.h:637`)。
	//    源码 :347 以「幸运」的名义读 `CHAR_getInt(enemyindex, CHAR_LUCK)`,
	//    而 `ENEMY_createEnemy:1076` 以「AI 变量」的名义往同一个槽写了 **0**
	//    ⇒ 捕获出的宠物幸运**在原版里就恒 0**,敌人从头到尾没有"幸运"这个属性。
	//    ★ 详见 `Enemy.h` 卷首那条 —— 它是 M1「合并别名是静默错误」的活样本。
	std::int32_t vital = 0;
	std::int32_t str = 0;
	std::int32_t tough = 0;
	std::int32_t dex = 0;
	std::int32_t luck = 0;

	// 四属性(源码 :348-351)。
	//
	// ⚠️★★ **顺序陷阱,本项目已栽过两次,这里是第三处**:
	//   · 原版 `CHAR_*AT` 的拷贝顺序是 **火 水 地 风**(源码 :348-351 逐行 FIREAT /
	//     WATERAT / EARTHAT / WINDAT);
	//   · 而 `Rules::Element` 是 **地(0) 水(1) 火(2) 风(3)**(Constants.h:207-213);
	//   · 再往前还有一层:`05` §3.4 相克表的表头是 **无 火 水 地 风**。
	//   ⇒ **三套顺序,两两不同。**
	// ★ 所以本结构用**具名字段**,并且从 `Combatant::elements[]` 取值时必须按
	//   `elements[static_cast<int>(Element::kFire)]` 具名下标取,
	//   **绝不按位置顺序拷** —— 前两次踩坑分别是「相克表头顺序」与
	//   「BATTLE_ATTR_NONE == 0 撞 Element::kEarth == 0」(§9.0.x 批次 0 归因),
	//   两次都是伤害算错而无一处报错。
	std::int32_t fire = 0;
	std::int32_t water = 0;
	std::int32_t earth = 0;
	std::int32_t wind = 0;

	std::int32_t level = 0;

	// ── 成长率:CHAR_ALLOCPOINT 的展开(M6,源码 :374)────────────────
	//
	// ★★ 原版是 **4 × 8 bit 位打包**在一个 int 里:
	//       vital = (p >> 24) & 0xFF;  str = (p >> 16) & 0xFF;
	//       tgh   = (p >>  8) & 0xFF;  dex = (p >>  0) & 0xFF;
	//   语义**不是**"玩家可分配的属性点",是宠物的四维成长率(03 §6.1)。
	//
	// ⚠️★ M6 要求「逐调用点保留各自的夹取上限」,而那三个上限归属**不同路径**:
	//     · 0..255  = 字段容量        ⇒ ★ 由 uint8_t 类型本身兑现,不需要运行期夹取
	//     · ≤ 60    = 宠物**创建**时   ⇒ `PET_getEvolutionAns`(进化 / 孵化路径)
	//     · ≤ 50    = 宠物**死亡扣属**时
	//   ★ 后两条都**不在捕获路径上**:源码 :374 是 `CharNew.data[CHAR_ALLOCPOINT] =
	//     CHAR_getInt(enemyindex, CHAR_ALLOCPOINT)` —— 直接拷,一处夹取都没有。
	//   ⇒ 本批**不加** ≤60/≤50 夹取。那不是遗漏:在捕获路径上加一个源码没有的夹取,
	//     就是"照文档写会引入原版没有的行为"那一类(§9.0.19 已四次抓到同族问题)。
	std::uint8_t growth_vital = 0;
	std::uint8_t growth_str = 0;
	std::uint8_t growth_tough = 0;
	std::uint8_t growth_dex = 0;

	// ── 名字与捕获等级 ─────────────────────────────────────────────
	NameStr name{};

	// 捕获等级(源码 `battle_event.c:3518`:`PETGETLV = 宠物 LV`)。
	// ⚠️★ 同槽异义:`CHAR_PETGETLV` == `CHAR_CHATVOLUME`(音量)。见卷首。
	std::int32_t capture_level = 0;

	// ── 图号(源码 :337-338)────────────────────────────────────────
	//
	// 源码把两个槽设成同一个值:`BASEBASEIMAGENUMBER` = `BASEIMAGENUMBER` = 敌人图号。
	// 前者是**原始**图号(变身 / 换装后能还原),后者是**当前**图号。
	//
	// ⚠️★ 这**不违反 DR-BT11**。那条裁定的是「雷尔免疫暴击不按图号 101813/101814
	//    硬编码,改数据驱动标志位」—— 管的是**判定逻辑**不得依赖图号。
	//    图号作为**显示数据**是必要的(客户端要知道画什么)。
	//    ⇒ 判定读 `Rules::CombatModifiers` 的标志位,渲染读这里,两者不可互替。
	std::int32_t origin_image = 0;
	std::int32_t base_image = 0;

	// ── AI 与评级(源码 :355 mod_ai · :364 pet_rank · variable_ai 见下)──────
	//
	// ⚠️★ 同槽异义两条,见卷首:mod_ai = CHAR_CHARM · variable_ai = CHAR_LUCK。
	// ⚠️★ `variable_ai` 在捕获路径上**不由 pet.c 写**,而是由 `battle_event.c:3549`
	//    在捕获成功后置 0 —— 与 `luck` 是同一个物理槽(见上方 `luck` 那条),
	//    源码 :347 先以「幸运」的名义拷了一次、:3549 再以「AI 变量」的名义清成 0。
	//    ★ 两个名字轮流写同一个槽,这就是 M1 那条约束存在的理由。
	std::int32_t pet_rank = 0;
	std::int32_t mod_ai = 0;
	std::int32_t variable_ai = 0;

	// ── Y 五项:初值快照(源码 :385-389,批次 M.4b)────────────────────
	//
	// ✅ **本批建起来了** —— M.1/M.2/M.3 三批都记着「有意不建」,理由是"四维尚无
	//    非 0 来源 ⇒ 算出来的 Y 只能是 0,把 0 当初值快照存下来比字段不存在更危险"。
	//    M.4b 让四维有了来源(捕获从 `Model::Enemy` 拷)⇒ 那条阻塞解除。
	//
	// ★ 取值 = 用**新宠自己的四维**跑 `deriveBaseStats` 的产物 + 等级:
	//      y_hp    ← WORKMAXHP      y_atk ← WORKFIXSTR
	//      y_def   ← WORKFIXTOUGH   y_quick ← WORKFIXDEX      y_lv ← CHAR_LV
	//   ⚠️ 顺序照源码::384 先 `CHAR_complianceParameter`(推导),:385-389 再取 WORK 值
	//     ⇒ **Y 是推导后的快照,不是四维本身**。
	//
	// ⚠️★★ **一处回源码核出来、与三份文档转述相反的事实**:此前记的是
	//    「Y 五项是升级 / 成长的基线」—— 源码里**没有任何计算读它们**。
	//    全树实测(2026-09-08)写点 4 处(宠物创建 1 + 敌人创建 3)、读点只有三类:
	//      ① 存档键表 `char.c:6386-6390`(持久化)
	//      ② 角色状态串下发 `char.c:2854-2858`
	//      ③ 宠物道具信息串 `char.c:9002-9006`
	//    ⇒ 语义是**给客户端看的「初始三围」显示值**(`mylua/charbase.c:277-281` 的
	//      中文名「初体力 / 初攻击 / 初防御 / 初速度 / 初等级」印证),
	//      **不参与任何服务端计算**。★ 这一条改变了它的归属判断:它属**显示 + 存档**面,
	//      不是成长公式的输入 ⇒ 将来接成长时**不要**去读它。
	std::int32_t y_hp = 0;
	std::int32_t y_atk = 0;
	std::int32_t y_def = 0;
	std::int32_t y_quick = 0;
	std::int32_t y_lv = 0;
};

// ── 文末:源码拷了、本批**有意不建**的字段(逐条记明,均非遗漏)─────────────
//
// ① **六种状态异常**(源码 :357-362:POISON / PARALYSIS / SLEEP / STONE / DRUNK /
//    CONFUSION)⇒ L4 状态系统未移植。★ 反击(DR-BT17 的余项)也排在 L4 之后、
//    依赖同一个 `BATTLE_GetDamageReact` 状态面 ⇒ 这批字段与那批一起来。
//
// ② ✅ ~~**Y 五项初值**~~ ⇒ **2026-09-08 批次 M.4b 已建**,见上方 `y_*` 字段。
//    ⚠️ 连带纠正了此前记在这里的一句转述(「升级 / 成长拿它做基线」)——
//    源码里没有任何计算读 Y 五项,详见 `y_hp` 那条的实测清单。
//
// ③ `CHAR_RARE` / `CHAR_PETID` / `CHAR_PETENEMYID`(源码 :363, :365-366)⇒ L4 内容导入(D 线)。
//    ⚠️ 与 `Enemy.h` 文末 ② 是同一批:敌人侧也没建 ⇒ 就算建了 Pet 侧也没有源可拷。
//
// ④ `CHAR_CRITIAL` / `CHAR_COUNTER`(源码 :367-368)⇒ 暴击已在 A.3 走
//    `Rules::CombatModifiers.equip_critical`;反击排在 L4 之后。
//    ⚠️ 别看到"暴击已实现"就把 critial 建上 —— 战斗输入走 Combatant,不走 L2 实体。
//
// ⑤ **宠技槽** `unionTable.indexOfPetskill[CHAR_MAXPETSKILLHAVE(7)]`(源码 :371-373)
//    ⇒ 宠技批次。★ 03 §3.2 已裁定 `unionTable` **保留但拆开**(宠物槽与宠物技能槽
//    是两件事),届时按那条落地。
//
// ⑥ ★ `CHAR_PETMAILEFFECT = RAND(0, PETMAIL_EFFECTMAX)`(源码 :369)⇒ 宠物邮件未移植。
//    ★★ **连带后果值得记**:不建它 ⇒ 那次 `RAND` 不摇 ⇒ `applyEvents` **不需要
//    注入 RandomSource**(它当前签名里没有)。反过来若照建,就得为一个不存在的子系统
//    改世界写那一层的分层。★ 有先例:源码自己把 `CHAR_EXP` 那行注释掉了(:352)。
//    ⚠️ 摇不摇会让随机序列与原版不同 —— 但原版不可运行(P1)、没有可比对的序列,
//      且黄金用例集是我们自己的基线 ⇒ 无影响。
//
// ⑦ `CHAR_SLOT`(源码 :354)⇒ ★ **含义未定**,不猜。属 03 §11 欠债 1(693 字段逐条
//    「含义 / 取值域 / 单位」,5–8 天)。⚠️ 建一个不知道含义的字段等于把猜测固化。
//
// ⑧ ★ `CHAR_setMaxExpFromLevel`(源码 :383)⇒ **经验曲线**(`06` §1:两条并行路径),
//    与 `Enemy.h` 文末 ⑥ 的战果结算同一前置。⚠️ 它在 :384 的推导**之前**调,
//    但推导不读经验 ⇒ 接它的位置不受推导约束。
//
// ⚠️★ **本批顺手校正了本文件的一批行号引用**(M.1 写下时系统性偏 1–2 行,
//    最误导的一处是把 `CHAR_SLOT` 记成 :352 —— 那一行其实是被注释掉的 `CHAR_EXP`)。
//    行号基准仍是展开视图(见卷首),校正依据是 2026-09-08 对 `char/pet.c:325-399` 的逐行复核。

} // namespace SA::Model

#endif // __SA_Pet_H__
