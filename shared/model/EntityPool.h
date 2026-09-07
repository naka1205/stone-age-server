// shared/model/EntityPool.h —— 定长实体池 + generation 句柄回收(M10)
//
// ★★ 03-domain-model.md §1「两个必须同时成立的形状」的**对内**一侧:
//     对外(shared/model)  CHAR_TYPE 分派的和类型 —— 强类型(EntityKind.h)
//     对内(world 运行时)  定长池 + int 下标句柄 —— 启动期一次性分配,运行期零分配
//   ⇒ 只做一侧都会失败(§1):只做和类型 + 堆对象图 ⇒ 15 §9.1 三根支柱失效
//     (从 GC 停顿变成 cache miss 与分配抖动);只做扁平池 + 宽表 ⇒ 别名静默错误照旧。
//
// ── 三根支柱(15 §9.1),本池的存在理由 ────────────────────────────────
//   ① 运行期零分配:启动期一次性分配 std::array,allocate/release 不碰堆;
//   ② 活对象数常量且几乎无指针图:句柄是 uint32 下标,不是裸指针;
//   ③ 单 tick 只触及 5.4% 槽位:遍历接口留口(本批次不做脏槽跟踪,见文末)。
//
// ── M10:句柄必须带 generation(Handle.h 已给类型,本池兑现其语义)──────
//   槽位复用是设计的一部分(定长池 + 三根支柱)⇒ 不能靠"不复用"回避悬空引用。
//   release() 使该槽 generation++ ⇒ 所有旧句柄作废;resolve() 校验 generation
//   不匹配即返回 nullptr —— 悬空引用**当场变空指针,而不是脏读一个被复用的槽**。
//   这正是 17 §7.2 那个真实故障(gmsv 重启 fdid 归零 ⇒ 迟到应答命中新连接)的根治。
//
// ⚠️ shared/ 只依赖标准库(01 §4)。本文件不 #include 任何项目内传输 / 会话类型。

#ifndef __SA_EntityPool_H__
#define __SA_EntityPool_H__

#include <array>
#include <cstddef>
#include <cstdint>

#include "model/Handle.h"

namespace SA::Model
{

// 定长实体池。
//
// ★ `Capacity` 是编译期常量 ⇒ 存储是 std::array,不是 vector:15 §9.1 的
//   「运行期零分配」要求整个生命周期不触碰堆。启动期一次性构造,此后只在
//   固定槽位上原地复用。
//
// ⚠️ 句柄的 `index` 是**池下标**(0..Capacity-1),不是裸指针(03 §1)。
//   实体访问一律经 resolve()——不直接暴露底层 array。
template <typename T, std::size_t Capacity>
class EntityPool
{
  public:
	static_assert(Capacity > 0, "EntityPool 容量必须为正");
	// ★ index 与 generation 都是 uint32(见 Handle.h)。这里只需容量能被 uint32 表示;
	//   自由链用 index 串成,kNoFreeSlot 借用一个不可能的下标值作链尾哨兵。
	static_assert(Capacity <= 0xFFFFFFFEu,
	              "EntityPool 容量须留一个 uint32 值给自由链尾哨兵");

	EntityPool() noexcept { reset(); }

	// 分配一个槽。★ 满时返回 kNullHandle —— 调用方**必须判**(容量是硬上限,
	//   不像 vector 会悄悄扩容;悄悄扩容会破坏三根支柱)。
	//
	// generation 语义:每个槽从 1 起(0 保留给空句柄,见 Handle.h);此后每次
	// release 时 ++。分配返回的句柄携带该槽**当前** generation,resolve 靠它鉴别新旧。
	EntityHandle allocate() noexcept
	{
		if (_free_head == kNoFreeSlot)
			return kNullHandle;

		const std::uint32_t idx = _free_head;
		Slot &s = _slots[idx];
		_free_head = s.next_free; // 从自由链摘下
		s.next_free = kNoFreeSlot;
		s.occupied = true;
		// ★ 发一个**干净**的槽:复用槽必须清掉前主人的数据,否则新主人会读到脏值
		//   (领域实体的静默错误,00 §10.4 那一类)。这是原地赋值,不是堆分配 ⇒
		//   不违反三根支柱的「运行期零分配」。调用方仍应完整初始化,但不必依赖它清零。
		s.value = T{};
		// ★ generation 在构造 / reset 时已置 1;release 时才 ++。
		//   ⇒ 这里不动 generation,分配到的就是"当前世代"。
		++_size;
		return EntityHandle{idx, s.generation};
	}

	// 回收一个句柄持有的槽。
	//
	// ★ 校验 generation:传入的是**过期句柄**(该槽已被 release 过并可能重分配)时,
	//   不做任何事、返回 false —— 否则会误回收一个属于新主人的活槽。
	// ⚠️ generation++ 在这里发生:回收即作废所有仍指向该槽的旧句柄(M10 的核心)。
	bool release(EntityHandle h) noexcept
	{
		Slot *s = slotOf(h);
		if (s == nullptr)
			return false;
		s->occupied = false;
		++s->generation; // ★ 作废旧句柄;溢出回绕见文末说明
		if (s->generation == 0)
			s->generation = 1; // 跳过 0(0 = 空句柄),回绕后仍是合法世代
		s->next_free = _free_head; // 入自由链
		_free_head = h.index;
		--_size;
		return true;
	}

