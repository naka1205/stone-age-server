// shared/model/EntityIndex.h —— 句柄的哈希索引(不得线性扫描)
//
// ★★ 01 §5.2 / 03-domain-model.md §8.2 裁定:**索引不得线性扫描**。
//    原版 getfdFromCdkey / getfdFromCharaIndex / getCharindexFromFdid /
//    getFdidFromCharaIndex **全是** `for(i=0;i<ConnectLen;i++)` 全表扫 + 每格加解锁,
//    **每条 saac 应答至少扫一次**(部分协议扫两次)。
//    csa8.0 的 fdnum=100 下可忽略,但 stoneage85 的 fdnum=1000 就是每条应答 1000 次比较。
//    ⇒ 新实现全部走哈希索引:account→session、char_name→entity、entity→connection,查找 O(1)。
//
// ── 本文件的职责边界 ──────────────────────────────────────────────────
//   ★ 索引**只做标识符 → 句柄的映射**,不做 generation 校验 —— 那是 EntityPool::resolve
//     的活(拿到句柄后 resolve 一次即知新旧)。索引存的句柄可能已悬空,这是**正常**的:
//     两步分工 = 「名字找到槽」+「槽还是不是那个主人」,合并会让索引承担它管不了的世代事。
//   ⚠️ 不引入 net / session 的具体类型(那是 L1/L0,shared 够不着,01 §4)。
//     account / connection 这类外部标识符在本层是**轻量占位 key**(uint64 / 定长名字串),
//     真正的 SessionId / ConnectionId 由 world 侧在接线时映射进来。
//
// ⚠️ shared/ 只依赖标准库(01 §4)。

#ifndef __SA_EntityIndex_H__
#define __SA_EntityIndex_H__

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "model/Handle.h"

namespace SA::Model
{

// 标识符 → 句柄的哈希索引。
//
// ★ Key 是调用方给的标识符类型(账号号 / 角色名 / 连接号…);Value 恒为 EntityHandle。
//   find 未命中返回 kNullHandle —— 与「resolve 悬空返回 nullptr」同族:
//   查找失败有一个**明确的空值**,不靠哨兵下标或异常。
template <typename Key>
class EntityIndex
{
  public:
	// 建立 / 覆盖映射。★ 覆盖是显式行为:同一 key 重新 insert 即替换旧句柄
	//   (如角色改名后旧名先 erase、新名 insert;或同名顶号)。返回是否为新增。
	bool insert(const Key &key, EntityHandle handle)
	{
		const auto res = _map.insert_or_assign(key, handle);
		return res.second;
	}

	// 查找。未命中返回 kNullHandle。
	// ⚠️ 命中返回的句柄**可能已悬空** —— 调用方须再经 EntityPool::resolve 校验 generation。
	EntityHandle find(const Key &key) const
	{
		const auto it = _map.find(key);
		return it == _map.end() ? kNullHandle : it->second;
	}

	// 移除映射。返回是否确有一条被移除。
	bool erase(const Key &key) { return _map.erase(key) != 0; }

	bool contains(const Key &key) const { return _map.find(key) != _map.end(); }
	std::size_t size() const noexcept { return _map.size(); }
	bool empty() const noexcept { return _map.empty(); }
	void clear() noexcept { _map.clear(); }

  private:
	std::unordered_map<Key, EntityHandle> _map;
};

// ── 三个具名索引(03 §8.2 点名的三条查找路径)───────────────────────────
//
// ★ key 类型是**占位**,不是最终类型:account / connection 的真实 id 在 L1/L0,
//   shared 够不着(见卷首)。这里统一用 uint64 作外部标识符、std::string 作角色名。
//   world 接线时把真实 SessionId/ConnectionId 收窄或映射到这些 key 上。

// 账号 → 会话所属实体(原 getfdFromCdkey 的替身)。
using AccountIndex = EntityIndex<std::uint64_t>;

// 角色名 → 实体(原按名找角色的替身)。
// ⚠️ 名字长度上限见 DR-TS5(31 字节);本索引不强制长度,长度约束在写入前由领域层做。
using CharNameIndex = EntityIndex<std::string>;

// 连接号 → 实体(原 getCharindexFromFdid 的替身)。
using ConnIndex = EntityIndex<std::uint64_t>;

} // namespace SA::Model

#endif // __SA_EntityIndex_H__
