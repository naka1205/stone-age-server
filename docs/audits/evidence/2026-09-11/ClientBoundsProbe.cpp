#include "BattlePresenter.h"
#include <memory>
int main()
{
	auto presenter = std::make_unique<SA::Client::BattlePresenter>();
	SA::Domain::BattleSelfInfo self{};
	presenter->reset(self);
	SA::Domain::BattleEvents events{};
	auto *e = events.events.push_back();
	e->body_kind = SA::Domain::BattleEvent::BodyKind::DAMAGE;
	e->body.damage.target = 20;
	e->body.damage.hp_delta = -1;
	presenter->consume(events);
}
