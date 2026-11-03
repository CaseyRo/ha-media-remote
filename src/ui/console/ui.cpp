#include "ui/ui.h"
#include "log.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

/**
 * What a remote without a picture shows, narrated to the serial console: the
 * half of ui/ui.h that both screenless builds share.
 *
 * The remote is perfectly capable of running without a display: it polls Home
 * Assistant, tracks the player, honours the idle timer and sends every
 * service call it would otherwise send. All that is missing is somewhere to
 * put the picture, and this is a line in the log instead. Nothing is ever
 * chosen here either -- a network, a player, a setting -- as there is no
 * screen to choose on.
 *
 * Each build adds the other half, which is where they differ: init(), poll(),
 * lastInteractionMs() and volumeDragging(). ui/headless reads no input at
 * all; ui/blind reads gestures off the glass of a panel that shows nothing.
 */
namespace ui {
namespace {

/** One line per change rather than per call: several of these land on every
 *  loop pass, and a console scrolling at that rate is no more readable than
 *  no console at all. */
char s_last[96] = {};
bool s_blanked = false;

void say(const char* fmt, ...) {
  char line[sizeof(s_last)];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  if (strcmp(line, s_last) == 0) {
    return;
  }
  snprintf(s_last, sizeof(s_last), "%s", line);
  LOG_INFO("UI: %s", line);
}

const char* playbackName(services::ha::PlaybackState state) {
  switch (state) {
    case services::ha::PlaybackState::kPlaying:  return "playing";
    case services::ha::PlaybackState::kPaused:   return "paused";
    case services::ha::PlaybackState::kIdle:     return "idle";
    case services::ha::PlaybackState::kOff:      return "off";
    default:                                     return "unavailable";
  }
}

}  // namespace

void showNowPlaying(const services::ha::PlayerState& state) {
  say("%s | %s - %s | vol %.2f", playbackName(state.playback),
      state.subtitle, state.title, state.volume);
}

void showBrowse() { say("browse list"); }
void showSearch() { say("search"); }
void showStatus(const DeviceStatus& s) {
  say("status: %s, player %s%s%s%s%s, %s up %lu s, %s ch %d, %d dBm, "
      "tx %.1f dBm",
      s.hostname, s.player, s.control[0] ? ", volume " : "", s.control,
      s.input[0] ? ", input " : "", s.input, s.ip,
      s.uptime_s, s.ssid, s.channel, s.rssi, s.tx_dbm);
}
void showWifi(const char* note, const char* current) {
  say("wifi networks%s%s (on %s)", note[0] ? ": " : "", note,
      current[0] ? current : "none");
}
void showWifiNetworks(const WifiNetwork*, int count) {
  say("wifi: %d networks", count);
}
// Nothing is ever chosen without a screen to choose on.
const char* wifiChosenSsid() { return ""; }
const char* wifiPassword() { return ""; }
void showPlayers(const char* note) {
  say("choose a player%s%s", note[0] ? ": " : "", note);
}
// Nothing is ever chosen without a screen to choose on.
const char* playerChosen() { return ""; }
// Nothing is tapped either, so the rows go nowhere.
void settingsBegin(const char* title, const char* note) {
  say("settings: %s", note[0] ? note : title);
}
void settingsAdd(const char*, const char*, SettingStyle) {}
void showSettings(int) {}
void showSettingPressed(int, bool) {}
void showIsolate(const IsolateCard& card) {
  say("isolate: %s (%d rooms)", card.title, card.room_count);
}
void showRestarting(const char* why) { say("restarting: %s", why); }
void showSearchResults() { say("search results"); }
void showSearching() { say("searching..."); }
void showVoiceStarting(const char* words) { say("starting %s", words); }
void showToast(const char* text, uint32_t) { say("toast: %s", text); }
bool toastExpired() { return false; }
void showLoadingList() { say("loading library"); }
void showLoading(const char* what) { say("loading %s", what); }

void showCommandPending(Intent, bool) {}

void showNeedsHaSetup() { say("needs Home Assistant setup"); }
void showHaSignIn(const char*, const char* url, const char* name) {
  say("link to %s: sign in at %s", name, url);
}
void showNoPlayer() { say("no player selected"); }
void showHaUnreachable(const char* detail) { say("HA unreachable: %s", detail); }
void showHaTokenRejected() { say("HA token rejected, tap to link again"); }
void showPortal() { say("setup portal"); }
void showConnecting(const char* ssid) { say("connecting to %s", ssid); }
void tickConnecting() {}
void showConnectFailed() { say("no Wi-Fi"); }
void showWifiLost() { say("no Wi-Fi: signal lost"); }
void showSettingsCleared() { say("settings cleared"); }

void refreshElapsed(const services::ha::PlayerState&) {}

bool isBlanked() { return s_blanked; }

void blank() {
  if (!s_blanked) {
    s_blanked = true;
    LOG_INFO("UI: stood down");
  }
}

void wake() {
  if (s_blanked) {
    s_blanked = false;
    s_last[0] = '\0';  // say the screen again once there is one
    LOG_INFO("UI: awake");
  }
}

// Artwork is pixels, and there are none. The app still calls these on every
// track change, which costs nothing here and keeps the call sites honest.
void requestArtwork(const char*) {}
bool artworkPending(const char*) { return false; }
bool takeArtworkFinished() { return false; }
void clearArtwork() {}

bool idleWork(Screen) { return false; }



}  // namespace ui
