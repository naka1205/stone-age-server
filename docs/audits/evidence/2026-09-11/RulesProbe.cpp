// Standalone audit probe. Nonzero exit means the original-source expectation failed.
// This compiles the new project only; it does not compile or run the legacy server.
#include "rules/Battle.h"
#include "rules/Status.h"
#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

using namespace SA::Rules;
using Kind = SA::Domain::BattleCommand::CommandKind;
using Body = SA::Domain::BattleEvent::BodyKind;
using Status = SA::Domain::BattleStatus;
int failures = 0;
void expect(const char *id, long long actual, long long expected)
{
	std::printf("%s %s observed=%lld expected=%lld\n",
	            actual == expected ? "PASS" : "FAIL", id, actual, expected);
	failures += actual != expected;
}
struct ProbeRandom : Random
{
	int fixed = -1;
	bool high = false;
	std::vector<std::pair<int, int>> calls;
	int rand(int lo, int hi) override
	{
		calls.push_back({lo, hi});
		if (fixed >= 0)
			return std::max(lo, std::min(hi, fixed));
		return high ? hi : lo;
	}
	int randMod(int n) override
	{
		calls.push_back({0, n - 1});
		return 0;
	}
};
Combatant unit(int slot, int quick)
{
	Combatant c{};
	c.occupied = true;
	c.kind = slot < 10 ? CombatantKind::kPlayer : CombatantKind::kEnemy;
	c.slot = static_cast<std::uint8_t>(slot);
	c.level = 20;
	c.hp = c.max_hp = 10000;
	c.attack = 100;
	c.defense = 50;
	c.quick = quick;
	c.luck = 10;
	c.charm = 100;
	c.vital = c.str = c.tough = c.dex = 2500;
	c.mods.no_duck = true;
	c.mods.unarmed = false;
	c.mods.attack_num_min = c.mods.attack_num_max = 1;
	return c;
}
void command(TurnCommands &commands, int slot, Kind kind, int target = 0)
{
	commands.present[slot] = true;
	auto &cmd = commands.commands[slot];
	cmd.command_kind = kind;
	if (kind == Kind::ATTACK)
		cmd.command.attack.target = target;
	if (kind == Kind::CAPTURE)
		cmd.command.capture.target = target;
}
int hitsBy(const SA::Domain::BattleEvents &events, int actor)
{
	int n = 0;
	for (std::size_t i = 0; i < events.events.size(); ++i)
		if (events.events[i].body_kind == Body::HIT &&
		    events.events[i].body.hit.attacker == static_cast<unsigned>(actor))
			++n;
	return n;
}
int main()
{
	{
		auto atk = unit(0, 100), def = unit(10, 100);
		atk.luck = 0;
		def.vital = def.str = def.tough = 1000;
		def.dex = 0;
		ProbeRandom rng;
		rng.fixed = 16;
		int observed = 0;
		const bool hit = rollStatusAttack(false, atk, def, 1, 30, 30, 1.0, rng, &observed);
		// battle_event.c:5088-5136: cast occurs after subtracting fVitalP.
		float total = static_cast<float>(def.vital + def.str + def.tough + def.dex);
		float proportion = static_cast<float>(def.vital) / total;
		float penalty = static_cast<float>(proportion / 0.25);
		penalty = static_cast<float>(penalty * 10.0);
		int source = static_cast<int>(30 - penalty);
		expect("F01.status_integer_conversion", observed, source);
		expect("F01.status_boundary_roll_16", hit, 16 < source);
	}
	{
		auto atk = unit(0, 100), def = unit(10, 100);
		atk.luck = 0;
		def.mods.equip_resist_weaken = 7;
		ProbeRandom rng;
		int observed = 0;
		rollStatusAttack(false, atk, def, 7, 30, 30, 1.0, rng, &observed);
		// SSRC80 enum parse: WORKWEAKEN=51, WORKBARRIER=53, WORKNOCAST=54;
		// legal BattleStatus is 1..43. The source comparisons cannot match.
		expect("F02.unreachable_equipment_resistance", observed, 20);
	}
	{
		auto c = unit(0, 100);
		SA::Domain::BattleCommand cmd{};
		cmd.command_kind = Kind::ATTACK;
		ProbeRandom rng;
		rng.high = true;
		int dex = computeActionDex(c, cmd, rng);
		std::printf("INFO F03.dex current=%d; SSRC80_high_endpoint=84; SSRC85_high_endpoint=108\n", dex);
		// Even retaining the currently selected 8.5 coefficient .1, base is quick+20.
		expect("F03.jitter_base_current_coefficient", rng.calls.front().second, 12);
		ProbeRandom item_rng;
		cmd.command_kind = Kind::USE_ITEM;
		expect("F03.item_priority_zero_roll", computeActionDex(c, cmd, item_rng), 138);
	}
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		BattleField f{};
		f.battle_id = 1;
		f.turn = 1;
		f.is_pvp = false;
		f.at(0) = unit(0, 500);
		f.at(10) = unit(10, 10);
		TurnCommands cmds{};
		if (scenario == 0)
		{
			command(cmds, 0, Kind::ESCAPE);
			command(cmds, 10, Kind::ATTACK, 0);
		}
		else if (scenario == 1)
		{
			f.at(10).level = 1;
			f.at(10).hp = 1;
			f.at(10).max_hp = 100;
			f.at(10).mods.capturable = true;
			f.at(0).mods.capture_bonus = 0;
			command(cmds, 0, Kind::CAPTURE, 10);
			command(cmds, 10, Kind::ATTACK, 0);
		}
		else
		{
			f.at(5) = unit(5, 20);
			f.at(5).kind = CombatantKind::kPet;
			command(cmds, 0, Kind::PET_IN);
			command(cmds, 5, Kind::ATTACK, 10);
			command(cmds, 10, Kind::WAIT);
		}
		ProbeRandom rng;
		SA::Domain::BattleEvents events{};
		if (!resolveTurn(f, cmds, RulesConfig{}, rng, events))
			return 2;
		if (scenario == 0)
			expect("F04.attack_after_target_escaped", hitsBy(events, 10), 0);
		if (scenario == 1)
		{
			int success = 0;
			for (std::size_t i = 0; i < events.events.size(); ++i)
				if (events.events[i].body_kind == Body::CAPTURE_ACT &&
				    events.events[i].body.capture_act.flags)
					++success;
			expect("CONTROL.capture_succeeded", success, 1);
			expect("F04.captured_enemy_still_attacks", hitsBy(events, 10), 0);
		}
		if (scenario == 2)
			expect("F04.recalled_pet_still_attacks", hitsBy(events, 5), 0);
	}
	{
		BattleField f{};
		f.at(0) = unit(0, 500);
		f.at(10) = unit(10, 10);
		f.at(10).status = static_cast<std::uint8_t>(Status::BATTLE_ST_SLEEP);
		f.at(10).status_turns = 4;
		TurnCommands cmds{};
		command(cmds, 0, Kind::ATTACK, 10);
		command(cmds, 10, Kind::WAIT);
		ProbeRandom rng;
		SA::Domain::BattleEvents events{};
		resolveTurn(f, cmds, RulesConfig{}, rng, events);
		int wake = 0, damage = 0;
		for (std::size_t i = 0; i < events.events.size(); ++i)
		{
			const auto &e = events.events[i];
			if (e.body_kind == Body::DAMAGE && e.body.damage.target == 10 &&
			    e.body.damage.hp_delta < 0)
				++damage;
			if (e.body_kind == Body::STATUS_CHANGE &&
			    e.body.status_change.status == Status::BATTLE_ST_SLEEP &&
			    !e.body.status_change.applied)
				++wake;
		}
		expect("CONTROL.sleep_target_was_damaged", damage, 1);
		expect("F05.damage_wakes_sleep", wake, 1);
	}
	{
		auto atk = unit(0, 100), def = unit(10, 100);
		def.mods.no_duck = false;
		atk.status = static_cast<std::uint8_t>(Status::BATTLE_ST_DRUNK);
		atk.status_turns = 3;
		ProbeRandom rng;
		rollDodge(atk, def, false, false, RulesConfig{}, rng);
		expect("F06.drunk_status_rng_draws", rng.calls.size(), 2);
	}
	{
		auto atk = unit(0, 100), def = unit(1, 100);
		atk.attack = 100;
		def.defense = 0;
		RulesConfig cfg{};
		cfg.damage_calc_percent = 100; // Isolate the formula.
		ProbeRandom rng;
		rng.high = true;
		BattleField field{};
		const int observed = computeDamage(field, atk, def, cfg, rng);
		// Original RAND(0,100/8) has span 13.5. u=.99 yields 13, not 12.
		const float source_k = static_cast<float>(static_cast<int>(13.5 * 0.99) - 100.0 / 16);
		const int source_damage = static_cast<int>(100.0 * 2.0 + source_k);
		expect("F16.damage_float_random_endpoint", observed, source_damage);
		std::printf("INFO F16.new_random_upper=%d original_support_upper=13\n", rng.calls.back().second);
	}
	std::printf("rules_source_expectations_failed=%d\n", failures);
	return failures ? 1 : 0;
}
