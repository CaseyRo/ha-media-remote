---
name: Puck 1 (ha-media-remote, round AMOLED)
description: Apple Music's Now Playing, played straight, on a 466 px round glass puck that remotes one Home Assistant speaker.
colors:
  ground-black: "#000000"
  surface-cell: "#1C1C1E"
  surface-raised: "#2C2C2E"
  text-primary: "#FFFFFF"
  text-secondary: "#A4A6A4"
  text-secondary-on-blur: "#BDBDBD"
  text-muted: "#636366"
  glyph-pressed: "#8E8E93"
  volume-muted: "#848284"
  state-good: "#30D158"
  state-warning: "#FF9F0A"
  state-error: "#FF453A"
typography:
  title:
    fontFamily: "Inter SemiBold, Noto Sans (fallback), Noto Sans KR (Korean)"
    fontSize: "39px"
    fontWeight: 600
    lineHeight: "39px"
  body:
    fontFamily: "Inter Regular, Noto Sans (fallback), Noto Sans KR (Korean)"
    fontSize: "33px"
    fontWeight: 400
    lineHeight: "33px"
  label:
    fontFamily: "Inter Medium, Noto Sans (fallback)"
    fontSize: "29px"
    fontWeight: 500
    lineHeight: "29px"
rounded:
  thumb: "8px"
  key: "10px"
  cover: "14px"
  row: "19px"
  pill: "9999px"
spacing:
  hairline: "2px"
  row-gap: "8px"
  status-line-gap: "12px"
  row-inset: "19px"
  text-inset: "23px"
  thumb-gap: "19px"
components:
  cover-now-playing:
    rounded: "{rounded.cover}"
    size: "163px"
  list-row:
    backgroundColor: "{colors.surface-cell}"
    textColor: "{colors.text-primary}"
    typography: "{typography.body}"
    rounded: "{rounded.row}"
    height: "89px"
    width: "342px"
  list-row-current:
    backgroundColor: "{colors.surface-raised}"
    textColor: "{colors.text-primary}"
    rounded: "{rounded.row}"
    height: "89px"
  list-row-pressed:
    backgroundColor: "{colors.text-primary}"
    textColor: "{colors.ground-black}"
    rounded: "{rounded.row}"
    height: "89px"
  list-thumb:
    backgroundColor: "{colors.surface-raised}"
    rounded: "{rounded.thumb}"
    size: "74px"
  list-header:
    textColor: "{colors.text-secondary}"
    typography: "{typography.label}"
  back-button:
    backgroundColor: "{colors.surface-raised}"
    textColor: "{colors.text-primary}"
    rounded: "{rounded.pill}"
    size: "42px"
  key:
    backgroundColor: "{colors.surface-cell}"
    textColor: "{colors.text-primary}"
    typography: "{typography.body}"
    rounded: "{rounded.key}"
    width: "52px"
    height: "50px"
  key-primary-ready:
    backgroundColor: "{colors.text-primary}"
    textColor: "{colors.ground-black}"
    rounded: "{rounded.key}"
    width: "252px"
    height: "54px"
  key-primary-empty:
    backgroundColor: "{colors.surface-cell}"
    textColor: "{colors.text-muted}"
    rounded: "{rounded.key}"
    width: "252px"
    height: "54px"
  search-type-pill:
    backgroundColor: "{colors.surface-cell}"
    textColor: "{colors.text-primary}"
    typography: "{typography.label}"
    rounded: "{rounded.pill}"
    height: "39px"
---

# Design System: Puck 1 (ha-media-remote, round AMOLED)

