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
   speaker name small at the bottom. Type: Inter (SF Pro may not ship on the
   puck).
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
