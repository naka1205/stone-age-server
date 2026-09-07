// tests/ModelPoolTest.cpp —— L2 实体池地基的用例(阶段 2,L2 起步)
//
// ═══════════════════════════════════════════════════════════════════════════
//  本文件守的是三条**结构性硬约束**,不是公式
// ═══════════════════════════════════════════════════════════════════════════
//
// 与 rules_battle 的黄金用例集不同,这里断言的不是"公式对不对"(那是 ③ 层不可自证),
// 而是 03-domain-model.md §9 的 M2 / M10 与 §8.2「索引不得线性扫描」在**结构上**成立:
//   · M10:句柄带 generation ⇒ 悬空句柄 resolve 返回 nullptr(不脏读复用槽);
//   · 池:定长、满时返回空句柄、回收后复用同一 index 但 generation 不同;
//   · 索引:标识符 → 句柄 O(1),未命中有明确空值,不做 generation 校验(那是池的活)。
//
// ★ 反向验证(用例集纪律):把 EntityPool::resolve 里 `s.generation != h.generation`
//   这道校验去掉 ⇒ 下面「回收后旧句柄 resolve == nullptr」当场转红 —— 证明该断言
//   真在读 generation,而不是只数分配次数。
//
// ⚠️ C++17 编译(见 tests/CMakeLists.txt):shared/ 要能在客户端工具链(GameStudio,
//    C++17)下跑,用例集也一样,否则"双端同一份"只有实现共享、验证不共享。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "model/EntityIndex.h"
#include "model/EntityKind.h"
#include "model/EntityPool.h"

#include <string>

using namespace SA::Model;

namespace
{
// 一个最小实体:只为验容器语义,不是任何族的真实字段。
struct Dummy
{
	int payload = 0;
};
} // namespace

// ── EntityPool:分配 / 回收 / generation ─────────────────────────────────

TEST_CASE("EntityPool:空句柄与初始状态")
{
	EntityPool<Dummy, 4> pool;
	CHECK(pool.size() == 0);
	CHECK(pool.capacity() == 4);
	CHECK(pool.empty());
	CHECK_FALSE(pool.full());

	// 空句柄 resolve 恒 nullptr(generation==0 ⇒ !valid())。
	CHECK(pool.resolve(kNullHandle) == nullptr);
}

TEST_CASE("EntityPool:分配返回有效句柄,resolve 命中同一实体")
{
	EntityPool<Dummy, 4> pool;
	const EntityHandle h = pool.allocate();
	REQUIRE(h.valid());
	CHECK(pool.size() == 1);

	Dummy *d = pool.resolve(h);
	REQUIRE(d != nullptr);
	d->payload = 42;
	// 同句柄再解析拿到同一对象。
	CHECK(pool.resolve(h)->payload == 42);
}

TEST_CASE("EntityPool:容量耗尽 ⇒ allocate 返回 kNullHandle,不悄悄扩容")
{
	// ★ 定长是硬上限 —— 悄悄扩容会破坏 15 §9.1 的三根支柱。满时必须给明确空值。
	EntityPool<Dummy, 3> pool;
	const EntityHandle a = pool.allocate();
	const EntityHandle b = pool.allocate();
	const EntityHandle c = pool.allocate();
	REQUIRE(a.valid());
	REQUIRE(b.valid());
	REQUIRE(c.valid());
	CHECK(pool.full());

	const EntityHandle overflow = pool.allocate();
	CHECK_FALSE(overflow.valid());
	CHECK(overflow == kNullHandle);
	CHECK(pool.size() == 3);
}

TEST_CASE("EntityPool:★ M10 —— 回收后旧句柄 resolve 返回 nullptr")
{
	// 这是本文件的核心断言。反向验证见文件卷首。
	EntityPool<Dummy, 4> pool;
	const EntityHandle h = pool.allocate();
	REQUIRE(pool.resolve(h) != nullptr);

	REQUIRE(pool.release(h));
	CHECK(pool.size() == 0);
	// ★ 旧句柄此刻悬空:槽 generation 已 ++,校验不匹配 ⇒ nullptr,不脏读。
	CHECK(pool.resolve(h) == nullptr);
}

TEST_CASE("EntityPool:回收后重分配复用同一 index,但 generation 不同")
{
	EntityPool<Dummy, 4> pool;
	const EntityHandle first = pool.allocate();
	pool.resolve(first)->payload = 7;
	REQUIRE(pool.release(first));

	// 自由链是 LIFO ⇒ 下一个分配拿回刚释放的那个 index。
	const EntityHandle second = pool.allocate();
	CHECK(second.index == first.index);       // ★ 同一物理槽复用
	CHECK(second.generation != first.generation); // ★ 但世代已变
	CHECK(second.valid());

	// 旧句柄仍解析失败(悬空),新句柄解析成功且是**干净**的槽。
	CHECK(pool.resolve(first) == nullptr);
	Dummy *d = pool.resolve(second);
	REQUIRE(d != nullptr);
	CHECK(d->payload == 0); // ★ allocate 发的是干净槽(清掉前主人的 payload=7)
}