Scope: the round 466 x 466 AMOLED panels of this fork (`amoled466-s3`, `amoled466c-s3`; the Waveshare ESP32-S3-Touch-AMOLED-1.75C, about 44.5 mm across at about 10.5 px/mm). The other boards in the fork share `include/ui/theme.h` and must keep building, but they are not the design target. The square layout (`src/ui/square/`) still carries the older pill, scrim and text-glow look, and nothing in it is described here. This is embedded firmware with no CSS. LovyanGFX composes every screen into an RGB565 frame. Tokens live in `include/ui/theme.h` as values for a 240 px design grid, scaled by `theme::px()` / `theme::textPx()` (x 1.94 on this panel). Every pixel figure in this file is the value at 466 px. Colour hexes are the intended Apple values. The panel shows their nearest RGB565 codes, kept in the sidecar.

## Overview

**Creative North Star: "The Cover Is the Screen"**

This is Apple Music's Now Playing as it looks on Apple Watch, at desk scale, played straight. The album cover fills the round glass twice. First as the ground: the whole cover blurred, a little more saturated and darkened. Then sharp at the top centre, with small rounded corners and a soft shadow. Everything else is white Inter set straight on that ground. The title and artist sit under the cover, with a thin scrubber, three plain filled glyphs, and one thin white volume ring at the rim. No cards, panels or chrome surround the controls. Leaving the cover behind, the world turns into Apple's dark system UI: black ground, inset grouped rows in charcoal greys, white text, and grey secondary text. Colour appears only to report a state. Because black pixels on the AMOLED are off, the round edge of the glass is the only frame the design has.

The device is glanced at from arm's length or across a desk, so legibility beats density. Type is large for a watch-like face (title 39 px, body 33 px), and the cover was made smaller than the mock so the type could grow. Motion is limited to the panel's own sleep and wake fade.

**Key Characteristics:**
- The blurred, darkened cover is the ground of now playing and standby. With no cover, a neutral dark ground and a music-note tile take its place.
- The sharp cover keeps one position (163 px square at the top centre, y 41) on both now playing and standby.
- White Inter at three weights. Secondary text is grey, with a brighter grey on the blur.
- Every curve and edge on the frame is anti-aliased, and every smooth field is dithered.
- Off now playing: black ground, charcoal inset grouped rows, a white pill for the primary or pressed control.
- No brand accent. Green, orange and red mean charging, degraded and error or low battery, and nothing else.

## Colors

Apple's dark system palette, monochrome by design. White and greys carry everything, and three system colours are reserved for state.

### Primary
- **White** (text-primary): all primary text, the transport glyphs, the volume ring fill and knob, the unlock ring, the scrubber fill. It is also the system's only "accent": a primary control that is ready (Search) and a pressed list row become a white pill with black text, as on watchOS.

### Neutral
- **Ground Black** (ground-black): the ground of every screen except now playing and standby, and of status cards and alerts. It is off-pixel black, so on this panel it is the bezel too.
- **Cell Charcoal** (surface-cell): list rows, settings rows, search result rows, keyboard keys, the search-type pill, the Search key while it is empty.
- **Raised Charcoal** (surface-raised): the current settings row, the back button's disc, the note tile behind missing art.
- **Secondary Grey** (text-secondary): grey on black at about 65% white. Used for list section headings, setting values, status labels ("Battery", "Rx"), alert detail lines, the scroll-arc thumb, and the padlock on standby.
- **Secondary Grey on Blur** (text-secondary-on-blur): the artist, elapsed and remaining times, and the standby battery percentage. It is a step brighter than Secondary Grey so it holds up on a light cover.
- **Muted Grey** (text-muted): disabled glyphs, the empty search query placeholder, the empty Search key's label.
- **Pressed Grey** (glyph-pressed): a transport glyph under the finger, and the quavers on the note tile.
- **Muted Volume Grey** (volume-muted): the volume fill while muted or while the volume device is off. The level stays visible, greyed.

### State
- **System Green** (state-good): charging. Used for the battery cell fill on standby and the battery value on the status page.
- **System Orange** (state-warning): degraded. Used for setup and alert titles, "Off here" on the top line, and warnings in the times line.
- **System Red** (state-error): failed or low. Used for the battery at 20% or less.

### Named Rules
**The Colour Means State Rule.** Hue appears only when it reports a state: green for charging, orange for degraded, red for error or low battery. Nothing else is coloured, whether decoration, emphasis, links or selection. Selection is Raised Charcoal, and pressed is white.

