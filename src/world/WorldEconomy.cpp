// src/world/WorldEconomy.cpp —— 摆摊系统与寄售拍卖市场系统实现
//
// 对应原版 char.c:9057 STREET_VENDOR, market/auction

#include "WorldImpl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace SA::World
{

// ══ 玩家摆摊系统 (阶段 2: 玩家地摊系统, 对齐官方 STREET_VENDOR) ══════════════
bool World::openStall(SA::Net::SessionId seller, const std::string &title)
{
	Impl &s = *_impl;
	if (s.conns.find(seller) == s.conns.end())
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(seller));
	if (p == nullptr)
		return false;

	if (p->hp <= 0 || s.inBattle(seller))
		return false;

	if (s.partyModeOf(seller) != PartyMode::kNone)
		return false;

	if (s.trade_of_session.count(seller) > 0)
		return false;

	auto it = s.stalls.find(seller);
	if (it != s.stalls.end() && it->second.open)
		return false;

	StallInfo info{};
	info.seller = seller;
	info.seller_name = p->name.c_str();
	info.title = title.empty() ? (info.seller_name + "的摊位") : title;
	info.floor = p->floor;
	info.x = p->x;
	info.y = p->y;
	info.open = false;
	s.stalls[seller] = std::move(info);
	return true;
}

bool World::setStallItem(SA::Net::SessionId seller, int item_slot, std::uint32_t price)
{
	Impl &s = *_impl;
	if (price == 0)
		return false;

	auto it = s.stalls.find(seller);
	if (it == s.stalls.end())
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(seller));
	if (p == nullptr)
		return false;

	if (item_slot < static_cast<int>(SA::Model::kStartItemArray) ||
	    static_cast<std::size_t>(item_slot) >= SA::Model::kMaxItemHave)
		return false;

	if (!p->items[static_cast<std::size_t>(item_slot)].valid())
		return false;

	const auto *item_obj = s.items.resolve(p->items[static_cast<std::size_t>(item_slot)]);
	if (item_obj == nullptr)
		return false;

	for (auto &entry : it->second.items)
	{
		if (entry.item_slot == item_slot)
		{
			entry.price = price;
			return true;
		}
	}

	if (it->second.items.size() >= kMaxStallItemSlots)
		return false;

	StallItemEntry entry{};
	entry.item_slot = item_slot;
	entry.price = price;
	entry.item_name = item_obj->name.c_str();
	it->second.items.push_back(std::move(entry));
	return true;
}

bool World::setStallPet(SA::Net::SessionId seller, int pet_slot, std::uint32_t price)
{
	Impl &s = *_impl;
	if (price == 0)
		return false;

	auto it = s.stalls.find(seller);
	if (it == s.stalls.end())
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(seller));
	if (p == nullptr)
		return false;

	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return false;

	if (!p->pets[static_cast<std::size_t>(pet_slot)].valid())
		return false;

	const auto *pet_obj = s.pets.resolve(p->pets[static_cast<std::size_t>(pet_slot)]);
	if (pet_obj == nullptr)
		return false;

	if (playerRidePetSlot(seller) == pet_slot)
	{
		dismountPet(seller);
	}

	if (p->default_pet == pet_slot)
	{
		p->default_pet = -1;
	}

	for (auto &entry : it->second.pets)
	{
		if (entry.pet_slot == pet_slot)
		{
			entry.price = price;
			return true;
		}
	}

	if (it->second.pets.size() >= kMaxStallPetSlots)
		return false;

	StallPetEntry entry{};
	entry.pet_slot = pet_slot;
	entry.price = price;
	entry.pet_name = pet_obj->name.c_str();
	entry.pet_level = pet_obj->level;
	it->second.pets.push_back(std::move(entry));
	return true;
}

