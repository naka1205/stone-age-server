// shared/model/Item.h —— Item 族实体(L2 领域模型,批次 I.1「背包 L2 地基」)
//
// ★★ 本文件的字段清单**不是自拟的**,取自展开视图 `include/item.h` 的
//    `ITEM_DATAINT` / `ITEM_DATACHAR` 枚举(8.0 投产开关组合下实际启用的下标)
//    + `csa8.0/gmsv/data/itemset6.txt`(投产主道具表,`setup.cf:335`)实际列。
//    ⇒ 建 / 不建**逐条记明**(见文末),不静默省略 —— 同 `Pet.h` 方法论。
//
// ⚠️ 行号基准 = `stoneage-plan/tools/unifdef_80/` **展开视图**,不是原始 8.5 树。
//
// ── 为什么 Item **不进 `EntityKind` 五族**(2026-09-10 用户拍板)──────────────
//   五族的判别键是 `CHAR_TYPE`(03-domain-model.md §2.1)—— 五族全是 `Char` 实体。
//   而背包道具在原版是**独立的全局池** `ITEM_item[itemnum]`(`item.c:486`,`use` 标志
//   占用),**不在 `CHAR_chara[]` 三段式角色池里**;角色通过 `CHAR_getItemIndex` 存
//   **池下标**引用道具(`item.c:497`)。⇒ Item 是与五族**并列**的独立池 + 独立句柄
//   语义,塞进 `EntityKind` 会污染「实体族 = CHAR_TYPE」这条 M2 硬约束。
//   ⚠️ `EntityKind` 卷首 kWorldObject 那条注释「地面道具不在 Char 池里」说的是**地面**
//     道具(掉在地上的);背包道具与地面道具将来是否共用本结构,留掉落批次定。
//
// ── 本批范围(I.1,道具域第一批)────────────────────────────────────────
//   ✅ 只建 Item 族 POD + 独立道具池句柄语义 + Player 背包槽,让后续三条链
//      (捕获扣道具 / 掉落 / 使用道具)有落脚点。
//   ⬜ **不接任何玩法链路** —— 不做扣 / 掉 / 用,不建道具效果分派(见文末)。
//
// ⚠️ shared/ 只依赖标准库 + 项目内 rules/ model/ domain/(01 §4)。

#ifndef __SA_Item_H__
#define __SA_Item_H__

#include <cstdint>

#include "model/Handle.h"
#include "sa_idl_runtime.h"

namespace SA::Model
{

// 道具名长度上限 = `STRING64`(展开视图 `util.h:15` `char string[64]`)。
// ⚠️★ 与角色名 `kNameMaxBytes`(31,DR-TS5)**不是同一个上限** —— 道具名在原版是
//    64 字节槽,不能借用角色名常量,否则长道具名会被静默截断。故单列。
inline constexpr std::size_t kItemNameMaxBytes = 63; // 64 槽含结尾 NUL ⇒ 可容 63 字节

using ItemNameStr = SA::IDL::FixedStr<kItemNameMaxBytes>;

// ★ 独立道具句柄:与 `EntityHandle` **同机制**(index + generation,M10),但**语义独立**
//   —— 它指向道具池而不是任一实体族池。取 using 别名而非新 struct,是因为 `EntityPool`
//   的 `allocate/resolve` 就以 `EntityHandle` 为句柄类型(见 EntityPool.h);新 struct
//   会要求改那个模板。别名让「道具句柄 ≠ 实体句柄」在**类型注释层面**成立,机制照复用。
using ItemHandle = EntityHandle;

// Item 族实体。
//
// ★ POD:三根支柱要求池是 std::array 且运行期零分配(15 §9.1)⇒ 本结构不含
//   任何自管内存的成员(ItemNameStr 是定长内联串)。
struct Item
{
	std::uint64_t uid = 0;
	// ── 身份(源码 `ITEM_DATAINT`:ITEM_ID / ITEM_NAME / ITEM_UNIQUECODE)──────
	//
	// `item_id` = `ITEM_ID`(展开视图 item.h:110,`data[]` 首项)—— 道具表主键,
	//   `ITEM_CHECKITEMTABLE` 校验它(`item.c:482`)。
	std::int32_t item_id = 0;

