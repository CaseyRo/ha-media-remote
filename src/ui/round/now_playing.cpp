#include "ui/now_playing.h"

#include <Arduino.h>

#include <esp_heap_caps.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "board/board.h"
#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/perf.h"
#include "ui/canvas.h"
#include "ui/cover_art.h"
#include "ui/paint.h"
#include "ui/peers_chip.h"
#include "ui/text.h"
#include "ui/theme.h"

// The round now playing, laid out as Apple Music's: the cover top centre on
// its own blur, title and artist under it, a scrubber with elapsed and
// remaining time, three plain glyphs, the volume as a thin ring at the rim.
// Nothing sits on an opaque shape any more, so every small repaint -- a glyph
// pressed, the scrubber's second -- first puts back the kept frame under it
// (canvasRestoreBackdropRegion) and draws anti-aliased over that (ui/paint.h).

namespace ui::now_playing {
namespace {

using services::ha::PlaybackState;
using services::ha::PlayerState;

/** Last state composed, so a pressed-button repaint keeps the same glyph
 *  instead of falling back to a default. */
PlayerState s_last_drawn;

/** What the ring the knob can cover looked like just before the volume arc
 *  went on, so a drag can put back what was under it. Laid out as a whole
 *  frame, so a pixel sits at the same offset in both, but only the ring is
 *  ever copied in -- see ringSpans(). PSRAM, claimed once. */
uint16_t* s_arc_backdrop = nullptr;
bool s_arc_backdrop_valid = false;
/** Where the knob is in the canvas right now. */
float s_arc_drawn_level = -1.0f;

/** The key the cover-and-text frame was kept under at the last compose, which
 *  the small repaints restore from; not valid when none was kept. */
uint32_t s_text_key = 0;
bool s_text_key_valid = false;

struct Button {
  Action action;
  int cx;
  int cy;
  int half;  // the glyph's half height
};

Button buttonFor(Action action) {
  const int y = theme::kNpTransportY;
  switch (action) {
    case Action::kPrevious:
      return {Action::kPrevious, theme::kCenterX - theme::kNpTransportSpacing,
              y, theme::kNpSkipHalfHeight};
    case Action::kNext:
      return {Action::kNext, theme::kCenterX + theme::kNpTransportSpacing, y,
              theme::kNpSkipHalfHeight};
    case Action::kPlayPause:
    default:
      return {Action::kPlayPause, theme::kCenterX, y,
              theme::kNpPlayHalfHeight};
  }
}

/** The box a glyph is drawn, restored and pushed in: wide enough for the
 *  widest of them (the skip pair, 2.15 half heights) with a pixel of air. */
void glyphBox(const Button& b, int& x, int& y, int& w, int& h) {
  const int half_w = (b.half * 6) / 5 + 2;
  x = b.cx - half_w;
  y = b.cy - b.half - 2;
  w = 2 * half_w + 1;
  h = 2 * b.half + 5;
}

bool isPlaying(const PlayerState& state) {
  return state.playback == PlaybackState::kPlaying;
}

/** Live state text for a player that has nothing to show. */
const char* idleLabel(const PlayerState& state) {
  switch (state.playback) {
    case PlaybackState::kPaused:
      return "Paused";
    case PlaybackState::kIdle:
      return "Idle";
    case PlaybackState::kOff:
      return "Off";
    case PlaybackState::kUnavailable:
      return "Unavailable";
    default:
      return "Nothing playing";
  }
}

bool supports(const PlayerState& state, uint32_t feature) {
  // A player that reports no features at all is usually an integration that
  // never set supported_features; assume the transport works rather than
  // greying out controls that would in fact respond.
  return state.supported_features == 0 ||
         (state.supported_features & feature) != 0;
}

bool enabledFor(Action action, const PlayerState& state) {
  if (state.playback == PlaybackState::kUnavailable ||
      state.playback == PlaybackState::kOff) {
    return false;
  }
  switch (action) {
    case Action::kPrevious:
      return supports(state, services::ha::kFeaturePreviousTrack);
    case Action::kNext:
      return supports(state, services::ha::kFeatureNextTrack);
    case Action::kPlayPause:
      return supports(state, services::ha::kFeaturePause |
                      services::ha::kFeaturePlay);
    default:
      return false;
  }
}

// --- Glyphs -----------------------------------------------------------------
// Apple's filled transport symbols: a play triangle, two pause bars, and a
// pair of triangles for each skip. Rasterised 4x4 supersampled into a
// coverage mask and laid over the backdrop, so their edges are as smooth as
// the type's.

struct Tri {
  float x0, y0, x1, y1, x2, y2;
};
struct Rect {
  float x0, y0, x1, y1;
};
struct Shape {
  Tri tri[2];
  int tris = 0;
  Rect rect[2];
  int rects = 0;
};

inline float edgeFn(float ax, float ay, float bx, float by, float px,
                    float py) {
  return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

bool inTri(const Tri& t, float px, float py) {
  const float d0 = edgeFn(t.x0, t.y0, t.x1, t.y1, px, py);
  const float d1 = edgeFn(t.x1, t.y1, t.x2, t.y2, px, py);
  const float d2 = edgeFn(t.x2, t.y2, t.x0, t.y0, px, py);
  const bool neg = d0 < 0.0f || d1 < 0.0f || d2 < 0.0f;
  const bool pos = d0 > 0.0f || d1 > 0.0f || d2 > 0.0f;
  return !(neg && pos);
}

Shape glyphShape(Action action, bool playing, float cx, float cy, float h) {
  Shape s;
  switch (action) {
    case Action::kPlayPause:
      if (playing) {
        const float bar = 0.56f * h;
        const float gap = 0.48f * h;
        const float x0 = cx - (2.0f * bar + gap) * 0.5f;
        s.rect[0] = {x0, cy - h, x0 + bar, cy + h};
        s.rect[1] = {x0 + bar + gap, cy - h, x0 + 2.0f * bar + gap, cy + h};
        s.rects = 2;
      } else {
        // A triangle's visual centre sits behind its box's: nudge it right.
        const float w = 1.73f * h;
        const float x0 = cx - w * 0.5f + w * 0.08f;
        s.tri[0] = {x0, cy - h, x0, cy + h, x0 + w, cy};
        s.tris = 1;
      }
      break;
    case Action::kNext:
    case Action::kPrevious: {
      const float w = 1.1f * h;
      const float overlap = 0.05f * h;
      const float total = 2.0f * w - overlap;
      if (action == Action::kNext) {
        const float x0 = cx - total * 0.5f;
        const float x1 = x0 + w - overlap;
        s.tri[0] = {x0, cy - h, x0, cy + h, x0 + w, cy};
        s.tri[1] = {x1, cy - h, x1, cy + h, x1 + w, cy};
      } else {
        const float x0 = cx + total * 0.5f;
        const float x1 = x0 - w + overlap;
        s.tri[0] = {x0, cy - h, x0, cy + h, x0 - w, cy};
        s.tri[1] = {x1, cy - h, x1, cy + h, x1 - w, cy};
      }
      s.tris = 2;
      break;
    }
    default:
      break;
  }
  return s;
}

/** The four glyphs' coverage, rasterised once each -- supersampling three of
 *  them on every compose cost tens of milliseconds of loop -- and kept in
 *  PSRAM: previous, next, play, pause. */
uint8_t* s_glyph_masks[4] = {};

int glyphIndex(Action action, bool playing) {
  switch (action) {
    case Action::kPrevious:
      return 0;
    case Action::kNext:
      return 1;
    default:
      return playing ? 3 : 2;
  }
}

const uint8_t* glyphMask(const Button& b, bool playing) {
  int bx, by, bw, bh;
  glyphBox(b, bx, by, bw, bh);
  uint8_t*& mask = s_glyph_masks[glyphIndex(b.action, playing)];
  if (mask != nullptr) {
    return mask;
  }
  mask = static_cast<uint8_t*>(heap_caps_malloc(
      static_cast<size_t>(bw) * bh, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (mask == nullptr) {
    return nullptr;
  }
  const Shape s = glyphShape(b.action, playing, b.cx + 0.5f, b.cy + 0.5f,
                             static_cast<float>(b.half));
  for (int my = 0; my < bh; ++my) {
    for (int mx = 0; mx < bw; ++mx) {
      int hits = 0;
      for (int sy = 0; sy < 4; ++sy) {
        const float py = by + my + (sy + 0.5f) * 0.25f;
        for (int sx = 0; sx < 4; ++sx) {
          const float px = bx + mx + (sx + 0.5f) * 0.25f;
          bool in = false;
          for (int i = 0; i < s.tris && !in; ++i) {
            in = inTri(s.tri[i], px, py);
          }
          for (int i = 0; i < s.rects && !in; ++i) {
            in = px >= s.rect[i].x0 && px < s.rect[i].x1 &&
                 py >= s.rect[i].y0 && py < s.rect[i].y1;
          }
          hits += in ? 1 : 0;
        }
      }
      mask[my * bw + mx] = static_cast<uint8_t>(hits * 255 / 16);
    }
  }
  return mask;
}

void drawGlyph(const Button& b, const PlayerState& state, bool pressed,
               bool enabled) {
  const uint8_t* mask = glyphMask(b, isPlaying(state));
  if (mask == nullptr) {
    return;
  }
  int bx, by, bw, bh;
  glyphBox(b, bx, by, bw, bh);
  // Pressed reads as the glyph dimming under the finger, as on Apple's.
  const uint16_t color = !enabled ? theme::kTextMuted
                         : pressed ? 0x8C71  // #8E8E93
                                   : theme::kTextPrimary;
  paint::mask(bx, by, bw, bh, mask, color);
}

// --- Volume ring ------------------------------------------------------------

/** Point on the arc's centreline at `fraction` along it, in frame coords. */
void volumePoint(float fraction, int& x, int& y) {
  const float deg =
      theme::kVolumeArcStartDeg + theme::kVolumeArcSweepDeg * fraction;
  const float rad = deg * static_cast<float>(M_PI) / 180.0f;
  const float r =
      (theme::kVolumeArcInnerRadius + theme::kVolumeArcOuterRadius) / 2.0f;
  x = theme::kCenterX + static_cast<int>(cosf(rad) * r);
  y = theme::kCenterY + static_cast<int>(sinf(rad) * r);
}

/** The stretch of row `y` the knob can ever cover, as up to two spans of
 *  [first, last] columns; returns how many. An annulus a knob wide either side
 *  of the arc's centreline, cut off below the arc's two ends -- the gap
 *  between them, where the elapsed chip sits, never has a knob in it.
 *
 *  About a quarter of the frame on the 360 px panel, which is the whole point:
 *  this is what compose copies, every time, in case a drag follows. */
int ringSpans(int y, int spans[2][2]) {
  constexpr float kCentreline =
      (theme::kVolumeArcInnerRadius + theme::kVolumeArcOuterRadius) / 2.0f;
  constexpr int kPad = theme::kVolumeKnobRadius + 2;
  constexpr float kOuter = kCentreline + kPad;
  constexpr float kInner = kCentreline - kPad;

  int end_x = 0;
  int low_end = 0;
  int other_end = 0;
  volumePoint(0.0f, end_x, low_end);
  volumePoint(1.0f, end_x, other_end);
  if (y > (low_end > other_end ? low_end : other_end) + kPad) {
    return 0;
  }
  const float dy = static_cast<float>(y - theme::kCenterY);
  if (fabsf(dy) > kOuter) {
    return 0;
  }
  const int outer = static_cast<int>(ceilf(sqrtf(kOuter * kOuter - dy * dy)));
  const int left = theme::kCenterX - outer < 0 ? 0 : theme::kCenterX - outer;
  const int right = theme::kCenterX + outer >= theme::kSize
                        ? theme::kSize - 1
                        : theme::kCenterX + outer;
  if (fabsf(dy) >= kInner) {
    spans[0][0] = left;  // above or below the hole: one chord
    spans[0][1] = right;
    return 1;
  }
  const int inner = static_cast<int>(floorf(sqrtf(kInner * kInner - dy * dy)));
  spans[0][0] = left;
  spans[0][1] = theme::kCenterX - inner;
  spans[1][0] = theme::kCenterX + inner;
  spans[1][1] = right;
  return 2;
}

void saveArcBackdrop(lgfx::LovyanGFX& gfx) {
  s_arc_backdrop_valid = false;
  if (!ui::canvasReady()) {
    return;  // composing straight onto the panel: nothing to read back
  }
  if (s_arc_backdrop == nullptr) {
    // Laid out as a whole frame so a pixel is at the same offset in both,
    // though only the ring is ever filled.
    s_arc_backdrop = static_cast<uint16_t*>(heap_caps_malloc(
        static_cast<size_t>(theme::kSize) * theme::kSize * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (s_arc_backdrop == nullptr) {
    return;
  }
  // readRect() and pushImage() agree on the layout between themselves, so the
  // round trip is exact whatever byte order the canvas keeps.
  int spans[2][2];
  for (int y = 0; y < theme::kSize; ++y) {
    const int n = ringSpans(y, spans);
    for (int i = 0; i < n; ++i) {
      gfx.readRect(spans[i][0], y, spans[i][1] - spans[i][0] + 1, 1,
                   s_arc_backdrop + static_cast<size_t>(y) * theme::kSize +
                       spans[i][0]);
    }
  }
  s_arc_backdrop_valid = true;
}

void drawVolumeArc(const PlayerState& state, float level,
                   const paint::Clip* clip) {
  level = level < 0.0f ? 0.0f : level > 1.0f ? 1.0f : level;
  const float r =
      (theme::kVolumeArcInnerRadius + theme::kVolumeArcOuterRadius) / 2.0f;
  const float half =
      (theme::kVolumeArcOuterRadius - theme::kVolumeArcInnerRadius) / 2.0f;
  const float cx = theme::kCenterX;
  const float cy = theme::kCenterY;
  // The track is white at a fifth, so it takes the cover's colour under it.
  paint::arc(cx, cy, r, half, theme::kVolumeArcStartDeg,
             theme::kVolumeArcEndDeg, 0xFFFF, 52, clip);
  // Grey when nothing will be heard at that level: muted, or the volume
  // device switched off.
  const uint16_t fill = state.muted || state.control_off
                            ? theme::kVolumeMutedFill
                            : theme::kVolumeFill;
  const float filled_end =
      theme::kVolumeArcStartDeg + theme::kVolumeArcSweepDeg * level;
  if (level > 0.004f) {
    paint::arc(cx, cy, r, half, theme::kVolumeArcStartDeg, filled_end, fill,
               255, clip);
  }
  // The knob is what makes it read as draggable rather than as a readout.
  constexpr float kRad = 3.14159265f / 180.0f;
  paint::disc(cx + cosf(filled_end * kRad) * r,
              cy + sinf(filled_end * kRad) * r, theme::kVolumeKnobRadius, fill,
              255, clip);
}

// --- Scrubber and times -----------------------------------------------------

/** Where the line under the scrubber is while it says the token was
 *  rejected, for a tap there to link again; not shown otherwise. */
struct ChipBox {
  bool shown = false;
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
};
ChipBox s_relink_chip;
/** A finger's worth around it. */
constexpr int kRelinkHitPad = theme::px(8);

/** What the times line says instead of times, or nullptr: offline (the time
 *  would run on from a stale state), and the lock. `warn` when it is a state
 *  to act on. */
const char* timesMessage(const PlayerState& state, bool& warn) {
  warn = true;
  if (state.offline) {
    return services::ha::tokenRejected() ? "Token rejected" : "Offline";
  }
  if (state.locked) {
    warn = state.lock_hint;
    return state.lock_hint ? "Hold both buttons" : "Locked";
  }
  return nullptr;
}

/** How far through the track, 0..1, or -1 when there is nothing to show: no
 *  duration, no position, or offline. */
float progressFraction(const PlayerState& state) {
  if (state.offline || state.duration_s <= 0.0f) {
    return -1.0f;
  }
  const float position = services::ha::interpolatedPosition(state);
  if (position < 0.0f) {
    return -1.0f;
  }
  const float f = position / state.duration_s;
  return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

constexpr int kScrubLeft = theme::kCenterX - theme::kNpScrubHalfWidth;
constexpr int kScrubWidth = 2 * theme::kNpScrubHalfWidth;

/** The band the scrubber and its times repaint in, once a second: inside the
 *  volume ring's inner edge down to the band's lowest row (wider, and the
 *  repaint took bites out of the ring's two ends), clear of the glyphs. */
constexpr int kBandHalfWidth = theme::px(86);
constexpr int kBandTop = theme::kNpScrubY - theme::px(6);
constexpr int kBandBottom = theme::kNpTimesY + theme::px(10);

void drawScrubber(lgfx::LovyanGFX& gfx, const PlayerState& state) {
  s_relink_chip.shown = false;
  const float f = progressFraction(state);
  const float h = theme::kNpScrubHeight;
  const float y = theme::kNpScrubY - h / 2.0f;
  if (f >= 0.0f) {
    paint::roundRect(kScrubLeft, y, kScrubWidth, h, h / 2.0f, 0xFFFF, 64);
    const float filled = std::max(h, kScrubWidth * f);
    paint::roundRect(kScrubLeft, y, filled, h, h / 2.0f, 0xFFFF, 240);
  }

  displayFontApplyHeight(gfx, theme::kNpTimesTextPx);
  bool warn = false;
  if (const char* message = timesMessage(state, warn)) {
    char line[text::kMaxLineLen];
    text::ellipsize(gfx, message, 2 * kBandHalfWidth, line, sizeof(line));
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setTextColor(warn ? theme::kWarning : theme::kNpSecondary);
    gfx.drawString(line, theme::kCenterX, theme::kNpTimesY);
    if (state.offline && services::ha::tokenRejected()) {
      const int w = gfx.textWidth(line);
      const int th = gfx.fontHeight();
      s_relink_chip = {true, theme::kCenterX - w / 2, theme::kNpTimesY - th / 2,
                       w, th};
    }
    return;
  }
  if (f < 0.0f) {
    return;
  }
  const float position = services::ha::interpolatedPosition(state);
  char elapsed[12];
  char remaining[14];
  text::formatDuration(position, elapsed, sizeof(elapsed));
  remaining[0] = '-';
  text::formatDuration(std::max(0.0f, state.duration_s - position),
                       remaining + 1, sizeof(remaining) - 1);
  gfx.setTextColor(theme::kNpSecondary);
  gfx.setTextDatum(textdatum_t::middle_left);
  gfx.drawString(elapsed, kScrubLeft, theme::kNpTimesY);
  gfx.setTextDatum(textdatum_t::middle_right);
  gfx.drawString(remaining, kScrubLeft + kScrubWidth, theme::kNpTimesY);
}

// --- Title and artist -------------------------------------------------------

/** The track text as laid out once, so the glow and the text share it. */
struct TrackText {
  char lines[theme::kTitleMaxLines][text::kMaxLineLen] = {};
  int count = 0;
  char artist[text::kMaxLineLen] = {};
  int title_y = 0;   // centre of the first title line
  int artist_y = 0;  // centre of the artist line
};

/** Title, then artist, as a block centred in the band between the cover and
 *  the scrubber, so one- and two-line titles sit in the same optical place. */
void layoutTrackText(lgfx::LGFXBase& gfx, const PlayerState& state,
                     TrackText& text) {
  const bool has_title = state.title[0] != '\0';
  const int mid = (theme::kNpTextTop + theme::kNpTextBottom) / 2;
  const int width = std::min(theme::usableWidthAt(mid, theme::kTextEdgeInset),
                             theme::px(196));
  displayFontApplyHeight(gfx, theme::kNpTitleTextPx);
  text.count = text::wrap(gfx, has_title ? state.title : idleLabel(state),
                          width, theme::kTitleMaxLines, text.lines);
  if (state.subtitle[0] != '\0') {
    displayFontApplyHeight(gfx, theme::kNpArtistTextPx);
    text::ellipsize(gfx, state.subtitle, width, text.artist,
                    sizeof(text.artist));
  }
  const int title_h = text.count * theme::kNpTitleLineHeight;
  const int artist_h = text.artist[0] != '\0'
                           ? theme::kNpArtistGap + theme::kNpArtistTextPx
                           : 0;
  const int top = mid - (title_h + artist_h) / 2;
  text.title_y = top + theme::kNpTitleLineHeight / 2;
  text.artist_y = top + title_h + theme::kNpArtistGap +
                  theme::kNpArtistTextPx / 2;
}

/** Draw `text` onto `gfx`, `dy` rows up from where it sits on the frame. With
 *  `mask`, everything in white, for the glow. */
void drawTrackLines(lgfx::LGFXBase& gfx, const TrackText& text, int dy,
                    bool mask) {
  gfx.setTextDatum(textdatum_t::middle_center);
  displayFontApplyHeight(gfx, theme::kNpTitleTextPx);
  gfx.setTextColor(theme::kTextPrimary);
  for (int i = 0; i < text.count; ++i) {
    gfx.drawString(text.lines[i], theme::kCenterX,
                   text.title_y + i * theme::kNpTitleLineHeight - dy);
  }
  if (text.artist[0] != '\0') {
    displayFontApplyHeight(gfx, theme::kNpArtistTextPx);
    gfx.setTextColor(mask ? theme::kTextPrimary : theme::kNpSecondary);
    gfx.drawString(text.artist, theme::kCenterX, text.artist_y - dy);
  }
}

void drawTrackText(lgfx::LovyanGFX& gfx, const PlayerState& state,
                   bool over_art) {
  if (state.title[0] == '\0' && state.hold_label) {
    return;  // most likely between tracks: blank, not "Nothing playing"
  }
  (void)over_art;
  TrackText text;
  layoutTrackText(gfx, state, text);
  // No glow under it: on a blurred ground a shadow shows as a box round each
  // line. The ground is darkened enough for white instead (paint.cpp).
  drawTrackLines(gfx, text, 0, false);
}

/** What the kept frame with the track text on it shows: the cover under it
 *  and every input drawTrackText() reads, hashed (FNV-1a). */
uint32_t trackTextKey(uint32_t art, bool has_art, const PlayerState& state) {
  uint32_t h = 2166136261u;
  auto mix = [&](uint8_t byte) {
    h ^= byte;
    h *= 16777619u;
  };
  auto mixText = [&](const char* text) {
    for (const char* p = text; *p != '\0'; ++p) {
      mix(static_cast<uint8_t>(*p));
    }
    mix(0);
  };
  for (int shift = 0; shift < 32; shift += 8) {
    mix(static_cast<uint8_t>(art >> shift));
  }
  mix(has_art ? 1 : 0);
  mix(state.hold_label ? 1 : 0);
  mixText(state.title);
  mixText(state.subtitle);
  if (state.title[0] == '\0') {
    mixText(idleLabel(state));
  }
  return h;
}

/** Draw the whole screen into the frame buffer. */
void compose(lgfx::LovyanGFX& gfx, const PlayerState& state,
             bool controls = true) {
  displayFontEnsureLoaded(gfx);

  // The cover, its blurred ground and the track text over them, kept from the
  // last compose while they are the same: they are most of a repaint's cost
  // -- a decode and a full-frame blur -- and a play, a pause or a volume step
  // touches none of them. Two frames, so a title that changes over the same
  // cover -- a radio stream -- redraws only the text.
  const bool has_art = ui::cover::hasArt();
  const uint32_t art = has_art ? ui::cover::identity() : 0;
  const uint32_t with_text = trackTextKey(art, has_art, state);
  bool over_art = has_art;
  if (ui::canvasRestoreBackdrop(ui::Backdrop::kArtAndText, with_text)) {
    s_text_key = with_text;
    s_text_key_valid = true;
  } else {
    over_art = has_art && ui::canvasRestoreBackdrop(ui::Backdrop::kArt, art);
    if (!over_art && has_art) {
      gfx.fillScreen(theme::kBackground);
      uint32_t t0 = millis();
      over_art = ui::cover::draw(gfx, theme::kNpCoverX, theme::kNpCoverY,
                                 theme::kNpCoverSize);
      perf::record("np.coverDecode", millis() - t0);
      if (over_art) {
        t0 = millis();
        paint::coverBackdrop(theme::kNpCoverX, theme::kNpCoverY,
                             theme::kNpCoverSize, theme::kNpCoverRadius);
        perf::record("np.coverBlur", millis() - t0);
        ui::canvasSaveBackdrop(ui::Backdrop::kArt, art);
      }
    }
    if (!over_art) {
      gfx.fillScreen(theme::kBackground);
      paint::plainBackdrop(theme::kNpCoverX, theme::kNpCoverY,
                           theme::kNpCoverSize, theme::kNpCoverRadius);
    }
    drawTrackText(gfx, state, over_art);
    // Kept only as what it was asked to be: art that would not draw this
    // time is tried again next time rather than remembered as a plain frame.
    s_text_key_valid = over_art == has_art;
    if (s_text_key_valid) {
      ui::canvasSaveBackdrop(ui::Backdrop::kArtAndText, with_text);
      s_text_key = with_text;
    }
  }
  // Standby is the cover and the track text alone: no controls to touch.
  if (!controls) {
    s_arc_backdrop_valid = false;
    return;
  }
  drawScrubber(gfx, state);
  // Before the arc's backdrop is kept, so that copy agrees with it wherever
  // the two might meet.
  for (const Action action :
       {Action::kPrevious, Action::kPlayPause, Action::kNext}) {
    drawGlyph(buttonFor(action), state, false, enabledFor(action, state));
  }
  if (volumeAvailable(state)) {
    saveArcBackdrop(gfx);
    drawVolumeArc(state, state.volume, nullptr);
    s_arc_drawn_level = state.volume;
  } else {
    s_arc_backdrop_valid = false;
  }
  ui::peers_chip::draw(gfx, state);
}

}  // namespace

void composeStandby(const PlayerState& state) {
  compose(ui::canvas(), state, false);
}

void draw(const PlayerState& state) {
  s_last_drawn = state;
  compose(ui::canvas(), state);
  ui::canvasPresent();
}

void refreshElapsed(const PlayerState& state) {
  // Put back the band from the kept cover-and-text frame and draw the second
  // over it. With nothing kept there is no clean ground to draw on: the line
  // waits for the next compose rather than smearing its own last second.
  const int x = theme::kCenterX - kBandHalfWidth;
  const int w = 2 * kBandHalfWidth;
  const int h = kBandBottom - kBandTop;
  if (!s_text_key_valid ||
      !ui::canvasRestoreBackdropRegion(ui::Backdrop::kArtAndText, s_text_key,
                                       x, kBandTop, w, h)) {
    return;
  }
  drawScrubber(ui::canvas(), state);
  ui::canvasPresentRegion(x, kBandTop, w, h);
}

void refreshVolume(const PlayerState& state, float level) {
  if (!s_arc_backdrop_valid || s_arc_drawn_level < 0.0f) {
    // No copy of what is under the ring. Draw over it and let the next
    // compose clean up.
    drawVolumeArc(state, level, nullptr);
    ui::canvasPresent();
    return;
  }

  // Rebuild the stretch of ring between where the knob was and where it is
  // going, in the canvas -- and push only that. Bounded by walking the arc
  // between the two levels, since a stretch of it can bulge well outside the
  // box its two ends make.
  const float from = s_arc_drawn_level < level ? s_arc_drawn_level : level;
  const float to = s_arc_drawn_level < level ? level : s_arc_drawn_level;
  int x0 = theme::kSize;
  int y0 = theme::kSize;
  int x1 = -1;
  int y1 = -1;
  const int steps = 1 + static_cast<int>((to - from) * 64.0f);
  for (int i = 0; i <= steps; ++i) {
    int x = 0;
    int y = 0;
    volumePoint(from + (to - from) * i / steps, x, y);
    x0 = x < x0 ? x : x0;
    y0 = y < y0 ? y : y0;
    x1 = x > x1 ? x : x1;
    y1 = y > y1 ? y : y1;
  }
  const int pad = theme::kVolumeKnobRadius + 2;
  x0 = x0 - pad < 0 ? 0 : x0 - pad;
  y0 = y0 - pad < 0 ? 0 : y0 - pad;
  x1 = x1 + pad >= theme::kSize ? theme::kSize - 1 : x1 + pad;
  y1 = y1 + pad >= theme::kSize ? theme::kSize - 1 : y1 + pad;
  const int w = x1 - x0 + 1;
  const int h = y1 - y0 + 1;

  // Only the ring was kept, and only the ring needs putting back: the arc and
  // the knob never draw outside it, and nothing else sits in it.
  lgfx::LovyanGFX& gfx = ui::canvas();
  int spans[2][2];
  for (int row = y0; row <= y1; ++row) {
    const int n = ringSpans(row, spans);
    for (int i = 0; i < n; ++i) {
      const int a = spans[i][0] > x0 ? spans[i][0] : x0;
      const int b = spans[i][1] < x1 ? spans[i][1] : x1;
      if (a <= b) {
        gfx.pushImage(a, row, b - a + 1, 1,
                      s_arc_backdrop + static_cast<size_t>(row) * theme::kSize +
                          a);
      }
    }
  }
  const paint::Clip clip{x0, y0, w, h};
  drawVolumeArc(state, level, &clip);
  ui::canvasPresentRegion(x0, y0, w, h);
  s_arc_drawn_level = level;
}

bool volumeAvailable(const PlayerState& state) {
  if (state.volume < 0.0f) {
    return false;
  }
  if (state.playback == PlaybackState::kUnavailable ||
      state.playback == PlaybackState::kOff) {
    return false;
  }
  // Tested against the *control* entity: it is the one the slider drives, and
  // where the two differ it is entirely normal for the receiver to set
  // VOLUME_SET while the player streaming into it does not.
  return state.control_features == 0 ||
         (state.control_features & services::ha::kFeatureVolumeSet) != 0;
}

float volumeLevelPerPx() {
  // Length of the arc the knob runs along, in pixels, measured down its
  // centreline -- the same radius volumePoint() places the knob at. One pixel
  // of finger travel is one pixel of arc, so the knob keeps pace with the
  // hand rather than with the angle it happens to subtend.
  constexpr float kRadius =
      (theme::kVolumeArcInnerRadius + theme::kVolumeArcOuterRadius) / 2.0f;
  const float arc_px = 2.0f * static_cast<float>(M_PI) * kRadius *
                       (theme::kVolumeArcSweepDeg / 360.0f);
  return arc_px > 1.0f ? 1.0f / arc_px : 0.0f;
}

bool volumeSwipeRegion(int x, int y) {
  (void)x;
  return y < theme::kCenterY;
}

bool seekRegion(int x, int y, const PlayerState& state) {
  if (progressFraction(state) < 0.0f ||
      (state.supported_features & services::ha::kFeatureSeek) == 0) {
    return false;
  }
  return y >= theme::kNpScrubY - theme::kNpSeekBandHalf &&
         y <= theme::kNpScrubY + theme::kNpSeekBandHalf &&
         x >= kScrubLeft - theme::kNpSeekBandHalf &&
         x <= kScrubLeft + kScrubWidth + theme::kNpSeekBandHalf;
}

float seekFraction(int x, int y) {
  (void)y;
  const float f = static_cast<float>(x - kScrubLeft) / kScrubWidth;
  return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

Action hitTest(int x, int y) {
  // The Token rejected line first: it is a control only while it says that.
  if (s_relink_chip.shown && x >= s_relink_chip.left - kRelinkHitPad &&
      x <= s_relink_chip.left + s_relink_chip.width + kRelinkHitPad &&
      y >= s_relink_chip.top - kRelinkHitPad &&
      y <= s_relink_chip.top + s_relink_chip.height + kRelinkHitPad) {
    return Action::kRelink;
  }
  // Nearest target wins rather than first-match, so a tap that lands between
  // two glyphs goes to the one it is actually closer to.
  constexpr int kHitRadiusSq =
      theme::kNpTransportHitRadius * theme::kNpTransportHitRadius;
  Action closest = Action::kNone;
  int closest_dist_sq = kHitRadiusSq + 1;
  for (const Action action :
       {Action::kPrevious, Action::kPlayPause, Action::kNext}) {
    const Button button = buttonFor(action);
    const int dx = x - button.cx;
    const int dy = y - button.cy;
    const int dist_sq = dx * dx + dy * dy;
    if (dist_sq <= kHitRadiusSq && dist_sq < closest_dist_sq) {
      closest = action;
      closest_dist_sq = dist_sq;
    }
  }
  return closest;
}

void showButtonPressed(Action action, bool pressed) {
  if (action == Action::kNone || action == Action::kRelink) {
    return;
  }
  const Button button = buttonFor(action);
  int x, y, w, h;
  glyphBox(button, x, y, w, h);
  if (!s_text_key_valid ||
      !ui::canvasRestoreBackdropRegion(ui::Backdrop::kArtAndText, s_text_key,
                                       x, y, w, h)) {
    return;
  }
  drawGlyph(button, s_last_drawn, pressed, enabledFor(action, s_last_drawn));
  ui::canvasPresentRegion(x, y, w, h);
}

}  // namespace ui::now_playing
