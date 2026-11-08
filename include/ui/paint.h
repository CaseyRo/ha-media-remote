#pragma once

#include <cstdint>

namespace ui::paint {

/**
 * Anti-aliased drawing straight into the canvas frame, for the parts of the
 * round now playing that LovyanGFX would draw with hard edges: the rings at
 * the rim, the scrubber, the transport glyphs, and the cover's backdrop,
 * rounded corners and shadow.
 *
 * Every call blends into the frame by coverage and does nothing when there is
 * no frame (canvasReady() false). Coordinates are the frame's; a pixel's centre
 * is at (x + 0.5, y + 0.5).
 */

/** A box the drawing is held inside, or the whole panel when null. */
struct Clip {
  int x;
  int y;
  int w;
  int h;
};

/** A ring segment `half_thick` either side of radius `r`, from `deg0` to
 *  `deg1` (LovyanGFX's angles: 0 at three o'clock, clockwise), with round
 *  ends. */
void arc(float cx, float cy, float r, float half_thick, float deg0, float deg1,
         uint16_t color, uint8_t alpha = 255, const Clip* clip = nullptr);

/** A filled disc. */
void disc(float cx, float cy, float r, uint16_t color, uint8_t alpha = 255,
          const Clip* clip = nullptr);

/** A filled triangle, its edges supersampled. */
void triangle(float x0, float y0, float x1, float y1, float x2, float y2,
              uint16_t color);

/** Apple's placeholder for missing art: a `size` tile of radius `radius` at
 *  (x, y) with two beamed quavers on it. */
void noteTile(int x, int y, int size, float radius);

/** A lightning bolt in the `w` x `h` box at (x, y): charging. */
void bolt(float x, float y, float w, float h, uint16_t color);

/** A filled rectangle with corners of radius `r`. */
void roundRect(float x, float y, float w, float h, float r, uint16_t color,
               uint8_t alpha = 255);

/** Round the corners of the `size` square at (x, y) to radius `r` by
 *  painting what lies outside them with `outside` -- the tile a thumbnail
 *  sits on. */
void roundCorners(int x, int y, int size, float r, uint16_t outside);

/** `color` laid over the frame at (x, y) by `coverage`, `w` x `h` bytes,
 *  row-major, 255 for solid. */
void mask(int x, int y, int w, int h, const uint8_t* coverage, uint16_t color);

/**
 * The cover's frame for now playing. Call with the cover already drawn
 * sharp into its `size` square at (x, y): fills the whole glass behind it
 * with the same cover, heavily blurred, darkened and dithered (RGB565 bands
 * in a smooth field otherwise), then lays a soft shadow under the square and
 * puts the cover back with corners of radius `radius`. One decode, no second
 * one: the blur is taken from the square's own pixels.
 */
void coverBackdrop(int x, int y, int size, int radius);

/** As coverBackdrop() with no cover: a dark neutral ground, and in the
 *  cover's place a tile with a music note on it. */
void plainBackdrop(int x, int y, int size, int radius);

}  // namespace ui::paint
