// shared/rules/CaptureItem.h —— 捕获条件道具:需求表 + 匹配(道具域批次「捕获扣道具」)
//
// ★★ 本文件是**纯规则常量 + 纯匹配函数**,无 I/O、无 platform 依赖 ⇒ 归 sa_shared
//    (shared/rules),与逃跑 / 暴击 / 捕获概率等 L3 纯函数同层。
//    「读玩家背包确认道具齐备」「捕获成功后删道具」是**有副作用的世界写**,不在此文件 ——
//    它们在 World 层(src/world),与 M.4b「公式在 rules、世界写在 world」同一分工。
//
// ── 取证结论(stoneage85 全宏 + StoneAge 双源交叉核,已闭合)────────────────
//   `_CAPTURE_FREES` **开**(三源一致:stoneage85 version.h:198 + StoneAge version.h:262
//     + unifdef -D)⇒ 走**全删**语义 `BATTLE_CaptureItemDelAll`(DR-BT10 记载正确)。
//   `_NEED_ITEM_ENEMY` **关**(双源无 #define)⇒ **不读 `needitemeneny.txt` 文件**!
//     8.0 净核用源码内**硬编码**的 `NeedEnemy[]` 表(`battle_event.c:3890`)。
//     ⚠️ data/ 目录里那个文件躺着但净核不读它 —— 别被文件名误导(纪律 ①)。
//   `_DEL_NOT_25_NEED_ITEM` **关** + `_WOLF_TAKE_AXE` **关**(正式 version.h 无,仅 .bak)
//     ⇒ 净核有效行 = 下表 **9 行**(双头狼 145/146 两行属 `_WOLF_TAKE_AXE`,不含)。
//   `getDelNeedItem()` 门属 `_NEED_ITEM_ENEMY` 段 ⇒ 8.0 **无条件删**,不看配置开关。
//   Lua 回调族(`RunItemDetachEvent` / `CaptureOkFunction` / `_ALLBLUES_LUA_*`)**全关**,
//     不复刻(与 Item.h 文末 ⑤ 同结论:04 §3.3.3「8.0 无 Lua」)。
//
// ── 匹配键的来源(被源码钉死)──────────────────────────────────────────────
//   `IsNeedCaptureItem` 按 `CHAR_getInt(idx, CHAR_PETID)` 匹配(`battle_event.c:3931`),
//   而敌人生成时 `CHAR_PETID = *(tp + E_T_TEMPNO)`(`enemy.c:1200` 等四处)⇒ 匹配键就是
//   **模板号**,即 `Model::Enemy::pet_id`(= `EnemyTemplate::temp_no`)。

#ifndef __SA_CaptureItem_H__
#define __SA_CaptureItem_H__

#include <cstddef>
#include <cstdint>

namespace SA::Rules
{

// 每只需求怪最多要几种道具 = 源码 `MAXCAPTRUEFREE`(`battle_event.c:3884`,拼写照原版)。
inline constexpr std::size_t kMaxCaptureFreeItems = 15;

// 需求道具表的一行:怪的模板号 + 所需道具 id 列表(-1 结尾 / 填充)。
struct CaptureNeedItem
{
	std::int32_t pet_id;                         // = CHAR_PETID = 模板号 E_T_TEMPNO
	std::int32_t item_ids[kMaxCaptureFreeItems]; // 需求道具 id,-1 = 无 / 列表结束
};

// ★★ 1:1 硬编码源码 `NeedEnemy[]`(`battle_event.c:3890-3907`,`_CAPTURE_FREES` 分支)。
//   净核有效行 9 条(`_DEL_NOT_25_NEED_ITEM` 关 ⇒ 伊甸任务那几行**在数组里,不被 #ifndef
//   包**,故计入;`_WOLF_TAKE_AXE` 关 ⇒ 145/146 双头狼两行不计入)。
//   ⚠️ 逐字节对源码复核过,别改数值(纪律 ⓪:照抄不编造理由)。
inline constexpr CaptureNeedItem kNeedItemEnemy[] = {
    {524, {2456, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // 伊甸任务(_DEL_NOT_25_NEED_ITEM 关 ⇒ 计入)
    {961, {20219, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {953, {20223, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {962, {20222, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {777, {20253, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {796, {20247, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {812, {20259, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {1105, {1690, 1691, 1692, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {8, {1810, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
};

inline constexpr std::size_t kNeedItemEnemyCount =
    sizeof(kNeedItemEnemy) / sizeof(kNeedItemEnemy[0]);

// 1:1 移植 `IsNeedCaptureItem`(`battle_event.c:3927-3939`,`_CAPTURE_FREES` 分支)。
// 返回该怪在 `kNeedItemEnemy` 中的行下标;-1 = 这只怪不需要任何条件道具。
//
// ★ `_CAPTURE_FREES` 开 ⇒ 返回的是**行下标**(源码 `return i`),不是道具 id ——
//   调用方据下标读整行 `item_ids[]`(全删语义需要遍历整行)。
inline int isNeedCaptureItem(std::int32_t pet_id) noexcept
{
	for (std::size_t i = 0; i < kNeedItemEnemyCount; ++i)
	{
		if (kNeedItemEnemy[i].pet_id == pet_id)
			return static_cast<int>(i);
	}
	return -1;
}

} // namespace SA::Rules

#endif // __SA_CaptureItem_H__