	// 道具名 = `ITEM_NAME`(`string[]` 首项,`item.c:502` 就读它做日志)。
	ItemNameStr name{};

	// 唯一码 = `ITEM_UNIQUECODE`(`string[]`)—— ★ 捕获日志(`battle_event.c` 的
	//   `ITEM_getChar(itemindex, ITEM_UNIQUECODE)`)与掉落都要它区分"同名不同个"。
	//   本批只存字段,不生成 —— 生成规则(时间戳 + 序号)留写入侧的批次。
	ItemNameStr unique_code{};

	// ── 分类与基本属性(源码 `ITEM_DATAINT`)────────────────────────────
	//
	// `type` = `ITEM_TYPE`(item.h:113)⇒ `ITEM_CATEGORY` 枚举(空手/斧/棒/…/料理/…)。
	//   ★ 捕获扣道具靠"料理类"(`ITEM_DISH`)判定,掉落 / 使用都要读它 ⇒ 地基字段。
	std::int32_t type = 0;

	// `level` = `ITEM_LEVEL`(item.h:116)· `cost` = `ITEM_COST`(item.h:112)。
	//   使用 / 买卖的通用判定字段;本批只存不判。
	std::int32_t level = 0;
	std::int32_t cost = 0;

	// ── 堆叠(源码 `_ITEMSET4_TXT`,展开视图确认 8.0 启用)──────────────
	//
	// ★ `ITEM_CANBEPILE`(可否堆叠)/ `ITEM_USEPILENUMS`(单格堆叠上限)。掉落与使用
	//   都要它(掉落判断能否并进已有格、使用消耗一个 pile)⇒ 背包地基必需。
	// ⚠️ **当前堆叠数量**(某一格里现在堆了几个)不在道具表、是运行期状态(原版存 workint)。
	std::int32_t can_be_pile = 0;
	std::int32_t use_pile_nums = 0;

	// ── 运行期堆叠数(批次 I.4「使用道具」建)──────────────────────────────
	//
	// ★ 某一格现在堆了几个。原版存 `workint`(运行期,非道具表)。I.1 文末 ④ 登记「留写入侧
	//   批次按各自语义建」—— 使用道具是第一个消费方:每用一个 `--current_pile`,归零则清槽
	//   + 释放实体(见 src/world/World.cpp `consumeUsedItems`)。
	// ⚠️ 写入路径(掉落 / 注入 seam)造 Item 时置为该格实际堆叠数(单个道具 = 1);`<=0` 视同
	//   不可用(空格)。⚠️ 掉落/捕获路径回填 current_pile 已随本批补上(World.cpp 造 Item 处)。
	std::int32_t current_pile = 0;

	// ── 掉落 / 存档行为(源码 `ITEM_DATAINT`,展开视图启用)────────────────
	//
	// ★ `ITEM_VANISHATDROP`(掉落即消失)/ `ITEM_DROPATLOGOUT`(登出掉落)——
	//   掉落批次与登出流程要读;本批只存。`ITEM_getvanishatdropFromITEMtabl` /
	//   `ITEM_getdropatlogoutFromITEMtabl`(item.c:2278/2283)是它们的 getter。
	std::int32_t vanish_at_drop = 0;
	std::int32_t drop_at_logout = 0;

