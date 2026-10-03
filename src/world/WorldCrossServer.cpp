// src/world/WorldCrossServer.cpp —— 跨线路社交体系与全服家族战系统实现 (阶段 8)
//
// 依据 00 §3.1 / §4.3 / §7 (D8 跨线路聊天室 506 行 & 全服家族庄园战)
// 约束: 本实现单元严格遵循单一暴露头 include/world/Api.h

#include "WorldImpl.h"
#include "data/Json.h"
#include "saac/Api.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace SA::World
{

inline constexpr int kCrossChannelShout = 100;
inline constexpr int kCrossChannelTell = 101;
inline constexpr int kCrossChannelRoomChat = 102;
inline constexpr int kCrossChannelRoomSync = 103;
inline constexpr int kCrossChannelFriendPresence = 104;
inline constexpr int kCrossChannelManorSync = 105;
inline constexpr int kCrossChannelSystemAnnounce = 106;

// ── SaacClient 连接与事件监听挂接 ─────────────────────────────────────
void World::setSaacClient(std::shared_ptr<SA::Saac::ISaacClient> client)
{
	_impl->saac_client = std::move(client);
	if (_impl->saac_client)
	{
		_impl->saac_client->registerBroadcastListener([impl = _impl.get()](const SA::Saac::WorldBroadcastMessage &msg)
		                                              { impl->handleCrossServerMessage(msg); });
	}
}

std::shared_ptr<SA::Saac::ISaacClient> World::saacClient() const noexcept
{
	return _impl->saac_client;
}

void World::Impl::broadcastCrossServer(int channel, const std::string &sender, const std::string &text)
{
	if (saac_client)
	{
		saac_client->broadcastWorldMessage(channel, sender, text);
	}
}

void World::Impl::handleCrossServerMessage(const SA::Saac::WorldBroadcastMessage &msg)
{
	switch (msg.channel)
	{
	case kCrossChannelShout:
	{
		ChatMessage chat{};
		chat.channel = ChatChannel::kTalkShout;
		chat.sender = 0;
		chat.sender_name = msg.sender;
		chat.text = msg.text;
		chat.color = 0;
		chat.timestamp_ms = msg.timestamp_ms > 0 ? msg.timestamp_ms : now_ms;
		for (const auto &kv : conns)
		{
			incoming_chats[kv.first].push_back(chat);
		}
		break;
	}
	case kCrossChannelTell:
	{
		auto parsed = SA::Data::Json::parse(msg.text);
		if (!parsed.ok || !parsed.value.isObject())
			break;
		const auto *target_v = parsed.value.find("target");
		const auto *sender_v = parsed.value.find("sender");
		const auto *text_v = parsed.value.find("text");
		const auto *color_v = parsed.value.find("color");
		if (!target_v || !sender_v || !text_v)
			break;

		const std::string target_name = target_v->asString();
		const std::string sender_name = sender_v->asString();
		const std::string chat_text = text_v->asString();
		const std::uint32_t color = color_v ? static_cast<std::uint32_t>(color_v->asNumber()) : 0;

		SA::Net::SessionId target_sid = 0;
		for (const auto &kv : conns)
		{
			const auto *p = players.resolve(player_of_session.find(kv.first));
			if (p && p->name.c_str() == target_name)
			{
				target_sid = kv.first;
				break;
			}
		}
		if (target_sid != 0)
		{
			auto it = address_books.find(target_sid);
			bool blocked = false;
			if (it != address_books.end())
			{
				for (const auto &card : it->second)
				{
					if (card.charname == sender_name && card.blocked)
					{
						blocked = true;
						break;
					}
				}
			}
			if (!blocked)
			{
				ChatMessage chat{};
				chat.channel = ChatChannel::kTalkTell;
				chat.sender = 0;
				chat.sender_name = sender_name;
				chat.target_name = target_name;
				chat.text = chat_text;
				chat.color = color;
				chat.timestamp_ms = msg.timestamp_ms > 0 ? msg.timestamp_ms : now_ms;
				incoming_chats[target_sid].push_back(std::move(chat));
			}
		}
		break;
	}
	case kCrossChannelRoomChat:
	{
		auto parsed = SA::Data::Json::parse(msg.text);
		if (!parsed.ok || !parsed.value.isObject())
			break;
		const auto *rid_v = parsed.value.find("room_id");
		const auto *sender_v = parsed.value.find("sender");
		const auto *text_v = parsed.value.find("text");
		const auto *color_v = parsed.value.find("color");
		if (!rid_v || !sender_v || !text_v)
			break;

		const std::uint32_t room_id = static_cast<std::uint32_t>(rid_v->asNumber());
		const std::string sender_name = sender_v->asString();
		const std::string text = text_v->asString();
		const std::uint32_t color = color_v ? static_cast<std::uint32_t>(color_v->asNumber()) : 0;

		auto rit = chat_rooms.find(room_id);
		if (rit != chat_rooms.end())
		{
			ChatMessage chat{};
			chat.channel = ChatChannel::kTalkRoom;
			chat.sender = 0;
			chat.sender_name = sender_name;
			chat.text = text;
			chat.color = color;
			chat.timestamp_ms = msg.timestamp_ms > 0 ? msg.timestamp_ms : now_ms;

			for (auto sid : rit->second.local_members)
			{
				incoming_chats[sid].push_back(chat);
			}
		}
		break;
	}
	case kCrossChannelRoomSync:
	{
		auto parsed = SA::Data::Json::parse(msg.text);
		if (!parsed.ok || !parsed.value.isObject())
			break;
		const auto *op_v = parsed.value.find("op");
		const auto *rid_v = parsed.value.find("room_id");
		if (!op_v || !rid_v)
			break;
		const std::string op = op_v->asString();
		const std::uint32_t room_id = static_cast<std::uint32_t>(rid_v->asNumber());

		if (op == "create")
		{
			const auto *name_v = parsed.value.find("room_name");
			const auto *creator_v = parsed.value.find("creator");
			const auto *pw_v = parsed.value.find("password");
			const auto *max_v = parsed.value.find("max_users");
			if (name_v && creator_v)
			{
				InternalChatRoom room{};
				room.room_id = room_id;
				room.room_name = name_v->asString();
				room.creator_name = creator_v->asString();
				room.password = pw_v ? pw_v->asString() : "";
				room.max_users = max_v ? static_cast<std::uint32_t>(max_v->asNumber()) : 20;
				room.all_member_names.insert(room.creator_name);
				chat_rooms[room_id] = std::move(room);
				if (room_id >= next_chat_room_id)
				{
					next_chat_room_id = room_id + 1;
				}
			}
		}
		else if (op == "join")
		{
			const auto *char_v = parsed.value.find("charname");
			if (char_v)
			{
				auto rit = chat_rooms.find(room_id);
				if (rit != chat_rooms.end())
				{
					rit->second.all_member_names.insert(char_v->asString());
				}
			}
		}
		else if (op == "leave")
		{
			const auto *char_v = parsed.value.find("charname");
			if (char_v)
			{
				auto rit = chat_rooms.find(room_id);
				if (rit != chat_rooms.end())
				{
					rit->second.all_member_names.erase(char_v->asString());
					if (rit->second.all_member_names.empty())
					{
						chat_rooms.erase(rit);
					}
				}
			}
		}
		break;
	}
	case kCrossChannelFriendPresence:
	{
		auto parsed = SA::Data::Json::parse(msg.text);
		if (!parsed.ok || !parsed.value.isObject())
			break;
		const auto *name_v = parsed.value.find("charname");
		const auto *online_v = parsed.value.find("online");
		if (!name_v || !online_v)
			break;
		const std::string charname = name_v->asString();
		const bool online = online_v->asBool();
		const auto *lvl_v = parsed.value.find("level");
		const auto *img_v = parsed.value.find("image");
		const int level = lvl_v ? static_cast<int>(lvl_v->asNumber()) : 1;
		const int img = img_v ? static_cast<int>(img_v->asNumber()) : 0;

		for (auto &kv : address_books)
		{
			for (auto &card : kv.second)
			{
				if (card.charname == charname)
				{
					card.online = online;
					if (online)
					{
						card.level = level;
						card.graphicsno = img;
					}
				}
			}
		}
		break;
	}
	case kCrossChannelManorSync:
	{
		auto parsed = SA::Data::Json::parse(msg.text);
		if (!parsed.ok || !parsed.value.isObject())
			break;
		const auto *op_v = parsed.value.find("op");
		const auto *manor_v = parsed.value.find("manor");
		if (!op_v || !manor_v)
			break;
		const std::string op = op_v->asString();
		const auto manor = static_cast<FamilyManor>(static_cast<std::uint8_t>(manor_v->asNumber()));

		if (op == "schedule")
		{
			const auto *c_fid = parsed.value.find("challenger_fid");
			const auto *d_fid = parsed.value.find("defender_fid");
			const auto *dep = parsed.value.find("deposit");
			const auto *sched = parsed.value.find("scheduled_ms");
			if (c_fid && d_fid && dep)
			{
				auto &war = manor_wars[manor];
				war.manor = manor;
				war.state = ManorWarState::kScheduled;
				war.challenger_family_id = static_cast<std::uint32_t>(c_fid->asNumber());
				war.defender_family_id = static_cast<std::uint32_t>(d_fid->asNumber());
				war.challenge_deposit = static_cast<std::uint32_t>(dep->asNumber());
				war.defender_score = 0;
				war.challenger_score = 0;
				war.scheduled_time_ms = sched ? static_cast<std::int64_t>(sched->asNumber()) : now_ms;
				war.war_end_time_ms = 0;
			}
		}
		else if (op == "score")
		{
			const auto *win_fid = parsed.value.find("winning_fid");
			const auto *pts = parsed.value.find("points");
			if (win_fid && pts)
			{
				const auto w_id = static_cast<std::uint32_t>(win_fid->asNumber());
				const auto p = static_cast<std::uint32_t>(pts->asNumber());
				auto &war = manor_wars[manor];
				if (war.state == ManorWarState::kInWar)
				{
					if (w_id == war.defender_family_id)
						war.defender_score += p;
					else if (w_id == war.challenger_family_id)
						war.challenger_score += p;
				}
			}
		}
		else if (op == "conclude")
		{
			const auto *vic_fid = parsed.value.find("victor_fid");
			if (vic_fid)
			{
				const auto victor_id = static_cast<std::uint32_t>(vic_fid->asNumber());
				auto &war = manor_wars[manor];
				war.state = ManorWarState::kCooldown;
				war.war_end_time_ms = now_ms + kManorWarCooldownMs;

				manor_owners[manor] = victor_id;
				auto fit = families.find(victor_id);
				if (fit != families.end())
				{
					fit->second.manor = manor;
				}
			}
		}
		else if (op == "occupy")
		{
			const auto *owner_fid = parsed.value.find("owner_fid");
			if (owner_fid)
			{
				const auto o_id = static_cast<std::uint32_t>(owner_fid->asNumber());
				manor_owners[manor] = o_id;
				auto fit = families.find(o_id);
				if (fit != families.end())
				{
					fit->second.manor = manor;
				}
			}
		}
		break;
	}
	case kCrossChannelSystemAnnounce:
	{
		auto parsed = SA::Data::Json::parse(msg.text);
		if (!parsed.ok || !parsed.value.isObject())
			break;
		const auto *text_v = parsed.value.find("text");
		const auto *color_v = parsed.value.find("color");
		if (!text_v)
			break;
		ChatMessage chat{};
		chat.channel = ChatChannel::kTalkSystem;
		chat.sender = 0;
		chat.sender_name = "系统公告";
		chat.text = text_v->asString();
		chat.color = color_v ? static_cast<std::uint32_t>(color_v->asNumber()) : 0xFFFF00;
		chat.timestamp_ms = msg.timestamp_ms > 0 ? msg.timestamp_ms : now_ms;
		for (const auto &kv : conns)
		{
			incoming_chats[kv.first].push_back(chat);
		}
		break;
	}
	}
}

// ── 跨线路独立聊天室接口实现 ──────────────────────────────────────────
std::uint32_t World::createChatRoom(SA::Net::SessionId session, const std::string &room_name,
                                    const std::string &password, std::uint32_t max_users)
{
	Impl &s = *_impl;
	if (room_name.empty() || room_name.size() > 32)
		return 0;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr || p->hp <= 0)
		return 0;

	const auto current_rid = playerChatRoom(session);
	if (current_rid != 0)
	{
		leaveChatRoom(session, current_rid);
	}

	const std::uint32_t rid = s.next_chat_room_id++;
	const std::uint32_t capacity = std::clamp<std::uint32_t>(max_users, 2, 50);

	Impl::InternalChatRoom room{};
	room.room_id = rid;
	room.room_name = room_name;
	room.creator_name = p->name.c_str();
	room.password = password;
	room.max_users = capacity;
	room.local_members.insert(session);
	room.all_member_names.insert(room.creator_name);

	s.chat_rooms[rid] = room;
	s.session_to_chat_room[session] = rid;

	if (s.saac_client)
	{
		SA::Data::Json::Object obj;
		obj["op"] = SA::Data::Json::Value::str("create");
		obj["room_id"] = SA::Data::Json::Value::number(rid);
		obj["room_name"] = SA::Data::Json::Value::str(room_name);
		obj["creator"] = SA::Data::Json::Value::str(room.creator_name);
		obj["password"] = SA::Data::Json::Value::str(password);
		obj["max_users"] = SA::Data::Json::Value::number(capacity);
		s.broadcastCrossServer(kCrossChannelRoomSync, p->name.c_str(), SA::Data::Json::stringify(SA::Data::Json::Value::obj(std::move(obj))));
	}

	return rid;
}

