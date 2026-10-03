// src/world/WorldSocial.cpp —— 名片夹、邮件与聊天频道系统实现
//
// 对应原版 addressbook.c, petmail.c / mail.c, char_talk.c

#include "WorldImpl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace SA::World
{

// ══ 名片夹与好友系统 (AddressBook System, 对齐 addressbook.c) ═══════════
bool World::requestAddressCard(SA::Net::SessionId requester, SA::Net::SessionId target)
{
	Impl &s = *_impl;
	if (requester == target)
		return false;

	SA::Model::Player *req_p = s.players.resolve(s.player_of_session.find(requester));
	SA::Model::Player *tar_p = s.players.resolve(s.player_of_session.find(target));
	if (req_p == nullptr || tar_p == nullptr)
		return false;

	if (req_p->hp <= 0 || tar_p->hp <= 0)
		return false;

	if (s.inBattle(requester) || s.inBattle(target))
		return false;

	if (req_p->floor != tar_p->floor)
		return false;

	const int dx = std::abs(req_p->x - tar_p->x);
	const int dy = std::abs(req_p->y - tar_p->y);
	if (std::max(dx, dy) > 2)
		return false;

	auto &req_cards = s.address_books[requester];
	auto &tar_cards = s.address_books[target];
	if (req_cards.size() >= kMaxAddressBook || tar_cards.size() >= kMaxAddressBook)
		return false;

	const std::string req_name = req_p->name.c_str();
	const std::string tar_name = tar_p->name.c_str();

	for (const auto &card : req_cards)
		if (card.charname == tar_name)
			return false;
	for (const auto &card : tar_cards)
		if (card.charname == req_name)
			return false;

	// 若对方已将发起者拉黑，则不可发起换名片请求
	for (const auto &card : tar_cards)
		if (card.charname == req_name && card.blocked)
			return false;

	s.pending_card_requests[requester] = target;
	return true;
}

bool World::acceptAddressCard(SA::Net::SessionId acceptor, SA::Net::SessionId requester)
{
	Impl &s = *_impl;
	auto it = s.pending_card_requests.find(requester);
	if (it == s.pending_card_requests.end() || it->second != acceptor)
		return false;

	SA::Model::Player *req_p = s.players.resolve(s.player_of_session.find(requester));
	SA::Model::Player *acc_p = s.players.resolve(s.player_of_session.find(acceptor));
	if (req_p == nullptr || acc_p == nullptr)
		return false;

	if (req_p->hp <= 0 || acc_p->hp <= 0)
		return false;

	if (s.inBattle(requester) || s.inBattle(acceptor))
		return false;

	if (req_p->floor != acc_p->floor)
		return false;

	const int dx = std::abs(req_p->x - acc_p->x);
	const int dy = std::abs(req_p->y - acc_p->y);
	if (std::max(dx, dy) > 2)
		return false;

	auto &req_cards = s.address_books[requester];
	auto &acc_cards = s.address_books[acceptor];
	if (req_cards.size() >= kMaxAddressBook || acc_cards.size() >= kMaxAddressBook)
		return false;

	// 双方原子互存名片
	AddressBookEntry to_acc{};
	to_acc.use = true;
	to_acc.session = requester;
	to_acc.charname = req_p->name.c_str();
	to_acc.level = req_p->level;
	to_acc.duelpoint = 0;
	to_acc.graphicsno = req_p->image;
	to_acc.transmigration = 0;
	to_acc.online = true;
	to_acc.blocked = false;
	acc_cards.push_back(to_acc);

	AddressBookEntry to_req{};
	to_req.use = true;
	to_req.session = acceptor;
	to_req.charname = acc_p->name.c_str();
	to_req.level = acc_p->level;
	to_req.duelpoint = 0;
	to_req.graphicsno = acc_p->image;
	to_req.transmigration = 0;
	to_req.online = true;
	to_req.blocked = false;
	req_cards.push_back(to_req);

	s.pending_card_requests.erase(it);
	return true;
}

bool World::removeAddressCard(SA::Net::SessionId session, std::size_t index)
{
	Impl &s = *_impl;
	auto it = s.address_books.find(session);
	if (it == s.address_books.end() || index >= it->second.size())
		return false;
	it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));
	return true;
}