**The No Brand Accent Rule.** The accent is white. Never add a brand hue or tint controls from the cover. The cover's colour lives only in the blurred ground.

## Typography

**Title / Body / Label Font:** Inter (OFL, `assets/fonts/Inter-*.otf`, licence in `assets/fonts/Inter-OFL.txt`), rendered natively per pixel size as VLW faces (`data/ui_font_29/33/39.vlw` carry the round design; `ui_font_47.vlw` is embedded for the shared title token but no round screen draws at 47 px) and never scaled. Noto Sans is the fallback, and Korean comes from Noto Sans KR as an outline font.

**Character:** a neutral grotesque that stands in for SF Pro, which cannot ship on the puck. Weight does the hierarchy work and colour does the rest.

Sizes are line heights (ascent plus descent), produced by `theme::textPx()` from the 240 px grid: 15 becomes 29, 17 becomes 33 and 20 becomes 39 on the round screens. Each extra size costs flash, so a new size needs a reason, plus a matching entry in `platformio.ini` and the font table.

### Hierarchy
- **Title** (SemiBold, 39 px): the now-playing track title, centred, at most two lines and then an ellipsis. Also status card and status page titles, and the search query.
- **Body** (Regular, 33 px): the artist, list row names, list and settings page titles, setting names, status body lines, key letters.
- **Label** (Medium, 29 px): elapsed and remaining times, the top line (battery percentage, the rooms line), list section headings, setting values, the search-type pill, QWERTY key letters.

### Named Rules
**The Step Down Before Cut Rule.** Where a single line will not fit, step down one size before ellipsizing. The whole name at the smaller size beats half of it at the larger. Example: the status page title drops from 39 to 33.

**The Two Lines Then Ellipsis Rule.** The title wraps to two lines at most and then ellipsizes. Every line is clipped to the chord of the circle at its own y.

## Layout

The panel is a 466 px circle, and every horizontal measure is a chord: `theme::chordHalfWidth(y)` gives the room at any row, and text is clipped to it less an inset. Everything is centred on x 233.