bool World::joinChatRoom(SA::Net::SessionId session, std::uint32_t room_id, const std::string &password)
{
	Impl &s = *_impl;
	auto it = s.chat_rooms.find(room_id);
	if (it == s.chat_rooms.end())
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr || p->hp <= 0)
		return false;

	if (!it->second.password.empty() && it->second.password != password)
		return false;

	if (it->second.all_member_names.size() >= it->second.max_users)
		return false;

	const auto current_rid = playerChatRoom(session);
	if (current_rid != 0)
	{
		if (current_rid == room_id)
			return true;
		leaveChatRoom(session, current_rid);
	}

	it->second.local_members.insert(session);
	it->second.all_member_names.insert(p->name.c_str());
	s.session_to_chat_room[session] = room_id;

	if (s.saac_client)
	{
		SA::Data::Json::Object obj;
		obj["op"] = SA::Data::Json::Value::str("join");
		obj["room_id"] = SA::Data::Json::Value::number(room_id);
		obj["charname"] = SA::Data::Json::Value::str(p->name.c_str());
		s.broadcastCrossServer(kCrossChannelRoomSync, p->name.c_str(), SA::Data::Json::stringify(SA::Data::Json::Value::obj(std::move(obj))));
	}

	return true;
}

bool World::leaveChatRoom(SA::Net::SessionId session, std::uint32_t room_id)
{
	Impl &s = *_impl;
	auto it = s.chat_rooms.find(room_id);
	if (it == s.chat_rooms.end())
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	const std::string name = p ? p->name.c_str() : "";

	it->second.local_members.erase(session);
	if (!name.empty())
	{
		it->second.all_member_names.erase(name);
	}
	s.session_to_chat_room.erase(session);

	if (s.saac_client && !name.empty())
	{
		SA::Data::Json::Object obj;
		obj["op"] = SA::Data::Json::Value::str("leave");
		obj["room_id"] = SA::Data::Json::Value::number(room_id);
		obj["charname"] = SA::Data::Json::Value::str(name);
		s.broadcastCrossServer(kCrossChannelRoomSync, name, SA::Data::Json::stringify(SA::Data::Json::Value::obj(std::move(obj))));
	}

	if (it->second.all_member_names.empty())
	{
		s.chat_rooms.erase(it);
	}

	return true;
}