bool World::removeStallItem(SA::Net::SessionId seller, int item_slot)
{
	auto it = _impl->stalls.find(seller);
	if (it == _impl->stalls.end())
		return false;

	for (auto eit = it->second.items.begin(); eit != it->second.items.end(); ++eit)
	{
		if (eit->item_slot == item_slot)
		{
			it->second.items.erase(eit);
			return true;
		}
	}
	return false;
}

bool World::removeStallPet(SA::Net::SessionId seller, int pet_slot)
{
	auto it = _impl->stalls.find(seller);
	if (it == _impl->stalls.end())
		return false;

	for (auto eit = it->second.pets.begin(); eit != it->second.pets.end(); ++eit)
	{
		if (eit->pet_slot == pet_slot)
		{
			it->second.pets.erase(eit);
			return true;
		}
	}
	return false;
}

bool World::startStallVending(SA::Net::SessionId seller)
{
	Impl &s = *_impl;
	auto it = s.stalls.find(seller);
	if (it == s.stalls.end())
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(seller));
	if (p == nullptr)
		return false;

	if (p->hp <= 0 || s.inBattle(seller) || s.partyModeOf(seller) != PartyMode::kNone ||
	    s.trade_of_session.count(seller) > 0)
		return false;

	if (it->second.items.empty() && it->second.pets.empty())
		return false;

	for (const auto &item_e : it->second.items)
	{
		if (!p->items[static_cast<std::size_t>(item_e.item_slot)].valid())
			return false;
	}
	for (const auto &pet_e : it->second.pets)
	{
		if (!p->pets[static_cast<std::size_t>(pet_e.pet_slot)].valid())
			return false;
	}

	it->second.open = true;
	it->second.floor = p->floor;
	it->second.x = p->x;
	it->second.y = p->y;

	auto cit = s.conns.find(seller);
	if (cit != s.conns.end())
	{
		cit->second.walk_seq.clear();
	}
	return true;
}

bool World::closeStall(SA::Net::SessionId seller)
{
	auto it = _impl->stalls.find(seller);
	if (it == _impl->stalls.end())
		return false;
	_impl->stalls.erase(it);
	return true;
}

bool World::isPlayerVending(SA::Net::SessionId seller) const
{
	auto it = _impl->stalls.find(seller);
	return it != _impl->stalls.end() && it->second.open;
}

std::optional<StallInfo> World::getPlayerStall(SA::Net::SessionId seller) const
{
	auto it = _impl->stalls.find(seller);
	if (it != _impl->stalls.end())
		return it->second;
	return std::nullopt;
}

std::vector<StallInfo> World::nearbyStalls(SA::Net::SessionId viewer, int max_distance) const
{
	std::vector<StallInfo> results;
	const auto *vp = _impl->players.resolve(_impl->player_of_session.find(viewer));
	if (vp == nullptr)
		return results;

	for (const auto &kv : _impl->stalls)
	{
		const auto &stall = kv.second;
		if (!stall.open)
			continue;
		if (stall.floor != vp->floor)
			continue;
		const int dx = std::abs(stall.x - vp->x);
		const int dy = std::abs(stall.y - vp->y);
		if (std::max(dx, dy) <= max_distance)
		{
			results.push_back(stall);
		}
	}
	return results;
}