	// 解析句柄 ⇒ 实体指针。generation 不匹配(悬空 / 空句柄)返回 nullptr。
	// ★ 这是 M10 的运行时兑现:悬空引用当场变 nullptr,不是脏读复用槽。
	T *resolve(EntityHandle h) noexcept
	{
		Slot *s = slotOf(h);
		return s == nullptr ? nullptr : &s->value;
	}
	const T *resolve(EntityHandle h) const noexcept
	{
		const Slot *s = slotOf(h);
		return s == nullptr ? nullptr : &s->value;
	}

	// 活跃槽数 / 容量。
	std::size_t size() const noexcept { return _size; }
	static constexpr std::size_t capacity() noexcept { return Capacity; }
	bool empty() const noexcept { return _size == 0; }
	bool full() const noexcept { return _size == Capacity; }

	// 某槽当前是否活跃(供遍历用)。⚠️ 这不是"句柄有效",是"该下标有活对象"。
	bool occupiedAt(std::size_t index) const noexcept
	{
		return index < Capacity && _slots[index].occupied;
	}

	// 取某下标的当前有效句柄(该下标必须活跃,否则返回 kNullHandle)。
	// ★ 遍历时用:for(i){ if(occupiedAt(i)) handleAt(i)... }。
	//   为「单 tick 只触及 5.4% 槽位」留的口 —— 本批次不做脏槽跟踪(见文末),
	//   调用方按需遍历;将来接活跃集时再收窄。
	EntityHandle handleAt(std::size_t index) const noexcept
	{
		if (index >= Capacity || !_slots[index].occupied)
			return kNullHandle;
		return EntityHandle{static_cast<std::uint32_t>(index),
		                    _slots[index].generation};
	}

	// 全量重置:所有槽回到空、generation 归 1、自由链重建为 0→1→…→N-1。
	// ⚠️ 这会作废**所有**已发出的句柄(它们的 generation 与重置后不一致)——
	//   仅供池创建与整场清空用,运行期不要调。
	void reset() noexcept
	{
		_free_head = 0;
		_size = 0;
		for (std::size_t i = 0; i < Capacity; ++i)
		{
			_slots[i].occupied = false;
			_slots[i].generation = 1; // 从 1 起,0 留给空句柄
			_slots[i].next_free =
			    (i + 1 < Capacity) ? static_cast<std::uint32_t>(i + 1) : kNoFreeSlot;
		}
	}

  private:
	// 自由链尾哨兵:不可能是合法下标(容量已 static_assert ≤ 0xFFFFFFFE)。
	static constexpr std::uint32_t kNoFreeSlot = 0xFFFFFFFFu;

	struct Slot
	{
		T value{};
		std::uint32_t generation = 1;
		std::uint32_t next_free = kNoFreeSlot; // 空闲时串自由链;占用时无意义
		bool occupied = false;
	};

	// 句柄 → 活跃槽指针,generation 校验不过返回 nullptr(内部工具)。
	Slot *slotOf(EntityHandle h) noexcept
	{
		if (!h.valid() || h.index >= Capacity)
			return nullptr;
		Slot &s = _slots[h.index];
		if (!s.occupied || s.generation != h.generation)
			return nullptr;
		return &s;
	}
	const Slot *slotOf(EntityHandle h) const noexcept
	{
		if (!h.valid() || h.index >= Capacity)
			return nullptr;
		const Slot &s = _slots[h.index];
		if (!s.occupied || s.generation != h.generation)
			return nullptr;
		return &s;
	}

	std::array<Slot, Capacity> _slots{};
	std::uint32_t _free_head = 0;
	std::size_t _size = 0;
};

// ── 文末:本批次有意留在门外的东西(均非遗漏)─────────────────────────
//
// ① **脏槽跟踪 / 活跃集**:三根支柱第 ③ 条「单 tick 只触及 5.4% 槽位」要的是
//    一个 tick 只遍历本 tick 被触及的槽,而不是全 Capacity 扫。本批次只留
//    occupiedAt/handleAt 的遍历口,不建活跃集 —— 那要和 world 的 tick 调度对齐
//    (谁在本 tick 变脏),属接线阶段,不该由纯结构层猜。
//
// ② **generation 回绕**:uint32 世代号在同一槽被 release 约 43 亿次后回绕。
//    release 里已跳过 0(回绕后仍是合法世代),但**理论上**回绕到某个恰好等于
//    一个远古悬空句柄的世代会误命中。现实中单场景实体不会 release 到这个量级,
//    且真正的续体安全在协议侧另有 (instance_id, generation, request_id) 三元组
//    (02 §7.1)兜底 ⇒ 本层不额外加宽,记明即可。
//
// ③ **类型化视图 / 各族池实例**:EntityPool<Player,N> / EntityPool<Pet,N> 等的
//    具体实例化,要等各族结构落地(后续批次)。本文件只给容器模板本身。

} // namespace SA::Model

#endif // __SA_EntityPool_H__