bool World::setAddressCardBlock(SA::Net::SessionId session, std::size_t index, bool blocked)
{
	Impl &s = *_impl;
	auto it = s.address_books.find(session);
	if (it == s.address_books.end() || index >= it->second.size())
		return false;
	it->second[index].blocked = blocked;
	return true;
}

std::vector<AddressBookEntry> World::playerAddressBook(SA::Net::SessionId session) const
{
	auto it = _impl->address_books.find(session);
	if (it == _impl->address_books.end())
		return {};
	return it->second;
}

std::size_t World::playerAddressBookCount(SA::Net::SessionId session) const
{
	auto it = _impl->address_books.find(session);
	if (it == _impl->address_books.end())
		return 0;
	return it->second.size();
}

bool World::isAddressCardBlocked(SA::Net::SessionId session, const std::string &charname) const
{
	auto it = _impl->address_books.find(session);
	if (it == _impl->address_books.end())
		return false;
	for (const auto &card : it->second)
	{
		if (card.charname == charname && card.blocked)
			return true;
	}
	return false;
}

// ══ 邮件与离线信件系统 (Mail System, 对齐 petmail.c / mail.c) ═════════
bool World::sendMail(SA::Net::SessionId sender, const std::string &receiver_name,
                     const std::string &title, const std::string &message,
                     int item_slot, int pet_slot, std::uint32_t gold)
{
	Impl &s = *_impl;
	if (title.empty() || title.size() > 64)
		return false;
	if (message.size() > 500)
		return false;
	if (receiver_name.empty())
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(sender));
	if (p == nullptr)
		return false;
	if (p->hp <= 0 || s.inBattle(sender))
		return false;

	auto &mbox = s.mailboxes[receiver_name];
	if (mbox.size() >= kMaxMailBoxSize)
		return false;

	// 1. 石币附件预检
	if (gold > 0)
	{
		if (static_cast<std::uint32_t>(std::max(0, p->gold)) < gold)
			return false;
	}

	// 2. 道具附件预检
	std::optional<SA::Model::Item> attached_item{};
	if (item_slot >= 0)
	{
		if (item_slot < static_cast<int>(SA::Model::kStartItemArray) ||
		    static_cast<std::size_t>(item_slot) >= SA::Model::kMaxItemHave)
			return false;
		if (!p->items[static_cast<std::size_t>(item_slot)].valid())
			return false;
		const SA::Model::Item *it = s.items.resolve(p->items[static_cast<std::size_t>(item_slot)]);
		if (it == nullptr)
			return false;
		attached_item = *it;
	}

	// 3. 宠物附件预检
	std::optional<SA::Model::Pet> attached_pet{};
	if (pet_slot >= 0)
	{
		if (pet_slot < 0 || static_cast<std::size_t>(pet_slot) >= SA::Model::kMaxPetHave)
			return false;
		if (!p->pets[static_cast<std::size_t>(pet_slot)].valid())
			return false;
		const SA::Model::Pet *pt = s.pets.resolve(p->pets[static_cast<std::size_t>(pet_slot)]);
		if (pt == nullptr)
			return false;
		attached_pet = *pt;
	}

	const std::uint64_t mid = s.next_mail_id++;

	// 执行扣除/剥离 (石币走 GoldLedger 审计)
	if (gold > 0)
	{
		(void)delGold(*p, GoldReason::kMailSend, static_cast<std::int32_t>(gold), 0, mid, s);
	}
	if (item_slot >= 0)
	{
		(void)s.items.release(p->items[static_cast<std::size_t>(item_slot)]);
		p->clearItemSlot(item_slot);
	}
	if (pet_slot >= 0)
	{
		if (playerRidePetSlot(sender) == pet_slot)
		{
			dismountPet(sender);
		}
		if (p->default_pet == pet_slot)
			p->default_pet = -1;
		(void)s.pets.release(p->pets[static_cast<std::size_t>(pet_slot)]);
		p->clearPetSlot(pet_slot);
	}

	MailEntry mail{};
	mail.mail_id = mid;
	mail.sender_name = p->name.c_str();
	mail.receiver_name = receiver_name;
	mail.title = title;
	mail.message = message;
	mail.sent_time_ms = s.now_ms;
	mail.is_read = false;
	mail.has_attachment = (attached_item.has_value() || attached_pet.has_value() || gold > 0);
	mail.attached_item = std::move(attached_item);
	mail.attached_pet = std::move(attached_pet);
	mail.attached_gold = gold;

	mbox.push_back(std::move(mail));
	return true;
}