bool World::buyFromStall(SA::Net::SessionId buyer, SA::Net::SessionId seller,
                         MarketAssetType asset_type, int slot)
{
	Impl &s = *_impl;
	if (buyer == seller)
		return false;

	if (!isPlayerVending(seller))
		return false;

	SA::Model::Player *bp = s.players.resolve(s.player_of_session.find(buyer));
	SA::Model::Player *sp = s.players.resolve(s.player_of_session.find(seller));
	if (bp == nullptr || sp == nullptr)
		return false;

	if (bp->hp <= 0 || sp->hp <= 0 || s.inBattle(buyer) || s.inBattle(seller))
		return false;

	if (bp->floor != sp->floor)
		return false;

	const int dx = std::abs(bp->x - sp->x);
	const int dy = std::abs(bp->y - sp->y);
	if (std::max(dx, dy) > 3)
		return false;

	auto stall_it = s.stalls.find(seller);
	if (stall_it == s.stalls.end() || !stall_it->second.open)
		return false;

	if (asset_type == MarketAssetType::kItem)
	{
		auto item_it = stall_it->second.items.end();
		for (auto it = stall_it->second.items.begin(); it != stall_it->second.items.end(); ++it)
		{
			if (it->item_slot == slot)
			{
				item_it = it;
				break;
			}
		}
		if (item_it == stall_it->second.items.end())
			return false;

		if (slot < static_cast<int>(SA::Model::kStartItemArray) ||
		    static_cast<std::size_t>(slot) >= SA::Model::kMaxItemHave)
			return false;

		const auto ih = sp->items[static_cast<std::size_t>(slot)];
		if (!ih.valid())
			return false;

		const int buyer_slot = bp->findFreeItemSlot();
		if (buyer_slot < 0)
			return false;

		const std::uint32_t price = item_it->price;
		if (bp->gold < static_cast<std::int32_t>(price))
			return false;

		(void)delGold(*bp, GoldReason::kMarketBuy, static_cast<std::int32_t>(price), 0, 0, s);
		auto tx = addGold(*sp, GoldReason::kMarketSellEarn, static_cast<std::int32_t>(price), 0, 0, s);
		if (tx.disposition == GoldDisposition::kClamped && tx.overflow > 0)
		{
			s.deliverSystemMail(sp->name.c_str(), "摆摊收入超额补发",
			                    "您摆摊出售道具获得的石币已超出随身携带上限，溢出部分转存附件。",
			                    std::nullopt, std::nullopt, static_cast<std::uint32_t>(tx.overflow));
		}

		sp->clearItemSlot(slot);
		bp->items[static_cast<std::size_t>(buyer_slot)] = ih;
		if (auto *item_obj = s.items.resolve(ih); item_obj != nullptr)
		{
			item_obj->owner = s.player_of_session.find(buyer);
		}

		stall_it->second.items.erase(item_it);
		if (stall_it->second.items.empty() && stall_it->second.pets.empty())
		{
			closeStall(seller);
		}
		return true;
	}
	else if (asset_type == MarketAssetType::kPet)
	{
		auto pet_it = stall_it->second.pets.end();
		for (auto it = stall_it->second.pets.begin(); it != stall_it->second.pets.end(); ++it)
		{
			if (it->pet_slot == slot)
			{
				pet_it = it;
				break;
			}
		}
		if (pet_it == stall_it->second.pets.end())
			return false;

		if (slot < 0 || static_cast<std::size_t>(slot) >= SA::Model::kMaxPetHave)
			return false;

		const auto ph = sp->pets[static_cast<std::size_t>(slot)];
		if (!ph.valid())
			return false;

		const int buyer_slot = bp->findFreePetSlot();
		if (buyer_slot < 0)
			return false;

		const std::uint32_t price = pet_it->price;
		if (bp->gold < static_cast<std::int32_t>(price))
			return false;

		if (sp->default_pet == slot)
		{
			sp->default_pet = -1;
		}

		(void)delGold(*bp, GoldReason::kMarketBuy, static_cast<std::int32_t>(price), 0, 0, s);
		auto tx = addGold(*sp, GoldReason::kMarketSellEarn, static_cast<std::int32_t>(price), 0, 0, s);
		if (tx.disposition == GoldDisposition::kClamped && tx.overflow > 0)
		{
			s.deliverSystemMail(sp->name.c_str(), "摆摊收入超额补发",
			                    "您摆摊出售宠物获得的石币已超出随身携带上限，溢出部分转存附件。",
			                    std::nullopt, std::nullopt, static_cast<std::uint32_t>(tx.overflow));
		}

		sp->clearPetSlot(slot);
		bp->pets[static_cast<std::size_t>(buyer_slot)] = ph;
		if (auto *pet_obj = s.pets.resolve(ph); pet_obj != nullptr)
		{
			pet_obj->owner = s.player_of_session.find(buyer);
		}

		stall_it->second.pets.erase(pet_it);
		if (stall_it->second.items.empty() && stall_it->second.pets.empty())
		{
			closeStall(seller);
		}
		return true;
	}

	return false;
}