std::uint32_t World::playerChatRoom(SA::Net::SessionId session) const
{
	auto it = _impl->session_to_chat_room.find(session);
	if (it == _impl->session_to_chat_room.end())
		return 0;
	return it->second;
}

std::vector<ChatRoomInfo> World::listChatRooms() const
{
	std::vector<ChatRoomInfo> res;
	res.reserve(_impl->chat_rooms.size());
	for (const auto &kv : _impl->chat_rooms)
	{
		const auto &r = kv.second;
		ChatRoomInfo info{};
		info.room_id = r.room_id;
		info.room_name = r.room_name;
		info.creator_name = r.creator_name;
		info.has_password = !r.password.empty();
		info.max_users = r.max_users;
		info.current_users = static_cast<std::uint32_t>(r.all_member_names.size());
		info.member_names.assign(r.all_member_names.begin(), r.all_member_names.end());
		res.push_back(std::move(info));
	}
	std::sort(res.begin(), res.end(), [](const ChatRoomInfo &a, const ChatRoomInfo &b)
	          { return a.room_id < b.room_id; });
	return res;
}

bool World::sendChatRoomMessage(SA::Net::SessionId session, const std::string &text, std::uint32_t color)
{
	Impl &s = *_impl;
	if (text.empty() || text.size() > 256)
		return false;

	const auto rid = playerChatRoom(session);
	if (rid == 0)
		return false;

	auto it = s.chat_rooms.find(rid);
	if (it == s.chat_rooms.end())
		return false;

	SA::Model::Player *p = s.players.resolve(s.player_of_session.find(session));
	if (p == nullptr)
		return false;

	ChatMessage chat{};
	chat.channel = ChatChannel::kTalkRoom;
	chat.sender = session;
	chat.sender_name = p->name.c_str();
	chat.text = text;
	chat.color = color;
	chat.timestamp_ms = s.now_ms;

	for (auto sid : it->second.local_members)
	{
		s.incoming_chats[sid].push_back(chat);
	}

	if (s.saac_client)
	{
		SA::Data::Json::Object obj;
		obj["room_id"] = SA::Data::Json::Value::number(rid);
		obj["sender"] = SA::Data::Json::Value::str(p->name.c_str());
		obj["text"] = SA::Data::Json::Value::str(text);
		obj["color"] = SA::Data::Json::Value::number(color);
		s.broadcastCrossServer(kCrossChannelRoomChat, p->name.c_str(), SA::Data::Json::stringify(SA::Data::Json::Value::obj(std::move(obj))));
	}

	return true;
}

