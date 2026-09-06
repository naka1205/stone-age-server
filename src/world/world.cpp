// src/world/world.cpp —— 最小 tick 与一场战斗的生命周期
//
// 01 §3.1 的 tick 顺序被**原样保留**(连未实现的四步也占位),
// 01 §3.2 的节拍层在这里第一次成为真东西:
//   ★★ 战斗推进速度 **不等于** tick 频率。
//      15 §5.2 实测 8.0 的 _BATTLE_TIME 与 _CHAR_LOOP_TIME 均为关
//      ⇒ 原版战斗速度就是 tick 频率,手感取决于当年的硬件与网络。
//      00 §0 又已认下 ④ 层「表现与手感永远无法验证」
//      ⇒ 节拍是**玩法参数**,必须可配、只能靠人试。

#include "world/api.h"

#include <algorithm>
#include <map>
#include <vector>

namespace SA::World {
namespace {

// 战斗事件缓冲。★ 每场战斗**复用一个**:domain::BattleEvents 是 7 KB 的 POD,
//   每回合新建一个就是每回合一次 7 KB 的拷贝(shared/rules/battle.h 的原话)。
struct BattleInstance {
  BattleId id = 0;
  SA::Rules::BattleField field{};
  SA::Rules::TurnCommands commands{};
  SA::Rules::SeededRandom rng{1};
  SA::Domain::BattleEvents events{};
  std::uint64_t seed = 0;
  SA::Platform::Millis next_turn_at_ms = 0;
  BattleStats stats{};
  std::vector<SA::Net::SessionId> members{};   // 订阅事件流的会话
  std::map<SA::Net::SessionId, std::uint8_t> slot_of{};
};

// 一侧是否已全灭。★ 这是**战斗结束**的判据,不是 L3 的事 ——
//   L3 只结算一个回合,"还要不要打下一回合"是世界的生命周期问题。
bool SideWipedOut(const SA::Rules::BattleField& field, bool enemy_side) {
  bool any_alive = false;
  for (int i = 0; i < SA::Rules::kSlotCount; ++i) {
    const SA::Rules::Combatant& c = field.at(i);
    if (!c.occupied) continue;
    const bool is_enemy = i >= SA::Rules::kSideOffset;
    if (is_enemy != enemy_side) continue;
    if (!c.dead && c.hp > 0) any_alive = true;
  }
  return !any_alive;
}

// 敌方 AI 的指令填充。★ 这不是新玩法,是 shared/rules/battle.h 里
//   `TurnCommands::present` 的原话所指定的分工:「该槽本回合是否有指令
//   (**敌方由 AI 填,视为齐备**)」—— L3 只结算,不替谁做决定。
//
// ⚠️★ **1.5 只填最简策略:普攻 + 打对面第一个活着的。**
//    真正的 NPC 行为(仇恨、技能选择、宠物指令)绑在批次 A 的指令分发链路上,
//    此处**有意不猜** —— 与批次 0.5 对暴击/反击「构成式齐全但判定入口缺失
//    就停在实现之前」是同一条纪律(00 §9.0.8 ②)。
//
// ⚠️ 玩家侧**不**在这里补默认指令:L3 已把「无指令 ⇒ 本回合不行动」写死
//   (battle.cpp 的 BuildActionOrder)。「玩家没提交该怎么办」是收集期与超时的
//   问题,属阶段 2,且是玩家可感知的玩法口径 ⇒ 须显式裁定,不由实现者定。
void FillEnemyCommands(const SA::Rules::BattleField& field,
                       SA::Rules::TurnCommands& commands) {
  // 目标:玩家侧第一个活着的。
  int target = -1;
  for (int i = 0; i < SA::Rules::kSideOffset; ++i) {
    const SA::Rules::Combatant& c = field.at(i);
    if (c.occupied && !c.dead && c.hp > 0) { target = i; break; }
  }
  if (target < 0) return;   // 对面全灭 ⇒ 本回合无需行动

  for (int i = SA::Rules::kSideOffset; i < SA::Rules::kSlotCount; ++i) {
    const SA::Rules::Combatant& c = field.at(i);
    if (!c.occupied || c.dead || c.hp <= 0) continue;
    if (commands.present[i]) continue;   // 已有指令(如被玩家操控的宠物)不覆盖

    SA::Domain::BattleCommand cmd{};
    cmd.command_kind = SA::Domain::BattleCommand::CommandKind::ATTACK;
    cmd.command.attack.target = static_cast<std::uint8_t>(target);
    commands.commands[i] = cmd;
    commands.present[i] = true;
  }
}

// ★★ 按事件列表把结果写回世界状态。
//
// 这一步**必须由调用方做**,不是 world 多管闲事:批次 0.5 的裁定
// (00 §9.0.8)是「L3 不写世界状态(`field` 是 const),回合内 HP 走**局部镜像**,
//  局部账回合结束即丢弃,**真正写回由调用方按事件列表执行**」——
// 那里还立了「`field` 逐字节不变」的 memcmp 回归断言来守住它。
//
// ⚠️★ 少了这一步的症状很有欺骗性:结算照跑、事件照发、回合数照涨,
//    但**没有任何人掉血** ⇒ 战斗永远打不完,而没有一处会报错。
//    (2026-09-04 src/ 首次接入构建时就是这样暴露的。)
//
// ⚠️ 1.5 只写 HP 与死亡:状态附加(§4.3)· 打飞(§3.8)· 换装 · 变身
//    绑在批次 A–D 的链路上,L3 此刻也不产它们的事件。
//    ⇒ **不猜**,与批次 0.5 对暴击/反击的处置同一条纪律。
void ApplyEvents(const SA::Domain::BattleEvents& events,
                 SA::Rules::BattleField& field) {
  for (std::size_t i = 0; i < events.events.size(); ++i) {
    const SA::Domain::BattleEvent& e = events.events[i];
    switch (e.body_kind) {
      case SA::Domain::BattleEvent::BodyKind::DAMAGE: {
        const SA::Domain::Damage& d = e.body.damage;
        if (d.target >= static_cast<std::uint32_t>(SA::Rules::kSlotCount)) break;
        SA::Rules::Combatant& c = field.at(static_cast<int>(d.target));
        if (!c.occupied) break;
        c.hp += d.hp_delta;          // hp_delta 是**负数**(L3 侧的约定)
        c.mp += d.mp_delta;
        if (c.mp < 0) c.mp = 0;
        // ⚠️ pet_hp_delta 暂不落地:1.5 的 Combatant 还没有骑宠的独立 HP 槽
        //    (那属 1.2 L2 实体族)。**记在这里,不要默默丢掉**。
        if (c.hp <= 0) { c.hp = 0; c.dead = true; }
        break;
      }
      case SA::Domain::BattleEvent::BodyKind::SET_HP: {
        const SA::Domain::SetHp& h = e.body.set_hp;
        if (h.target >= static_cast<std::uint32_t>(SA::Rules::kSlotCount)) break;
        SA::Rules::Combatant& c = field.at(static_cast<int>(h.target));
        if (!c.occupied) break;
        c.hp = h.hp;
        if (c.hp <= 0) { c.hp = 0; c.dead = true; }
        break;
      }
      case SA::Domain::BattleEvent::BodyKind::ESCAPE: {
        // 批次 A.1:逃跑事件的世界写回。
        const SA::Domain::Escape& esc = e.body.escape;
        if (esc.actor >= static_cast<std::uint32_t>(SA::Rules::kSlotCount)) break;
        SA::Rules::Combatant& c = field.at(static_cast<int>(esc.actor));
        if (!c.occupied) break;
        // ★★ 计数器**无论成败都 +1**(源码 BATTLE_Escape:4346 无条件 ++;§6.1
        //    「失败也累积」)。这就是 L3 读 escape_count+2 里的那个 +1 的落地处 ——
        //    下一回合再逃时基数已抬高,DR-BT15 的线性放大器由此生效。
        ++c.escape_count;
        if (esc.succeeded) {
          // ★ 逃跑成功 ⇒ 移出战场,**不是战死**:置 occupied=false 让 SideWipedOut
          //   把它当作"已不在场"。⚠️ 不置 dead=true —— 逃跑者没被击败,
          //   把它记成阵亡会污染战果/经验结算(阶段 2)。
          c.occupied = false;
        }
        break;
      }
      case SA::Domain::BattleEvent::BodyKind::KNOCKBACK_STATE: {
        // 批次 A.4:打飞溢出累加器的世界写回。★ L3 已算好累加后的新值,**直接写**,
        //   不在此重算门槛 —— 重算等于把 RollKnockback 实现第二遍(DR-BT5 反对的双份
        //   实现),且在免疫+一击的角落会分叉(见 battle_events.proto 的 KnockbackState)。
        //   累加(打穿 += 溢出)与清零(命中打飞归 0)的语义都封在 L3,这里只落值。
        const SA::Domain::KnockbackState& ks = e.body.knockback_state;
        if (ks.target >= static_cast<std::uint32_t>(SA::Rules::kSlotCount)) break;
        SA::Rules::Combatant& c = field.at(static_cast<int>(ks.target));
        if (!c.occupied) break;
        c.ultimate_accumulator = ks.accumulator;
        break;
      }
      case SA::Domain::BattleEvent::BodyKind::CAPTURE_ACT: {
        // 批次 A.2:捕获事件的世界写回。
        const SA::Domain::CaptureAct& cap = e.body.capture_act;
        if (cap.target >= static_cast<std::uint32_t>(SA::Rules::kSlotCount)) break;
        SA::Rules::Combatant& tgt = field.at(static_cast<int>(cap.target));
        if (!tgt.occupied) break;
        if (cap.flags != 0u) {
          // ★ 捕获成功 ⇒ 被捕目标离场(源码 `BATTLE_Exit`),**不是战死** ——
          //   置 occupied=false 让 SideWipedOut 视其"已不在场"。同逃跑,不置 dead。
          // ⚠️★ **1.5 有意不做的三件世界写,均非遗漏,而是缺落脚点**:
          //   ① 生成宠物入攻方队伍(`PET_createPetFromCharaIndex`)—— 要 L2 宠物模型
          //      与角色背包(1.2 / 阶段 2);
          //   ② 删除条件捕获道具(DR-BT10「全删」)—— 要道具系统(阶段 2);
          //   ③ 攻方 `capture_bonus` 清零(源码 :4121)—— 1.5 的 field 就地 mutate,
          //      按理应清,但 capture_bonus 属 mods、当前无路径设置它 ⇒ 记在此,
          //      待道具/技能能设置它时一并落地,不提前写一处无人触发的清零。
          tgt.occupied = false;
        }
        // 失败 ⇒ 无世界写(目标留场),事件仅供客户端演出。
        break;
      }
      default:
        // 其余事件是**表现**(HIT / TEXT_BOX / …)或未移植链路的占位,
        // 对世界状态无影响 ⇒ 显式落到这里,不是遗漏。
        break;
    }
  }
}

// ── 1.4 demo 的战场(脚手架,见 platform/api.h 的 DemoBattleConfig)────
//
// ⚠️★ **这些数字不是内容数据,也不假装是。**
//   00 §0 已认下 ③ 层「规则不可自证」与 ④ 层「表现与手感永远无法验证」,
//   真正的敌人数值属 L4 内容导入(D 线),不在 1.4 的路径上。
//   ⇒ 此处只需满足两条**可判定**的性质,它们都由用例钉着:
//     ① 客户端一条指令不发,战斗也要在有限回合内结束 ——
//        否则 demo 挂起时分不清是"敌人打不动"还是"事件流断了";
//     ② 客户端正常出招时,战斗**更快**结束 ⇒ 指令确实被采纳了。
//        ★ 这一条才是 1.4 真正要证明的东西:上行链路是通的。
SA::Rules::BattleField MakeDemoField() {
  SA::Rules::BattleField f{};

  SA::Rules::Combatant& me = f.at(0);
  me.occupied = true;
  me.kind = SA::Rules::CombatantKind::kPlayer;
  me.slot = 0;
  me.level = 20;
  me.hp = 400;
  me.max_hp = 400;
  me.mp = 100;
  me.max_mp = 100;
  me.attack = 300;
  me.defense = 40;
  me.quick = 200;
  me.luck = 10;

  SA::Rules::Combatant& foe = f.at(SA::Rules::kSideOffset);
  foe.occupied = true;
  foe.kind = SA::Rules::CombatantKind::kEnemy;
  foe.slot = static_cast<std::uint8_t>(SA::Rules::kSideOffset);
  foe.level = 18;
  foe.hp = 260;
  foe.max_hp = 260;
  foe.attack = 260;
  foe.defense = 30;
  foe.quick = 150;
  foe.luck = 5;
  return f;
}

}  // namespace

// ★★ config.cpp 把 demo_battle.slot 的上限写死成 9,因为 L0 够不着 L3
//    (platform 不依赖 rules,那是分层的硬约束)。⇒ 两处一致性由这里守。
//    ⚠️ 少了它,某天 kSideOffset 改了、配置校验照旧,表现是玩家被放进敌方半场
//      而没有任何一处报错 —— 00 §10.4 那类静默错误。
static_assert(SA::Rules::kSideOffset == 10,
              "demo_battle.slot 的配置上限(config.cpp 里的 9)是按 "
              "kSideOffset == 10 写死的;kSideOffset 变了就要同步改那里");

struct World::Impl {
  // 一条连接上的全部状态。★ Connection 与 Session 在 1.5 是 1:1,
  //   但类型是分开的 —— 01 §5.2 明写两者生命周期不同,
  //   压在一起正是原版 LoginType 的毛病。重连窗口留到阶段 2。
  struct Conn {
    SA::Net::ConnectionId conn_id = 0;
    SA::Net::FrameReader reader{};
    std::unique_ptr<SA::Net::Session> session;
    std::vector<std::uint8_t> outbound{};
  };

