#include "ui/ui.h"

#include <Arduino.h>

#include <cmath>
#include <cstdlib>

#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "board/board.h"
#include "config.h"
#include "log.h"
#include "hardware/battery.h"
#include "hardware/boot_button.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "hardware/encoder.h"
#include "hardware/touch.h"
#include "services/browse.h"
#include "services/search.h"
#include "services/perf.h"
#include "services/wifi_setup.h"
#include "ui/artwork.h"
#include "ui/back_button.h"
#include "ui/browse_list.h"
#include "ui/canvas.h"
#include "ui/cover_art.h"
#include "ui/now_playing.h"
#include "ui/paint.h"
#include "ui/isolate_card.h"
#include "ui/peers_chip.h"
#include "ui/player_pick.h"
#include "ui/search.h"
#include "ui/settings_list.h"
#include "ui/status_screens.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "ui/wifi_join.h"

/**
 * The round panel's half of ui/ui.h.
 *
 * Everything here is about a circle: where the transport buttons sit, that a
 * volume swipe runs sideways across the top because the arc follows the
 * bezel, that a list is clipped to the chord at its own y. A square panel
 * would answer the same calls differently and src/app/ would not notice.
 */
namespace ui {
namespace {

using services::ha::PlayerState;
using Action = now_playing::Action;

// --- Volume drag -----------------------------------------------------------
/** The finger owns the level while it is down. */
bool s_vol_dragging = false;
/** The press in progress was a volume swipe on a device that was off, and
 *  has asked for it to be switched on; it does nothing more until it lifts. */
bool s_vol_powering = false;
float s_vol_level = 0.0f;
float s_vol_drawn = -1.0f;
int s_vol_last_x = 0;
unsigned long s_vol_sent_ms = 0;
/** The finger is seeking along the progress line, and where to, 0..1. The
 *  drag's state is s_drag_state: a seek and a volume drag are never both
 *  under one finger. */
bool s_seek_dragging = false;
float s_seek_fraction = -1.0f;
/** When the drag was claimed, and the x the finger landed at: for telling a
 *  flick from a drag when it lifts. */
unsigned long s_vol_claimed_ms = 0;
int s_vol_landed_x = 0;
/** Snapshot taken when the slider was grabbed, reused for the whole drag:
 *  mute and the feature bits cannot change under a finger. */
PlayerState s_drag_state;

/** Smallest level change worth a repaint: one step of the 252 degree arc that
 *  moves the knob about a pixel at the 240 px baseline. */
constexpr float kVolumeRedrawEpsilon = 0.004f;

// --- List scrolling --------------------------------------------------------
/** One set of state: the browse list and the search results are never on
 *  screen at the same time. */
bool s_list_dragging = false;
int s_list_last_y = 0;
/** Display px per millisecond, signed with the finger. */
float s_list_velocity = 0.0f;
unsigned long s_list_move_ms = 0;
unsigned long s_list_glide_ms = 0;
/** When the list last moved, by drag or by glide, for kListTapGuardMs. */
unsigned long s_list_moved_ms = 0;
/** When the list was last actually repainted, for board::kListRedrawMinMs. */
unsigned long s_list_draw_ms = 0;
/** The finger has moved the list somewhere the glass has not caught up with
 *  yet. Held until the ration allows a repaint, and flushed on release so the
 *  list never settles a few pixels from where it was left. */
bool s_list_redraw_pending = false;

// --- The knob, on a board with one ------------------------------------------
/** A turn owns the level, as a finger does, until the knob has rested for
 *  kKnobSettleMs. */
bool s_knob_adjusting = false;
float s_knob_level = 0.0f;
unsigned long s_knob_turned_ms = 0;
/** The knob scrolled a list the glass has not caught up with yet. */
bool s_knob_redraw_pending = false;
/** Volume per click: fifty clicks from silent to full. */
constexpr float kKnobVolumeStep = 0.02f;
/** How long the knob rests before a turn is over and the final level sent. */
constexpr unsigned long kKnobSettleMs = 400;
/** A list moves a row per click. */
constexpr int kKnobScrollPx = theme::kListRowHeight + theme::kListRowGap;
/** When the knob last asked for the volume device to be switched on, and
 *  how long it waits before asking again while the state still says off. */
unsigned long s_knob_power_ms = 0;
constexpr unsigned long kKnobPowerRetryMs = 3000;

/** What a scrollable screen offers the drag handler. Function pointers rather
 *  than virtuals: there are two, both known at compile time, and neither
 *  wants a vtable. */
struct ScrollableList {
  bool (*scrollByPx)(int delta);
  bool (*atScrollLimit)(int direction);
  void (*redraw)();
};

// redrawRows() rather than draw(): a drag only moves the rows and the
// indicator, and repainting the rest costs more than the scroll can afford.
constexpr ScrollableList kBrowseList{browse_list::scrollByPx,
                                     browse_list::atScrollLimit,
                                     browse_list::redrawRows};
constexpr ScrollableList kSearchResults{search::scrollByPx,
                                        search::atScrollLimit,
                                        search::redrawResults};
constexpr ScrollableList kWifiNetworks{wifi_join::scrollByPx,
                                       wifi_join::atScrollLimit,
                                       wifi_join::draw};
constexpr ScrollableList kPlayerList{player_pick::scrollByPx,
                                     player_pick::atScrollLimit,
                                     player_pick::draw};
constexpr ScrollableList kSettingsList{settings_list::scrollByPx,
                                       settings_list::atScrollLimit,
                                       settings_list::draw};

constexpr int kDragSlopPx =
    static_cast<int>(board::kTouchTapSlopPx * board::kUiScale + 0.5f);

/** How near the edge of the circle a press lands to be at the rim. There a
 *  finger coming on from the bezel -- a swipe down begun at the very top --
 *  is read crooked at first: on the DFRobot, 21 px along the rim and 1 px
 *  down before it went down at all. */
constexpr int kRimPx = theme::px(10);

/** Whether (x, y) is at the rim of the circle. */
bool atRim(int x, int y) {
  const int dx = x - theme::kCenterX;
  const int dy = y - theme::kCenterY;
  const int inner = theme::kRadius - kRimPx;
  return dx * dx + dy * dy >= inner * inner;
}

/** A sideways swipe in the volume's half that lifts this soon after it was
 *  claimed, having gone at least kFlickMinPx, is a flick: the swipe to the
 *  search or the inputs, as lower down, not a volume change. A drag meant
 *  for the volume moves slower than that, or not as far. Nothing goes to
 *  the volume until this long has passed, so a flick never touches it. */
constexpr unsigned long kFlickMaxMs = 250;
/** About a fifth of the panel, at the 240 px design size. */
constexpr int kFlickMinPx = static_cast<int>(48 * board::kUiScale + 0.5f);

/** Whether a volume swipe or a turn of the knob should switch the volume
 *  device on rather than set its level: it is off, and it can be switched on
 *  -- a device reporting no features at all counting as one that can, as
 *  everywhere else. One that cannot gets the level as it is, as before. */
bool powerOnForVolume(const PlayerState& state) {
  return state.control_off &&
         (state.control_features == 0 ||
          (state.control_features & services::ha::kFeatureTurnOn) != 0);
}

/** Drive the volume arc from a horizontal swipe across the top of the panel.
 *
 *  The touch driver only reports gestures once a press completes, which is no
 *  use to a slider, so this tracks the live position itself and cancels the
 *  press so its release is not also read as a swipe.
 *
 *  The press may start anywhere in the top half rather than on the 7 px
 *  track, because landing a fingertip on that track is a precision task on a
 *  1.28" circle and this is the control most used without looking. And the
 *  movement is *relative*: the level shifts by however far the finger went,
 *  so a swipe adjusts from wherever the volume already was instead of jumping
 *  it to whatever angle the finger points at.
 *
 *  Only a predominantly horizontal press is claimed. A vertical one is left
 *  alone so swipe-up still opens the browse list. And a quick flick, rather
 *  than a drag, is the swipe to the search or the inputs, as anywhere else
 *  on now playing: see kFlickMaxMs.
 *
 *  Returns true when the caller should be told the level changed, or about
 *  the swipe a flick was. */
bool driveVolume(Screen screen, const PlayerState* state, Input& out) {
  if (screen != Screen::kNowPlaying) {
    s_vol_dragging = false;
    s_vol_powering = false;
    return false;
  }

  if (!hw::touchIsDown()) {
    s_vol_powering = false;
    if (!s_vol_dragging) {
      return false;
    }
    // A flick: the swipe it would have been lower down. Nothing was sent to
    // the volume yet -- see kFlickMaxMs -- so there is nothing to undo but
    // the knob, which the screen this opens covers anyway.
    const int travel = s_vol_last_x - s_vol_landed_x;
    if (millis() - s_vol_claimed_ms < kFlickMaxMs &&
        abs(travel) >= kFlickMinPx) {
      s_vol_dragging = false;
      out.intent = travel < 0 ? Intent::kOpenSearch : Intent::kOpenSources;
      return true;
    }
    // Always close with an authoritative set: the throttle below may have
    // skipped the last position the finger actually rested on. The app is
    // told before the flag drops, because between the two there is no guard
    // on the volume at all and a poll landing in that window is exactly the
    // one carrying the pre-change level.
    s_vol_dragging = false;
    out.intent = Intent::kSetVolume;
    out.level = s_vol_level;
    return true;
  }

  int x = 0;
  int y = 0;
  hw::touchPosition(x, y);

  if (s_vol_powering) {
    return false;
  }
  if (!s_vol_dragging) {
    const int travel_x = hw::touchDragDx();
    const int travel_y = hw::touchDragDy();
    if (abs(travel_x) < kDragSlopPx || abs(travel_x) <= abs(travel_y)) {
      return false;  // not committed sideways yet, or it is a vertical swipe
    }
    // Where the finger landed, not where it is now: a swipe that began in the
    // top half stays a volume swipe even once it has wandered below centre.
    if (!now_playing::volumeSwipeRegion(x - travel_x, y - travel_y)) {
      return false;
    }
    // One that landed at the rim may be a swipe down read crooked: it has to
    // go plainly sideways, twice the slop and twice as far as down.
    if (atRim(x - travel_x, y - travel_y) &&
        (abs(travel_x) < 2 * kDragSlopPx ||
         abs(travel_x) < 2 * abs(travel_y))) {
      return false;
    }
    // A device that is off: the swipe switches it on, as a turn of the knob
    // does, and the next one sets the level. The press is claimed, so its
    // release is not read as a swipe to the search or the inputs.
    if (state != nullptr && powerOnForVolume(*state)) {
      hw::touchCancel();
      s_vol_powering = true;
      out.intent = Intent::kPowerOnForVolume;
      return true;
    }
    if (state == nullptr || !now_playing::volumeAvailable(*state)) {
      return false;
    }

    s_drag_state = *state;
    hw::touchCancel();
    s_vol_dragging = true;
    s_vol_sent_ms = 0;
    // Start from where the volume actually is. The slop travelled getting
    // here is deliberately not applied, so the arc does not jump on grab.
    s_vol_level = state->volume;
    s_vol_drawn = -1.0f;
    s_vol_last_x = x;
    s_vol_claimed_ms = millis();
    s_vol_landed_x = x - travel_x;
  } else {
    const int moved = x - s_vol_last_x;
    s_vol_last_x = x;
    s_vol_level += moved * now_playing::volumeLevelPerPx();
    if (s_vol_level < 0.0f) {
      s_vol_level = 0.0f;
    } else if (s_vol_level > 1.0f) {
      s_vol_level = 1.0f;
    }
  }

  // This runs on every loop pass, far faster than the panel or the eye needs.
  // Repaint only when the knob would actually move by a pixel's worth.
  if (fabsf(s_vol_level - s_vol_drawn) >= kVolumeRedrawEpsilon) {
    s_vol_drawn = s_vol_level;
    s_drag_state.volume = s_vol_level;
    now_playing::refreshVolume(s_drag_state, s_vol_level);
  }

  // Held back while the swipe could still turn out to be a flick.
  if (millis() - s_vol_claimed_ms >= kFlickMaxMs &&
      millis() - s_vol_sent_ms >= config::kVolumeSendIntervalMs) {
    s_vol_sent_ms = millis();
    out.intent = Intent::kSetVolume;
    out.level = s_vol_level;
    return true;
  }
  return false;
}

/** Seek along the progress line: a sideways drag that starts on it, or on the
 *  elapsed chip over it (now_playing::seekRegion()), as the volume's starts
 *  in the top half. The line and the chip follow the finger, showing the
 *  time it points at; nothing is sent until it lifts, and then only the
 *  one seek. A vertical press is left alone, for swipe-up, as is any press
 *  elsewhere in the lower half, for the swipes to the search and the inputs.
 *
 *  Returns true when the caller should be told to seek. */
bool driveSeek(Screen screen, const PlayerState* state, Input& out) {
  if (screen != Screen::kNowPlaying) {
    s_seek_dragging = false;
    return false;
  }
  if (!hw::touchIsDown()) {
    if (!s_seek_dragging) {
      return false;
    }
    s_seek_dragging = false;
    out.intent = Intent::kSeek;
    out.level = s_seek_fraction;
    return true;
  }

  int x = 0;
  int y = 0;
  hw::touchPosition(x, y);
  if (!s_seek_dragging) {
    const int travel_x = hw::touchDragDx();
    const int travel_y = hw::touchDragDy();
    if (abs(travel_x) < kDragSlopPx || abs(travel_x) <= abs(travel_y)) {
      return false;
    }
    if (state == nullptr ||
        !now_playing::seekRegion(x - travel_x, y - travel_y, *state)) {
      return false;
    }
    s_drag_state = *state;
    hw::touchCancel();
    s_seek_dragging = true;
    s_seek_fraction = -1.0f;
  }

  // The preview: the line and the chip at the time under the finger, held
  // there -- sampled_ms 0 stops the interpolation that would run it on.
  const float f = now_playing::seekFraction(x, y);
  if (fabsf(f - s_seek_fraction) >= kVolumeRedrawEpsilon) {
    s_seek_fraction = f;
    s_drag_state.position_s = f * s_drag_state.duration_s;
    s_drag_state.sampled_ms = 0;
    now_playing::refreshElapsed(s_drag_state);
  }
  return false;
}

/** Scroll a list under the finger, and let it glide when released.
 *
 *  Only a predominantly *vertical* press is taken. A horizontal one is left
 *  alone so it can still resolve as the swipe-right that leaves the list. */
void driveList(const ScrollableList& list, bool active) {
  /** Below this the glide is over; above it, a flick keeps going. px/ms. */
  constexpr float kGlideStopSpeed = 0.02f;
  /** Fraction of speed kept per frame. Lower stops sooner. */
  constexpr float kGlideDecay = 0.88f;
  constexpr unsigned long kGlideFrameMs = 16;

  if (!active) {
    s_list_dragging = false;
    s_list_velocity = 0.0f;
    return;
  }

  if (hw::touchIsDown()) {
    int x = 0;
    int y = 0;
    hw::touchPosition(x, y);

    if (!s_list_dragging) {
      // A finger on a gliding list catches it. Only stops it: the press that
      // did so is refused as a tap (see listTapAllowed()), and leaving the
      // velocity set would let the glide carry on once it lifts.
      if (s_list_velocity != 0.0f) {
        s_list_velocity = 0.0f;
        s_list_moved_ms = millis();
      }
      const int travel_x = hw::touchDragDx();
      const int travel_y = hw::touchDragDy();
      if (abs(travel_y) < kDragSlopPx || abs(travel_y) <= abs(travel_x)) {
        return;  // not committed vertically yet, or it is a sideways swipe
      }
      hw::touchCancel();
      s_list_dragging = true;
      s_list_velocity = 0.0f;
      s_list_last_y = y;
      s_list_move_ms = millis();
      return;
    }

    const int dy = y - s_list_last_y;
    if (dy == 0) {
      return;
    }
    const unsigned long now = millis();
    const unsigned long dt = now - s_list_move_ms;
    if (dt > 0) {
      // Smoothed, so one jittery sample cannot fling the list.
      const float sample = static_cast<float>(dy) / static_cast<float>(dt);
      s_list_velocity = s_list_velocity * 0.6f + sample * 0.4f;
    }
    s_list_last_y = y;
    s_list_move_ms = now;

    // Dragging down moves the content down, which is scrolling *up* the list.
    // Scrolled on every sample so the position follows the finger exactly;
    // repainted only as often as the board can finish one, because asking for
    // repaints faster than that does not make the list smoother -- it queues
    // them, and on a panel scanned continuously out of PSRAM it starves the
    // scan-out of the bus and breaks up the picture instead.
    if (list.scrollByPx(-dy)) {
      s_list_moved_ms = now;
      s_list_redraw_pending = true;
    }
    if (s_list_redraw_pending &&
        now - s_list_draw_ms >= board::kListRedrawMinMs) {
      s_list_draw_ms = now;
      s_list_redraw_pending = false;
      list.redraw();
    }
    return;
  }

  if (s_list_dragging) {
    s_list_dragging = false;
    s_list_glide_ms = millis();
    // Whatever the ration held back, now that there is no finger to keep up
    // with -- otherwise the list rests wherever the last repaint happened to
    // land rather than where it was released.
    if (s_list_redraw_pending) {
      s_list_redraw_pending = false;
      s_list_draw_ms = millis();
      list.redraw();
    }
    // A finger that stopped before lifting meant to stop. Timed by the
    // touch samples, not by when this pass ran: on a board whose loop takes
    // longer than this between passes -- a list repaint on a framebuffer
    // panel -- the time since this last saw the finger move was always
    // longer, and every fling stopped dead on release.
    if (hw::touchStillBeforeLiftMs() > 120) {
      s_list_velocity = 0.0f;
    }
  }

  if (s_list_velocity == 0.0f) {
    return;
  }
  // The glide is rationed by the same ceiling as the drag: it repaints the
  // whole list per step, so on a board where that costs more than a frame it
  // would starve the scan-out exactly as a drag does.
  constexpr unsigned long kFrameMs = board::kListRedrawMinMs > kGlideFrameMs
                                         ? board::kListRedrawMinMs
                                         : kGlideFrameMs;
  const unsigned long now = millis();
  const unsigned long elapsed = now - s_list_glide_ms;
  if (elapsed < kFrameMs) {
    return;
  }
  s_list_glide_ms = now;

  // Distance and decay both taken from the time that actually passed rather
  // than from a nominal frame, so a slower repaint makes the glide coarser
  // without also making it travel further or last longer.
  //
  // Capped, though, because that reasoning holds for a slow repaint and not
  // for a stall. Nothing here bounds `elapsed`, and the loop can lose most of
  // a second to a blocking fetch; at a typical fling speed that multiplied out
  // to a single step of more than a screen height, after which the decay below
  // -- 0.88 to the power of the same number -- killed the velocity outright.
  // The list teleported and stopped dead. Half a screen is the most a step may
  // cover, so a fling always lands somewhere you watched it go.
  constexpr int kMaxGlideStepPx = board::kDisplayHeight / 2;
  int step = static_cast<int>(-s_list_velocity * elapsed);
  if (step > kMaxGlideStepPx) {
    step = kMaxGlideStepPx;
  } else if (step < -kMaxGlideStepPx) {
    step = -kMaxGlideStepPx;
  }
  if (step != 0 && list.scrollByPx(step)) {
    s_list_moved_ms = now;
    s_list_draw_ms = now;
    list.redraw();
  } else if (list.atScrollLimit(step)) {
    s_list_velocity = 0.0f;
    return;
  }

  s_list_velocity *=
      powf(kGlideDecay, static_cast<float>(elapsed) / kGlideFrameMs);
  if (fabsf(s_list_velocity) < kGlideStopSpeed) {
    s_list_velocity = 0.0f;
  }
}

/** The list on `screen` that scrolls, or nullptr when it has none. */
const ScrollableList* scrollableOn(Screen screen) {
  switch (screen) {
    case Screen::kBrowse:
      return &kBrowseList;
    case Screen::kSearch:
      // Only the results scroll; the keyboard has nothing to move, and
      // scrollByPx() says so.
      return search::showingResults() ? &kSearchResults : nullptr;
    case Screen::kWifi:
      return wifi_join::onList() ? &kWifiNetworks : nullptr;
    case Screen::kPlayers:
      return &kPlayerList;
    case Screen::kSettings:
    case Screen::kSources:
      return &kSettingsList;
    default:
      return nullptr;
  }
}

void driveListScrolling(Screen screen) {
  const ScrollableList* list = scrollableOn(screen);
  driveList(list != nullptr ? *list : kBrowseList, list != nullptr);
}

/** Turn the knob: the volume on now playing, a row a click on a screen with a
 *  list, nothing elsewhere. A finger on the glass comes first, and the knob
 *  waits while one is down.
 *
 *  Returns true when the caller should be told the level changed. */
bool driveKnob(Screen screen, const PlayerState* state, Input& out) {
  const int detents = hw::encoderTakeDetents();
  const unsigned long now = millis();

  // A turn is over once the knob has rested, or now playing has gone. It
  // closes with an authoritative set, told to the app before the flag drops,
  // as a drag's release is: the throttle below may have held back the last
  // level, and a poll landing between the two would carry the old one.
  if (s_knob_adjusting &&
      (screen != Screen::kNowPlaying ||
       (detents == 0 && now - s_knob_turned_ms >= kKnobSettleMs))) {
    s_knob_adjusting = false;
    out.intent = Intent::kSetVolume;
    out.level = s_knob_level;
    return true;
  }

  const ScrollableList* list = scrollableOn(screen);
  if (list == nullptr) {
    s_knob_redraw_pending = false;
  }
  if (detents != 0 && !hw::touchIsDown() && !s_vol_dragging) {
    if (screen == Screen::kNowPlaying) {
      // The volume of a device that is off goes nowhere: a turn switches it
      // on first, as play would, and the turns after the state says it is on
      // set the level. One request, however many clicks, until the state
      // catches up or kKnobPowerRetryMs passes.
      if (state != nullptr && powerOnForVolume(*state)) {
        if (s_knob_power_ms == 0 ||
            now - s_knob_power_ms >= kKnobPowerRetryMs) {
          s_knob_power_ms = now;
          out.intent = Intent::kPowerOnForVolume;
          return true;
        }
        return false;
      }
      s_knob_power_ms = 0;
      if (state == nullptr || !now_playing::volumeAvailable(*state)) {
        return false;
      }
      if (!s_knob_adjusting) {
        s_knob_adjusting = true;
        s_drag_state = *state;
        s_knob_level = state->volume;
        s_vol_sent_ms = 0;
      }
      s_knob_turned_ms = now;
      s_knob_level += detents * kKnobVolumeStep;
      if (s_knob_level < 0.0f) {
        s_knob_level = 0.0f;
      } else if (s_knob_level > 1.0f) {
        s_knob_level = 1.0f;
      }
      s_drag_state.volume = s_knob_level;
      now_playing::refreshVolume(s_drag_state, s_knob_level);
      if (now - s_vol_sent_ms >= config::kVolumeSendIntervalMs) {
        s_vol_sent_ms = now;
        out.intent = Intent::kSetVolume;
        out.level = s_knob_level;
        return true;
      }
      return false;
    }
    // Clockwise goes down the list, as a finger dragging up does.
    if (list != nullptr && list->scrollByPx(detents * kKnobScrollPx)) {
      s_list_velocity = 0.0f;
      s_list_moved_ms = now;
      s_knob_redraw_pending = true;
    }
  }
  // Repainted only as often as the board can finish one; see driveList().
  if (s_knob_redraw_pending && list != nullptr && !hw::touchIsDown() &&
      now - s_list_draw_ms >= board::kListRedrawMinMs) {
    s_knob_redraw_pending = false;
    s_list_draw_ms = now;
    list->redraw();
  }
  return false;
}

/** A press of the knob, off now playing, as the gesture it stands for: on a
 *  card a tap, which is all a card takes; anywhere else a swipe right, which
 *  is back on every screen that has one. */
hw::TouchReport knobPressAsTouch(Screen screen) {
  hw::TouchReport report;
  report.event = screen == Screen::kMessage ? hw::TouchEvent::kTap
                                            : hw::TouchEvent::kSwipeRight;
  report.x = board::kDisplayWidth / 2;
  report.y = board::kDisplayHeight / 2;
  report.down_ms = millis();
  report.unwatched_ms = 0;
  return report;
}

/** Whether a tap on a scrolling list is a tap, rather than a press that
 *  caught a moving list or a swipe that was never seen to move. */
bool listTapAllowed(const hw::TouchReport& report) {
  if (report.unwatched_ms > config::kTouchWatchGapMs) {
    LOG_DEBUG("UI: list tap ignored, unwatched for %lu ms",
                  report.unwatched_ms);
    return false;
  }
  if (s_list_moved_ms != 0 &&
      static_cast<long>(report.down_ms -
                        (s_list_moved_ms + config::kListTapGuardMs)) < 0) {
    LOG_DEBUG("UI: list tap ignored, the list was moving");
    return false;
  }
  return true;
}

Input browseTouch(const hw::TouchReport& report) {
  Input out;
  if (report.event == hw::TouchEvent::kTap && !listTapAllowed(report)) {
    return out;
  }
  switch (report.event) {
    case hw::TouchEvent::kTap: {
      int row = -1;
      const auto result = browse_list::handleTap(report.x, report.y, row);
      if (result == browse_list::Result::kSelected) {
        // Acknowledge first. play_media on an artist has taken four seconds
        // and more on a live instance, and a screen that simply stops for
        // that long reads as a device that missed the tap.
        browse_list::showStarting(row);
        out.intent = Intent::kPlayBrowseRow;
        // Resolved here: a row is a layout idea, an item is not.
        out.index = browse_list::itemForRow(row);
      } else if (result == browse_list::Result::kDismissed) {
        out.intent = Intent::kBackToNowPlaying;
      }
      break;
    }
    case hw::TouchEvent::kSwipeLeft:
      out.intent = Intent::kOpenSearch;
      break;
    // Vertical swipes never arrive here: driveList() claims the press as soon
    // as it commits to an axis and cancels the gesture, so the list has
    // already moved with the finger.
    case hw::TouchEvent::kSwipeRight:
      out.intent = Intent::kBackToNowPlaying;
      break;
    default:
      break;
  }
  return out;
}

Input searchTouch(const hw::TouchReport& report) {
  Input out;
  // The back button does what a swipe either way does, results or keyboard:
  // nothing on this screen moves sideways, so a swipe left -- the one that
  // opened it -- goes back too, as on the inputs.
  const bool back =
      report.event == hw::TouchEvent::kSwipeRight ||
      report.event == hw::TouchEvent::kSwipeLeft ||
      (report.event == hw::TouchEvent::kTap &&
       back_button::hit(report.x, report.y));
  if (back) {
    // Back out one step at a time: results to keyboard, then keyboard to
    // wherever the search was opened from, which the app knows.
    if (search::backToKeyboard()) {
      search::draw();
    } else {
      out.intent = Intent::kSearchBack;
    }
    return out;
  }
  // Only the results scroll. The keyboard is typed on, and a key press must
  // not be second-guessed.
  if (report.event == hw::TouchEvent::kTap && search::showingResults() &&
      !listTapAllowed(report)) {
    return out;
  }
  switch (report.event) {
    case hw::TouchEvent::kTap: {
      int index = -1;
      switch (search::handleTap(report.x, report.y, index)) {
        case search::Result::kChanged:
          search::draw();
          break;
        case search::Result::kSearch:
          // This frame has to reach the panel before the blocking call, not
          // after, or the screen simply stops for the length of the request.
          search::showSearching();
          out.intent = Intent::kRunSearch;
          break;
        case search::Result::kSelected:
          // Acknowledge before blocking. play_media on an artist takes
          // seconds, and in radio mode Music Assistant also has to build a
          // queue, so this is the longest wait anywhere in the firmware.
          search::showStarting(index);
          out.intent = Intent::kPlaySearchResult;
          out.index = index;
          break;
        case search::Result::kBack:
          if (search::backToKeyboard()) {
            search::draw();
          }
          break;
        default:
          break;
      }
      break;
    }
    default:
      break;
  }
  return out;
}

Input wifiTouch(const hw::TouchReport& report) {
  Input out;
  const bool back = report.event == hw::TouchEvent::kSwipeRight ||
                    (report.event == hw::TouchEvent::kTap &&
                     back_button::hit(report.x, report.y));
  if (back) {
    // Back out one page at a time: password to list, list to wherever the
    // list was opened from.
    if (wifi_join::back()) {
      wifi_join::draw();
    } else {
      out.intent = Intent::kWifiLeave;
    }
    return out;
  }
  if (report.event == hw::TouchEvent::kTap && wifi_join::onList() &&
      !listTapAllowed(report)) {
    return out;
  }
  switch (report.event) {
    case hw::TouchEvent::kTap:
      switch (wifi_join::handleTap(report.x, report.y)) {
        case wifi_join::Result::kChanged:
          wifi_join::draw();
          break;
        case wifi_join::Result::kJoin:
          out.intent = Intent::kWifiJoin;
          break;
        case wifi_join::Result::kRescan:
          out.intent = Intent::kWifiRescan;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
  return out;
}

Input playersTouch(const hw::TouchReport& report) {
  Input out;
  // Nothing to back out to: with no player there is nothing else to show.
  // The gear, top right, goes to the settings instead.
  if (report.event != hw::TouchEvent::kTap || !listTapAllowed(report)) {
    return out;
  }
  if (statusScreenGearHit(report.x, report.y)) {
    out.intent = Intent::kOpenSettings;
    return out;
  }
  switch (player_pick::handleTap(report.x, report.y)) {
    case player_pick::Result::kChosen:
      out.intent = Intent::kChoosePlayer;
      break;
    case player_pick::Result::kRefresh:
      out.intent = Intent::kPlayersRefresh;
      break;
    default:
      break;
  }
  return out;
}

/** Whether a tap in the volume's half of now playing may toggle the volume
 *  device's power: it claims TURN_ON or TURN_OFF, either -- the toggle is
 *  Home Assistant's, which knows which one applies. A device reporting no
 *  features at all counts as supporting them, as it does everywhere else:
 *  that is an integration that never set the attribute. */
bool powerToggleAvailable(const PlayerState& state) {
  constexpr uint32_t kOnOff =
      services::ha::kFeatureTurnOn | services::ha::kFeatureTurnOff;
  return state.control_features == 0 ||
         (state.control_features & kOnOff) != 0;
}

Input settingsTouch(const hw::TouchReport& report) {
  Input out;
  if (report.event == hw::TouchEvent::kTap &&
      back_button::hit(report.x, report.y)) {
    out.intent = Intent::kSettingsBack;  // as a swipe right
    return out;
  }
  if (report.event == hw::TouchEvent::kTap) {
    if (!listTapAllowed(report)) {
      return out;
    }
    const int row = settings_list::rowAt(report.x, report.y);
    if (row >= 0) {
      out.intent = Intent::kSettingsTap;
      out.index = row;
    }
  } else if (report.event == hw::TouchEvent::kSwipeRight) {
    // Back out one step at a time; the app knows which list this is.
    out.intent = Intent::kSettingsBack;
  }
  return out;
}

/** The sources are drawn by the settings list, and touched as it is; only
 *  what a tap and a back mean differ. */
Input sourcesTouch(const hw::TouchReport& report) {
  // A swipe either way goes back, as on the search: the inputs were opened by
  // a swipe right, and nothing here moves sideways.
  if (report.event == hw::TouchEvent::kSwipeLeft) {
    Input out;
    out.intent = Intent::kSourcesBack;
    return out;
  }
  Input out = settingsTouch(report);
  if (out.intent == Intent::kSettingsTap) {
    out.intent = Intent::kSourceTap;
  } else if (out.intent == Intent::kSettingsBack) {
    out.intent = Intent::kSourcesBack;
  }
  return out;
}

/** Isolate's card has its own buttons and nothing to scroll, so a tap is
 *  taken as it lands. */
Input isolateTouch(const hw::TouchReport& report) {
  Input out;
  if (report.event == hw::TouchEvent::kSwipeRight) {
    out.intent = Intent::kIsolateCancel;
    return out;
  }
  if (report.event != hw::TouchEvent::kTap) {
    return out;
  }
  switch (isolate_card::hit(report.x, report.y)) {
    case isolate_card::Result::kConfirm:
      out.intent = Intent::kIsolateConfirm;
      break;
    case isolate_card::Result::kCancel:
      out.intent = Intent::kIsolateCancel;
      break;
    case isolate_card::Result::kNone:
      break;
  }
  return out;
}

}  // namespace

size_t psramMallocFromBytes() {
#if defined(BOARD_PANEL_RGB)
  return config::kPsramMallocFromBytesRgb;
#else
  return config::kPsramMallocFromBytes;
#endif
}

bool init(uint8_t rotation) {
  displayInit(rotation);
  canvasInit();
  // The cover's buffer, before Wi-Fi as it asks. Without it every cover
  // streams -- fetched again and decoded on every repaint -- which is how
  // this went unnoticed when the call fell out of the app: the art still
  // showed, only slowly.
  cover::init();
  artwork::init();
  hw::touchInit();
  hw::encoderInit();
  if (hw::batteryInit()) {
    // The battery over the network, for logging a run-down with no cable:
    // {"percent":93,"charging":false}, percent -1 with no battery.
    wifiAddWebPage("/battery", [](WebServer& server) {
      char body[48];
      snprintf(body, sizeof(body), "{\"percent\":%d,\"charging\":%s}",
               hw::batteryPercent(),
               hw::batteryCharging() ? "true" : "false");
      server.send(200, "application/json", body);
    });
  }
  perf::registerPage();
  // The composed frame as a 24-bit BMP: a look at the screen without a camera,
  // for checking a layout from the desk. Read from the loop, as the web server
  // runs there, so it never meets a half-drawn frame -- and so nothing on the
  // puck answers while it sends: the radio is kept awake for it and the rows
  // go in large chunks, which took a frame from about 17 s to about 1. Fetch
  // one at a time, never on a tight loop.
  wifiAddWebPage("/frame", [](WebServer& server) {
    if (!canvasReady()) {
      server.send(503, "text/plain", "no frame buffer");
      return;
    }
    constexpr int kW = theme::kSize;
    constexpr int kRowBytes = (kW * 3 + 3) & ~3;
    constexpr int kRowsPerChunk = 32;
    constexpr uint32_t kData = static_cast<uint32_t>(kRowBytes) * kW;
    auto* chunk = static_cast<uint8_t*>(heap_caps_malloc(
        kRowBytes * kRowsPerChunk, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (chunk == nullptr) {
      server.send(503, "text/plain", "no memory");
      return;
    }
    uint8_t head[54] = {'B', 'M'};
    auto put32 = [&](int at, uint32_t v) {
      for (int i = 0; i < 4; ++i) head[at + i] = (v >> (8 * i)) & 0xFF;
    };
    put32(2, 54 + kData);
    put32(10, 54);
    put32(14, 40);
    put32(18, kW);
    put32(22, kW);
    head[26] = 1;
    head[28] = 24;
    put32(34, kData);
    WiFi.setSleep(WIFI_PS_NONE);
    server.setContentLength(54 + kData);
    server.send(200, "image/bmp", "");
    server.sendContent(reinterpret_cast<const char*>(head), sizeof(head));
    const uint16_t* pixels = canvasPixels(canvas());
    const bool swapped = canvasByteSwapped();
    int filled = 0;
    for (int y = kW - 1; y >= 0; --y) {  // BMP rows run bottom-up
      uint8_t* out = chunk + filled * kRowBytes;
      memset(out, 0, kRowBytes);
      const uint16_t* line = pixels + static_cast<size_t>(y) * kW;
      for (int x = 0; x < kW; ++x) {
        uint16_t c = line[x];
        if (swapped) c = static_cast<uint16_t>((c >> 8) | (c << 8));
        out[x * 3 + 0] = static_cast<uint8_t>((c & 31) * 255 / 31);
        out[x * 3 + 1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
        out[x * 3 + 2] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
      }
      if (++filled == kRowsPerChunk || y == 0) {
        server.sendContent(reinterpret_cast<const char*>(chunk),
                           filled * kRowBytes);
        filled = 0;
      }
    }
    heap_caps_free(chunk);
    WiFi.setSleep(config::kWifiModemSleep ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
  });
  return true;
}

namespace {
/** Standby is up in place of now playing: see dim(). */
bool s_dimmed = false;
/** Whether it was charging when standby was last drawn. */
bool s_drawn_charging = false;
/** No touch acts: PWR and BOOT together set it, and the pair held clears it. */
bool s_locked = false;
/** Both buttons are down, or were and not both let go yet; when that began,
 *  and whether this press has locked or unlocked already. */
bool s_pair = false;
unsigned long s_pair_since = 0;
bool s_pair_done = false;

/** The ring round the edge that fills while the pair is held to unlock,
 *  from the top, clockwise, on the volume ring's track; and how far it is
 *  drawn. */
constexpr float kUnlockRingStartDeg = -90.0f;
float s_ring_deg = kUnlockRingStartDeg;

/** Fill the ring to `fraction` of the way round, a tenth of a turn or so at a
 *  time: only the new stretch is drawn, and where the panel takes no direct
 *  writes (the AMOLED) the box round it is pushed from the frame. The repaint
 *  that unlocking, or letting go early, asks for takes it down again. */
void drawUnlockRing(float fraction) {
  if (displayIsBlanked()) {
    return;
  }
  const float to = kUnlockRingStartDeg + 360.0f * fraction;
  if (to - s_ring_deg < 10.0f) {
    return;
  }
  // Drawn into the frame and pushed, on every panel: anti-aliased, a little
  // heavier than the volume ring it runs over, in white.
  const float r =
      (theme::kVolumeArcInnerRadius + theme::kVolumeArcOuterRadius) / 2.0f;
  const float half =
      (theme::kVolumeArcOuterRadius - theme::kVolumeArcInnerRadius) / 2.0f +
      theme::px(1);
  paint::arc(theme::kCenterX, theme::kCenterY, r, half, s_ring_deg, to,
             theme::kTextPrimary);
  // The stretch's extent, widened by the ring's width.
  constexpr float kRad = 3.14159265f / 180.0f;
  int x0 = theme::kSize, y0 = theme::kSize, x1 = 0, y1 = 0;
  for (float a = s_ring_deg;; a += 5.0f) {
    const float t = (a < to ? a : to) * kRad;
    const int x = theme::kCenterX + static_cast<int>(cosf(t) * r);
    const int y = theme::kCenterY + static_cast<int>(sinf(t) * r);
    x0 = x < x0 ? x : x0;
    x1 = x > x1 ? x : x1;
    y0 = y < y0 ? y : y0;
    y1 = y > y1 ? y : y1;
    if (a >= to) {
      break;
    }
  }
  const int pad = static_cast<int>(half) + 3;
  canvasPresentRegion(x0 - pad, y0 - pad, x1 - x0 + 2 * pad,
                      y1 - y0 + 2 * pad);
  s_ring_deg = to;
}

/** The panel's own brightness down while standby is up, on a panel
 *  that has one; elsewhere the face's dim pixels are all the dimming. */
void dimPanel(bool dimmed) {
#if defined(BOARD_PANEL_AMOLED)
  displaySetDimmed(dimmed);
#else
  (void)dimmed;
#endif
}
}  // namespace

Input poll(Screen screen, const PlayerState* state) {
  Input out;

  // A card's swipe and gear belong to the card. Anything the app shows in
  // its place goes up under another screen -- the list's "Starting" frame
  // comes back as a card's screen, but by way of the list -- so seeing any
  // other screen is enough to know the card has gone.
  if (screen != Screen::kMessage) {
    statusScreenForgetCard();
  }

#if defined(BOARD_PANEL_AMOLED)
  displayTick();
#endif

  // The two buttons, on a board whose power manager reports PWR. Together
  // they lock the touch screen, or, held kUnlockHoldMs, unlock it -- one of
  // the two per press. The pair spends its press: letting go of PWR is no
  // sleep, and BOOT held with PWR is no settings reset.
  const bool boot = hw::bootButtonPressed();
  const bool pwr = hw::powerKeyHeld();
  bool pwr_press = hw::powerKeyTakePress();
  if (boot && pwr) {
    if (!s_pair) {
      s_pair = true;
      s_pair_since = millis();
      s_pair_done = false;
      s_ring_deg = kUnlockRingStartDeg;
    }
    if (!s_pair_done && !s_locked) {
      s_pair_done = true;
      s_locked = true;
      hw::touchCancel();
      out.intent = Intent::kLock;
    } else if (!s_pair_done) {
      const unsigned long held = millis() - s_pair_since;
      if (held >= config::kUnlockHoldMs) {
        s_pair_done = true;
        s_locked = false;
        out.intent = Intent::kUnlock;
      } else {
        drawUnlockRing(static_cast<float>(held) / config::kUnlockHoldMs);
      }
    }
  }
  if (s_pair) {
    pwr_press = false;
    if (boot) {
      bootButtonSparePress();
    }
    if (!boot && !pwr) {
      s_pair = false;
      // Let go before the second was up: the ring comes down, and the chip
      // says what it wanted.
      if (!s_pair_done) {
        out.intent = Intent::kLockedTouch;
      }
    }
    return out;
  }

  // PWR alone, briefly: the screen to sleep, or awake again, and nothing else.
  if (pwr_press) {
    if (displayIsBlanked()) {
      wake();
      out.intent = Intent::kWokeFromTouch;
    } else {
      out.intent = Intent::kSleep;
    }
    return out;
  }

  // A touch on a blanked panel, or on standby, only wakes it. Handled
  // before anything else reads the touch state, so the same press cannot
  // also press a button.
  if (displayIsBlanked() || s_dimmed) {
    // A whole tap can now arrive in one poll -- down and up queued behind a
    // slow pass -- so a completed gesture wakes the panel too, not only a
    // finger seen resting on it.
    hw::TouchReport discard;
    // The knob wakes it as well, and its clicks and press are spent doing so.
    const bool turned = hw::encoderTakeDetents() != 0;
    const bool pressed = hw::encoderTakePress();
    if (turned || pressed || hw::touchIsDown() || hw::touchPoll(discard)) {
      if (s_dimmed) {
        s_dimmed = false;
        dimPanel(false);
        hw::touchCancel();
      } else {
        wake();
      }
      out.intent = Intent::kWokeFromTouch;
    }
    return out;
  }

  // Locked: no touch acts, and the knob is spent as well. A finished touch
  // says how to unlock.
  if (s_locked) {
    hw::TouchReport discard;
    const bool turned = hw::encoderTakeDetents() != 0;
    const bool pressed = hw::encoderTakePress();
    if (hw::touchPoll(discard) || turned || pressed) {
      out.intent = Intent::kLockedTouch;
    }
    return out;
  }

  if (driveVolume(screen, state, out)) {
    return out;
  }
  if (driveSeek(screen, state, out)) {
    return out;
  }
  if (driveKnob(screen, state, out)) {
    return out;
  }
  driveListScrolling(screen);

  hw::TouchReport report;
  if (!hw::touchPoll(report)) {
    if (!hw::encoderTakePress()) {
      return out;
    }
    // On now playing the knob's press is play and pause, the button it is
    // most often reached for instead of.
    if (screen == Screen::kNowPlaying) {
      out.intent = Intent::kPlayPause;
      return out;
    }
    report = knobPressAsTouch(screen);
  }

  if (screen == Screen::kSearch) {
    return searchTouch(report);
  }
  if (screen == Screen::kBrowse) {
    return browseTouch(report);
  }
  if (screen == Screen::kWifi) {
    return wifiTouch(report);
  }
  if (screen == Screen::kPlayers) {
    return playersTouch(report);
  }
  if (screen == Screen::kSettings) {
    return settingsTouch(report);
  }
  if (screen == Screen::kSources) {
    return sourcesTouch(report);
  }
  if (screen == Screen::kIsolate) {
    return isolateTouch(report);
  }
  if (screen == Screen::kStarting) {
    // A pick on its way, which Music Assistant can take a long while over:
    // the back button -- or a swipe right, as everywhere -- stops waiting,
    // and a swipe down goes to the status page, as from now playing, and the
    // settings from there. Nothing else, so a stray tap cannot land on a
    // list that is no longer drawn.
    if ((report.event == hw::TouchEvent::kTap &&
         back_button::hit(report.x, report.y)) ||
        report.event == hw::TouchEvent::kSwipeRight) {
      out.intent = Intent::kCancelStart;
    } else if (report.event == hw::TouchEvent::kSwipeDown) {
      out.intent = Intent::kOpenStatus;
    }
    return out;
  }
  if (screen == Screen::kMessage) {
    // Most of what the cards complain about is fixed in the portal, and the
    // app notices on its own. A tap is passed up all the same: the Setup and
    // No Wi-Fi cards open the network list on it. A card about something
    // wrong also has a swipe down to the status page, to see what, and the
    // gear to the settings, to change it.
    const uint8_t ways = statusScreenCardWays();
    if (report.event == hw::TouchEvent::kTap &&
        (ways & kCardGearToSettings) != 0 &&
        statusScreenGearHit(report.x, report.y)) {
      out.intent = Intent::kOpenSettings;
    } else if (report.event == hw::TouchEvent::kSwipeDown &&
               (ways & kCardSwipeToStatus) != 0) {
      out.intent = Intent::kOpenStatus;
    } else if (report.event == hw::TouchEvent::kTap) {
      out.intent = Intent::kTapCard;
    }
    return out;
  }
  if (screen == Screen::kStatus) {
    // The gear opens the settings, and a swipe left goes on to the Wi-Fi
    // networks, as one does from the list to the search. A swipe up undoes
    // the swipe down that opened it, back to whatever screen that was; any
    // other tap or swipe goes back to now playing -- the back button among
    // them, which is there to say so.
    if (report.event == hw::TouchEvent::kTap &&
        statusScreenGearHit(report.x, report.y)) {
      out.intent = Intent::kOpenSettings;
    } else if (report.event == hw::TouchEvent::kSwipeLeft) {
      out.intent = Intent::kOpenWifi;
    } else if (report.event == hw::TouchEvent::kSwipeUp) {
      out.intent = Intent::kStatusBack;
    } else if (report.event != hw::TouchEvent::kNone) {
      out.intent = Intent::kBackToNowPlaying;
    }
    return out;
  }

  if (report.event == hw::TouchEvent::kSwipeUp) {
    out.intent = Intent::kOpenBrowse;
    return out;
  }
  if (report.event == hw::TouchEvent::kSwipeDown) {
    out.intent = Intent::kOpenStatus;
    return out;
  }
  // Straight to the search, without the list on the way. Only a swipe that
  // started in the lower half gets here: one in the top half is the volume,
  // claimed by driveVolume() as soon as it commits sideways -- which sends
  // the same intents itself when that one turns out to be a flick.
  if (report.event == hw::TouchEvent::kSwipeLeft) {
    out.intent = Intent::kOpenSearch;
    return out;
  }
  // The other way, the volume device's inputs: the lower half again.
  if (report.event == hw::TouchEvent::kSwipeRight) {
    out.intent = Intent::kOpenSources;
    return out;
  }
  if (report.event != hw::TouchEvent::kTap) {
    return out;
  }

  // The rooms chip first: it sits in the half where a tap otherwise toggles
  // the volume device's power.
  if (peers_chip::hit(report.x, report.y)) {
    out.intent = Intent::kIsolate;
    return out;
  }

  switch (now_playing::hitTest(report.x, report.y)) {
    case Action::kPrevious:
      out.intent = Intent::kPrevious;
      break;
    case Action::kPlayPause:
      out.intent = Intent::kPlayPause;
      break;
    case Action::kNext:
      out.intent = Intent::kNext;
      break;
    case Action::kRelink:
      out.intent = Intent::kRelinkHa;
      break;
    default:
      // A tap in the half a sideways swipe sets the volume in toggles the
      // volume device's power. A swipe there is the volume, and a tap is
      // never one: it moved less than the tap slop, in less than
      // kTouchTapMaxMs.
      if (state != nullptr &&
          now_playing::volumeSwipeRegion(report.x, report.y) &&
          powerToggleAvailable(*state)) {
        out.intent = Intent::kTogglePower;
      }
      break;
  }
  return out;
}

// --- What to show ----------------------------------------------------------

void showNowPlaying(const PlayerState& state) { now_playing::draw(state); }

void showBrowse() {
  hw::touchCancel();
  browse_list::opened();
  s_list_dragging = false;
  s_list_velocity = 0.0f;
  browse_list::draw();
}

void showStatus(const DeviceStatus& status) { statusScreenDevice(status); }

void showWifi(const char* note, const char* current) {
  hw::touchCancel();
  wifi_join::open(note, current);
  s_list_dragging = false;
  s_list_velocity = 0.0f;
  wifi_join::draw();
}

void showWifiNetworks(const WifiNetwork* networks, int count) {
  wifi_join::setNetworks(networks, count);
  if (wifi_join::onList()) {
    wifi_join::draw();
  }
}

void showPlayers(const char* note) {
  hw::touchCancel();
  player_pick::open(note);
  s_list_dragging = false;
  s_list_velocity = 0.0f;
  player_pick::draw();
}

const char* playerChosen() { return player_pick::chosenEntity(); }

void settingsBegin(const char* title, const char* note) {
  if (!settings_list::begin(title, note)) {
    LOG_WARN("UI: no memory for the settings list");
  }
}

void settingsAdd(const char* text, const char* detail, SettingStyle style) {
  settings_list::add(text, detail, style);
}

void showSettings(int focus) {
  hw::touchCancel();
  settings_list::open(focus);
  s_list_dragging = false;
  s_list_velocity = 0.0f;
  settings_list::draw();
}

void showSettingPressed(int row, bool pressed) {
  settings_list::showPressed(row, pressed);
}

void showIsolate(const IsolateCard& card) {
  hw::touchCancel();
  isolate_card::draw(card);
}

void showRestarting(const char* why) {
  statusScreenMessage("Restarting", why, "", theme::kStatusPlain,
                      theme::kStatusPlainText);
}

const char* wifiChosenSsid() { return wifi_join::chosenSsid(); }

const char* wifiPassword() { return wifi_join::password(); }

void showSearch() {
  hw::touchCancel();
  search::reset();
  s_list_dragging = false;
  s_list_velocity = 0.0f;
  search::draw();
}

void showSearchResults() { search::draw(); }

void showSearching() { search::showSearching(); }

void showLoading(const char* what) {
  statusScreenMessage("Loading", what, "", theme::kStatusPlain,
                      theme::kStatusPlainText);
}

void showCommandPending(Intent intent, bool pending) {
  switch (intent) {
    case Intent::kPrevious:
      now_playing::showButtonPressed(Action::kPrevious, pending);
      break;
    case Intent::kPlayPause:
      now_playing::showButtonPressed(Action::kPlayPause, pending);
      break;
    case Intent::kNext:
      now_playing::showButtonPressed(Action::kNext, pending);
      break;
    default:
      break;
  }
}

void showLoadingList() {
  statusScreenMessage("Loading", "music", "", theme::kStatusPlain,
                      theme::kStatusPlainText);
}

void showNeedsHaSetup() {
  hw::touchCancel();
  statusScreenNeedsHaSetup();
}

void showHaSignIn(const char* qr_text, const char* url, const char* name) {
  hw::touchCancel();
  statusScreenHaSignIn(qr_text, url, name);
}

void showNoPlayer() {
  hw::touchCancel();
  statusScreenNoPlayer();
}

void showHaUnreachable(const char* detail) { statusScreenHaUnreachable(detail); }
void showHaTokenRejected() { statusScreenHaTokenRejected(); }

void showPortal() { statusScreenPortal(); }

void showConnecting(const char* ssid) { statusScreenConnectingBegin(ssid); }

void tickConnecting() { statusScreenConnectingTick(); }

void showConnectFailed() { statusScreenConnectFailed(); }

void showWifiLost() { statusScreenWifiLost(); }

void showSettingsCleared() { statusScreenWifiReset(); }

void refreshElapsed(const PlayerState& state) {
  // Not under a seek: the line and the chip show where the finger is.
  if (s_seek_dragging) {
    return;
  }
  now_playing::refreshElapsed(state);
}

// --- Panel power -----------------------------------------------------------

bool isBlanked() { return displayIsBlanked(); }

void blank() {
  s_dimmed = false;
  displayBlank();
}

void wake() {
  if (!displayIsBlanked()) {
    return;
  }
  displayWake();
  // The touch that woke the screen must not also land on a control.
  hw::touchCancel();
}

/** Above the cover on standby: a padlock while locked, and the battery as a
 *  drawn cell with its percentage -- green while charging, red at a fifth or
 *  less -- on a board with one. */
void drawStandbyStatus(lgfx::LovyanGFX& gfx) {
  const int pct = hw::batteryPercent();
  const bool charging = hw::batteryCharging();
  if (pct < 0 && !s_locked) {
    return;
  }
  const int y = theme::kNpTopLineY;
  displayFontApplyHeight(gfx, theme::kNpTimesTextPx);
  char label[8] = {};
  if (pct >= 0) {
    snprintf(label, sizeof(label), "%d%%", pct);
  }
  const int lock_w = s_locked ? theme::px(7) : 0;
  const int cell_w = pct >= 0 ? theme::px(12) : 0;
  const int nub_w = pct >= 0 ? theme::px(1) + 1 : 0;
  const int gap = theme::px(4);
  const int text_w = label[0] != '\0' ? gfx.textWidth(label) : 0;
  const int total = lock_w + (lock_w && cell_w ? gap : 0) + cell_w + nub_w +
                    (text_w ? gap + text_w : 0);
  int x = theme::kCenterX - total / 2;
  if (s_locked) {
    // A padlock: the shackle as a half ring over a rounded body.
    const float bw = lock_w;
    const float bh = theme::px(5) + 1;
    const float body_top = y - bh / 2.0f + theme::px(1);
    paint::arc(x + bw / 2.0f, body_top, bw * 0.3f, theme::px(1) * 0.6f, 180.0f,
               360.0f, theme::kTextSecondary);
    paint::roundRect(x, body_top, bw, bh, theme::px(1), theme::kTextSecondary);
    x += lock_w + gap;
  }
  if (pct >= 0) {
    const float ch = theme::px(6);
    const float top = y - ch / 2.0f;
    const uint16_t level_color = charging   ? theme::kGood
                                 : pct <= 20 ? theme::kError
                                             : theme::kTextPrimary;
    paint::roundRect(x, top, cell_w, ch, ch * 0.3f, 0xFFFF, 90);
    paint::roundRect(x + cell_w + 1, y - ch * 0.2f, nub_w, ch * 0.4f,
                     nub_w * 0.5f, 0xFFFF, 90);
    const float inset = theme::px(1);
    const float fill = (cell_w - 2 * inset) * (pct > 100 ? 100 : pct) / 100.0f;
    if (fill > 0.5f) {
      paint::roundRect(x + inset, top + inset, fill, ch - 2 * inset,
                       ch * 0.2f, level_color);
    }
    x += cell_w + nub_w + gap;
    gfx.setTextDatum(textdatum_t::middle_left);
    gfx.setTextColor(theme::kTextSecondary);
    gfx.drawString(label, x, y);
  }
}

void dim(const PlayerState& state) {
  if (!s_dimmed) {
    dimPanel(true);
  }
  now_playing::composeStandby(state);
  drawStandbyStatus(canvas());
  canvasPresent();
  s_drawn_charging = hw::batteryCharging();
  s_dimmed = true;
}

bool isDimmed() { return s_dimmed; }

bool dimStale() {
  static unsigned long s_checked_ms = 0;
  if (!s_dimmed || millis() - s_checked_ms < 10000) {
    return false;
  }
  s_checked_ms = millis();
  return hw::batteryCharging() != s_drawn_charging;
}

bool isLocked() { return s_locked; }

void undim() {
  if (s_dimmed) {
    dimPanel(false);
  }
  s_dimmed = false;
}

// --- Artwork ---------------------------------------------------------------

void requestArtwork(const char* picture) { artwork::requestCover(picture); }

bool artworkPending(const char* picture) {
  return artwork::coverPending(picture);
}

bool takeArtworkFinished() { return artwork::takeCoverFinished(); }

void clearArtwork() {
  cover::clear();
  artwork::forgetCover();
}

bool idleWork(Screen screen) {
  // Thumbnails are fetched by artwork's own worker now, so nothing here
  // blocks: this queues what the screen wants, collects what has arrived, and
  // decides whether that is worth a repaint.
  //
  // Arrivals are gathered per screen and repainted together. The worker can
  // land several a second once its connection is warm, and a full list
  // repaint is the better part of 150 ms on the Qualia -- one per image
  // would put back a slice of the stall the worker exists to remove.
  static Screen s_pending_screen = Screen::kMessage;
  static uint32_t s_pending = 0;
  static unsigned long s_repainted_ms = 0;

  if (screen != s_pending_screen) {
    s_pending_screen = screen;
    s_pending = 0;  // a new screen is composed whole, pictures and all
  }

  if (screen == Screen::kSearch) {
    if (!search::showingResults()) {
      return false;
    }
    s_pending |= search::updateThumbs();
  } else {
    // The list's own artwork is filled in from wherever the user happens to
    // be, so a swipe up lands on a finished list rather than one loading.
    //
    // Except while the poll task is reloading it: that holds the list's lock
    // across the network, and asking for the item count would wait out the
    // whole load on the loop. The load only runs off the list screens.
    if (screen != Screen::kBrowse && services::browse::busy()) {
      return false;
    }
    const uint32_t finished = browse_list::updateThumbs();
    if (screen == Screen::kBrowse) {
      s_pending |= finished;
    }
  }

  if (s_pending == 0) {
    return false;
  }
  // Never under a finger or a glide: those repaint the rows themselves, and
  // with the pictures in, since artwork::draw() shows whatever has arrived.
  if (hw::touchIsDown() || s_list_velocity != 0.0f ||
      millis() - s_repainted_ms < config::kThumbRepaintMinMs) {
    return false;
  }

  const bool visible = screen == Screen::kSearch
                           ? search::anyResultVisible(s_pending)
                           : browse_list::anyItemVisible(s_pending);
  s_pending = 0;
  if (!visible) {
    return false;  // they will be drawn when scrolled to
  }
  s_repainted_ms = millis();
  if (screen == Screen::kSearch) {
    search::draw();
  } else {
    // Not redrawRows(): that may move the rows already on the panel, and
    // those are the ones without the new pictures.
    browse_list::repaintRows();
  }
  return true;
}

unsigned long lastInteractionMs() {
  const unsigned long touch = hw::touchLastActivityMs();
  const unsigned long knob = hw::encoderLastActivityMs();
  // The later of the two, wrap-safe; 0 is "never".
  if (knob == 0) {
    return touch;
  }
  return touch == 0 || static_cast<long>(knob - touch) > 0 ? knob : touch;
}

bool volumeDragging() { return s_vol_dragging || s_knob_adjusting; }

}  // namespace ui
