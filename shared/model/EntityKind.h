// shared/model/EntityKind.h —— L2 领域模型的实体族判别键(M2)
//
// ★★ M2 硬约束(03-domain-model.md §9 / 01 §7.2):
//    拆分的判别键是 `CHAR_TYPE` ⇒ **和类型,不是 693 列宽表**。
//    47 种 `CHAR_TYPE` 字段集高度重叠,但**同槽异义**(M1):同一物理 slot
//    在不同族下语义完全不同 —— `CHAR_CHARM`(魅力)在宠物身上叫 `CHAR_MODAI`(AI 模式)。
//    宽表会把 47 种实体的字段全塞进一行、90% 恒为默认值,且合并任意两个别名
//    都是**静默错误**(不报错、不崩溃、数据烂掉)。
//    ⇒ 各族必须是**独立强类型**,由本枚举分派。
//
// ── 本文件的范围(2026-09-07,L2 地基起步)────────────────────────────
//   ✅ 只立**族判别键**与两条裁定的边界注释,让实体池 / 各族结构有分派依据。
//   ⬜ 不建各族的字段(Pet 的主人/融合码/技能槽等 —— 后续批次);
//   ⬜ 不建 NPC 行为注册表 / windowtalkedfunc 分派(07 §4,阶段 2 后段)。
//
// ⚠️ shared/ 只依赖标准库(01 §4)。

#ifndef __SA_EntityKind_H__
#define __SA_EntityKind_H__

#include <cstdint>

namespace SA::Model
{

// 实体族(03-domain-model.md §2.1 的五族)。
//
// ★ 归族的判据**不是名字,是"哪些字段真被读写"**(§2.1):47 种 CHAR_TYPE 按
//   字段集 + 生命周期归成五族。
//
//   | 族           | 含 CHAR_TYPE(举例)                              | 特征 |
//   |--------------|---------------------------------------------------|------|
//   | kPlayer      | PLAYER                                            | 唯一有账号绑定、有存档、有连接 |
//   | kPet         | PET                                               | ★ 有主人字段、融合码、技能槽;与 Player 同槽异义最严重(§4.2)|
//   | kEnemy       | ENEMY                                             | 由模板生成,无存档 |
//   | kNpc         | TOWNPEOPLE/HEALER/WINDOWMAN/SHOP/DOOR/BOX/…        | ★ 共用一块 NPC 通用暂存区 NPCWORKINT1..10(§4.1)|
//   | kWorldObject | 地面道具、地面石币                                | 不在 Char 池里(10-world-map.md §4)|
enum class EntityKind : std::uint8_t
{
	kPlayer = 0,
	kPet = 1,
	kEnemy = 2,
	kNpc = 3,
	kWorldObject = 4,
};

inline constexpr std::uint8_t kEntityKindCount = 5;

// ── 两条裁定的边界(§2.2 / §2.3)——本文件只立注释,不建实现 ─────────────
//
// ★ §2.2 **Interactive(kNpc)族不做成 47 个类**:16 §3.1 实测 66 个 NPC functionset
//   里 46 个有 windowtalkedfunc(69.7%)、只有 16 个有 loopfunc。⇒ 绝大多数 NPC 玩法
//   是无状态的 `(actor, window_id, choice) -> WindowOpen` 纯函数,状态存在实体的少量
//   工作字段里;只有 16 个带 loopfunc 的才是真 tick 驱动状态机(赌场轮盘、宠物赛跑…)。
//   ⇒ kNpc 是**一个族**,靠工作字段 + 行为绑定分派,不是 47 个子类型。
//
// ★ §2.3 **不复刻字符串→函数指针的运行期绑定**:原版链路
//     functionset= → functionSet[66] 回调名字符串 → function.c 331 条名表 hashpjw+strcmp
//     → 查不到返回 NULL,**不报错不日志**(实测三处可断且全部静默,18+16+6 例)。
//   ⇒ 新模型用接口 / 函数值直接注册,**断链在编译期发现**。
//   ⚠️ 迁移时须先跑那三重检查,否则会把已死的子系统当活的实现(16 §9.2 的 11 个弃用项)。

} // namespace SA::Model

#endif // __SA_EntityKind_H__
