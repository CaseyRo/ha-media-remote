#include "ui/clock_face.h"

#include <cstdio>

#include "config.h"
#include "hardware/display_font.h"
#include "ui/canvas.h"
#include "ui/text.h"
#include "ui/theme.h"

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

  // What is playing, under the time: the title on up to two lines, the
  // artist on its own, each fitted to the panel at the lowest of them.
  constexpr int kLineH = theme::kElapsedTextPx + theme::kElapsedTextPx / 4;
  constexpr int kFirstLineDy = theme::kClockTextPx / 2 + theme::kElapsedTextPx;
  char lines[3][text::kMaxLineLen] = {};
  int count = 0;
  if (state != nullptr && state->title[0] != '\0') {
    displayFontApplyHeight(gfx, theme::kElapsedTextPx);
    const bool artist = state->artist[0] != '\0';
    // The width where a line can sit lowest: the third, with a block of
    // three centred as below.
    constexpr int kBlock3 = -theme::kClockTextPx / 2 + kFirstLineDy +
                            2 * kLineH + theme::kElapsedTextPx / 2;
    const int lowest = theme::kCenterY - kBlock3 / 2 + kFirstLineDy + 2 * kLineH;
    const int width = theme::usableWidthAt(lowest, theme::kTextEdgeInset);
    count = text::wrap(gfx, state->title, width, 2, lines);
    if (artist) {
      text::ellipsize(gfx, state->artist, width, lines[count],
                      sizeof(lines[count]));
      ++count;
    }
  }

  // The time and the lines under it, centred as one block, so a long title
  // lifts the time rather than running down into the narrow foot of the
  // panel.
  const int top = -theme::kClockTextPx / 2;
  const int bottom = count == 0 ? theme::kClockTextPx / 2
                                : kFirstLineDy + (count - 1) * kLineH +
                                      theme::kElapsedTextPx / 2;
  const int x = theme::kCenterX + step[0];
  const int y = theme::kCenterY - (top + bottom) / 2 + step[1];

  char hhmm[8];
  strftime(hhmm, sizeof(hhmm), "%H:%M", &local);
  displayFontApplyHeight(gfx, theme::kClockTextPx);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setTextColor(theme::kTextMuted);
  gfx.drawString(hhmm, x, y);

  displayFontApplyHeight(gfx, theme::kElapsedTextPx);
  for (int i = 0; i < count; ++i) {
    gfx.drawString(lines[i], x, y + kFirstLineDy + i * kLineH);
  }
  canvasPresent();
}

}  // namespace clock_face
}  // namespace ui
