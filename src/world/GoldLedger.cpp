// world/GoldLedger.cpp —— 石币唯一入口的实现(声明见 world/Api.h 的 GoldLedger 节)
//
// ★★ 本文件是**全仓唯一**允许直接写 `Player::gold` 的地方。
//    `tools/check_gold_writes.py` 是这条纪律的执行者:src/ 与 shared/ 里任何
//    别处的 `.gold =` / `->gold +=`(含其余复合赋值)都会被它报红。
//    ⚠️ 这条守卫不是风格检查,它挡的是 `12` §2.3 那 62.5% 的绕过 —— 那类写点
//       **不报错、不崩溃**,只是悄悄少一次上限判定或少一条日志(00 §10.4 第一类)。

#include "world/Api.h"

#include <algorithm>

namespace SA::World
{

// 三步的公共前半:① 钳位(源码 `char_base.c:3232` / `:3267` 同一句)。
//
// ⚠️ 这一步**必须**照抄原版的「先钳当前余额」:它不是防御性代码,而是可观察语义 ——
//    已经超上限的余额(原版可由 `NPC_AcceptDel` 推过上限,`12` §3.2)在下一次
//    变更时被**拉回上限**。用例钉着它(「超上限的存量被拉回上限」)。
namespace
{
// 返回钳位后的余额,并把剪掉多少记进 `*clipped`(0 = 存量本就在上限内)。
std::int32_t clampToCap(SA::Model::Player &player, std::int32_t cap,
                        std::int32_t *clipped) noexcept
{
	*clipped = 0;
	if (player.gold > cap)
	{
		*clipped = player.gold - cap;
		player.gold = cap;
	}
	return player.gold;
}
} // namespace

GoldTx addGold(SA::Model::Player &player, GoldReason reason, std::int32_t delta,
               std::int32_t trans, std::uint64_t correlation, const GoldAuditSink &audit) noexcept
{
	GoldTx tx{};
	tx.carrier = GoldCarrier::kGold;
	tx.reason = reason;
	tx.delta = delta;
	tx.correlation = correlation;

	const std::int32_t cap = maxHaveGold(trans);

	// ── ① 钳位 ──
	std::int32_t clipped = 0;
	const std::int32_t base = clampToCap(player, cap, &clipped);
	tx.before = base;
	tx.pre_clamped = clipped;

	// 负数入账按 0 处理:入账口不接受"倒扣"(倒扣是汇的事,走 delGold)。
	const std::int32_t gain = std::max<std::int32_t>(0, delta);

	// ── ② 溢出处置(由 `reason` 决定,不由调用点自选)──
	//
	// ★ 战斗产币的处置 = 钳位(超出上限的部分销毁)。**证据边界**:SSRC85
	//   `battle/battle.c:3851-3860` 直接 `CHAR_setInt(CHAR_GOLD, 钳位值)`,
	//   **不像 `_CHAR_AddGold` 那样转存宝箱**(`12` §2.3 C5 / §5.4 C24 的三处静默销毁之一)。
	//   ⇒ 本批不发明"转宝箱":那条语义属于 `CHAR_PERSONAGOLD` 载体,而它是显式划出的
	//      后续域(DR-EC1 / 本批范围)。★ 与原版的唯一差别是 DR-EC4 要求的:**销毁不再静默**,
	//      审计事件带着 `overflow` 与处置名。
	const std::int64_t sum = static_cast<std::int64_t>(base) + static_cast<std::int64_t>(gain);
	if (sum > cap)
	{
		tx.applied = cap - base;
		tx.overflow = static_cast<std::int32_t>(sum - cap);
		tx.after = cap;
		tx.disposition = GoldDisposition::kClamped;
	}
	else
	{
		tx.applied = gain;
		tx.overflow = 0;
		tx.after = static_cast<std::int32_t>(sum);
		tx.disposition = GoldDisposition::kApplied;
	}
	player.gold = tx.after;

	// ── ③ 写审计 —— 无条件 ──
	audit.onGoldTx(tx);
	return tx;
}

GoldTx delGold(SA::Model::Player &player, GoldReason reason, std::int32_t delta,
               std::int32_t trans, std::uint64_t correlation, const GoldAuditSink &audit) noexcept
{
	GoldTx tx{};
	tx.carrier = GoldCarrier::kGold;
	tx.reason = reason;
	tx.delta = delta;
	tx.correlation = correlation;

	const std::int32_t cap = maxHaveGold(trans);

	// ── ① 钳位 ──
	// ★ 请求量先被钳到上限 —— 照抄 `char_base.c:3270` 的原文
	//   `gold = (gold>MaxGold)?MaxGold:gold;`。⚠️ 这条在**上限边界上有可观察差异**:
	//   余额恰为上限、请求量 > 上限时,不钳会"余额不足"被拒,钳了则扣成 0。
	//   用例 `EC:扣账请求量先钳到上限` 钉着它(照抄,不"修")。
	const std::int32_t amount = std::min(std::max<std::int32_t>(0, delta), cap);
	std::int32_t clipped = 0;
	const std::int32_t base = clampToCap(player, cap, &clipped);
	tx.before = base;
	tx.pre_clamped = clipped;

	// ── ② 余额不足 ⇒ 拒绝(DR-EC3:不清零)──
	if (base < amount)
	{
		tx.applied = 0;
		tx.overflow = 0;
		tx.after = base;
		tx.disposition = GoldDisposition::kRejected;
		audit.onGoldTx(tx);
		return tx;
	}

	tx.applied = amount;
	tx.overflow = 0;
	tx.after = base - amount;
	if (tx.after < 0) // 走不到(上面已判 base >= amount),守零成本
		tx.after = 0;
	tx.disposition = GoldDisposition::kApplied;
	player.gold = tx.after;

	// ── ③ 写审计 —— 无条件(拒绝那条路径也写:`disposition=kRejected` 让"被拒绝"可追)──
	audit.onGoldTx(tx);
	return tx;
}

} // namespace SA::World
