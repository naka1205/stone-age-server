// shared/model/Player.h —— Player 族实体(L2 领域模型,批次 M.1)
//
// ★ 本批只建**捕获链路真正要读写**的那一小块:宠物槽 + 捕获计数 + 名字。
//   Player 的完整字段面是 03 §11 欠债 1(693 字段逐条含义,5–8 天)与 §4 的
//   65 条别名展开,**都不在本批**。⇒ 这不是"Player 族已建好",是"它有了第一块地基"。
//
// ⚠️ M1 / §4.2:Player 与 Pet 是**两个独立类型**,不是同一类型的两个 flag ——
//    理由与那三对同槽异义见 Pet.h 卷首。
//
// ⚠️ shared/ 只依赖标准库 + 项目内 rules/ model/ domain/(01 §4)。

#ifndef __SA_Player_H__
#define __SA_Player_H__

#include <array>
#include <cstddef>
#include <cstdint>

#include "model/EntityKind.h"
#include "model/Handle.h"
#include "model/Item.h"
#include "model/Pet.h"

namespace SA::Model
{

// 宠物槽数 = `CHAR_MAXPETHAVE`(展开视图 `include/char_base.h:31`)。
inline constexpr std::size_t kMaxPetHave = 5;

// ── 背包 / 装备槽数(批次 I.1,展开视图 `include/char_base.h`)──────────────
//
// ★ 原版 `indexOfExistItems[CHAR_MAXITEMHAVE]` 是**装备位 + 背包连续一个数组**:
//     kEquipPlaceNum   = 9   `CHAR_EQUIPPLACENUM`(枚举 CHAR_HEAD..CHAR_EQGLOVE)
//     kItemNumPerKind  = 15  `CHAR_MAXITEMNUM`
//     kMaxItemHave     = 9 + 15*3 = 54  `CHAR_MAXITEMHAVE`(:314,8.0 走 *3 支)
//     kStartItemArray  = 9   `CHAR_STARTITEMARRAY`(= kEquipPlaceNum,背包起点下标)
//   ⚠️★ 8.0 走 `*3` 支(展开视图 :314),不是 `*1`(:316,宏关闭时代)—— 回展开视图核过。
//   ⚠️ 装备位 4 项(EQBELT/EQSHIELD/EQSHOES/EQGLOVE)在原始 8.5 由 `_ITEM_EQUITSPACE`/
//     `_EQUIT_NEWGLOVE` 包裹,展开视图判为 8.0 启用 ⇒ kEquipPlaceNum = 9。
// ★★ **本批只建槽位、照原版连续布局**(捕获扣道具的循环 `for(i=CHAR_STARTITEMARRAY;
//    i<CheckCharMaxItem; i++)` 依赖这个布局)。装备位段(0..kStartItemArray-1)的
//    **穿戴语义**留装备域批次填 —— 本批不赋含义,只留可寻址的槽。
inline constexpr std::size_t kEquipPlaceNum = 9;
inline constexpr std::size_t kItemNumPerKind = 15;
inline constexpr std::size_t kStartItemArray = kEquipPlaceNum;
inline constexpr std::size_t kMaxItemHave = kEquipPlaceNum + kItemNumPerKind * 3; // 54

// 宠技槽数 = `CHAR_MAXPETSKILLHAVE`(同上 :36)。★ 本批不用,记在此处是因为
// 03 §3.2 已裁定 `unionTable` **保留但拆开** —— 宠物槽与宠物技能槽是两件事,
// 两个常量放在一起才看得出它们**不是**同一个上限(原版 5 vs 7)。
inline constexpr std::size_t kMaxPetSkillHave = 7;

// Player 族实体。
struct Player
{
	// ★ 类型与族的对应在编译期可查(M2,同 Pet::kKind)。
	static constexpr EntityKind kKind = EntityKind::kPlayer;

	// 角色名。上限 31 字节(DR-TS5)。
	NameStr name{};

	// ── 宠物槽(原 `unionTable.indexOfPet[CHAR_MAXPETHAVE]`)────────────
	//
	// ★ 存**句柄**而不是池下标:M10 要求引用带 generation,否则宠物槽被回收重分配后
	//   旧下标会指向新主人的宠物(定长池的固有问题,Handle.h 卷首 ②)。
	//
	// ⚠️★ **悬空句柄会被 findFreePetSlot() 算作"占用"**,这是有意的两步分工
	//    (与 EntityIndex.h 卷首同一条):本结构只管「哪个槽有引用」,「引用还指向
	//    活宠物吗」是 `EntityPool::resolve` 的活。
	//    ⇒ **释放一只 Pet 时必须同步 clearPetSlot()**,否则那个槽永久占用而
	//      没有任何一处会报错。★ ModelPoolTest 有一条用例专门钉这个后果。
	std::array<EntityHandle, kMaxPetHave> pets{};

