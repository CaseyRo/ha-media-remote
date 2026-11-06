# Design brief: Puck 1, Apple Music straight

Confirmed by Casey on 6 Nov 2026 (impeccable shape). Direction chosen from four on
https://claude.ai/artifact/RfnQdBE9cUVpWob67nvxcn.

1. **Job and audience.** Casey at his desk, glancing from arm's length or across
   the desk; later the caravan with other media. Operate mode: know what plays,
   nudge it.
2. **Outcome and proof.** Cover, title and artist read at a glance; the three
   controls hit first time; finished enough to stay out all day. Judged on the
   real panel with real Music Assistant covers.
3. **Direction.** Apple Music's now playing as on Apple Watch: big cover top
   centre, title and artist under it, a scrubber with elapsed and remaining, three
   plain glyphs (no circles, no chrome), the **blurred cover** behind it all
   (Casey's choice over a tinted gradient), volume as a thin ring at the rim, the
   speaker name small at the bottom (dropped in the build: see FIRST VIEWPORT).
   Type: Inter (SF Pro may not ship on the puck).
4. **Scope.** Every screen in one pass: now playing, standby, the swipe-up list,
   search, status page, settings, Wi-Fi join, player pick, status cards, lock hint
   and unlock ring. Unchanged: gestures, timings, both buttons, HA only; the other
   boards keep building. Anti-goals: slower to respond, the cover losing its
   place, gimmicky motion.
5. **States.** Titles wrap to two lines, then an ellipsis; no artist; no cover
   (neutral ground, music glyph); paused, idle, buffering, player gone, Wi-Fi
   reconnecting; locked, charging, low battery; later no duration (no scrubber)
   and h:mm:ss; Korean titles.
6. **Layout and feedback.** Standby keeps the cover exactly where it is: it only
   drops scrubber and controls and adds battery % at the top. Targets at least
   44 px; a touch dims the glyph briefly, no bounce. Lists and settings as Apple
   inset grouped rows with a small rounded cover. The unlock ring is the same thin
   white ring as volume. Motion only where it already is (sleep/wake fade); a
   cover crossfade on track change only if it costs no response time.
7. **Constraints.** RGB565 bands smooth gradients (dither or keep ranges short).
   Inter as VLW at no more sizes than today; Noto stays as fallback. White text,
   no brand accent; colour only for state: green charging, orange degraded, red
   error and low battery.

## Direction contract

THESIS: the cover is the screen. A round puck should read like Apple Watch's Now Playing at desk scale, never a phone app squeezed into a circle: no cards, no panels, no chrome around controls.

OWN-WORLD: the cover, blurred full-bleed and darkened, is the ground; the sharp cover sits on it with a soft shadow and small corner radius. White Inter in three weights, secondary text at 60% white, one thin white volume ring at the rim. Lists and settings are Apple inset grouped rows on near-black. Colour only means state: green charging, orange degraded, red error.

STORY: Casey glances, knows what plays and how loud, touches a plain glyph and sees it answer at once.

FIRST VIEWPORT: now playing on the 466 px circle. Cover 163 px square, radius 14, top centre at y 41, on its own blur with a soft shadow; the title (SemiBold 39 px line, wraps to two lines) and the artist (Regular, 65% white) centred together in the band under it; a 6 px scrubber 240 px wide with elapsed left and remaining right; previous, play/pause, next as plain filled glyphs 105 px apart with 50 px hit radii; volume as a 4 px white ring over 270 degrees with a small knob. Two changes from the mock, both for the panel's 44 mm: the cover is smaller than the mock's 196 px so the type can be large enough to read across a desk, and the speaker name is left out, since the puck only ever has its one player (principle 2) and the band it needed went to the title.

FORM: canon (the category standard, played straight), bar set by Apple Music, Apple Watch Now Playing and Music Assistant. seed:893a8695d492d2ca

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance
