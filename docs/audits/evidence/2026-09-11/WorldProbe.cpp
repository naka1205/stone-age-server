// New-server integration probes only. Nonzero exit reports fidelity/lifecycle violations.
#include "model/Player.h"
#include "rules/Status.h"
#include "world/Api.h"
#include <cstdio>
#include <vector>

using namespace SA::World;
using Kind = SA::Domain::BattleCommand::CommandKind;
int failures = 0;
void expect(const char *id, long long actual, long long expected)
{
	std::printf("%s %s observed=%lld expected=%lld\n", actual == expected ? "PASS" : "FAIL", id, actual, expected);
	failures += actual != expected;
}
struct Fixture
{
	SA::Platform::ServerConfig config = SA::Platform::parseConfig(
	                                        R"({"protocol_version":1,"log_level":"error","tempo":{"tick_hz":100,"battle_turn_interval_ms":1000}})")
	                                        .config;
	SA::Platform::ManualClock clock{0};
	SA::Platform::Logger logger{SA::Platform::LogLevel::kError};
	SA::Platform::RandomSource random{0xABCDEF};
	SA::Net::LoopbackTransport transport{};
	World world{config, clock, logger, random, transport};
	SA::Net::ConnectionId connect()
	{
		auto id = transport.connect();
		SA::Transport::HandshakeRequest h{};
		h.protocol_version = 1;
		h.client_build.assign("audit");
		std::vector<std::uint8_t> bytes;
		SA::Wire::encodeFramed(1, h, bytes);
		transport.deliver(id, bytes.data(), bytes.size());
		world.tick();
		return id;
	}
	void step()
	{
		clock.advance(1000);
		world.tick();
	}
	void command(SA::Net::SessionId id, BattleId battle, Kind kind, int value = 0)
	{
		const auto *f = world.battleField(battle);
		if (!f)
		{
			std::puts("SETUP_ERROR missing battle");
			++failures;
			return;
		}
		SA::Domain::BattleCommand cmd{};
		cmd.battle_id = battle;
		cmd.turn = f->turn;
		cmd.command_kind = kind;
		if (kind == Kind::PET_OUT)
			cmd.command.pet_out.pet_slot = value;
		if (kind == Kind::CAPTURE)
			cmd.command.capture.target = value;
		world.onBattleCommand(id, cmd);
	}
	void walk(SA::Net::SessionId id)
	{
		auto p = world.playerPos(id);
		SA::Domain::WalkRequest req{};
		req.x = p.x;
		req.y = p.y;
		req.direction.assign("c");
		world.onWalk(id, req);
	}
};
SA::Rules::BattleField field()
{
	SA::Rules::BattleField f{};
	for (int slot : {0, 11})
	{
		auto &c = f.at(slot);
		c.occupied = true;
		c.slot = slot;
		c.kind = slot == 0 ? SA::Rules::CombatantKind::kPlayer : SA::Rules::CombatantKind::kEnemy;
		c.level = 20;
		c.hp = c.max_hp = 100000;
		c.attack = 1;
		c.defense = 500;
		c.quick = slot == 0 ? 500 : 10;
		c.charm = 200;
		c.luck = 10;
		c.mods.no_duck = true;
		c.mods.unarmed = false;
		c.mods.attack_num_min = c.mods.attack_num_max = 1;
		c.vital = c.str = c.tough = c.dex = 2500;
	}
	return f;
}
int main()
{
	{
		Fixture f;
		auto id = f.connect();
		auto bf = field();
		bf.at(0).hp = 50;
		bf.at(0).max_hp = 100;
		bf.at(0).status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_PARALYSIS);
		bf.at(0).status_turns = 3;
		auto battle = f.world.startBattle(bf);
		f.world.joinBattle(battle, id, 0);
		f.world.loadItemEffects({ItemEffect{1234, 50}});
		SA::Model::Item item{};
		item.item_id = 1234;
		item.current_pile = 1;
		int slot = f.world.giveItemToPlayer(id, item);
		SA::Domain::BattleCommand cmd{};
		cmd.battle_id = battle;
		cmd.turn = f.world.battleField(battle)->turn;
		cmd.command_kind = Kind::USE_ITEM;
		cmd.command.use_item.item_slot = slot;
		cmd.command.use_item.target = 0;
		f.world.onBattleCommand(id, cmd);
		f.step();
		expect("F07.paralyzed_actor_item_retained", f.world.playerItemSlotsUsed(id), 1);
		expect("CONTROL.paralysis_ticks", f.world.battleField(battle)->at(0).status_turns, 2);
	}
	{
		Fixture f;
		auto id = f.connect();
		SA::Model::Item item{};
		item.item_id = 1234;
		item.current_pile = 1;
		f.world.giveItemToPlayer(id, item);
		expect("CONTROL.item_allocated", f.world.itemCount(), 1);
		f.world.onDisconnected(id);
		expect("CONTROL.player_released", f.world.playerCount(), 0);
		expect("F09.disconnected_owner_item_released", f.world.itemCount(), 0);
	}
	{
		Fixture f;
		auto id = f.connect();
		auto battle = f.world.startBattle(field());
		f.world.joinBattle(battle, id, 0);
		auto before = f.world.playerPos(id);
		f.walk(id);
		f.world.tick();
		expect("F10.movement_during_battle", f.world.playerPos(id).x - before.x, 0);
	}
	{
		Fixture f;
		auto id = f.connect();
		auto start = f.world.playerPos(id);
		for (int i = 0; i < 5; ++i)
		{
			f.walk(id);
			f.clock.advance(10);
			f.world.tick();
		}
		std::printf("OBSERVATION walk_packets_50ms steps=%d (legacy comparison recorded separately)\n",
		            f.world.playerPos(id).x - start.x);
	}
	{
		Fixture f;
		auto id = f.connect();
		auto battle = f.world.startBattle(field());
		f.world.joinBattle(battle, id, 0);
		EnemyTemplate t{};
		t.stats = SA::Rules::SpawnTemplate{4.5, 10, 20, 12, 15, 25};
		t.capture_difficulty = 11;
		t.image = 100250;
		t.temp_no = 1;
		t.name.assign("audit pet");
		EnemyEncounter enc{};
		enc.enemy_id = 9;
		enc.temp_no = 1;
		enc.lv_min = enc.lv_max = 1;
		enc.capturable = true;
		if (!f.world.spawnEnemyToField(battle, 10, t, enc, 1))
			return 2;
		for (int i = 0; i < 20 && f.world.petCount() == 0; ++i)
		{
			f.command(id, battle, Kind::CAPTURE, 10);
			f.step();
		}
		expect("CONTROL.captured_pet_exists", f.world.petCount(), 1);
		if (f.world.petCount() == 0)
			return 2;
		const int stored_hp = f.world.playerPetAt(id, 0)->hp;
		f.command(id, battle, Kind::PET_OUT, 0);
		f.step();
		auto *bf = const_cast<SA::Rules::BattleField *>(f.world.battleField(battle));
		if (!bf || !bf->at(5).occupied)
			return 2;
		// Inject a valid post-damage battlefield state through the existing observation seam.
		// Actual damage/poison also updates this Combatant only, never Model::Pet::hp.
		const int wounded_hp = stored_hp > 1 ? stored_hp - 1 : 1;
		bf->at(5).hp = wounded_hp;
		f.command(id, battle, Kind::PET_IN);
		f.step();
		expect("F08.pet_hp_written_back_on_recall", f.world.playerPetAt(id, 0)->hp, wounded_hp);
		f.command(id, battle, Kind::PET_OUT, 0);
		f.step();
		expect("F08.pet_reentry_preserves_injury", f.world.battleField(battle)->at(5).hp, wounded_hp);
	}
	{
		Fixture f;
		auto id = f.connect();
		auto bf = field();
		bf.at(0).status = static_cast<std::uint8_t>(SA::Domain::BattleStatus::BATTLE_ST_POISON);
		bf.at(0).status_turns = 4;
		auto battle = f.world.startBattle(bf);
		f.world.joinBattle(battle, id, 0);
		for (int i = 0; i < 3; ++i)
			f.step();
		std::printf("OBSERVATION no_player_command turns_resolved=%u poison_turns=%d\n",
		            f.world.stats(battle)->turns_resolved, f.world.battleField(battle)->at(0).status_turns);
	}
	{
		SA::Rules::BattleField bf{};
		SA::Model::Enemy e{};
		e.level = 1;
		e.hp = 20;
		e.vital = e.str = e.tough = e.dex = 500;
		int entered = 0;
		for (int slot = 10; slot < 20; ++slot)
			entered += enterEnemyToField(bf, slot, e);
		expect("F14.ten_enemy_slots_available", entered, 10);
	}
	{
		Fixture f;
		auto id = f.connect();
		SA::Model::Item item{};
		item.item_id = 999;
		item.current_pile = 1;
		f.world.giveItemToPlayer(id, item);
		EncountArea area{};
		area.enemy_max_num = 1;
		area.floor = f.world.playerPos(id).floor;
		area.width = area.height = 63;
		area.zorder = 1;
		area.prob_min = area.prob_max = 120;
		area.group_id.fill(-1);
		area.group_id[0] = 1;
		area.group_prob[0] = 1;
		EnemyGroup group{};
		group.group_id = 1;
		group.appear_by_item_id = 999;
		group.enemy_id.fill(-1);
		group.enemy_id[0] = 9;
		group.create_prob[0] = 1;
		EnemyEncounter enc{};
		enc.enemy_id = 9;
		enc.temp_no = 1;
		enc.lv_min = enc.lv_max = 1;
		EnemyTemplate t{};
		t.temp_no = 1;
		t.stats = SA::Rules::SpawnTemplate{4.5, 10, 20, 12, 15, 25};
		f.world.loadEncounterTables({area}, {group}, {enc}, {t});
		f.walk(id);
		f.world.tick();
		expect("F15.owned_item_unlocks_encounter", f.world.enemyCount(), 1);
	}
	{
		Fixture f;
		for (int i = 0; i < 10; ++i)
		{
			auto bf = field();
			bf.at(11).occupied = false;
			auto id = f.world.startBattle(bf);
			f.step();
			if (!f.world.stats(id) || !f.world.stats(id)->finished)
				return 2;
		}
		std::printf("OBSERVATION F19.finished_battle_objects_retained=%zu after_10_finished\n", f.world.battleCount());
	}
	std::printf("world_expectations_failed=%d\n", failures);
	return failures ? 1 : 0;
}