// ══ 寄售与拍卖市场系统 (阶段 2: 市场/拍卖行系统) ════════════════════════════
std::uint64_t World::listMarketItem(SA::Net::SessionId seller, int item_slot, std::uint32_t price)
{
	Impl &s = *_impl;
	if (price == 0)
		return 0;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(seller));
	if (p == nullptr)
		return 0;

	if (p->hp <= 0 || s.inBattle(seller))
		return 0;

	if (item_slot < static_cast<int>(SA::Model::kStartItemArray) ||
	    static_cast<std::size_t>(item_slot) >= SA::Model::kMaxItemHave)
		return 0;

	const auto ih = p->items[static_cast<std::size_t>(item_slot)];
	if (!ih.valid())
		return 0;

	const auto *item = s.items.resolve(ih);
	if (item == nullptr)
		return 0;

	const std::string seller_name = p->name.c_str();
	std::size_t active_count = 0;
	for (const auto &kv : s.market_listings)
	{
		if ((kv.second.seller_session == seller || kv.second.seller_name == seller_name) &&
		    !kv.second.sold && !kv.second.cancelled)
		{
			++active_count;
		}
	}
	if (active_count >= kMaxMarketListingsPerPlayer)
		return 0;

	if (p->gold < static_cast<std::int32_t>(kMarketListingFee))
		return 0;

	const std::uint64_t lid = s.next_market_listing_id++;

	(void)delGold(*p, GoldReason::kMarketListFee, static_cast<std::int32_t>(kMarketListingFee), 0, lid, s);

	SA::Model::Item snapshot = *item;
	s.items.release(ih);
	p->clearItemSlot(item_slot);

	MarketListing listing{};
	listing.listing_id = lid;
	listing.seller_session = seller;
	listing.seller_name = seller_name;
	listing.asset_type = MarketAssetType::kItem;
	listing.asset_name = snapshot.name.c_str();
	listing.asset_level = snapshot.level;
	listing.price = price;
	listing.list_timestamp_ms = s.now_ms;
	listing.item_data = std::move(snapshot);
	listing.sold = false;
	listing.cancelled = false;

	s.market_listings[lid] = std::move(listing);
	return lid;
}

std::uint64_t World::listMarketPet(SA::Net::SessionId seller, int pet_slot, std::uint32_t price)
{
	Impl &s = *_impl;
	if (price == 0)
		return 0;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(seller));
	if (p == nullptr)
		return 0;

	if (p->hp <= 0 || s.inBattle(seller))
		return 0;

	if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
		return 0;

	const auto ph = p->pets[static_cast<std::size_t>(pet_slot)];
	if (!ph.valid())
		return 0;

	const auto *pet = s.pets.resolve(ph);
	if (pet == nullptr)
		return 0;

	const std::string seller_name = p->name.c_str();
	std::size_t active_count = 0;
	for (const auto &kv : s.market_listings)
	{
		if ((kv.second.seller_session == seller || kv.second.seller_name == seller_name) &&
		    !kv.second.sold && !kv.second.cancelled)
		{
			++active_count;
		}
	}
	if (active_count >= kMaxMarketListingsPerPlayer)
		return 0;

	if (p->gold < static_cast<std::int32_t>(kMarketListingFee))
		return 0;

	if (playerRidePetSlot(seller) == pet_slot)
	{
		dismountPet(seller);
	}

	if (p->default_pet == pet_slot)
	{
		p->default_pet = -1;
	}

	const std::uint64_t lid = s.next_market_listing_id++;

	(void)delGold(*p, GoldReason::kMarketListFee, static_cast<std::int32_t>(kMarketListingFee), 0, lid, s);

	SA::Model::Pet snapshot = *pet;
	s.pets.release(ph);
	p->clearPetSlot(pet_slot);

	MarketListing listing{};
	listing.listing_id = lid;
	listing.seller_session = seller;
	listing.seller_name = seller_name;
	listing.asset_type = MarketAssetType::kPet;
	listing.asset_name = snapshot.name.c_str();
	listing.asset_level = snapshot.level;
	listing.price = price;
	listing.list_timestamp_ms = s.now_ms;
	listing.pet_data = std::move(snapshot);
	listing.sold = false;
	listing.cancelled = false;

	s.market_listings[lid] = std::move(listing);
	return lid;
}

