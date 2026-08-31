// shared/model/handle.h —— 实体句柄
//
// ★★ M10 硬约束(03-domain-model.md §9):**句柄必须带 generation**。
//
// 这不是理论洁癖,依据是两处实测故障:
//
//   ① 17 §7.2:`SERVSTATE_init()` 把 `servstate.fdid` **归零**
//      ⇒ gmsv 每次启动都从 0 重新发放续体号;而 saac 侧 `is_game_server_login(ti)`
//      只检查槽位登录状态、**没有世代号**
//      ⇒ 重启后旧实例的迟到应答可能命中新实例的同号连接,**投递给不相干的玩家**。
//      ★ 这是从原版实测挖出的**真实故障模式**,不是理论风险,且多实例下危害成倍放大。
//
//   ② 定长池的固有问题:槽位复用后,旧引用仍指向该槽。
//      而 00 §1.2 / 15 §9.1 要求保留扁平池布局(三根支柱),
//      ⇒ 槽位复用是设计的一部分,不能靠"不复用"回避。
//
// ⚠️ 与之配套的另一半在协议侧:服务间续体带 (instance_id, generation, request_id)
//    三元组(02 §7.1),缺一不可。本文件只管进程内。

#ifndef SG_SHARED_MODEL_HANDLE_H
#define SG_SHARED_MODEL_HANDLE_H

#include <cstdint>

namespace sg::model {

// 实体句柄。
//
// ★ `index` 是**定长池的下标**,不是指针 —— 03 §1:
//   「句柄是 int 下标,不是裸指针;实体访问经类型化视图,不直接暴露池。」
struct EntityHandle {
  std::uint32_t index      = 0;
  std::uint32_t generation = 0;  // ★ 槽位每次重用递增;0 保留给"空句柄"

  constexpr bool valid() const noexcept { return generation != 0; }

  friend constexpr bool operator==(EntityHandle a, EntityHandle b) noexcept {
    return a.index == b.index && a.generation == b.generation;
  }
  friend constexpr bool operator!=(EntityHandle a, EntityHandle b) noexcept {
    return !(a == b);
  }
};

inline constexpr EntityHandle kNullHandle{};

}  // namespace sg::model

#endif  // SG_SHARED_MODEL_HANDLE_H
