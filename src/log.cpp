#include "log.h"

#include <Arduino.h>

#include <esp_heap_caps.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <freertos/FreeRTOS.h>

namespace logging {
namespace {

char letter(config::LogLevel level) {
  switch (level) {
    case config::LogLevel::kError:
      return 'E';
    case config::LogLevel::kWarn:
      return 'W';
    case config::LogLevel::kInfo:
      return 'I';
    case config::LogLevel::kDebug:
      return 'D';
    default:
      return 'V';
  }
}

#if defined(NET_LOG)
/** The last kNetLogBytes of the log, a ring in PSRAM: taken by the first
 *  line, from boot on. */
std::atomic<char*> s_ring{nullptr};
size_t s_ring_head = 0;  // where the next byte goes
bool s_ring_full = false;
portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;
/** No more of one line than this: a response body logged whole is cut. */
constexpr size_t kRingLineMax = 1024;

char* ring() {
  char* r = s_ring.load();
  if (r != nullptr) {
    return r;
  }
  char* fresh = static_cast<char*>(heap_caps_malloc(
      kNetLogBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (fresh == nullptr) {
    return nullptr;
  }
  if (!s_ring.compare_exchange_strong(r, fresh)) {
    free(fresh);  // another task's line took one first
    return r;
  }
  return fresh;
}

void keep(const char* line, size_t len) {
  char* r = ring();
  if (r == nullptr) {
    return;
  }
  if (len > kRingLineMax) {
    line += len - kRingLineMax;  // its end, with the newline
    len = kRingLineMax;
  }
  taskENTER_CRITICAL(&s_ring_lock);
  const size_t first = len < kNetLogBytes - s_ring_head
                           ? len
                           : kNetLogBytes - s_ring_head;
  memcpy(r + s_ring_head, line, first);
  memcpy(r, line + first, len - first);
  s_ring_head += len;
  if (s_ring_head >= kNetLogBytes) {
    s_ring_head -= kNetLogBytes;
    s_ring_full = true;
  }
  taskEXIT_CRITICAL(&s_ring_lock);
}
#endif

void emit(const char* line, size_t len) {
  Serial.write(reinterpret_cast<const uint8_t*>(line), len);
#if defined(NET_LOG)
  keep(line, len);
#endif
}

}  // namespace

void write(config::LogLevel level, const char* fmt, ...) {
  // Most lines fit here, on the caller's stack. A response body logged after
  // a failed request does not, and gets a PSRAM buffer of its own below.
  char line[256];
  const int prefix = snprintf(line, sizeof(line), "%8lu %c ",
                              static_cast<unsigned long>(millis()),
                              letter(level));
  if (prefix <= 0) {
    return;
  }

  va_list args;
  va_start(args, fmt);
  va_list again;
  va_copy(again, args);
  const int n = vsnprintf(line + prefix, sizeof(line) - prefix, fmt, args);
  va_end(args);
  if (n < 0) {
    va_end(again);
    return;
  }

  const size_t total = static_cast<size_t>(prefix) + static_cast<size_t>(n);
  if (total + 1 < sizeof(line)) {
    line[total] = '\n';
    emit(line, total + 1);
    va_end(again);
    return;
  }

  // Longer than the stack buffer: the whole of it, in one write, so it
  // cannot be split by another task's line.
  char* big = static_cast<char*>(
      heap_caps_malloc(total + 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (big == nullptr) {
    big = static_cast<char*>(malloc(total + 2));
  }
  if (big == nullptr) {
    // No room for it: the truncated line is better than none.
    line[sizeof(line) - 2] = '\n';
    emit(line, sizeof(line) - 1);
    va_end(again);
    return;
  }
  memcpy(big, line, static_cast<size_t>(prefix));
  vsnprintf(big + prefix, total + 2 - prefix, fmt, again);
  va_end(again);
  big[total] = '\n';
  emit(big, total + 1);
  free(big);
}

size_t recent(char* out, size_t out_len) {
  if (out_len == 0) {
    return 0;
  }
  out[0] = '\0';
#if defined(NET_LOG)
  char* r = s_ring.load();
  if (r == nullptr || out_len < 2) {
    return 0;
  }
  // The newest out_len - 1 bytes, copied out under the lock.
  size_t n = 0;
  taskENTER_CRITICAL(&s_ring_lock);
  const size_t held = s_ring_full ? kNetLogBytes : s_ring_head;
  const size_t want = held < out_len - 1 ? held : out_len - 1;
  const size_t start = (s_ring_head + kNetLogBytes - want) % kNetLogBytes;
  const size_t first = want < kNetLogBytes - start ? want
                                                   : kNetLogBytes - start;
  memcpy(out, r + start, first);
  memcpy(out + first, r, want - first);
  n = want;
  taskEXIT_CRITICAL(&s_ring_lock);
  out[n] = '\0';
  // From the first whole line, if the oldest was cut by the ring.
  if (n == kNetLogBytes || n == out_len - 1) {
    const char* nl = static_cast<const char*>(memchr(out, '\n', n));
    if (nl != nullptr) {
      const size_t skip = static_cast<size_t>(nl - out) + 1;
      memmove(out, out + skip, n - skip + 1);
      n -= skip;
    }
  }
  return n;
#else
  return 0;
#endif
}

}  // namespace logging
