// src/platform/log.cpp —— 结构化日志的输出侧
//
// ⚠️ 本文件是**将来最该被替换的那一个**(01 §12 定的是 spdlog)。
//    ★ 但调用点不会跟着变:调用方给的是 (level, event, fields),
//      不是一句拼好的话。换 Sink 只动这里。
//
// 输出形态:每行一条 `logfmt`(key=value 空格分隔),stderr。
//   选它不是因为好看,是因为它**同时**能被人扫和被机器切,
//   而 1.5 阶段还没有日志收集端可言。

#include "platform/api.h"

#include <cstdio>
#include <string>

namespace sa::platform {
namespace {

const char* LevelName(LogLevel l) noexcept {
  switch (l) {
    case LogLevel::kTrace: return "trace";
    case LogLevel::kDebug: return "debug";
    case LogLevel::kInfo:  return "info";
    case LogLevel::kWarn:  return "warn";
    case LogLevel::kError: return "error";
  }
  return "?";
}

// 事件名。★ 与枚举一一对应,**不**用宏生成 ——
//   漏一条时编译器会在 switch 上告警(-Wswitch 属 -Wall),
//   而宏表漏一条是悄无声息的。
const char* EventName(LogEvent e) noexcept {
  switch (e) {
    case LogEvent::kUnspecified:          return "unspecified";
    case LogEvent::kServerStarting:       return "server_starting";
    case LogEvent::kServerReady:          return "server_ready";
    case LogEvent::kServerStopping:       return "server_stopping";
    case LogEvent::kConfigLoaded:         return "config_loaded";
    case LogEvent::kConfigRejected:       return "config_rejected";
    case LogEvent::kModuleLoaded:         return "module_loaded";
    case LogEvent::kConnectionAccepted:   return "connection_accepted";
    case LogEvent::kConnectionClosed:     return "connection_closed";
    case LogEvent::kHandshakeAccepted:    return "handshake_accepted";
    case LogEvent::kHandshakeRejected:    return "handshake_rejected";
    case LogEvent::kFrameRejected:        return "frame_rejected";
    case LogEvent::kSessionStateChanged:  return "session_state_changed";
    case LogEvent::kTickBudgetExceeded:   return "tick_budget_exceeded";
    case LogEvent::kBattleStarted:        return "battle_started";
    case LogEvent::kBattleTurnResolved:   return "battle_turn_resolved";
    case LogEvent::kBattleFinished:       return "battle_finished";
    case LogEvent::kBattleSeed:           return "battle_seed";
    case LogEvent::kBattleEventsTruncated:return "battle_events_truncated";
  }
  return "unknown";
}

// logfmt 的值:含空格或引号就加引号。
void AppendValue(std::string& out, std::string_view v) {
  bool needs_quote = v.empty();
  for (const char c : v) {
    if (c == ' ' || c == '"' || c == '=' || c == '\n' || c == '\t') {
      needs_quote = true;
      break;
    }
  }
  if (!needs_quote) {
    out.append(v.data(), v.size());
    return;
  }
  out.push_back('"');
  for (const char c : v) {
    if (c == '"' || c == '\\') out.push_back('\\');
    if (c == '\n') { out.append("\\n"); continue; }
    if (c == '\t') { out.append("\\t"); continue; }
    out.push_back(c);
  }
  out.push_back('"');
}

}  // namespace

void Logger::Log(LogLevel level, LogEvent event,
                 std::initializer_list<LogField> fields) const {
  if (!Enabled(level)) return;

  std::string line;
  line.reserve(128);
  line.append("level=").append(LevelName(level));
  line.append(" event=").append(EventName(event));
  line.append(" code=").append(std::to_string(static_cast<std::uint16_t>(event)));

  for (const LogField& f : fields) {
    line.push_back(' ');
    line.append(f.key());
    line.push_back('=');
    switch (f.kind()) {
      case LogField::Kind::kInt:
        line.append(std::to_string(f.as_int()));
        break;
      case LogField::Kind::kUInt:
        line.append(std::to_string(f.as_uint()));
        break;
      case LogField::Kind::kBool:
        line.append(f.as_bool() ? "true" : "false");
        break;
      case LogField::Kind::kStr:
        AppendValue(line, f.as_str());
        break;
    }
  }
  line.push_back('\n');

  // stderr 无缓冲语义 ⇒ 崩溃时最后几行不会丢。
  // ⚠️ 这在 1.5 是对的(单进程、低频事件);真正上量时要换成异步 Sink,
  //    否则日志会成为 01 §2「主线程绝不允许阻塞」的破口。
  std::fwrite(line.data(), 1, line.size(), stderr);
  ++emitted_;
}

}  // namespace sa::platform
