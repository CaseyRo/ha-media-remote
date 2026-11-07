#include "services/perf.h"

#include <WebServer.h>

#include <cstdio>
#include <cstring>

#include "services/wifi_setup.h"

namespace perf {
namespace {

struct Slot {
  const char* name = nullptr;
  uint32_t max_since = 0;  // since the last read of /perf
  uint32_t max_ever = 0;
  uint32_t slow = 0;       // runs over kSlowMs
};
constexpr int kSlots = 24;
Slot s_slots[kSlots];

struct Slow {
  uint32_t at_s = 0;
  const char* name = nullptr;
  uint32_t ms = 0;
};
constexpr int kKeep = 16;
Slow s_recent[kKeep];
int s_next = 0;
int s_count = 0;

}  // namespace

void record(const char* name, uint32_t ms) {
  Slot* slot = nullptr;
  for (Slot& s : s_slots) {
    if (s.name == name || s.name == nullptr) {
      slot = &s;
      break;
    }
  }
  if (slot == nullptr) {
    return;  // more names than slots: the rest go unmeasured
  }
  slot->name = name;
  slot->max_since = ms > slot->max_since ? ms : slot->max_since;
  slot->max_ever = ms > slot->max_ever ? ms : slot->max_ever;
  if (ms >= kSlowMs) {
    ++slot->slow;
    s_recent[s_next] = {millis() / 1000, name, ms};
    s_next = (s_next + 1) % kKeep;
    s_count = s_count < kKeep ? s_count + 1 : kKeep;
  }
}

void registerPage() {
  wifiAddWebPage("/perf", [](WebServer& server) {
    String body;
    body.reserve(2048);
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"uptime_s\":%lu,\"slots\":{",
             static_cast<unsigned long>(millis() / 1000));
    body += buf;
    bool first = true;
    for (Slot& s : s_slots) {
      if (s.name == nullptr) {
        break;
      }
      snprintf(buf, sizeof(buf), "%s\"%s\":[%lu,%lu,%lu]", first ? "" : ",",
               s.name, static_cast<unsigned long>(s.max_since),
               static_cast<unsigned long>(s.max_ever),
               static_cast<unsigned long>(s.slow));
      body += buf;
      s.max_since = 0;
      first = false;
    }
    body += "},\"slow\":[";
    for (int i = 0; i < s_count; ++i) {
      const Slow& e = s_recent[(s_next - s_count + i + kKeep) % kKeep];
      snprintf(buf, sizeof(buf), "%s[%lu,\"%s\",%lu]", i ? "," : "",
               static_cast<unsigned long>(e.at_s), e.name,
               static_cast<unsigned long>(e.ms));
      body += buf;
    }
    body += "]}";
    server.send(200, "application/json", body);
  });
}

}  // namespace perf
