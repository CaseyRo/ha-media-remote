#include "ui/paint.h"

#include <esp_heap_caps.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ui/canvas.h"
#include "ui/theme.h"

namespace ui::paint {
namespace {

constexpr int kSide = theme::kSize;
constexpr float kRad = 3.14159265f / 180.0f;

/** Where a call draws: the frame, its byte order, and the box it may touch,
 *  [x0, x1) by [y0, y1). */
struct Target {
  uint16_t* buf = nullptr;
  bool swapped = false;
  int x0 = 0;
  int y0 = 0;
  int x1 = kSide;
  int y1 = kSide;
};

bool target(Target& t, const Clip* clip) {
  if (!canvasReady()) {
    return false;
  }
  t.buf = canvasPixels(canvas());
  if (t.buf == nullptr) {
    return false;
  }
  t.swapped = canvasByteSwapped();
  if (clip != nullptr) {
    t.x0 = std::max(0, clip->x);
    t.y0 = std::max(0, clip->y);
    t.x1 = std::min(kSide, clip->x + clip->w);
    t.y1 = std::min(kSide, clip->y + clip->h);
  }
  return t.x1 > t.x0 && t.y1 > t.y0;
}

inline uint16_t swap16(uint16_t v) {
  return static_cast<uint16_t>((v >> 8) | (v << 8));
}

/** `s` over `d` by `a`, 0..256, in RGB565. */
inline uint16_t blend565(uint16_t d, uint16_t s, uint32_t a) {
  if (a >= 256) {
    return s;
  }
  if (a == 0) {
    return d;
  }
  const uint32_t ia = 256 - a;
  const uint32_t r = (((d >> 11) & 31) * ia + ((s >> 11) & 31) * a + 128) >> 8;
  const uint32_t g = (((d >> 5) & 63) * ia + ((s >> 5) & 63) * a + 128) >> 8;
  const uint32_t b = ((d & 31) * ia + (s & 31) * a + 128) >> 8;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

inline void plot(const Target& t, int x, int y, uint16_t color, uint32_t a) {
  uint16_t& p = t.buf[static_cast<size_t>(y) * kSide + x];
  const uint16_t host = t.swapped ? swap16(p) : p;
  const uint16_t out = blend565(host, color, a);
  p = t.swapped ? swap16(out) : out;
}

/** Coverage 0..1 as the 0..256 blend weight, scaled by `alpha`. */
inline uint32_t weight(float coverage, uint8_t alpha) {
  if (coverage <= 0.0f) {
    return 0;
  }
  if (coverage > 1.0f) {
    coverage = 1.0f;
  }
  return static_cast<uint32_t>(coverage * alpha * (256.0f / 255.0f) + 0.5f);
}

/** Signed distance from (px, py) to a rounded box: negative inside. */
inline float roundBoxDistance(float px, float py, float x, float y, float w,
                              float h, float r) {
  const float hx = w * 0.5f;
  const float hy = h * 0.5f;
  const float qx = fabsf(px - (x + hx)) - (hx - r);
  const float qy = fabsf(py - (y + hy)) - (hy - r);
  const float ox = qx > 0.0f ? qx : 0.0f;
  const float oy = qy > 0.0f ? qy : 0.0f;
  const float inside = std::min(std::max(qx, qy), 0.0f);
  return sqrtf(ox * ox + oy * oy) + inside - r;
}

inline void unpack8(uint16_t c, float& r, float& g, float& b) {
  r = static_cast<float>((c >> 11) & 31) * (255.0f / 31.0f);
  g = static_cast<float>((c >> 5) & 63) * (255.0f / 63.0f);
  b = static_cast<float>(c & 31) * (255.0f / 31.0f);
}

/** Ordered dither to RGB565 (fillFromGrid): each channel floored after a
 *  threshold from a 4x4 Bayer matrix, so a smooth field averages to its true
 *  value instead of stepping. */
constexpr uint8_t kBayer[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};


/** The sharp cover, kept while its backdrop is laid under it. PSRAM. */
uint16_t* s_cover = nullptr;
size_t s_cover_cap = 0;

/** Darken a soft shadow under the rounded square at (x, y). */
void shadow(const Target& t, int x, int y, int size, int radius) {
  const float dy = size * 0.05f;
  const float feather = size * 0.13f;
  constexpr float kMax = 0.5f;  // darkest, under the square itself
  const int f = static_cast<int>(feather) + 1;
  const int ya = std::max(t.y0, y - f + static_cast<int>(dy));
  const int yb = std::min(t.y1, y + size + f + static_cast<int>(dy));
  const int xa = std::max(t.x0, x - f);
  const int xb = std::min(t.x1, x + size + f);
  for (int py = ya; py < yb; ++py) {
    uint16_t* line = t.buf + static_cast<size_t>(py) * kSide;
    for (int px = xa; px < xb; ++px) {
      const float d = roundBoxDistance(px + 0.5f, py + 0.5f, x, y + dy, size,
                                       size, radius);
      float a = kMax;
      if (d > 0.0f) {
        const float k = 1.0f - d / feather;
        if (k <= 0.0f) {
          continue;
        }
        a = kMax * k * k;
      }
      const uint16_t host = t.swapped ? swap16(line[px]) : line[px];
      const uint16_t out =
          blend565(host, 0x0000, static_cast<uint32_t>(a * 256.0f));
      line[px] = t.swapped ? swap16(out) : out;
    }
  }
}

/** Put the kept square back at (x, y) with rounded corners over whatever is
 *  there now. */
void restoreRounded(const Target& t, const uint16_t* src, int x, int y,
                    int size, int radius) {
  for (int sy = 0; sy < size; ++sy) {
    const int py = y + sy;
    if (py < t.y0 || py >= t.y1) {
      continue;
    }
    uint16_t* line = t.buf + static_cast<size_t>(py) * kSide;
    const uint16_t* row = src + static_cast<size_t>(sy) * size;
    const bool corner_row = sy < radius || sy >= size - radius;
    for (int sx = 0; sx < size; ++sx) {
      const int px = x + sx;
      if (px < t.x0 || px >= t.x1) {
        continue;
      }
      float cov = 1.0f;
      if (corner_row && (sx < radius || sx >= size - radius)) {
        cov = 0.5f - roundBoxDistance(px + 0.5f, py + 0.5f, x, y, size, size,
                                      radius);
      }
      if (cov >= 1.0f) {
        line[px] = t.swapped ? swap16(row[sx]) : row[sx];
      } else if (cov > 0.0f) {
        const uint16_t host = t.swapped ? swap16(line[px]) : line[px];
        const uint16_t out =
            blend565(host, row[sx], static_cast<uint32_t>(cov * 256.0f));
        line[px] = t.swapped ? swap16(out) : out;
      }
    }
  }
}

/** Fill the glass from a `kGrid` square of colours, scaled up bilinearly over
 *  the whole panel, each row kept by `keep(row)` (0..1), dithered. */
constexpr int kGrid = 12;
struct Grid {
  float c[kGrid][kGrid][3];
};

void fillFromGrid(const Target& t, const Grid& grid, float keep_top,
                  float keep_bottom) {
  // Fixed point throughout -- 8.8 colour, 0..256 weights, the column lookups
  // made once -- since this covers the whole glass on every new cover: in
  // floats it was 200 ms of a track change.
  static int16_t col_x0[kSide];
  static uint16_t col_fx[kSide];
  static bool cols_ready = false;
  if (!cols_ready) {
    for (int px = 0; px < kSide; ++px) {
      float g = (px + 0.5f) * kGrid / kSide - 0.5f;
      g = g < 0.0f ? 0.0f : g > kGrid - 1 ? static_cast<float>(kGrid - 1) : g;
      col_x0[px] = static_cast<int16_t>(g);
      col_fx[px] = static_cast<uint16_t>((g - col_x0[px]) * 256.0f + 0.5f);
    }
    cols_ready = true;
  }
  int32_t row_c[kGrid + 1][3];
  for (int py = 0; py < kSide; ++py) {
    const int half = theme::chordHalfWidth(py);
    if (half <= 0) {
      continue;
    }
    const int xa = std::max(0, theme::kCenterX - half - 1);
    const int xb = std::min(kSide, theme::kCenterX + half + 1);
    const float keep =
        keep_top + (keep_bottom - keep_top) * (static_cast<float>(py) / kSide);
    float gy = (py + 0.5f) * kGrid / kSide - 0.5f;
    gy = gy < 0.0f ? 0.0f : gy > kGrid - 1 ? static_cast<float>(kGrid - 1) : gy;
    const int y0 = static_cast<int>(gy);
    const int y1 = y0 + 1 < kGrid ? y0 + 1 : y0;
    const float fy = gy - y0;
    for (int gx = 0; gx < kGrid; ++gx) {
      for (int ch = 0; ch < 3; ++ch) {
        const float v = (grid.c[y0][gx][ch] * (1.0f - fy) +
                         grid.c[y1][gx][ch] * fy) *
                        keep;
        row_c[gx][ch] = static_cast<int32_t>(v * 256.0f);  // 8.8
      }
    }
    for (int ch = 0; ch < 3; ++ch) {
      row_c[kGrid][ch] = row_c[kGrid - 1][ch];  // x0 + 1 past the last
    }
    const uint8_t* bayer = kBayer[py & 3];
    uint16_t* line = t.buf + static_cast<size_t>(py) * kSide;
    for (int px = xa; px < xb; ++px) {
      const int x0 = col_x0[px];
      const int32_t fx = col_fx[px];
      const int32_t* a = row_c[x0];
      const int32_t* b = row_c[x0 + 1];
      const int32_t r = (a[0] * (256 - fx) + b[0] * fx) >> 8;
      const int32_t g = (a[1] * (256 - fx) + b[1] * fx) >> 8;
      const int32_t bl = (a[2] * (256 - fx) + b[2] * fx) >> 8;
      // value * 31/255 (or 63/255) in 8.8, plus the Bayer step, floored.
      const int32_t d = bayer[px & 3] * 16 + 8;
      int32_t r5 = (((r * 7967) >> 16) + d) >> 8;
      int32_t g6 = (((g * 16191) >> 16) + d) >> 8;
      int32_t b5 = (((bl * 7967) >> 16) + d) >> 8;
      r5 = r5 > 31 ? 31 : r5;
      g6 = g6 > 63 ? 63 : g6;
      b5 = b5 > 31 ? 31 : b5;
      const uint16_t out = static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
      line[px] = t.swapped ? swap16(out) : out;
    }
  }
}

// Static rather than on the 8 KB loop stack: together they are over 5 KB.
Grid s_grid;
Grid s_tmp;
float s_sums[kGrid][kGrid][3];
int s_counts[kGrid][kGrid];

}  // namespace

void arc(float cx, float cy, float r, float half_thick, float deg0, float deg1,
         uint16_t color, uint8_t alpha, const Clip* clip) {
  Target t;
  if (!target(t, clip)) {
    return;
  }
  if (deg1 < deg0) {
    std::swap(deg0, deg1);
  }
  const float span = std::min(deg1 - deg0, 360.0f);
  const float end = deg0 + span;
  // The box the stretch can touch, walked rather than taken from its ends: a
  // stretch can bulge well past the box its ends make.
  float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
  for (float a = deg0;; a += 4.0f) {
    const float aa = a < end ? a : end;
    const float px = cx + cosf(aa * kRad) * r;
    const float py = cy + sinf(aa * kRad) * r;
    bx0 = std::min(bx0, px);
    by0 = std::min(by0, py);
    bx1 = std::max(bx1, px);
    by1 = std::max(by1, py);
    if (a >= end) {
      break;
    }
  }
  const float pad = half_thick + 2.0f;
  const int xa = std::max(t.x0, static_cast<int>(floorf(bx0 - pad)));
  const int ya = std::max(t.y0, static_cast<int>(floorf(by0 - pad)));
  const int xb = std::min(t.x1, static_cast<int>(ceilf(bx1 + pad)) + 1);
  const int yb = std::min(t.y1, static_cast<int>(ceilf(by1 + pad)) + 1);
  const float e0x = cx + cosf(deg0 * kRad) * r;
  const float e0y = cy + sinf(deg0 * kRad) * r;
  const float e1x = cx + cosf(end * kRad) * r;
  const float e1y = cy + sinf(end * kRad) * r;
  const float outer = r + half_thick + 1.0f;
  const float inner = std::max(0.0f, r - half_thick - 1.0f);
  const float outer2 = outer * outer;
  const float inner2 = inner * inner;
  for (int y = ya; y < yb; ++y) {
    const float fy = y + 0.5f - cy;
    const float fy2 = fy * fy;
    if (fy2 > outer2) {
      continue;
    }
    // Only the annulus's own pixels on this row: one span through it, or two
    // either side of its hole. Walking the whole box meant a full-panel scan
    // per ring at the rim.
    const float xo = sqrtf(outer2 - fy2);
    const float xi = fy2 < inner2 ? sqrtf(inner2 - fy2) : 0.0f;
    const int spans[2][2] = {
        {static_cast<int>(floorf(cx - xo)), static_cast<int>(ceilf(cx - xi))},
        {static_cast<int>(floorf(cx + xi)), static_cast<int>(ceilf(cx + xo))}};
    const int nspans = xi > 0.0f ? 2 : 1;
    for (int sp = 0; sp < nspans; ++sp) {
      const int from = std::max(xa, spans[sp][0]);
      const int to =
          std::min(xb, nspans == 1 ? spans[1][1] + 1 : spans[sp][1] + 1);
      for (int x = from; x < to; ++x) {
      const float fx = x + 0.5f - cx;
      const float d2 = fx * fx + fy2;
      if (d2 > outer2 || d2 < inner2) {
        continue;
      }
      const float d = sqrtf(d2);
      float along = atan2f(fy, fx) / kRad - deg0;
      along = fmodf(along, 360.0f);
      if (along < 0.0f) {
        along += 360.0f;
      }
      float dist;
      if (along <= span) {
        dist = fabsf(d - r);
      } else {
        const float a0 = hypotf(x + 0.5f - e0x, y + 0.5f - e0y);
        const float a1 = hypotf(x + 0.5f - e1x, y + 0.5f - e1y);
        dist = a0 < a1 ? a0 : a1;
      }
      const uint32_t w = weight(half_thick + 0.5f - dist, alpha);
      if (w != 0) {
        plot(t, x, y, color, w);
      }
      }
    }
  }
}

void disc(float cx, float cy, float r, uint16_t color, uint8_t alpha,
          const Clip* clip) {
  Target t;
  if (!target(t, clip)) {
    return;
  }
  const int xa = std::max(t.x0, static_cast<int>(floorf(cx - r - 1.0f)));
  const int ya = std::max(t.y0, static_cast<int>(floorf(cy - r - 1.0f)));
  const int xb = std::min(t.x1, static_cast<int>(ceilf(cx + r + 1.0f)) + 1);
  const int yb = std::min(t.y1, static_cast<int>(ceilf(cy + r + 1.0f)) + 1);
  for (int y = ya; y < yb; ++y) {
    const float fy = y + 0.5f - cy;
    for (int x = xa; x < xb; ++x) {
      const float fx = x + 0.5f - cx;
      const uint32_t w =
          weight(r + 0.5f - sqrtf(fx * fx + fy * fy), alpha);
      if (w != 0) {
        plot(t, x, y, color, w);
      }
    }
  }
}

void roundRect(float x, float y, float w, float h, float r, uint16_t color,
               uint8_t alpha) {
  Target t;
  if (!target(t, nullptr) || w <= 0.0f || h <= 0.0f) {
    return;
  }
  r = std::min(r, std::min(w, h) * 0.5f);
  const int xa = std::max(t.x0, static_cast<int>(floorf(x)) - 1);
  const int ya = std::max(t.y0, static_cast<int>(floorf(y)) - 1);
  const int xb = std::min(t.x1, static_cast<int>(ceilf(x + w)) + 1);
  const int yb = std::min(t.y1, static_cast<int>(ceilf(y + h)) + 1);
  for (int py = ya; py < yb; ++py) {
    for (int px = xa; px < xb; ++px) {
      const uint32_t wgt = weight(
          0.5f - roundBoxDistance(px + 0.5f, py + 0.5f, x, y, w, h, r), alpha);
      if (wgt != 0) {
        plot(t, px, py, color, wgt);
      }
    }
  }
}

void roundCorners(int x, int y, int size, float r, uint16_t outside) {
  Target t;
  if (!target(t, nullptr) || size <= 0) {
    return;
  }
  const int span = static_cast<int>(ceilf(r)) + 1;
  for (int py = y; py < y + size; ++py) {
    if (py < t.y0 || py >= t.y1 || (py >= y + span && py < y + size - span)) {
      continue;
    }
    for (int px = x; px < x + size; ++px) {
      if (px < t.x0 || px >= t.x1 ||
          (px >= x + span && px < x + size - span)) {
        continue;
      }
      const float d =
          roundBoxDistance(px + 0.5f, py + 0.5f, x, y, size, size, r);
      const uint32_t w = weight(d + 0.5f, 255);
      if (w != 0) {
        plot(t, px, py, outside, w);
      }
    }
  }
}

void mask(int x, int y, int w, int h, const uint8_t* coverage,
          uint16_t color) {
  Target t;
  if (!target(t, nullptr) || coverage == nullptr) {
    return;
  }
  for (int my = 0; my < h; ++my) {
    const int py = y + my;
    if (py < t.y0 || py >= t.y1) {
      continue;
    }
    const uint8_t* row = coverage + static_cast<size_t>(my) * w;
    for (int mx = 0; mx < w; ++mx) {
      const int px = x + mx;
      if (px >= t.x0 && px < t.x1 && row[mx] != 0) {
        plot(t, px, py, color, row[mx] + (row[mx] >> 7));
      }
    }
  }
}

void coverBackdrop(int x, int y, int size, int radius) {
  Target t;
  if (!target(t, nullptr) || size <= 0) {
    return;
  }
  const size_t need = static_cast<size_t>(size) * size;
  if (s_cover_cap < need) {
    heap_caps_free(s_cover);
    s_cover = static_cast<uint16_t*>(heap_caps_malloc(
        need * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_cover_cap = s_cover != nullptr ? need : 0;
  }
  if (s_cover == nullptr) {
    return;  // leaves the sharp square on black: still a cover
  }
  // Keep the square, in host order, and pool it into the grid as it goes.
  memset(s_sums, 0, sizeof(s_sums));
  memset(s_counts, 0, sizeof(s_counts));
  auto& sums = s_sums;
  auto& counts = s_counts;
  for (int sy = 0; sy < size; ++sy) {
    const int py = y + sy;
    const uint16_t* line = t.buf + static_cast<size_t>(py) * kSide + x;
    uint16_t* row = s_cover + static_cast<size_t>(sy) * size;
    const int gy = sy * kGrid / size;
    for (int sx = 0; sx < size; ++sx) {
      const uint16_t c = t.swapped ? swap16(line[sx]) : line[sx];
      row[sx] = c;
      if (((sx | sy) & 1) == 0) {
        const int gx = sx * kGrid / size;
        float r, g, b;
        unpack8(c, r, g, b);
        sums[gy][gx][0] += r;
        sums[gy][gx][1] += g;
        sums[gy][gx][2] += b;
        ++counts[gy][gx];
      }
    }
  }
  for (int gy = 0; gy < kGrid; ++gy) {
    for (int gx = 0; gx < kGrid; ++gx) {
      const float n = counts[gy][gx] > 0 ? counts[gy][gx] : 1;
      for (int ch = 0; ch < 3; ++ch) {
        s_grid.c[gy][gx][ch] = sums[gy][gx][ch] / n;
      }
    }
  }
  // Two passes of a [1 2 1] kernel each way, then a little more colour: a
  // blur greys a cover out, and Apple's grounds are richer than their art.
  for (int pass = 0; pass < 2; ++pass) {
    Grid& tmp = s_tmp;
    tmp = s_grid;
    for (int gy = 0; gy < kGrid; ++gy) {
      for (int gx = 0; gx < kGrid; ++gx) {
        const int l = gx > 0 ? gx - 1 : gx;
        const int r = gx < kGrid - 1 ? gx + 1 : gx;
        for (int ch = 0; ch < 3; ++ch) {
          s_grid.c[gy][gx][ch] = (tmp.c[gy][l][ch] + 2 * tmp.c[gy][gx][ch] +
                                  tmp.c[gy][r][ch]) * 0.25f;
        }
      }
    }
    tmp = s_grid;
    for (int gy = 0; gy < kGrid; ++gy) {
      const int u = gy > 0 ? gy - 1 : gy;
      const int d = gy < kGrid - 1 ? gy + 1 : gy;
      for (int gx = 0; gx < kGrid; ++gx) {
        for (int ch = 0; ch < 3; ++ch) {
          s_grid.c[gy][gx][ch] = (tmp.c[u][gx][ch] + 2 * tmp.c[gy][gx][ch] +
                                  tmp.c[d][gx][ch]) * 0.25f;
        }
      }
    }
  }
  constexpr float kSaturate = 1.35f;
  for (int gy = 0; gy < kGrid; ++gy) {
    for (int gx = 0; gx < kGrid; ++gx) {
      float* c = s_grid.c[gy][gx];
      const float lum = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
      for (int ch = 0; ch < 3; ++ch) {
        const float v = lum + (c[ch] - lum) * kSaturate;
        c[ch] = v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v;
      }
    }
  }
  // Darker towards the foot, where the transport sits on it, and darker
  // overall the lighter the cover: its brightest patch is brought down to
  // about 100 of 255, so white text reads on it with no shadow.
  float brightest = 1.0f;
  for (int gy = 0; gy < kGrid; ++gy) {
    for (int gx = 0; gx < kGrid; ++gx) {
      const float* c = s_grid.c[gy][gx];
      brightest = std::max(brightest,
                           0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]);
    }
  }
  const float keep_top = std::max(0.30f, std::min(0.62f, 100.0f / brightest));
  fillFromGrid(t, s_grid, keep_top, keep_top * 0.68f);
  shadow(t, x, y, size, radius);
  restoreRounded(t, s_cover, x, y, size, radius);
}

void plainBackdrop(int x, int y, int size, int radius) {
  Target t;
  if (!target(t, nullptr)) {
    return;
  }
  for (int gy = 0; gy < kGrid; ++gy) {
    for (int gx = 0; gx < kGrid; ++gx) {
      for (int ch = 0; ch < 3; ++ch) {
        s_grid.c[gy][gx][ch] = ch == 2 ? 52.0f : 46.0f;  // a cool dark grey
      }
    }
  }
  fillFromGrid(t, s_grid, 1.0f, 0.30f);
  shadow(t, x, y, size, radius);
  roundRect(x, y, size, size, radius, 0x2965);  // #2C2C2E
  // Two beamed quavers, as Apple's placeholder: heads, stems, a beam.
  const float s = static_cast<float>(size);
  const float cx = x + s * 0.5f;
  const float cy = y + s * 0.5f;
  constexpr uint16_t kNote = 0x8C71;  // #8E8E93
  const float head = s * 0.085f;
  const float stem = s * 0.035f;
  const float lx = cx - s * 0.11f;
  const float rx = cx + s * 0.15f;
  const float hy = cy + s * 0.16f;
  const float top = cy - s * 0.2f;
  disc(lx - head * 0.6f, hy, head, kNote);
  disc(rx - head * 0.6f, hy - s * 0.03f, head, kNote);
  roundRect(lx + head * 0.4f - stem, top, stem, hy - top, stem * 0.5f, kNote);
  roundRect(rx + head * 0.4f - stem, top, stem, hy - s * 0.03f - top,
            stem * 0.5f, kNote);
  roundRect(lx + head * 0.4f - stem, top, rx - lx + stem, s * 0.075f,
            s * 0.02f, kNote);
}

}  // namespace ui::paint