  Impl(const SA::Platform::ServerConfig& cfg, SA::Platform::Clock& clk,
       SA::Platform::Logger& log, SA::Platform::RandomSource& rnd,
       SA::Net::Transport& tp)
      : config(cfg), clock(clk), logger(log), random(rnd), transport(tp) {}

  SA::Platform::ServerConfig config;
  SA::Platform::Clock& clock;
  SA::Platform::Logger& logger;
  SA::Platform::RandomSource& random;
  SA::Net::Transport& transport;

  std::map<SA::Net::ConnectionId, Conn> conns;
  // 1.5 里 SessionId == ConnectionId(见上)。
  std::map<BattleId, BattleInstance> battles;

  SA::Rules::RulesConfig rules_config{};
  BattleId next_battle_id = 1;
  std::uint64_t ticks = 0;
  SA::Platform::Millis now_ms = 0;
  bool shutdown_requested = false;
  bool stopped = false;
};

World::World(const SA::Platform::ServerConfig& config,
             SA::Platform::Clock& clock, SA::Platform::Logger& logger,
             SA::Platform::RandomSource& random,
             SA::Net::Transport& transport)
    : impl_(std::make_unique<Impl>(config, clock, logger, random, transport)) {
  transport.SetEvents(this);
}

World::~World() = default;

// ══ tick(01 §3.1)═══════════════════════════════════════════════
void World::Tick() {
  Impl& s = *impl_;
  if (s.stopped) return;
  ++s.ticks;

  // ── 1. 时钟推进 ──
  // ★ 统一时钟源、单调时钟。整个 tick 内**只取一次** ——
  //   同一 tick 里两处取到不同的"现在"会让节拍判断出现自相矛盾的结果。
  s.now_ms = s.clock.NowMs();

  // ── 2. 网络入站 ──
  // 从传输层取已到达的字节,派发到会话。★ 不阻塞(01 §2)。
  s.transport.Poll();

  // ── 3. NPC 生成 ──  ⬜ 阶段 2

  // ── 4. 战斗推进 ──  ★ 受节拍层控制,不等于 tick 频率(01 §3.2)
  {
    std::vector<BattleId> finished;
    for (auto& kv : s.battles) {
      BattleInstance& b = kv.second;
      if (b.stats.finished) continue;
      if (s.now_ms < b.next_turn_at_ms) continue;

      // ★ 敌方 AI 先填指令(见 FillEnemyCommands 卷首:这是 battle.h 指定的分工)。
      FillEnemyCommands(b.field, b.commands);

      // 结算一个回合。⚠️ 返回 false = 事件超过 256 条被迫截断。
      //   05 §10.4 记着原版无上界 strcat 的教训 ⇒ **必须处理**,不可当没看见。
      const bool ok = SA::Rules::ResolveTurn(b.field, b.commands,
                                             s.rules_config, b.rng, b.events);
      if (!ok) {
        b.stats.truncated_once = true;
        s.logger.Log(SA::Platform::LogLevel::kWarn,
                     SA::Platform::LogEvent::kBattleEventsTruncated,
                     {{"battle_id", b.id},
                      {"turn", static_cast<std::uint64_t>(b.field.turn)}});
        // ⚠️ 本批次先如实记账并继续 —— 01 §12 的取向是「宁可分包,不可静默截断」,
        //    而分包要改 BattleEvents 的下发形状(加 seq / more 标志),
        //    那是 IDL 的改动(0.2),不该被一次 world 的实现顺手带过。
        //    ⇒ 已登记为欠债,见 docs/01 §13。
      }

      // ★★ 写回世界状态 —— 见 ApplyEvents 卷首:L3 有意不写,调用方必须写。
      ApplyEvents(b.events, b.field);

      b.stats.events_emitted += static_cast<std::uint32_t>(b.events.events.size());
      ++b.stats.turns_resolved;

      // 下发事件流。★ 这就是 1.4 demo 的验收对象:**事件流端到端一致**。
      for (const SA::Net::SessionId sid : b.members) {
        const auto it = s.conns.find(sid);
        if (it == s.conns.end()) continue;
        Impl::Conn& c = it->second;
        if (c.session == nullptr) continue;
        (void)c.session->Push(b.events, c.outbound);
      }

      s.logger.Log(SA::Platform::LogLevel::kDebug,
                   SA::Platform::LogEvent::kBattleTurnResolved,
                   {{"battle_id", b.id},
                    {"turn", static_cast<std::uint64_t>(b.field.turn)},
                    {"events", static_cast<std::uint64_t>(b.events.events.size())}});

      // 本回合的指令用完即清 —— 指令是**本回合**的输入,
      // 留着会让下一回合重放上一回合的动作。
      b.commands = SA::Rules::TurnCommands{};
      ++b.field.turn;
      b.next_turn_at_ms =
          s.now_ms + static_cast<SA::Platform::Millis>(
                         s.config.tempo.battle_turn_interval_ms);

      if (SideWipedOut(b.field, true) || SideWipedOut(b.field, false)) {
        b.stats.finished = true;
        finished.push_back(b.id);
      } else {
        // 下一回合开始 —— ready_mask 留 0:1.5 没有"谁已提交指令"的收集期,
        // 指令一到就存下。收集期与超时是阶段 2 的事。
        SA::Domain::BattleTurnBegin begin;
        begin.battle_id = b.id;
        begin.turn = b.field.turn;
        begin.ready_mask = 0;
        for (const SA::Net::SessionId sid : b.members) {
          const auto it = s.conns.find(sid);
          if (it == s.conns.end() || it->second.session == nullptr) continue;
          (void)it->second.session->Push(begin, it->second.outbound);
        }
      }
    }
    for (const BattleId id : finished) {
      const auto it = s.battles.find(id);
      if (it == s.battles.end()) continue;
      s.logger.Log(SA::Platform::LogLevel::kInfo,
                   SA::Platform::LogEvent::kBattleFinished,
                   {{"battle_id", id},
                    {"turns", static_cast<std::uint64_t>(
                                  it->second.stats.turns_resolved)}});
    }
  }

  // ── 5. 角色循环 ──   ⬜ 阶段 2
  // ── 6. 定时业务 ──   ⬜ 阶段 2
  // ── 7. 出站聚合 ──   ⬜ 阶段 2(CA/CD 视野聚合;1.5 无视野)
  //
  // ⚠️ 但**出站字节仍要发出去** —— 上面第 4 步往 outbound 里写了东西。
  //    这不是 §7 说的那种聚合(那是视野 Appear/Disappear 攒批),
  //    只是"把已经生成的字节交给传输层"。别把这里读成 §7 已经做了。
  for (auto& kv : s.conns) {
    Impl::Conn& c = kv.second;
    if (c.outbound.empty()) continue;
    (void)s.transport.Send(c.conn_id, c.outbound.data(), c.outbound.size());
    c.outbound.clear();
  }

  // ── 8. 关闭检查 ──
  if (s.shutdown_requested) {
    // ⚠️ 01 §11.2 的完整停服流程(拒绝新连接 → 广播倒计时 → 逐会话保存
    //    → 等在途请求收敛 → 落盘确认)在 1.5 **做不了也不该做**:
    //    没有 storage、没有跨模块请求。这里只做能做的那部分。
    s.logger.Log(SA::Platform::LogLevel::kInfo,
                 SA::Platform::LogEvent::kServerStopping,
                 {{"connections", static_cast<std::uint64_t>(s.conns.size())},
                  {"battles", static_cast<std::uint64_t>(s.battles.size())}});
    // ⚠️★ **不能边遍历 s.conns 边关**:transport.Close() 会**同步**回调
    //    OnDisconnected,而它做的第一件事就是 s.conns.erase(it)
    //    ⇒ 迭代器当场失效。首次运行本用例即 SIGSEGV(2026-09-04)。
    //    ⇒ 先取快照,再逐个关。
    std::vector<SA::Net::ConnectionId> closing;
    closing.reserve(s.conns.size());
    for (const auto& kv : s.conns) closing.push_back(kv.first);
    for (const SA::Net::ConnectionId cid : closing) {
      const auto it = s.conns.find(cid);
      if (it != s.conns.end() && it->second.session != nullptr) {
        it->second.session->Close();
      }
      s.transport.Close(cid);
    }
    s.stopped = true;
  }
}

// ══ 战斗生命周期 ═════════════════════════════════════════════════
BattleId World::StartBattle(const SA::Rules::BattleField& field) {
  Impl& s = *impl_;
  const BattleId id = s.next_battle_id++;

  BattleInstance b;
  b.id = id;
  b.field = field;
  b.field.battle_id = id;
  b.seed = s.random.NextSeed();
  b.rng = SA::Rules::SeededRandom(b.seed);
  b.next_turn_at_ms =
      s.now_ms +
      static_cast<SA::Platform::Millis>(s.config.tempo.battle_turn_interval_ms);

  const std::uint64_t seed = b.seed;
  s.battles.emplace(id, std::move(b));

  s.logger.Log(SA::Platform::LogLevel::kInfo,
               SA::Platform::LogEvent::kBattleStarted, {{"battle_id", id}});
  // ★★ 种子单独一条,级别 info:它是可回放的**唯一**凭据。
  //    调低成 debug 就等于在生产上关掉了可回放性。
  s.logger.Log(SA::Platform::LogLevel::kInfo,
               SA::Platform::LogEvent::kBattleSeed,
               {{"battle_id", id},
                {"seed", seed},
                {"master_seed", s.random.master_seed()}});
  return id;
}

bool World::JoinBattle(BattleId battle, SA::Net::SessionId session,
                       std::uint8_t slot) {
  Impl& s = *impl_;
  const auto bit = s.battles.find(battle);
  if (bit == s.battles.end()) return false;
  if (slot >= SA::Rules::kSlotCount) return false;

  const auto cit = s.conns.find(session);
  if (cit == s.conns.end() || cit->second.session == nullptr) return false;
  // ⚠️ 只有握手过的会话能入场 —— 否则一条没握手的连接就能拿到事件流。
  if (cit->second.session->state() == SA::Net::SessionState::kAnonymous ||
      cit->second.session->closed()) {
    return false;
  }

  BattleInstance& b = bit->second;
  if (std::find(b.members.begin(), b.members.end(), session) !=
      b.members.end()) {
    return false;
  }
  b.members.push_back(session);
  b.slot_of[session] = slot;
  cit->second.session->MarkOnline();

  // ★★ 入场即下发**自己是谁**与**现在是第几回合**,否则客户端无从组指令:
  //    BattleCommand 要带 battle_id 与 turn,而这两样它此刻都还不知道
  //    —— 缺这一步,上行链路在 demo 里根本走不到。
  //
  // ⚠️ 这不是 demo 专用的东西,所以放在 JoinBattle 而不是 OnSessionReady:
  //    任何入场路径(阶段 2 的选角、观战加入)都需要它。
  SA::Domain::BattleSelfInfo self;
  self.battle_id = b.id;
  self.slot = slot;
  self.mp = b.field.at(slot).mp;
  // ⚠️ menu_flags 留 0:菜单构成(观战加入 / 先制 / 宠物菜单开关)属阶段 2 的
  //    UI 契约,此处**不猜** —— 与批次 0.5 对暴击/反击的处置同一条纪律。
  self.menu_flags = 0;
  // ★ 但 cannot_act **不留 0**:DR-BT5 把「能否行动」定为 Rules::CheckCanAct
  //   这一个真源,而它已经在 L3 里 ⇒ 照真源填,不是硬编码一个"可以行动"。
  self.cannot_act = SA::Rules::CheckCanAct(b.field.at(slot));
  (void)cit->second.session->Push(self, cit->second.outbound);

  SA::Domain::BattleTurnBegin begin;
  begin.battle_id = b.id;
  begin.turn = b.field.turn;
  begin.ready_mask = 0;   // 1.5 没有收集期,理由见 Tick 第 4 步
  (void)cit->second.session->Push(begin, cit->second.outbound);

  // ⚠️ 入场日志放在这里而不是调用方:任何入场路径都该留痕,
  //   而"谁在哪场的哪个槽"是排查战斗问题时第一个要问的东西。
  s.logger.Log(SA::Platform::LogLevel::kInfo,
               SA::Platform::LogEvent::kBattleJoined,
               {{"battle_id", b.id},
                {"session_id", session},
                {"slot", static_cast<std::uint64_t>(slot)}});
  return true;
}

// ══ TransportEvents ═════════════════════════════════════════════
void World::OnConnected(SA::Net::ConnectionId id) {
  Impl& s = *impl_;
  Impl::Conn c;
  c.conn_id = id;
  // 1.5:SessionId == ConnectionId。⚠️ 阶段 2 加重连窗口时这条要断开 ——
  //    那正是 01 §5.2 把两者分开的理由。
  c.session = std::make_unique<SA::Net::Session>(
      id, s.config.protocol_version, s.config.heartbeat_interval_ms, this);
  s.conns.emplace(id, std::move(c));

  s.logger.Log(SA::Platform::LogLevel::kDebug,
               SA::Platform::LogEvent::kConnectionAccepted, {{"conn_id", id}});
}

void World::OnBytes(SA::Net::ConnectionId id, const std::uint8_t* data,
                    std::size_t n) {
  Impl& s = *impl_;
  const auto it = s.conns.find(id);
  if (it == s.conns.end()) return;
  Impl::Conn& c = it->second;

  if (!c.reader.Push(data, n)) {
    s.logger.Log(SA::Platform::LogLevel::kWarn,
                 SA::Platform::LogEvent::kFrameRejected,
                 {{"conn_id", id}, {"reason", std::string_view("buffer_limit")}});
    s.transport.Close(id);
    return;
  }

  for (;;) {
    const std::uint8_t* payload = nullptr;
    std::uint32_t len = 0;
    const SA::Net::FrameStatus st = c.reader.Next(&payload, &len);
    if (st == SA::Net::FrameStatus::kNeedMore) break;
    if (st != SA::Net::FrameStatus::kOk) {
      // ⚠️ kTooLarge / kEmpty 不可恢复:字节流已无法对齐(见 net/api.h)。
      s.logger.Log(SA::Platform::LogLevel::kWarn,
                   SA::Platform::LogEvent::kFrameRejected,
                   {{"conn_id", id},
                    {"reason", std::string_view(
                                   st == SA::Net::FrameStatus::kTooLarge
                                       ? "frame_too_large"
                                       : "frame_empty")}});
      s.transport.Close(id);
      return;
    }

    const bool ok = c.session->HandleFrame(payload, len, c.outbound);
    c.reader.Pop();
    if (!ok) {
      s.logger.Log(SA::Platform::LogLevel::kWarn,
                   SA::Platform::LogEvent::kHandshakeRejected,
                   {{"conn_id", id},
                    {"msg_id", static_cast<std::uint64_t>(
                                   c.session->last_reject_msg_id())},
                    {"state", std::string_view(SA::Net::SessionStateName(
                                  c.session->state()))}});
      // ★ 先把已生成的出站字节发出去(可能含 HandshakeRejected),再关。
      if (!c.outbound.empty()) {
        (void)s.transport.Send(id, c.outbound.data(), c.outbound.size());
        c.outbound.clear();
      }
      s.transport.Close(id);
      return;
    }
  }
}

void World::OnDisconnected(SA::Net::ConnectionId id) {
  Impl& s = *impl_;
  const auto it = s.conns.find(id);
  if (it == s.conns.end()) return;
  if (it->second.session != nullptr) it->second.session->Close();

  for (auto& kv : s.battles) {
    std::vector<SA::Net::SessionId>& m = kv.second.members;
    m.erase(std::remove(m.begin(), m.end(), id), m.end());
    kv.second.slot_of.erase(id);
  }
  s.conns.erase(it);

  s.logger.Log(SA::Platform::LogLevel::kDebug,
               SA::Platform::LogEvent::kConnectionClosed, {{"conn_id", id}});
}

// ══ SessionHost ═════════════════════════════════════════════════
void World::OnSessionReady(SA::Net::SessionId id) {
  Impl& s = *impl_;
  s.logger.Log(SA::Platform::LogLevel::kInfo,
               SA::Platform::LogEvent::kHandshakeAccepted,
               {{"session_id", id}});

  // ── 1.4 demo 的入场装配(默认关,见 platform/api.h 的 DemoBattleConfig)──
  //
  // ⚠️★ 这是**脚手架**:真玩法里「握手完进哪里」是选角与登录点的结果(阶段 2,
  //    要 storage)。此处走捷径是为了让 1.4 有一条能被客户端走通的路径,
  //    而不是因为这条捷径是对的。⇒ 阶段 2 接上选角时整块删掉。
  if (!s.config.demo_battle.enabled) return;

  // ★ 每条会话开**自己的**一场,不共用:多会话共用一场就要回答
  //   "第二个人落在哪个槽""先来的打到一半后来的怎么进",那是组队/观战的玩法口径
  //   (阶段 2),不该由一段 demo 脚手架顺手定下来。
  const BattleId battle = StartBattle(MakeDemoField());
  const std::uint8_t slot = s.config.demo_battle.slot;
  if (!JoinBattle(battle, id, slot)) {
    // ⚠️ 进不去要**报出来**。这条路径上 JoinBattle 的每一个 false 都意味着
    //    上面刚建的战斗成了没人看的孤儿,而客户端会停在"连上了但什么都没发生"
    //    —— 那正是 00 §10.4 说的静默错误。
    s.logger.Log(SA::Platform::LogLevel::kError,
                 SA::Platform::LogEvent::kBattleJoinFailed,
                 {{"battle_id", battle},
                  {"session_id", id},
                  {"reason", std::string_view("demo_join_failed")}});
    return;
  }
  s.logger.Log(SA::Platform::LogLevel::kDebug,
               SA::Platform::LogEvent::kSessionStateChanged,
               {{"session_id", id},
                {"state", std::string_view("online")},
                {"demo", true}});
}

void World::OnBattleCommand(SA::Net::SessionId id,
                            const SA::Domain::BattleCommand& cmd) {
  Impl& s = *impl_;
  const auto bit = s.battles.find(cmd.battle_id);
  if (bit == s.battles.end()) return;
  BattleInstance& b = bit->second;

  // ⚠️ 指令必须指向**当前**回合。02 §1.3 的取向:不靠"下一个到达的包就是回复",
  //    这里同理 —— 迟到的上一回合指令若被采纳,会在新回合里执行一个过期的决定。
  if (cmd.turn != b.field.turn) return;

  const auto sit = b.slot_of.find(id);
  if (sit == b.slot_of.end()) return;
  const std::uint8_t slot = sit->second;
  if (slot >= SA::Rules::kSlotCount) return;

  b.commands.commands[slot] = cmd;
  b.commands.present[slot] = true;
}

void World::OnSessionClosed(SA::Net::SessionId id) {
  impl_->logger.Log(SA::Platform::LogLevel::kDebug,
                    SA::Platform::LogEvent::kSessionStateChanged,
                    {{"session_id", id},
                     {"state", std::string_view("closed")}});
}

// ══ 观察面 ═══════════════════════════════════════════════════════
void World::RequestShutdown() noexcept { impl_->shutdown_requested = true; }

bool World::stopped() const noexcept { return impl_->stopped; }

std::uint64_t World::ticks() const noexcept { return impl_->ticks; }

std::size_t World::session_count() const noexcept {
  return impl_->conns.size();
}

const BattleStats* World::stats(BattleId id) const {
  const auto it = impl_->battles.find(id);
  return it == impl_->battles.end() ? nullptr : &it->second.stats;
}

SA::Net::SessionState World::session_state(SA::Net::SessionId id) const {
  const auto it = impl_->conns.find(id);
  if (it == impl_->conns.end() || it->second.session == nullptr) {
    return SA::Net::SessionState::kClosed;
  }
  return it->second.session->state();
}

}  // namespace SA::World