TEST_CASE("EntityPool:release 过期句柄不误伤新主人")
{
	EntityPool<Dummy, 4> pool;
	const EntityHandle first = pool.allocate();
	REQUIRE(pool.release(first));
	const EntityHandle second = pool.allocate(); // 复用同槽,新世代
	REQUIRE(second.valid());

	// ★ 用**过期**的 first 再 release ⇒ generation 不匹配 ⇒ 拒绝,不回收 second 的槽。
	CHECK_FALSE(pool.release(first));
	CHECK(pool.size() == 1);
	CHECK(pool.resolve(second) != nullptr); // second 仍活着
}

TEST_CASE("EntityPool:遍历口 occupiedAt / handleAt 与活跃状态一致")
{
	EntityPool<Dummy, 4> pool;
	const EntityHandle a = pool.allocate(); // index 0
	const EntityHandle b = pool.allocate(); // index 1
	(void)b;
	REQUIRE(pool.release(a)); // 释放 index 0

	CHECK_FALSE(pool.occupiedAt(a.index)); // 0 已空
	CHECK(pool.handleAt(a.index) == kNullHandle);

	// 至少有一个活跃槽,且其 handleAt 能 resolve。
	std::size_t alive = 0;
	for (std::size_t i = 0; i < pool.capacity(); ++i)
	{
		if (!pool.occupiedAt(i))
			continue;
		++alive;
		const EntityHandle h = pool.handleAt(i);
		CHECK(h.valid());
		CHECK(pool.resolve(h) != nullptr);
	}
	CHECK(alive == pool.size());
}

// ── EntityIndex:标识符 → 句柄 ────────────────────────────────────────────

TEST_CASE("EntityIndex:insert / find / erase 的基本语义")
{
	CharNameIndex idx;
	const EntityHandle h{3, 5};

	CHECK(idx.empty());
	CHECK(idx.insert("alice", h));       // 新增返回 true
	CHECK(idx.size() == 1);
	CHECK(idx.contains("alice"));
	CHECK(idx.find("alice") == h);

	// 未命中 ⇒ 明确的空值。
	CHECK(idx.find("bob") == kNullHandle);

	CHECK(idx.erase("alice"));
	CHECK_FALSE(idx.erase("alice")); // 已不在
	CHECK(idx.find("alice") == kNullHandle);
}

TEST_CASE("EntityIndex:同 key 重新 insert 覆盖旧句柄(改名 / 顶号)")
{
	CharNameIndex idx;
	const EntityHandle first{1, 1};
	const EntityHandle second{9, 2};

	CHECK(idx.insert("hero", first));
	CHECK_FALSE(idx.insert("hero", second)); // 覆盖,非新增
	CHECK(idx.size() == 1);
	CHECK(idx.find("hero") == second); // ★ 覆盖是显式行为
}

TEST_CASE("EntityIndex:索引存的句柄可能悬空 —— 校验交给池,不在索引里做")
{
	// ★ 03 §8.2 的两步分工:索引「名字 → 槽」,池「槽还是不是那个主人」。
	EntityPool<Dummy, 4> pool;
	CharNameIndex idx;

	const EntityHandle h = pool.allocate();
	CHECK(idx.insert("npc", h));
	REQUIRE(pool.release(h)); // 实体没了,但索引项还在(正常)

	const EntityHandle looked_up = idx.find("npc");
	CHECK(looked_up == h);                  // 索引照样命中
	CHECK(pool.resolve(looked_up) == nullptr); // 池负责发现它悬空
}

// ── EntityKind:和类型判别键(M2)─────────────────────────────────────────

TEST_CASE("EntityKind:五族值域完整且互不相等")
{
	static_assert(static_cast<std::uint8_t>(EntityKind::kPlayer) == 0, "");
	static_assert(static_cast<std::uint8_t>(EntityKind::kWorldObject) ==
	                  kEntityKindCount - 1,
	              "五族的最后一个应等于 kEntityKindCount-1");
	static_assert(kEntityKindCount == 5, "03 §2.1 的五族");

	CHECK(EntityKind::kPlayer != EntityKind::kPet);
	CHECK(EntityKind::kPet != EntityKind::kEnemy);
	CHECK(EntityKind::kEnemy != EntityKind::kNpc);
	CHECK(EntityKind::kNpc != EntityKind::kWorldObject);
}