	// ── 背包 / 装备槽(原 `indexOfExistItems[CHAR_MAXITEMHAVE]`,批次 I.1)──────
	//
	// ★ 与 `pets[]` 完全同一条纪律:存**句柄**(带 generation,M10)而不是池下标 ——
	//   道具槽被回收重分配后,旧下标会指向新主人的道具(定长池固有问题,Handle.h ②)。
	//   ⇒ **释放一个 Item 时必须同步 clearItemSlot()**,否则那个槽永久占用而无处报错。
	//
	// ★ 连续布局:`[0, kStartItemArray)` 是装备位、`[kStartItemArray, kMaxItemHave)`
	//   是背包格。本批只建槽,不赋装备位穿戴语义(留装备域)。
	// ⚠️ 悬空句柄会被 `findFreeItemSlot()` 算作"占用"—— 与 pets 的两步分工同理。
	std::array<ItemHandle, kMaxItemHave> items{};

	// 当前出战宠在 `pets[]` 中的下标(原 `CHAR_DEFAULTPET`)。-1 = 无出战宠。
	// ★ 唯一写者 = 换宠指令 PET_OUT(设槽号)/ PET_IN(设 -1)(DR-BT21);`joinBattle`
	//   读它自动带宠。M.1/M.2 按「不建没人用的字段」纪律留白,本批补上写者后解禁。
	std::int32_t default_pet = -1;

	// 累计捕获数(原 `CHAR_GETPETCOUNT`,源码 `battle_event.c:3543`)。
	std::int32_t capture_count = 0;

	// 累计经验值(原 `CHAR_EXP`,战果结算批次)。
	//
	// ★ 战斗胜利时按等级差衰减把死亡敌人的经验累加进来(`World.cpp` finished 段,
	//   源码 `BATTLE_AddExp` 的 `EXPGET_MAXLEVEL=5` / `DIV=15` 段)。
	// ⚠️ 本批**只累积、不触发升级** —— 升级要读 `exp.txt`(下一级所需经验,D 线数据
	//   未导入)+ 属成长域,且与 `fmdplevelexp`(那是家族声望,`00` §10.2)无关。
	//   ⇒ 玩家等级不因经验变化;不在此建 `level`(等级差衰减读**战场 Combatant** 的等级,
	//   见 `World.cpp` finished 段;M.1 起「不建没人用的字段」,真做升级那批再建)。
	std::int32_t exp = 0;
	// Persistent character attributes (char.c creation; derived stats remain transient).
	std::int32_t level = 1;
	std::int32_t hp = 0;
	std::int32_t mp = 100;
	std::int32_t max_mp = 100;
	std::int32_t vital = 0;
	std::int32_t str = 0;
	std::int32_t tough = 0;
	std::int32_t dex = 0;
	std::int32_t luck = 0;
	std::int32_t charm = 60;
	std::int32_t earth = 0;
	std::int32_t water = 0;
	std::int32_t fire = 0;
	std::int32_t wind = 0;
	std::int32_t image = 0;
	std::int32_t face_image = 0;
	std::int32_t gold = 0;

	// ── 位置(批次 W.1。原 CHAR_FLOOR / CHAR_X / CHAR_Y / CHAR_DIR)──────────
	//
	// ★ 服务端权威(10 §4.1):客户端只做预测,服务端校验碰撞后写这里。
	// ⚠️ 走路的**运行时**态(方向串 + 上次走一步的时刻)不在此 —— 那是原版 work 区
	//    (CHAR_WORKWALKARRAY / WORKWALKSTARTSEC),不存档、生命周期随连接 ⇒ 放 World::Impl::Conn。
	std::int32_t floor = 0;
	std::int32_t x = 0;
	std::int32_t y = 0;
	std::uint8_t dir = 0; // 0-7 八方向(CHAR_ctodirmode 的 dir 值域)

	// ── 宠物槽操作 ────────────────────────────────────────────────

