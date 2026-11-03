#include "ui/toast.h"

#include <Arduino.h>

#include <cstdio>

#include "hardware/display_font.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "ui/ui.h"

/**
 * A line over now playing for a few seconds: the words a voice request
 * heard. It is drawn into now playing's compose rather than over whatever is
 * on the panel, so it needs nothing of the frame underneath, and goes when
 * now playing is composed again without it.
 */

namespace ui {
namespace {

char s_text[120] = {};
unsigned long s_until_ms = 0;
bool s_showing = false;

/** Where it sits: the rooms chip's height, near the top of the glass. */
constexpr int kToastY = theme::px(42);

}  // namespace

void showToast(const char* text, uint32_t ms) {
  snprintf(s_text, sizeof(s_text), "%s", text);
  s_until_ms = millis() + ms;
  s_showing = true;
}

bool toastExpired() {
  if (!s_showing || static_cast<long>(millis() - s_until_ms) < 0) {
    return false;
  }
  s_showing = false;
  return true;
}

namespace toast {

void draw(lgfx::LovyanGFX& gfx) {
  if (!s_showing || static_cast<long>(millis() - s_until_ms) >= 0) {
    return;
  }
  displayFontApplyHeight(gfx, theme::kSubtitleTextPx);
  gfx.setTextDatum(textdatum_t::middle_center);
  const int text_h = gfx.fontHeight();
  const int h = text_h + 2 * theme::kElapsedPillPadY;
  // As wide as the glass allows at the pill's top edge, the narrower one on
  // a round panel.
  const int max_w = theme::usableWidthAt(kToastY - h / 2,
                                         theme::kTextEdgeInset);
  char line[text::kMaxLineLen];
  text::ellipsize(gfx, s_text, max_w - 2 * theme::kElapsedPillPadX, line,
                  sizeof(line));
  const int w = gfx.textWidth(line) + 2 * theme::kElapsedPillPadX;
  gfx.fillRoundRect(theme::kCenterX - w / 2, kToastY - h / 2, w, h,
                    theme::kElapsedPillRadius, theme::kSurface);
  gfx.setTextColor(theme::kAccent);
  gfx.drawString(line, theme::kCenterX, kToastY);
}

}  // namespace toast
}  // namespace ui
