// Client contract/presentation probes. Nonzero exit reports a known audit finding.
#include "BattlePresenter.h"
#include "net/ClientApi.h"
#include <cstdio>
#include <vector>
int failures = 0;
void expect(const char *id, long long actual, long long expected)
{
	std::printf("%s %s observed=%lld expected=%lld\n", actual == expected ? "PASS" : "FAIL", id, actual, expected);
	failures += actual != expected;
}
int main()
{
	SA::Net::ClientSession session(1, 1, nullptr);
	std::vector<std::uint8_t> bytes;
	session.beginHandshake(bytes);
	bytes.clear();
	SA::Transport::HandshakeAccepted accepted{};
	accepted.session_id = 1;
	SA::Wire::encodeFramed(1, accepted, bytes);
	expect("CONTROL.handshake", session.feed(bytes.data(), bytes.size()), 1);
	bytes.clear();
	SA::Domain::BattleEvents events{};
	events.battle_id = 1;
	events.turn = 1;
	SA::Wire::encodeFramed(0, events, bytes);
	expect("CONTROL.battle_events", session.feed(bytes.data(), bytes.size()), 1);
	bytes.clear();
	SA::Domain::BattleResult result{};
	result.battle_id = 1;
	result.player_won = true;
	SA::Wire::encodeFramed(0, result, bytes);
	expect("F11.accept_battle_result", session.feed(bytes.data(), bytes.size()), 1);
	std::printf("INFO rejected_msg_id=0x%04x\n", session.last_reject_msg_id());
	SA::Client::BattlePresenter presenter;
	SA::Domain::BattleSelfInfo self{};
	self.battle_id = 1;
	self.slot = 0;
	presenter.reset(self);
	auto *e = events.events.push_back();
	e->body_kind = SA::Domain::BattleEvent::BodyKind::DAMAGE;
	e->body.damage.target = 10;
	e->body.damage.hp_delta = -10;
	e->body.damage.flags = static_cast<unsigned>(SA::Domain::DamageFlag::DAMAGE_FLAG_NORMAL);
	const auto view = presenter.consume(events);
	expect("CONTROL.target_has_view", view.units.size(), 1);
	// Ground truth for this scenario: target started at 100 HP and took 10 damage.
	// Current protocol supplies no initial HP; the client must not invent a dead unit.
	if (!view.units.empty())
	{
		expect("F12.display_hp_matches_authority", view.units[0].hp, 90);
		expect("F12.nonlethal_hit_keeps_unit_alive", view.units[0].alive, 1);
	}
	std::printf("client_expectations_failed=%d\n", failures);
	return failures ? 1 : 0;
}
