#include "ui/ui.h"

#include <Arduino.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board/board.h"
#include "config.h"
#include "hardware/audio.h"
#include "hardware/touch.h"
#include "log.h"

/**
 * ui/ui.h on a board whose touch and speaker work but whose screen does not:
 * the AMOLED 1.75 with a dead panel. What would be drawn is narrated to the
 * serial console, as the headless build does; the glass is read for four
 * gestures that need no picture to aim at, each answered with a sound.
 *
 *  - A tap plays or pauses, with two notes rising or falling.
 *  - A swipe across the middle, left or right, skips to the next track or
 *    back to the one before, with three notes stepping up or down.
 *  - A finger drawn round the rim sets the volume, a full turn from silent to
 *    full, clockwise up, ticking at every 5%; twice at either end.
 *  - A press held still is a voice request for as long as it is held.
 *  - A double tap says what is playing.
 *
 * A single tap waits kDoubleTapMs to be sure it is not the first of two, so
 * play and pause land that much after the finger lifts.
 *
 * The panel itself is never started, and is held in reset.
 */
namespace ui {
namespace {

using services::ha::PlaybackState;
using services::ha::PlayerState;

/** A tap followed by another within this is a double tap. */
constexpr unsigned long kDoubleTapMs = 300;
/** A press held this long without moving is a voice request. */
constexpr unsigned long kHoldMs = 500;
/** Movement within this is still a press held still, or a tap. */
constexpr int kStillPx = 24;
/** A volume drag starts in the outer part of the glass: from this fraction
 *  of the radius out. The inner part is for taps and holds. */
constexpr float kRimFraction = 0.55f;
/** How far a press has to go before it is decided whether it is a turn of
 *  the rim or a swipe: far enough for the direction to mean something. */
constexpr int kDecidePx = 30;
/** A turn goes along the rim: its distance round must be this many times
 *  its change in distance from the centre. Less is a swipe across. At first
 *  anything that began in the outer part was a turn, and a swipe across the
 *  middle -- which begins there -- moved the volume a third of the way. */
constexpr float kAlongPerAcross = 1.5f;
/** Volume per radian: a full turn is the whole range. */
constexpr float kLevelPerRadian = 1.0f / (2.0f * static_cast<float>(M_PI));
/** A tick each time the volume crosses a multiple of this. */
constexpr float kTickStep = 0.05f;

constexpr float kCentre = (board::kDisplayDiameter - 1) / 2.0f;
constexpr float kRadius = board::kDisplayDiameter / 2.0f;

/** One line per change rather than per call, as the headless build does. */
char s_last[96] = {};
bool s_blanked = false;

// The press in progress.
bool s_was_down = false;
unsigned long s_down_seen_ms = 0;
/** It has been claimed as a voice request, or as a volume drag. */
bool s_talking = false;
bool s_dragging = false;
bool s_powering = false;  // claimed to switch the volume device on
bool s_swiping = false;   // decided: a swipe, left for the release

// The volume drag.
float s_angle = 0.0f;
float s_level = 0.0f;
int s_tick = 0;
bool s_at_end = false;
unsigned long s_vol_sent_ms = 0;

// A tap waiting to see whether it is the first of two.
bool s_tap_pending = false;
unsigned long s_tap_ms = 0;

unsigned long s_last_interaction_ms = 0;

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

const char* playbackName(PlaybackState state) {
  switch (state) {
    case PlaybackState::kPlaying:  return "playing";
    case PlaybackState::kPaused:   return "paused";
    case PlaybackState::kIdle:     return "idle";
    case PlaybackState::kOff:      return "off";
    default:                       return "unavailable";
  }
}

/** The rules the round screen's volume arc follows, for the same reasons. */
bool volumeAvailable(const PlayerState& state) {
  if (state.volume < 0.0f || state.playback == PlaybackState::kUnavailable ||
      state.playback == PlaybackState::kOff) {
    return false;
  }
  return state.control_features == 0 ||
         (state.control_features & services::ha::kFeatureVolumeSet) != 0;
}

bool powerOnForVolume(const PlayerState& state) {
  return state.control_off &&
         (state.control_features == 0 ||
          (state.control_features & services::ha::kFeatureTurnOn) != 0);
}

float angleOf(int x, int y) {
  return atan2f(y - kCentre, x - kCentre);
}

float radiusOf(int x, int y) {
  return hypotf(x - kCentre, y - kCentre);
}

/** The volume, under a finger going round the rim. Clockwise is up: on the
 *  glass y grows downwards, so the angle grows clockwise. */
void followRim(int x, int y, Input& out) {
  const float a = angleOf(x, y);
  float delta = a - s_angle;
  if (delta > static_cast<float>(M_PI)) {
    delta -= 2.0f * static_cast<float>(M_PI);
  } else if (delta < -static_cast<float>(M_PI)) {
    delta += 2.0f * static_cast<float>(M_PI);
  }
  s_angle = a;
  float level = s_level + delta * kLevelPerRadian;
  level = level < 0.0f ? 0.0f : level > 1.0f ? 1.0f : level;
  s_level = level;

  const bool at_end = level <= 0.0f || level >= 1.0f;
  const int tick = static_cast<int>(floorf(level / kTickStep + 0.5f));
  if (at_end && !s_at_end) {
    hw::audio::cue(hw::audio::Cue::kLimit);
  } else if (tick != s_tick && !at_end) {
    hw::audio::cue(hw::audio::Cue::kTick);
  }
  s_at_end = at_end;
  s_tick = tick;

  if (millis() - s_vol_sent_ms >= config::kVolumeSendIntervalMs) {
    s_vol_sent_ms = millis();
    out.intent = Intent::kSetVolume;
    out.level = s_level;
  }
}

/** A press that is down: does it become a voice request, or a drag round
 *  the rim, and if a drag, where has it got to. */
void whileDown(const PlayerState* state, Input& out) {
  int x = 0;
  int y = 0;
  hw::touchPosition(x, y);
  const int dx = hw::touchDragDx();
  const int dy = hw::touchDragDy();
  const unsigned long now = millis();

  if (s_dragging) {
    followRim(x, y, out);
    return;
  }
  if (s_talking || s_powering || s_swiping) {
    return;
  }

  const int travel = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
  if (travel <= kStillPx && now - s_down_seen_ms >= kHoldMs) {
    hw::touchCancel();  // its lift is not a tap
    s_talking = true;
    s_tap_pending = false;
    out.intent = Intent::kTalkStart;
    LOG_INFO("UI: held -- talk");
    return;
  }

  if (travel < kDecidePx) {
    return;
  }
  const int start_x = x - dx;
  const int start_y = y - dy;
  const float r0 = radiusOf(start_x, start_y);
  float turned = angleOf(x, y) - angleOf(start_x, start_y);
  if (turned > static_cast<float>(M_PI)) {
    turned -= 2.0f * static_cast<float>(M_PI);
  } else if (turned < -static_cast<float>(M_PI)) {
    turned += 2.0f * static_cast<float>(M_PI);
  }
  const float along = fabsf(turned) * r0;
  const float across = fabsf(radiusOf(x, y) - r0);
  if (r0 < kRimFraction * kRadius || along < kAlongPerAcross * across) {
    s_swiping = true;  // across the glass, or begun in the middle
    return;
  }
  hw::touchCancel();  // its lift is not a swipe
  s_tap_pending = false;
  if (state != nullptr && powerOnForVolume(*state)) {
    s_powering = true;
    out.intent = Intent::kPowerOnForVolume;
    return;
  }
  if (state == nullptr || !volumeAvailable(*state)) {
    hw::audio::cue(hw::audio::Cue::kLimit);  // nothing to turn
    s_powering = true;  // claimed, and does nothing more
    return;
  }
  s_dragging = true;
  s_level = state->volume;
  s_tick = static_cast<int>(floorf(s_level / kTickStep + 0.5f));
  s_at_end = false;
  s_vol_sent_ms = 0;
  // From where the drag began: what it has come round already counts.
  s_angle = angleOf(start_x, start_y);
  LOG_INFO("UI: turning the volume from %.2f", s_level);
  followRim(x, y, out);
}

}  // namespace

// Nothing drawn, so nothing scanned out of PSRAM to compete with.
size_t psramMallocFromBytes() { return config::kPsramMallocFromBytes; }

bool init(uint8_t) {
  // The panel stays in reset: nothing will be drawn on it.
  pinMode(board::kDisplayPinRst, OUTPUT);
  digitalWrite(board::kDisplayPinRst, LOW);
  const bool touch = hw::touchInit();
  LOG_INFO("UI: no screen -- touch %s, sounds and voice",
           touch ? "read" : "MISSING");
  return true;
}

Input poll(Screen, const PlayerState* state) {
  Input out;
  const unsigned long now = millis();
  hw::TouchReport report;
  const bool gesture = hw::touchPoll(report);
  const bool down = hw::touchIsDown();

  if (down) {
    s_last_interaction_ms = now;
    if (!s_was_down) {
      s_was_down = true;
      s_down_seen_ms = now;
      // Whatever this press turns out to be, it will want a sound: the
      // amplifier's 150 ms warm-up starts now rather than after it.
      hw::audio::cue(hw::audio::Cue::kWarm);
    }
    whileDown(state, out);
    if (out.intent != Intent::kNone) {
      return out;
    }
  } else if (s_was_down) {
    // The finger has lifted.
    s_was_down = false;
    if (s_talking) {
      s_talking = false;
      out.intent = Intent::kTalkEnd;
      LOG_INFO("UI: let go -- done talking");
      return out;
    }
    s_powering = false;
    s_swiping = false;
    if (s_dragging) {
      // Closed with an exact set: the throttle may have held the last back.
      s_dragging = false;
      out.intent = Intent::kSetVolume;
      out.level = s_level;
      LOG_INFO("UI: volume %.2f", s_level);
      return out;
    }
  }

  // A swipe the rim did not claim: one that began in the middle. Sideways
  // only; up and down mean nothing here.
  if (gesture && (report.event == hw::TouchEvent::kSwipeLeft ||
                  report.event == hw::TouchEvent::kSwipeRight)) {
    const bool next = report.event == hw::TouchEvent::kSwipeLeft;
    hw::audio::cue(next ? hw::audio::Cue::kNext : hw::audio::Cue::kPrevious);
    out.intent = next ? Intent::kNext : Intent::kPrevious;
    LOG_INFO("UI: swipe -- %s", next ? "next" : "previous");
    return out;
  }

  if (gesture && report.event == hw::TouchEvent::kTap) {
    if (s_tap_pending && now - s_tap_ms <= kDoubleTapMs) {
      s_tap_pending = false;
      out.intent = Intent::kSayNowPlaying;
      LOG_INFO("UI: double tap -- say what is playing");
      return out;
    }
    s_tap_pending = true;
    s_tap_ms = now;
  }
  if (s_tap_pending && !down && now - s_tap_ms > kDoubleTapMs) {
    s_tap_pending = false;
    const bool playing =
        state != nullptr && state->playback == PlaybackState::kPlaying;
    hw::audio::cue(playing ? hw::audio::Cue::kPause : hw::audio::Cue::kPlay);
    out.intent = Intent::kPlayPause;
    LOG_INFO("UI: tap -- %s", playing ? "pause" : "play");
  }
  return out;
}

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

unsigned long lastInteractionMs() { return s_last_interaction_ms; }

bool volumeDragging() { return s_dragging; }

}  // namespace ui
