#pragma once

#include <Arduino.h>

#include <cstdint>

/**
 * Where the loop's time goes, for finding the moments the remote stops
 * answering a touch. Each named stretch of the loop keeps its longest run;
 * any stretch over kSlowMs is kept with when it happened. Read as JSON at
 * /perf on the portal; a read starts the "since" maxima afresh.
 */
namespace perf {

constexpr uint32_t kSlowMs = 200;

/** `name` (a string literal) just took `ms`. */
void record(const char* name, uint32_t ms);

/** /perf on the portal. */
void registerPage();

}  // namespace perf

/** Run `expr` and record how long it took under `name`. */
#define PERF_TIMED(name, expr)                       \
  do {                                               \
    const uint32_t perf_t0_ = millis();              \
    expr;                                            \
    perf::record(name, millis() - perf_t0_);         \
  } while (0)