// ── 全服系统公告接口实现 ──────────────────────────────────────────────
bool World::broadcastSystemAnnouncement(const std::string &announcement_text, std::uint32_t color)
{
	Impl &s = *_impl;
	if (announcement_text.empty() || announcement_text.size() > 512)
		return false;

	ChatMessage chat{};
	chat.channel = ChatChannel::kTalkSystem;
	chat.sender = 0;
	chat.sender_name = "系统公告";
	chat.text = announcement_text;
	chat.color = color;
	chat.timestamp_ms = s.now_ms;

	for (const auto &kv : s.conns)
	{
		s.incoming_chats[kv.first].push_back(chat);
	}

	if (s.saac_client)
	{
		SA::Data::Json::Object obj;
		obj["text"] = SA::Data::Json::Value::str(announcement_text);
		obj["color"] = SA::Data::Json::Value::number(color);
		s.broadcastCrossServer(kCrossChannelSystemAnnounce, "系统公告", SA::Data::Json::stringify(SA::Data::Json::Value::obj(std::move(obj))));
	}

	return true;
}

// ── 跨线路全服庄园战同步驱动实现 ──────────────────────────────────────
bool World::syncManorStateToCrossServer(FamilyManor manor)
{
	Impl &s = *_impl;
	if (!s.saac_client)
		return false;

	const auto owner_id = manorOwnerFamily(manor);
	SA::Data::Json::Object obj;
	obj["op"] = SA::Data::Json::Value::str("occupy");
	obj["manor"] = SA::Data::Json::Value::number(static_cast<std::uint8_t>(manor));
	obj["owner_fid"] = SA::Data::Json::Value::number(owner_id);
	s.broadcastCrossServer(kCrossChannelManorSync, "ManorSync", SA::Data::Json::stringify(SA::Data::Json::Value::obj(std::move(obj))));
	return true;
}

