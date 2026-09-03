// src/platform/config.cpp —— 配置装载与校验
//
// 01 §11.1:原版按 setup.cf 依次加载数十个数据文件,**任一失败即拒绝启动**。
// ★ 这个"快速失败"的性质**保留** —— 内容数据不全就起来,只会在玩家碰到时才炸。
//
// ⚠️★ 本文件收集**全部**错误再返回,不是遇到第一个就走人。
//    理由:配置是人手写的,一次告诉他三处错比让他改一处再跑一次快三倍。
//    这与"拒绝启动"不矛盾 —— 只要有一条错就不启动,但要一次说完。

#include "platform/api.h"

#include "internal/json.h"

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace sa::platform {
namespace {

void AddError(ConfigResult& r, std::string path, std::string message) {
  r.errors.push_back(ConfigError{std::move(path), std::move(message)});
}

std::string Join(std::string_view prefix, std::string_view key) {
  if (prefix.empty()) return std::string(key);
  return std::string(prefix) + "." + std::string(key);
}

// 读一个无符号整数并校验范围。缺键 ⇒ 保持默认值,不报错(有默认值就是可选)。
// ⚠️ 但**类型错**与**越界**一律报错 —— 那是写错了,不是没写。
bool ReadUInt(const json::Value& obj, std::string_view prefix, const char* key,
              std::uint64_t lo, std::uint64_t hi, std::uint64_t& out,
              ConfigResult& r) {
  const json::Value* v = obj.Find(key);
  if (v == nullptr) return true;  // 用默认值
  if (!v->is_number()) {
    AddError(r, Join(prefix, key), "应为整数");
    return false;
  }
  const double d = v->as_number();
  if (std::floor(d) != d) {
    AddError(r, Join(prefix, key), "应为整数,不能有小数部分");
    return false;
  }
  if (d < 0.0) {
    AddError(r, Join(prefix, key), "不能为负数");
    return false;
  }
  // double 能精确表示的整数上限是 2^53;超过它再比较范围已经没有意义。
  if (d > 9007199254740992.0) {
    AddError(r, Join(prefix, key), "数值过大");
    return false;
  }
  const std::uint64_t u = static_cast<std::uint64_t>(d);
  if (u < lo || u > hi) {
    std::ostringstream os;
    os << "取值须在 [" << lo << ", " << hi << "] 之间,实际为 " << u;
    AddError(r, Join(prefix, key), os.str());
    return false;
  }
  out = u;
  return true;
}

// ★ 未知键一律报错,不是忽略。
//   一个拼错的 "listen_prot" 被忽略,表现是"服务端起在了另一个端口而没人知道为什么"
//   —— 正是 00 §10.4 那类不报错、不崩溃的静默错误。
void RejectUnknownKeys(const json::Value& obj, std::string_view prefix,
                       const std::set<std::string>& known, ConfigResult& r) {
  for (const auto& kv : obj.as_object()) {
    if (known.find(kv.first) == known.end()) {
      AddError(r, Join(prefix, kv.first), "未知的配置项");
    }
  }
}

bool ParseLogLevel(std::string_view s, LogLevel& out) {
  if (s == "trace") { out = LogLevel::kTrace; return true; }
  if (s == "debug") { out = LogLevel::kDebug; return true; }
  if (s == "info")  { out = LogLevel::kInfo;  return true; }
  if (s == "warn")  { out = LogLevel::kWarn;  return true; }
  if (s == "error") { out = LogLevel::kError; return true; }
  return false;
}

// 01 §12:--modules=gateway,world,session,social 之类决定装载什么。
// ⚠️ 1.5 只有 world 一个模块真实存在(00 §9.0.4 明写不要 gateway/social/storage)。
//    ⇒ 这里认得其余名字,但**装载它们会被拒绝**,而不是默默不装。
bool IsKnownModule(const std::string& m) {
  return m == "world" || m == "gateway" || m == "session" || m == "social";
}

bool IsImplementedModule(const std::string& m) { return m == "world"; }

}  // namespace

ConfigResult ParseConfig(std::string_view json_text) {
  ConfigResult r;

  const json::ParseOutcome parsed = json::Parse(json_text);
  if (!parsed.ok) {
    std::ostringstream os;
    os << "第 " << parsed.line << " 行(偏移 " << parsed.offset << "):" << parsed.error;
    AddError(r, "<文件>", os.str());
    return r;
  }
  if (!parsed.value.is_object()) {
    AddError(r, "<文件>", "顶层必须是一个 JSON 对象");
    return r;
  }

  const json::Value& root = parsed.value;
  ServerConfig cfg;

  RejectUnknownKeys(root, "",
                    {"listen_port", "protocol_version", "heartbeat_interval_ms",
                     "rng_seed", "log_level", "tempo", "modules"},
                    r);

  // ── listen_port ───────────────────────────────────────────
  // 下限 1024:1023 以下要 root,而这个服务端没有任何理由以 root 运行。
  std::uint64_t port = cfg.listen_port;
  if (ReadUInt(root, "", "listen_port", 1024, 65535, port, r)) {
    cfg.listen_port = static_cast<std::uint16_t>(port);
  }

  // ── protocol_version ──────────────────────────────────────
  // ★ 0 不是合法版本:02 §2.1 要求版本不等即拒,而 0 常常是"字段没填"的表现,
  //   让它合法就等于让"忘了填"和"确实是 0 版"无法区分。
  std::uint64_t pv = cfg.protocol_version;
  if (ReadUInt(root, "", "protocol_version", 1, 0xFFFFFFFFull, pv, r)) {
    cfg.protocol_version = static_cast<std::uint32_t>(pv);
  }

  // ── heartbeat_interval_ms ─────────────────────────────────
  // 下限 1000:比这更密的心跳只是在给自己造流量;上限 300000 = 5 分钟,
  // 再长就失去"发现对端已经不在了"的意义。
  std::uint64_t hb = cfg.heartbeat_interval_ms;
  if (ReadUInt(root, "", "heartbeat_interval_ms", 1000, 300000, hb, r)) {
    cfg.heartbeat_interval_ms = static_cast<std::uint32_t>(hb);
  }

  // ── rng_seed ──────────────────────────────────────────────
  // 0 = 由启动时刻派生。★ 允许 0,因为它有明确语义,不是"没填"。
  std::uint64_t seed = cfg.rng_seed;
  if (ReadUInt(root, "", "rng_seed", 0, 0xFFFFFFFFFFFFFFFFull, seed, r)) {
    cfg.rng_seed = seed;
  }

  // ── log_level ─────────────────────────────────────────────
  if (const json::Value* lv = root.Find("log_level"); lv != nullptr) {
    if (!lv->is_string()) {
      AddError(r, "log_level", "应为字符串");
    } else if (!ParseLogLevel(lv->as_string(), cfg.log_level)) {
      AddError(r, "log_level",
               "只能是 trace / debug / info / warn / error 之一,实际为 \"" +
                   lv->as_string() + "\"");
    }
  }

  // ── tempo(01 §3.2:玩法参数,不是性能参数)──────────────────
  if (const json::Value* tp = root.Find("tempo"); tp != nullptr) {
    if (!tp->is_object()) {
      AddError(r, "tempo", "应为对象");
    } else {
      RejectUnknownKeys(*tp, "tempo",
                        {"tick_hz", "battle_turn_interval_ms",
                         "char_loop_interval_ms"},
                        r);

      // tick_hz 上限 1000:再高单 tick 预算就不足 1 毫秒,
      // 而 01 §10 要求 tick 耗时可观测、有预算。
      std::uint64_t hz = cfg.tempo.tick_hz;
      if (ReadUInt(*tp, "tempo", "tick_hz", 1, 1000, hz, r)) {
        cfg.tempo.tick_hz = static_cast<std::uint32_t>(hz);
      }

      // ⚠️ 战斗回合间隔的上下限是**玩法**判断,不是技术判断:
      //    15 §5.2 实测原版 _BATTLE_TIME 为关 ⇒ 没有原版值可抄,
      //    00 §0 又已认下 ④ 层永远无法验证 ⇒ 这两个界只是"离谱保护",
      //    真值要靠人试。**别把它读成"我们知道正确值在这个区间"。**
      std::uint64_t bt = cfg.tempo.battle_turn_interval_ms;
      if (ReadUInt(*tp, "tempo", "battle_turn_interval_ms", 100, 60000, bt, r)) {
        cfg.tempo.battle_turn_interval_ms = static_cast<std::uint32_t>(bt);
      }

      std::uint64_t cl = cfg.tempo.char_loop_interval_ms;
      if (ReadUInt(*tp, "tempo", "char_loop_interval_ms", 100, 60000, cl, r)) {
        cfg.tempo.char_loop_interval_ms = static_cast<std::uint32_t>(cl);
      }
    }
  }

  // ── modules ───────────────────────────────────────────────
  if (const json::Value* mods = root.Find("modules"); mods != nullptr) {
    if (!mods->is_array()) {
      AddError(r, "modules", "应为字符串数组");
    } else if (mods->as_array().empty()) {
      AddError(r, "modules", "至少要装载一个模块");
    } else {
      std::vector<std::string> names;
      std::set<std::string> seen;
      bool bad = false;
      for (std::size_t i = 0; i < mods->as_array().size(); ++i) {
        const json::Value& item = mods->as_array()[i];
        const std::string path = "modules[" + std::to_string(i) + "]";
        if (!item.is_string()) {
          AddError(r, path, "应为字符串");
          bad = true;
          continue;
        }
        const std::string& name = item.as_string();
        if (!IsKnownModule(name)) {
          AddError(r, path, "未知模块 \"" + name + "\"");
          bad = true;
          continue;
        }
        if (!IsImplementedModule(name)) {
          // ★ 不是"忽略未实现的模块" —— 那会让部署配置写着 social
          //   而实际没装,直到有人用到才发现。
          AddError(r, path,
                   "模块 \"" + name +
                       "\" 属阶段 2,尚未实现;阶段 1.5 只有 world "
                       "(见 00 §9.0.4)");
          bad = true;
          continue;
        }
        if (!seen.insert(name).second) {
          AddError(r, path, "模块 \"" + name + "\" 重复出现");
          bad = true;
          continue;
        }
        names.push_back(name);
      }
      if (!bad) cfg.modules = std::move(names);
    }
  }

  r.ok = r.errors.empty();
  if (r.ok) r.config = std::move(cfg);
  return r;
}

ConfigResult LoadConfigFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    ConfigResult r;
    AddError(r, "<文件>", "读不到配置文件:" + path);
    return r;
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  return ParseConfig(buf.str());
}

}  // namespace sa::platform