bool World::cancelMarketListing(SA::Net::SessionId seller, std::uint64_t listing_id)
{
	Impl &s = *_impl;
	auto it = s.market_listings.find(listing_id);
	if (it == s.market_listings.end())
		return false;

	auto &listing = it->second;
	if (listing.sold || listing.cancelled)
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(seller));
	if (p == nullptr)
		return false;

	if (listing.seller_session != seller && listing.seller_name != p->name.c_str())
		return false;

	if (listing.asset_type == MarketAssetType::kItem && listing.item_data.has_value())
	{
		const int s_idx = giveItemToPlayer(seller, *listing.item_data);
		if (s_idx < 0)
		{
			s.deliverSystemMail(listing.seller_name, "寄售物品下架返还",
			                    "因背包已满或系统资源受限，下架物品已退回邮箱附件。",
			                    listing.item_data);
		}
		else
		{
			auto ih = p->items[static_cast<std::size_t>(s_idx)];
			if (auto *item_obj = s.items.resolve(ih); item_obj != nullptr)
			{
				item_obj->owner = s.player_of_session.find(seller);
			}
		}
	}
	else if (listing.asset_type == MarketAssetType::kPet && listing.pet_data.has_value())
	{
		const int s_idx = givePetToPlayer(seller, *listing.pet_data);
		if (s_idx < 0)
		{
			s.deliverSystemMail(listing.seller_name, "寄售宠物下架返还",
			                    "因宠物栏已满或系统资源受限，下架宠物已退回邮箱附件。",
			                    std::nullopt, listing.pet_data);
		}
	}

	listing.cancelled = true;
	return true;
}