	// 找一个空宠物槽,满则返回 −1。
	//
	// ★★ **照抄** `CHAR_getCharPetElement`(展开视图 `char/char_base.c:1548-1570`)
	//    的两段结构,而不是简写成"找第一个空槽" —— 因为原版的额度判定里有第二项:
	//
	//      j = 已占 indexOfPet 槽数
	//      k = 跟随宠数(CHAR_WORKPETFOLLOW + i,5 槽)
	//      if (j + k) >= CHAR_MAXPETHAVE  ⇒  −1        ← ★ 两者吃**同一个**上限
	//
	//    ⚠️ 本批 **k 恒 0**(跟随宠 petFollow 未移植)⇒ 判据退化为 j >= 5,与"找不到
	//      空槽"等价。★ 但形状照留:将来接跟随宠时只需把 `following` 填上,不必
	//      重新回源码考古这条额度是怎么算的。
	int findFreePetSlot() const noexcept
	{
		int used = 0;
		for (std::size_t i = 0; i < kMaxPetHave; ++i)
		{
			if (pets[i].valid())
				++used;
		}

		// ★ 跟随宠:未移植 ⇒ 恒 0(见上)。
		const int following = 0;

		if (used + following >= static_cast<int>(kMaxPetHave))
			return -1;

		for (std::size_t i = 0; i < kMaxPetHave; ++i)
		{
			if (!pets[i].valid())
				return static_cast<int>(i);
		}
		// ★ 走不到:上面的额度判定已保证还有空槽(k == 0 时两者等价)。
		//   保留这条返回是因为 k 将来非 0 时它就**可能**走到 —— 那时 j + k < 5
		//   却 5 个槽全占是一个矛盾状态,返回 −1 比越界更好。
		return -1;
	}

	// 清空某个宠物槽(原 `CHAR_setCharPet(charaindex, i, -1)`)。
	// ★ 释放 Pet 的那一侧必须调它,理由见 `pets` 的注释。
	bool clearPetSlot(int slot) noexcept
	{
		if (slot < 0 || static_cast<std::size_t>(slot) >= kMaxPetHave)
			return false;
		pets[static_cast<std::size_t>(slot)] = kNullHandle;
		return true;
	}

	// ── 背包槽操作(批次 I.1)──────────────────────────────────────────
	//
	// 找一个空**背包**槽,满则返回 −1。
	//
	// ★★ 只在背包段 `[kStartItemArray, kMaxItemHave)` 找 —— 照原版
	//    `getFreeItemSpace`(展开视图 `char_base.c`,循环从 `CHAR_STARTITEMARRAY`
	//    起)。装备位段 `[0, kStartItemArray)` **不是**放"新拿到的道具"的地方
	//    (那是穿戴槽,由装备域按位置写),故不在候选内。
	// ⚠️ 悬空句柄算作占用(两步分工:本结构只管"哪个槽有引用",引用是否指向活道具
	//    是 `EntityPool::resolve` 的活)⇒ 释放 Item 的那侧必须 `clearItemSlot`。
	int findFreeItemSlot() const noexcept
	{
		for (std::size_t i = kStartItemArray; i < kMaxItemHave; ++i)
		{
			if (!items[i].valid())
				return static_cast<int>(i);
		}
		return -1;
	}

	// 清空某个道具槽(原 `CHAR_setItemIndex(charaindex, i, -1)`)。
	// ★ 释放 Item 的那一侧必须调它,理由见 `items` 的注释。
	// ⚠️ 全域 `[0, kMaxItemHave)` 都可清(装备位卸下也走它),不限背包段。
	bool clearItemSlot(int slot) noexcept
	{
		if (slot < 0 || static_cast<std::size_t>(slot) >= kMaxItemHave)
			return false;
		items[static_cast<std::size_t>(slot)] = kNullHandle;
		return true;
	}
};

// ── 文末:本批**有意不建**的 Player 字段(举其要,均非遗漏)────────────────
//
// ① ★ `CHAR_CDKEY` / 账号绑定 ⇒ 本批不落盘、不接账号域。连带 Pet 的 `owner_cdkey`
//    也没建(Pet.h 卷首已记:它会撞出一条 cdkey 长度上限的新决策,留到落盘时登记)。
//
// ② 石币 ⇒ ★★ 03 §7:石币在 8.0 是 **5 个并存载体 + 4 个独立上限**,
//    「玩家有多少钱」没有单一真值 ⇒ 必须走 `GoldLedger` 单入口(08 §3),
//    而那挂在阶段 2 的 2.2 审计事件模型上。**捕获链路不需要钱** ⇒ 不在本批。
//
// ③ `addressBook[80]` ⇒ 03 §3.2 已裁定**移出实体做成独立聚合**(占 sizeof(Char) 的
//    39% 却是纯社交数据,且 17 §5.5 已证它参与删角 Saga)。
//
// ④ ✅ ~~背包~~ ⇒ **2026-09-10 批次 I.1 已建**,见上方 `items[]` + `findFreeItemSlot` /
//    `clearItemSlot`。⚠️ 只建**槽 + 空/满判定**,不接扣 / 掉 / 用任一玩法链路(道具域
//    后续三批)。仓库 / 称号 / 家族仍各属其域,未建。★ 捕获第 5 步
//    (`BATTLE_CaptureItemDelAll`,DR-BT10「全删」)的落脚点由此解锁,但那一步仍待
//    捕获扣道具批次做(它还需 `needitemeneny.txt` 敌人侧表,见 Item.h 与欠债表)。

} // namespace SA::Model

#endif // __SA_Player_H__
