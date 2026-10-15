#include "ui/ui.h"
#include "config.h"
#include "log.h"

/**
 * ui/ui.h with no panel behind it: what it shows is narrated by
 * ui/console, which it shares with ui/blind, and it reports no input, so a
 * headless build is a remote that watches rather than one that is used.
 *
 * It exists for two reasons. It is what the firmware links against while a
 * panel is being brought up -- everything above ui/ui.h can be built and
 * exercised before the first pixel -- and it is the proof that the seam is
 * real. Anything src/app/ needs that cannot be answered here is something
 * that leaked.
 */
namespace ui {

// No panel, so nothing in PSRAM to compete with.
size_t psramMallocFromBytes() { return config::kPsramMallocFromBytes; }

bool init(uint8_t) {
  LOG_INFO("UI: headless -- no panel, serial only");
  return true;
}

Input poll(Screen, const services::ha::PlayerState*) { return Input{}; }

unsigned long lastInteractionMs() { return 0; }  // nobody can touch it

bool volumeDragging() { return false; }

}  // namespace ui