std::vector<MailEntry> World::playerMails(SA::Net::SessionId session) const
{
	const SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return {};
	auto it = _impl->mailboxes.find(p->name.c_str());
	if (it == _impl->mailboxes.end())
		return {};
	return it->second;
}

std::size_t World::playerMailCount(SA::Net::SessionId session) const
{
	const SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return 0;
	auto it = _impl->mailboxes.find(p->name.c_str());
	if (it == _impl->mailboxes.end())
		return 0;
	return it->second.size();
}

bool World::readMail(SA::Net::SessionId session, std::uint64_t mail_id)
{
	SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return false;
	auto it = _impl->mailboxes.find(p->name.c_str());
	if (it == _impl->mailboxes.end())
		return false;
	for (auto &m : it->second)
	{
		if (m.mail_id == mail_id)
		{
			m.is_read = true;
			return true;
		}
	}
	return false;
}

bool World::takeMailAttachment(SA::Net::SessionId session, std::uint64_t mail_id)
{
	Impl &s = *_impl;
	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;
	auto it = s.mailboxes.find(p->name.c_str());
	if (it == s.mailboxes.end())
		return false;

	MailEntry *target_mail = nullptr;
	for (auto &m : it->second)
	{
		if (m.mail_id == mail_id)
		{
			target_mail = &m;
			break;
		}
	}
	if (target_mail == nullptr || !target_mail->has_attachment)
		return false;

	// 1. 背包容量前置预检
	if (target_mail->attached_item.has_value())
	{
		if (p->findFreeItemSlot() < 0)
			return false;
	}

	// 2. 宠物栏容量前置预检
	if (target_mail->attached_pet.has_value())
	{
		if (p->findFreePetSlot() < 0)
			return false;
	}

	// 3. 石币上限前置预检
	if (target_mail->attached_gold > 0)
	{
		if (static_cast<std::int64_t>(p->gold) + target_mail->attached_gold > maxHaveGold(0))
			return false;
	}

	// 原子提取附件
	if (target_mail->attached_item.has_value())
	{
		const int s_idx = giveItemToPlayer(session, *target_mail->attached_item);
		if (s_idx < 0)
			return false;
		target_mail->attached_item.reset();
	}

	if (target_mail->attached_pet.has_value())
	{
		const int s_idx = givePetToPlayer(session, *target_mail->attached_pet);
		if (s_idx < 0)
			return false;
		target_mail->attached_pet.reset();
	}

	if (target_mail->attached_gold > 0)
	{
		(void)addGold(*p, GoldReason::kMailReceive, static_cast<std::int32_t>(target_mail->attached_gold),
		              0, mail_id, s);
		target_mail->attached_gold = 0;
	}

	target_mail->has_attachment = false;
	target_mail->is_read = true;
	return true;
}

bool World::deleteMail(SA::Net::SessionId session, std::uint64_t mail_id)
{
	SA::Model::Player *p = _impl->players.resolve(_impl->player_of_session.find(session));
	if (p == nullptr)
		return false;
	auto it = _impl->mailboxes.find(p->name.c_str());
	if (it == _impl->mailboxes.end())
		return false;

	for (auto vit = it->second.begin(); vit != it->second.end(); ++vit)
	{
		if (vit->mail_id == mail_id)
		{
			if (vit->has_attachment)
				return false;
			it->second.erase(vit);
			return true;
		}
	}
	return false;
}