bool World::buyMarketListing(SA::Net::SessionId buyer, std::uint64_t listing_id)
{
	Impl &s = *_impl;
	auto it = s.market_listings.find(listing_id);
	if (it == s.market_listings.end())
		return false;

	auto &listing = it->second;
	if (listing.sold || listing.cancelled)
		return false;

	SA::Model::Player *bp = s.players.resolve(s.player_of_session.find(buyer));
	if (bp == nullptr)
		return false;

	if (bp->hp <= 0 || s.inBattle(buyer))
		return false;

	if (listing.seller_session == buyer || listing.seller_name == bp->name.c_str())
		return false;

	if (listing.asset_type == MarketAssetType::kItem)
	{
		if (bp->findFreeItemSlot() < 0)
			return false;
	}
	else if (listing.asset_type == MarketAssetType::kPet)
	{
		if (bp->findFreePetSlot() < 0)
			return false;
	}

	if (bp->gold < static_cast<std::int32_t>(listing.price))
		return false;

	(void)delGold(*bp, GoldReason::kMarketBuy, static_cast<std::int32_t>(listing.price), 0, listing_id, s);

	const std::uint32_t tax = (listing.price * kMarketTaxRatePercent) / 100;
	const std::uint32_t net_revenue = listing.price - tax;

	GoldTx tax_tx{};
	tax_tx.carrier = GoldCarrier::kGold;
	tax_tx.reason = GoldReason::kMarketTaxFee;
	tax_tx.delta = static_cast<std::int32_t>(tax);
	tax_tx.applied = static_cast<std::int32_t>(tax);
	tax_tx.correlation = listing_id;
	tax_tx.disposition = GoldDisposition::kApplied;
	s.onGoldTx(tax_tx);

	if (listing.asset_type == MarketAssetType::kItem && listing.item_data.has_value())
	{
		const int s_idx = giveItemToPlayer(buyer, *listing.item_data);
		if (s_idx < 0)
		{
			s.deliverSystemMail(bp->name.c_str(), "购买商品投递",
			                    "因系统原因商品未直接入包，已安全转存附件。",
			                    listing.item_data);
		}
		else
		{
			auto ih = bp->items[static_cast<std::size_t>(s_idx)];
			if (auto *item_obj = s.items.resolve(ih); item_obj != nullptr)
			{
				item_obj->owner = s.player_of_session.find(buyer);
			}
		}
	}
	else if (listing.asset_type == MarketAssetType::kPet && listing.pet_data.has_value())
	{
		const int s_idx = givePetToPlayer(buyer, *listing.pet_data);
		if (s_idx < 0)
		{
			s.deliverSystemMail(bp->name.c_str(), "购买宠物投递",
			                    "因系统原因宠物未直接入栏，已安全转存附件。",
			                    std::nullopt, listing.pet_data);
		}
	}

	SA::Model::Player *sp = nullptr;
	if (listing.seller_session != 0)
	{
		auto cit = s.conns.find(listing.seller_session);
		if (cit != s.conns.end() && !cit->second.detached)
		{
			sp = s.players.resolve(s.player_of_session.find(listing.seller_session));
		}
	}

	if (sp != nullptr)
	{
		auto tx = addGold(*sp, GoldReason::kMarketSellEarn, static_cast<std::int32_t>(net_revenue), 0, listing_id, s);
		if (tx.disposition == GoldDisposition::kClamped && tx.overflow > 0)
		{
			s.deliverSystemMail(listing.seller_name, "拍卖收入超额补发",
			                    "您的拍卖品已售出，但随身石币达到上限，溢出部分转存附件。",
			                    std::nullopt, std::nullopt, static_cast<std::uint32_t>(tx.overflow));
		}
	}
	else
	{
		s.deliverSystemMail(listing.seller_name, "拍卖行成交到账",
		                    "您的拍卖品已成功售出，扣除 5% 手续费后的净收益已存入附件。",
		                    std::nullopt, std::nullopt, net_revenue);
	}

	listing.sold = true;
	return true;
}

std::optional<MarketListing> World::getMarketListing(std::uint64_t listing_id) const
{
	auto it = _impl->market_listings.find(listing_id);
	if (it != _impl->market_listings.end())
		return it->second;
	return std::nullopt;
}

std::vector<MarketListing> World::searchMarket(const std::string &keyword,
                                               std::optional<MarketAssetType> type_filter) const
{
	std::vector<MarketListing> results;
	for (const auto &kv : _impl->market_listings)
	{
		const auto &listing = kv.second;
		if (listing.sold || listing.cancelled)
			continue;
		if (type_filter.has_value() && listing.asset_type != *type_filter)
			continue;
		if (!keyword.empty())
		{
			if (listing.asset_name.find(keyword) == std::string::npos)
				continue;
		}
		results.push_back(listing);
	}
	return results;
}

std::vector<MarketListing> World::playerMarketListings(SA::Net::SessionId seller) const
{
	std::vector<MarketListing> results;
	const auto *p = _impl->players.resolve(_impl->player_of_session.find(seller));
	const std::string seller_name = p ? p->name.c_str() : "";

	for (const auto &kv : _impl->market_listings)
	{
		const auto &listing = kv.second;
		if (listing.seller_session == seller || (!seller_name.empty() && listing.seller_name == seller_name))
		{
			results.push_back(listing);
		}
	}
	return results;
}

std::size_t World::activeMarketListingCount() const
{
	std::size_t count = 0;
	for (const auto &kv : _impl->market_listings)
	{
		if (!kv.second.sold && !kv.second.cancelled)
			++count;
	}
	return count;
}

} // namespace SA::World