	// ── 主人反指(运行期,源码 `workint[ITEM_WORKCHARAINDEX]`,item.c:492)───────
	//
	// ★ 原版道具池成员反指主人角色下标,`_ITEM_initExistItemsOne` 靠它避免"同一道具
	//   被两个角色引用"(item.c:493-508)。本批换成**玩家句柄**(带 generation,M10):
	//   主人下线后 resolve 返 nullptr,不脏读复用槽。
	// ⚠️ 本批**不接**任何写者(不做捡起 / 掉落 / 捕获)⇒ 它恒为空句柄,是**登记在案的
	//   留白**,不是遗漏 —— 与 Pet.h 的 default_pet「不建没人用的字段」同纪律的反面:
	//   这里字段建了但写者留白,因为池的归属校验(将来 initExistItems 的等价物)必然要它,
	//   建了才能让写入批次直接接。★ 观察面能断言它"目前恒空"。
	ItemHandle owner{};
};

// ── 文末:源码有、本批**有意不建**的字段(逐条记明,均非遗漏)─────────────
//
// ① **装备加成大段**(`ITEM_MODIFYATTACK/DEFENCE/QUICK/HP/MP/LUCK/CHARM/AVOID`、
//    `ITEM_ATTACKNUM_MIN/MAX`、`ITEM_MODIFYATTRIB(VALUE)`,展开视图 item.h:143-155)
//    ⇒ **装备域**。背包地基只需"这一格里是什么道具",装备穿戴后如何改四维是穿戴
//    链路的事(与 `Progression::deriveBaseStats` 的装备段 DR-DT9 文末对齐)。
//
// ② **合成 / 镶嵌 / 套装**(`_ITEM_INSLAY` 的 `ITEM_TYPECODE/INLAYCODE`、
//    `ITEM_CANMERGEFROM/TO`/`ITEM_MERGEFLG`、`_SUIT_ITEM` 的 `ITEM_SUITCODE`)
//    ⇒ 各属合成 / 镶嵌 / 套装域,均非背包地基。
//
// ③ **魔法道具**(`ITEM_MAGICID/PROB/USEMP`)+ **状态异常附加**(`ITEM_POISON/
//    PARALYSIS/SLEEP/STONE/DRUNK/CONFUSION`,item.h:172-177)⇒ 使用道具批次 + L4
//    状态系统(与 `Pet.h` 文末 ① 同一个 L4 前置)。
//
// ④ **运行期状态**:`current_pile`(某格现堆几个)✅ **批次 I.4 建**(见上「运行期堆叠数」)·
//    `workint[]` 其余全段(除主人反指 / 现堆叠数)⇒ 仍属写入侧状态,由各自批次按语义建,
//    本批不猜其归属。
//
// ⑤ ★★ **functable / Lua 回调名全段**(`ITEM_INITFUNC`..`ITEM_LASTFUNCTION`,
//    它们**占用 `string[]` 下标空间**故 `ITEM_DATACHARNUM == ITEM_LASTFUNCTION`)
//    ⇒ **8.0 无 Lua,不复刻**。★ 双重依据:(a) 与捕获链 `CaptureOkFunction` 同结论
//    (`_ALLBLUES_LUA_*` 在 StoneAge 全树无 #define,04 §3.3.3「8.0 无 Lua 集成」);
//    (b) `EntityKind.h` §2.3 已裁定**不复刻字符串→函数指针的运行期绑定**(原版
//    `getFunctionPointerFromName` + `function.c` 名表 hashpjw+strcmp,查不到返回 NULL
//    不报错)⇒ 道具效果将来用接口 / 函数值直接注册,断链编译期发现。
//    ⚠️ 这条是本批**最该被读的一段**:`_ITEM_initExistItemsOne:514` 的
//      `getFunctionPointerFromName(itm->string[ITEM_INITFUNC].string)` 正是那条要弃的链。
//
// ⑥ `ITEM_BASEIMAGENUMBER`(图号)· `ITEM_SECRETNAME`(鉴定前名)· `ITEM_EFFECTSTRING`
//    (效果说明串)· `ITEM_ARGUMENT`(参数串)⇒ **显示 / 客户端**面,背包地基不需要;
//    接客户端道具信息串(原版 `char.c:9002` 那类)的批次再建,避免建一堆"存了没人读"。
//
// ⑦ `ITEM_CDKEY` / `ITEM_FORUSERNAME` / `ITEM_FORUSERCDKEY`(绑定 / 专属)⇒ 账号域
//    (与 `Pet.h` 的 `owner_cdkey`、`Player` 的 `CHAR_CDKEY` 同一个未落盘前置,
//    且会撞出 cdkey 字节上限的新决策 ⇒ 落盘时一并登记)。

} // namespace SA::Model

#endif // __SA_Item_H__
