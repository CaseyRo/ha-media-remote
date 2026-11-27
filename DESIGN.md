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

<!-- impeccable:design-map -->
Name: Puck 1 (ha-media-remote, round AMOLED)
Colors: primary: text-primary #FFFFFF; neutral: ground-black #000000; state: state-good #30D158; neutral: surface-cell #1C1C1E; neutral: surface-raised #2C2C2E; neutral: text-secondary #A4A6A4 (+6 more in the sidecar)
Fonts: Inter SemiBold (title); Inter Regular (body); Inter Medium (label)
Spacing: hairline 2px, row-gap 8px, status-line-gap 12px, row-inset 19px, text-inset 23px, thumb-gap 19px
Radius: thumb 8px, key 10px, cover 14px, row 19px, pill 9999px
North Star: "The Cover Is the Screen"
Rules:
- The Colour Means State Rule. Hue appears only when it reports a state: green for charging, orange for degraded, red for error or low battery.
- The No Brand Accent Rule. The accent is white.
- The Step Down Before Cut Rule. Where a single line will not fit, step down one size before ellipsizing.
- The Two Lines Then Ellipsis Rule. The title wraps to two lines at most and then ellipsizes.
- The Cover Never Moves Rule. The cover's size and position are identical on now playing and standby.
- The No Text Glow Rule. Text on the blur gets no shadow, glow, halo or scrim.
- (+1 more)
Elevation:
- Cover shadow: a soft rounded-box shadow under the sharp cover.
- (+2 more)
Don't:
- Don't put a shadow, glow, halo or scrim behind text on the blurred cover.
- Don't put panels, cards, pills or discs behind anything on now playing.
- Don't add a brand accent or tint controls with the cover's colour.
- Don't add motion: no transitions, bounces, slides or animated crossfades beyond the existing sleep/wake fade and the loading face's travelling arc.
- Don't move the cover between now playing and standby, or bring back the speaker's name: the puck only ever has its one player.
- Don't draw hard-edged (LovyanGFX non-smooth) shapes on the round frame, or leave a smooth gradient undithered.
- Don't fill a whole screen with a state colour.
- Don't add a new type size or weight without embedding its VLW face for this board and keeping `platformio.ini` and the font table in step.
Map: include/ui/theme.h
Map: src/ui/square/
Map: assets/fonts/Inter-OFL.txt
Map: data/ui_font_29.vlw
Map: data/ui_font_33.vlw
Map: data/ui_font_39.vlw
Map: platformio.ini
Map: include/ui/paint.h
Full: .impeccable/design-full.md (read a section when you need its detail)