// ══ 聊天频道与分级广播 (Chat & Channel System, 对齐 char_talk.c) ═══════
bool World::sendChat(SA::Net::SessionId sender, ChatChannel channel,
                     const std::string &text, const std::string &target_name,
                     std::uint32_t color)
{
	Impl &s = *_impl;
	if (text.empty() || text.size() > 256)
		return false;

	SA::Model::Player *sender_p = s.players.resolve(s.player_of_session.find(sender));
	if (sender_p == nullptr)
		return false;

	ChatMessage msg{};
	msg.channel = channel;
	msg.sender = sender;
	msg.sender_name = sender_p->name.c_str();
	msg.target_name = target_name;
	msg.text = text;
	msg.color = color;
	msg.timestamp_ms = s.now_ms;

	switch (channel)
	{
	case ChatChannel::kTalkNormal:
	{
		for (const auto &kv : s.conns)
		{
			const auto sid = kv.first;
			const auto *tar_p = s.players.resolve(s.player_of_session.find(sid));
			if (tar_p != nullptr && tar_p->floor == sender_p->floor)
			{
				const int dx = std::abs(sender_p->x - tar_p->x);
				const int dy = std::abs(sender_p->y - tar_p->y);
				if (std::max(dx, dy) <= 9)
				{
					s.incoming_chats[sid].push_back(msg);
				}
			}
		}
		return true;
	}
	case ChatChannel::kTalkParty:
	{
		if (playerPartyMode(sender) == PartyMode::kNone)
			return false;
		const auto members = playerPartyMembers(sender);
		for (auto sid : members)
		{
			s.incoming_chats[sid].push_back(msg);
		}
		return true;
	}
	case ChatChannel::kTalkShout:
	{
		for (const auto &kv : s.conns)
		{
			s.incoming_chats[kv.first].push_back(msg);
		}
		if (s.saac_client)
		{
			s.broadcastCrossServer(100, sender_p->name.c_str(), text);
		}
		return true;
	}
	case ChatChannel::kTalkTell:
	{
		if (target_name.empty())
			return false;
		SA::Net::SessionId target_sid = 0;
		for (const auto &kv : s.conns)
		{
			const auto *tar_p = s.players.resolve(s.player_of_session.find(kv.first));
			if (tar_p != nullptr && target_name == tar_p->name.c_str())
			{
				target_sid = kv.first;
				break;
			}
		}
		if (target_sid == sender)
			return false;

		if (target_sid != 0)
		{
			if (isAddressCardBlocked(target_sid, sender_p->name.c_str()))
				return false;

			s.incoming_chats[target_sid].push_back(msg);
			s.incoming_chats[sender].push_back(msg);
			return true;
		}

		if (s.saac_client)
		{
			SA::Data::Json::Object obj;
			obj["target"] = SA::Data::Json::Value::str(target_name);
			obj["sender"] = SA::Data::Json::Value::str(sender_p->name.c_str());
			obj["text"] = SA::Data::Json::Value::str(text);
			obj["color"] = SA::Data::Json::Value::number(color);
			s.broadcastCrossServer(101, sender_p->name.c_str(), SA::Data::Json::stringify(SA::Data::Json::Value::obj(std::move(obj))));

			s.incoming_chats[sender].push_back(msg);
			return true;
		}

		return false;
	}
	case ChatChannel::kTalkFamily:
	{
		const auto fid = playerFamilyId(sender);
		if (fid == 0)
			return false;
		auto fam_it = s.families.find(fid);
		if (fam_it == s.families.end())
			return false;
		for (const auto &m : fam_it->second.members)
		{
			if (m.online && m.session != 0)
			{
				s.incoming_chats[m.session].push_back(msg);
			}
		}
		return true;
	}
	case ChatChannel::kTalkRoom:
	{
		return sendChatRoomMessage(sender, text, color);
	}
	case ChatChannel::kTalkSystem:
	{
		return broadcastSystemAnnouncement(text, color);
	}
	}
	return false;
}

std::vector<ChatMessage> World::pollChatMessages(SA::Net::SessionId session)
{
	auto it = _impl->incoming_chats.find(session);
	if (it == _impl->incoming_chats.end())
		return {};
	std::vector<ChatMessage> result = std::move(it->second);
	it->second.clear();
	return result;
}

std::size_t World::pendingChatMessageCount(SA::Net::SessionId session) const
{
	auto it = _impl->incoming_chats.find(session);
	if (it == _impl->incoming_chats.end())
		return 0;
	return it->second.size();
}

void World::notifyAddressBookStatus(SA::Net::SessionId session, bool online)
{
	_impl->notifyAddressBookStatus(session, online);
}

} // namespace SA::World