bool World::syncManorWarScheduleToCrossServer(FamilyManor manor)
{
	Impl &s = *_impl;
	if (!s.saac_client)
		return false;

	auto it = s.manor_wars.find(manor);
	if (it == s.manor_wars.end())
		return false;

	SA::Data::Json::Object obj;
	obj["op"] = SA::Data::Json::Value::str("schedule");
	obj["manor"] = SA::Data::Json::Value::number(static_cast<std::uint8_t>(manor));
	obj["challenger_fid"] = SA::Data::Json::Value::number(it->second.challenger_family_id);
	obj["defender_fid"] = SA::Data::Json::Value::number(it->second.defender_family_id);
	obj["deposit"] = SA::Data::Json::Value::number(it->second.challenge_deposit);
	obj["scheduled_ms"] = SA::Data::Json::Value::number(static_cast<double>(it->second.scheduled_time_ms));
	s.broadcastCrossServer(kCrossChannelManorSync, "ManorSync", SA::Data::Json::stringify(SA::Data::Json::Value::obj(std::move(obj))));
	return true;
}

bool World::syncManorDuelScoreToCrossServer(FamilyManor manor, std::uint32_t winning_family_id, std::uint32_t score_points)
{
	Impl &s = *_impl;
	if (!s.saac_client)
		return false;

	SA::Data::Json::Object obj;
	obj["op"] = SA::Data::Json::Value::str("score");
	obj["manor"] = SA::Data::Json::Value::number(static_cast<std::uint8_t>(manor));
	obj["winning_fid"] = SA::Data::Json::Value::number(winning_family_id);
	obj["points"] = SA::Data::Json::Value::number(score_points);
	s.broadcastCrossServer(kCrossChannelManorSync, "ManorSync", SA::Data::Json::stringify(SA::Data::Json::Value::obj(std::move(obj))));
	return true;
}

bool World::syncManorWarConclusionToCrossServer(FamilyManor manor, std::uint32_t victorious_family_id)
{
	Impl &s = *_impl;
	if (!s.saac_client)
		return false;

	SA::Data::Json::Object obj;
	obj["op"] = SA::Data::Json::Value::str("conclude");
	obj["manor"] = SA::Data::Json::Value::number(static_cast<std::uint8_t>(manor));
	obj["victor_fid"] = SA::Data::Json::Value::number(victorious_family_id);
	s.broadcastCrossServer(kCrossChannelManorSync, "ManorSync", SA::Data::Json::stringify(SA::Data::Json::Value::obj(std::move(obj))));
	return true;
}

} // namespace SA::World
