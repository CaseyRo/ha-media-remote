#include "ui/clock_face.h"

#include <cstdio>

#include "config.h"
#include "hardware/battery.h"
#include "hardware/display_font.h"
#include "ui/canvas.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "ui/ui.h"

namespace ui {
namespace clock_face {
namespace {

/** Where the face sits, one step a minute: a few pixels about the centre and
 *  back, so a panel left on it all afternoon does not keep the same pixels
 *  lit. An AMOLED's pixels age with their hours on. */
constexpr int8_t kWander[][2] = {{0, 0},  {5, 2},  {-3, 5},  {-6, -1},
                                 {2, -5}, {6, 4},  {-4, -4}, {1, 6}};
constexpr int kWanderSteps = sizeof(kWander) / sizeof(kWander[0]);

}  // namespace

bool now(struct tm& out) {
  const time_t t = time(nullptr);
  if (t < config::kClockSetAfter) {
    return false;
  }
  localtime_r(&t, &out);
  return true;
}

void draw(const services::ha::PlayerState* state, const struct tm& local) {
  lgfx::LovyanGFX& gfx = canvas();
  gfx.fillScreen(theme::kBackground);
  const int8_t* step = kWander[local.tm_min % kWanderSteps];

  constexpr int kLineH = theme::kElapsedTextPx + theme::kElapsedTextPx / 4;
  constexpr int kFirstLineDy = theme::kClockTextPx / 2 + theme::kElapsedTextPx;
  displayFontApplyHeight(gfx, theme::kElapsedTextPx);

  // Above the time: the battery, on a board with one, and the lock.
  char top[48] = {};
  const int pct = hw::batteryPercent();
  const char* lock = isLocked() ? "Locked" : "";
  if (pct >= 0) {
    snprintf(top, sizeof(top), "%s%s%d%%%s", lock, lock[0] ? " \xC2\xB7 " : "",
             pct, hw::batteryCharging() ? " charging" : "");
  } else {
    snprintf(top, sizeof(top), "%s", lock);
  }

  // Under it, what is playing: the title on up to two lines and the artist
  // on its own, each fitted to the panel where the lowest line can sit -- the
  // third, in a block with the top line, centred as below.
  char lines[3][text::kMaxLineLen] = {};
  int count = 0;
  // Only while it plays: a paused or idle player keeps its last title, and
  // under a clock that reads as if it were still playing.
  if (state != nullptr && state->title[0] != '\0' &&
      state->playback == services::ha::PlaybackState::kPlaying) {
    constexpr int kWidestBlock = 2 * kLineH;  // top line to the third, about y
    const int lowest =
        theme::kCenterY - kWidestBlock / 2 + kFirstLineDy + 2 * kLineH;
    const int width = theme::usableWidthAt(lowest, theme::kTextEdgeInset);
    count = text::wrap(gfx, state->title, width, 2, lines);
    if (state->artist[0] != '\0') {
      text::ellipsize(gfx, state->artist, width, lines[count],
                      sizeof(lines[count]));
      ++count;
    }
  }

  // All of it centred as one block, so a long title lifts the time rather
  // than running into the narrow foot of the round panel.
  const int top_dy = top[0] != '\0'
                         ? -(kFirstLineDy + theme::kElapsedTextPx / 2)
                         : -theme::kClockTextPx / 2;
  const int bottom_dy = count == 0 ? theme::kClockTextPx / 2
                                   : kFirstLineDy + (count - 1) * kLineH +
                                         theme::kElapsedTextPx / 2;
  const int x = theme::kCenterX + step[0];
  const int y = theme::kCenterY - (top_dy + bottom_dy) / 2 + step[1];

  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setTextColor(theme::kTextMuted);
  if (top[0] != '\0') {
    gfx.drawString(top, x, y - kFirstLineDy);
  }
  for (int i = 0; i < count; ++i) {
    gfx.drawString(lines[i], x, y + kFirstLineDy + i * kLineH);
  }

  char hhmm[8];
  strftime(hhmm, sizeof(hhmm), "%H:%M", &local);
  displayFontApplyHeight(gfx, theme::kClockTextPx);
  gfx.drawString(hhmm, x, y);
  canvasPresent();
}

}  // namespace clock_face
}  // namespace ui
