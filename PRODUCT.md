# Product

<!-- impeccable:product-schema 1 -->

## Platform

embedded

(Not web, iOS or Android: a custom renderer on a round AMOLED. Web and native
platform guidance does not apply.)

## Users

Casey, the owner, at his office desk. The puck sits beside him; he glances at
what is playing on the office speaker, nudges the volume, skips, or picks
something from the recents. Mostly glances, sometimes from across the desk.

Later also in his caravan, where it controls other media playback, not only
music.

## Product Purpose

Puck 1: a dedicated remote for one Home Assistant media player on a round
glass puck. It shows what is playing and puts the everyday controls under a
finger, with no phone, no app, and nothing installed on the server.

Success: the speaker is under the thumb at a glance, and the puck is good
enough to look at that it stays on the desk all day.

## Positioning

A physical, glanceable object rather than an app. It only ever knows one
Home Assistant media player entity; whatever plays -- Music Assistant, a TV, a
radio -- is reached through that one entity, and swapping what plays never
changes the puck.

## Operating Context

- On the desk, on its USB-C cable most of the time, sometimes on battery: a
  few hours from full (about 4-5 h measured on 6 Nov 2026, under heavy
  testing).
- Touch on glass, with no dial and no detents, plus two hardware buttons:
  PWR sleeps and wakes the screen; PWR and BOOT together lock the touch
  screen, held a second unlock it.
- Talks only to Home Assistant over 2.4 GHz Wi-Fi; Music Assistant behind it
  supplies the recent list and search.
- Future: a caravan, and media other than music through the same remote.

## Capabilities and Constraints

- Hardware: Waveshare ESP32-S3-Touch-AMOLED-1.75C. 466 x 466 round AMOLED
  (CO5300, quad SPI) where black pixels are off; CST9217 touch; AXP2101 power
  manager (battery %, the PWR key); ESP32-S3 with 8 MB PSRAM; no RTC.
- Renderer: LovyanGFX composes a 466 x 466 RGB565 frame in PSRAM and pushes it
  over QSPI in even-aligned windows; nothing draws on the glass directly. A
  full compose (cover decode, scrim, blur) is the expensive part, so screens
  compose on change and push small regions for small updates.
- Text: VLW faces rendered natively per pixel size, never scaled; Noto Sans,
  with Korean drawn from an outline font. Each extra size costs flash, and
  LovyanGFX copies each glyph onto the 8 KB loop stack, so very large type
  needs care.
- Power: rich when awake, frugal on standby and idle. Standby is now playing
  without its controls at panel brightness 25; the screen blanks after five
  minutes idle; Wi-Fi uses modem sleep.
- Function that stays: now playing (cover, title, artist, volume ring, seek,
  transport, chips), the swipe-up list (recent and recommended through Music
  Assistant), search, the status page, settings, Wi-Fi join, player pick,
  status cards, standby, the lock and its unlock ring.
- Scope: this fork, Puck 1 only. Free to diverge from upstream
  ha-media-remote; other boards in the fork only need to keep building.
- Open: which "other media" the caravan brings (TV, video, podcasts) is not
  decided.

## Brand Commitments

- Reference Casey made binding: take inspiration from Music Assistant's
  frosted-glass web UI ("Apple-fy" the design).
- Chosen 6 Nov 2026: the category standard played straight. The craft bar is
  Apple Music, Apple Watch Now Playing and Music Assistant; the blurred cover
  stays as the ground.
- Names in use: Puck 1; the device's own name on the network is
  `media-remote`.

## Evidence on Hand

- Live content: Casey's Home Assistant players and Music Assistant library,
  with real covers.
- Spec and wireframes: `~/dev/pucks/docs/puck-1-music-controller.md` and
  `~/dev/pucks/docs/img/puck-1-screens.png`.
- No captures of the device's own screens exist yet; there is no screenshot
  path from the panel.

## Product Principles

1. Glance first: what is playing reads from across the desk; controls come
   second.
2. One player, no setup: the puck knows one entity, and everything reaches it
   through that.
3. Rich awake, frugal asleep.
4. Touch on glass must feel certain: no action happens by accident.
5. Media-neutral: music today, other media in the caravan later.