**Now playing, top to bottom:**
- Top line at y 21. Awake, it is the rooms line as plain text ("+2 rooms", or "Off here" in orange). On standby it is the battery.
- Cover: 163 px square, top centre, y 41 to 204.
- Title and artist, centred together in the band from y 212 to 324. The artist is 2 px under the title.
- Scrubber at y 334: 240 px wide, 6 px tall, fully rounded.
- Elapsed (left-aligned to the scrubber's start) and remaining (right-aligned to its end, with a leading minus) at y 355. A status message replaces both when there is no time to show.
- Transport at y 410: previous, play/pause, next, 105 px apart. The play glyph is 42 px tall and the skip glyphs 32 px. Each has a 50 px hit radius, under half the spacing, so targets never overlap.
- Volume: a 4 px ring at radius 225 to 229, sweeping 270 degrees centred on 12 o'clock. Its ends sit at the lower left and lower right, which leaves the gap across the foot for the transport.

**Standby** is the same frame with the scrubber, times, glyphs and volume ring removed. The battery (a drawn cell plus percentage) and, while locked, a padlock go on the top line. Panel brightness drops to 25.

**The Cover Never Moves Rule.** The cover's size and position are identical on now playing and standby. Only the controls leave. Any change to one layout is a change to both.

**Lists** (browse, search results, settings): black ground. The list starts at y 62, which keeps it clear of the narrow top of the circle. Rows are 89 px tall with 8 px between them. Every row through the middle shares one width (342 px, `kListRowMaxHalfWidth`), so all rows share a single text column, as in Apple's grouped lists. Only rows near the rim narrow with the glass, and they never go below 234 px, letting the bezel cut their corners rather than pinching them to a sliver. Section headings are a line of text, not a card. The scroll indicator is a thin arc riding the right-hand bezel.

**Status page and cards:** centred lines stacked around the screen's centre, with 12 px between lines. On the status page, label and value sit on one centred line.

**Touch targets:** at least 44 px. Hit areas are drawn generously around small glyphs (back button: 42 px disc, 86 px hit circle).

## Elevation & Depth

Depth comes from two things only: the cover's shadow on its blurred ground, and tonal steps between greys off now playing. There is no other shadow, glow or blur anywhere.

### Shadow Vocabulary
- **Cover shadow**: a soft rounded-box shadow under the sharp cover. It is offset down by 5% of the cover's size (8 px) and feathers out over 13% of it (21 px). It is darkest (50% black) right under the square and falls off quadratically. Used for the now-playing and standby cover, and the no-cover note tile in its place.

### The Ground
- **Blurred cover**: the sharp cover is pooled into a 12 x 12 grid. That grid gets two [1 2 1] passes each way and a 1.65x saturation boost, so the ground is richer than the art, as Apple's is. It is then scaled bilinearly over the whole glass and darkened. The darkening adapts: the brightest patch is brought to about 100 of 255 (keeping 30% to 62% of the colour at the top) and falls to 68% of that at the foot, where the transport sits. White text therefore reads on any cover without help.
- **No-cover ground**: a neutral dark grey (46 of 255), full at the top and falling to 30% at the foot, with the note tile in the cover's place.

### Named Rules
**The No Text Glow Rule.** Text on the blur gets no shadow, glow, halo or scrim. The adaptive darkening is what makes white text legible. If a cover defeats it, tune the darkening, never the text.

**The Tonal Step Rule.** Off now playing, raise by stepping grey (black, then Cell Charcoal, then Raised Charcoal), never by shadow.

## Shapes

Small, Apple-sized radii on rectangles, full rounds on pills and discs, and round ends on every ring and line. The circle of the glass is the dominant shape, and rings follow it.

- Cover: 14 px corners. List rows: 19 px. Keys and the Search key: 10 px. Thumbnails and the note tile in rows: 8 px. Pills (search type) and the back button: fully round.
- Rings (volume, unlock, scroll arc) are concentric with the glass and have round ends. The volume ring has an 8 px round knob at its level. Without the knob it reads as a readout rather than something you can drag.
- Glyphs are plain filled shapes with no disc, circle or outline behind them: triangles and bars for transport, a drawn padlock, battery cell and bolt, a chevron in the back button's disc, the beamed-quaver note tile.

## Components

### Now playing
- **Cover:** see Layout and Elevation. The sharp square is restored over its own shadow, with anti-aliased corners.
- **Title and artist:** white SemiBold title, artist in Secondary Grey on Blur. They are centred together as one block, and the block grows from its centre when the title wraps.
- **Scrubber:** the track is white at 25% opacity, so the cover's colour shows through. The fill is white at 94%. Both are fully rounded. A sideways drag starting within 31 px above or below it, or just past either end, seeks.
- **Transport glyphs:** previous, play/pause, next. They are white and plain, with no circles. Pressed is the glyph dimming to Pressed Grey under the finger: no bounce, no scale. Disabled is Muted Grey.
- **Volume ring:** the track is white at about 20% opacity, so it takes on the cover's colour. The fill and knob are white, or Muted Volume Grey while muted or off. A horizontal swipe anywhere in the top half drives it by the distance the finger travels. There is no hit band on the ring.

### Standby status line
The padlock (Secondary Grey) while locked. The battery is a drawn cell: outline at 35% white, fill in white, green while charging, red at 20% or less. While charging, a white bolt is drawn over the fill, because a green fill alone reads as "healthy". The percentage follows in Label weight, in Secondary Grey on Blur.

### Unlock ring
The same white ring as the volume, 2 px heavier, filling clockwise from 12 o'clock as the unlock is held. It is drawn incrementally, roughly a tenth of a turn at a time.

### List rows (browse, search results)
- **Shape:** Cell Charcoal fill, 19 px corners, 89 px tall, anti-aliased.
- **Content:** a 74 px thumbnail with 8 px corners, then the name in white Body, ellipsized.
- **No art:** the thumbnail slot keeps its place and shows the note tile, so every row's text starts in the same column.
- **Section heading:** plain Label text in Secondary Grey, centred, with no card.
- **Back:** a 42 px Raised Charcoal disc with a white chevron, at the left of the page's title line.
- **Scroll indicator:** a 3 px arc at the right-hand rim. The track is white at about 15% and the thumb is Secondary Grey.

### Settings rows
Same shape as list rows. The text is centred, watchOS's way round: the setting's name first, in white Body, and its value under it in Label weight, Secondary Grey. The current row is Raised Charcoal. A pressed row is white with black text.

### Search
- **Keys:** Cell Charcoal, 52 x 50 px with 10 px corners and a 4 px gap. White letters, Body weight (Label weight on the narrower QWERTY keys). Four alphabetical rows of seven keys, fitted to the chord at each row.
- **Search key:** one wide key, 252 x 54 px, in sentence case ("Search"). It is white with black text once there is a query, and Cell Charcoal with a Muted Grey label while the query is empty.
- **Type pill:** a fully round Cell Charcoal pill showing what is being searched for (Artist, Album, Track, Playlist, Radio) with a small arrowhead. Tapping it steps to the next type.
- **Query line:** Title weight, white. The placeholder is in Muted Grey.

### Status page and cards
- **Status page:** the device name as title, white, stepping down a size before it truncates. Below it, label/value lines in Body weight. Labels are Secondary Grey and values white, or a state colour (battery green while charging, red at 20% or less). The back button sits at the left and a grey gear at the right of the title line.
- **Alerts and setup cards:** black ground, the title in System Orange, the detail lines in Secondary Grey. Never a full screen of colour.
- **Plain cards:** black ground, white title, Secondary Grey detail.

### Motion
The only motion is the panel's brightness fade on sleep and wake: 8 steps over about 150 ms, sent as controller brightness commands with no redraw. Everything else changes in a single frame.

## Do's and Don'ts

### Do:
- **Do** set the blurred, darkened cover as the ground of now playing and standby, and the neutral dark ground with the note tile when there is no cover.
- **Do** keep the cover at 163 px, top centre, y 41, on both now playing and standby.
- **Do** anti-alias every ring, line, glyph, rounded corner and disc that goes on the frame (`ui/paint.h`, or `fillSmooth*`), and ordered-dither (4 x 4 Bayer) every smooth gradient field, because RGB565 bands otherwise.
- **Do** use colour only for state: System Green for charging, System Orange for degraded, System Red for error and low battery.
- **Do** keep list and settings rows at one shared width through the middle, with one text column and a thumbnail slot that keeps its place.
- **Do** show a press by dimming the glyph to Pressed Grey, or by turning a row or key white with black text.
- **Do** step text down one embedded size before ellipsizing a single-line name.
- **Do** write labels in sentence case ("Search", "Off here").

### Don't:
- **Don't** put a shadow, glow, halo or scrim behind text on the blurred cover. Tune the ground's darkening instead.
- **Don't** put panels, cards, pills or discs behind anything on now playing. The rooms line, times and glyphs sit straight on the blur.
- **Don't** add a brand accent or tint controls with the cover's colour. The accent is white.
- **Don't** add motion: no transitions, bounces, slides or animated crossfades beyond the existing sleep/wake fade. A cover crossfade on track change is allowed only if it costs no response time, and none ships today.
- **Don't** move the cover between now playing and standby, or bring back the speaker's name: the puck only ever has its one player.
- **Don't** draw hard-edged (LovyanGFX non-smooth) shapes on the round frame, or leave a smooth gradient undithered.
- **Don't** fill a whole screen with a state colour. Alerts are black, with the title in the state's colour.
- **Don't** add a new type size or weight without embedding its VLW face for this board and keeping `platformio.ini` and the font table in step.
