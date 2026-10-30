#pragma once

#include <cstdint>

/**
 * The wake word, heard on the remote itself: microWakeWord's "Hey Jarvis"
 * model, run by TensorFlow Lite Micro on what the microphone hears, 30 ms at
 * a time. Nothing leaves the remote until it is heard; what follows is a
 * voice request like any other (services::voice).
 *
 * It does not listen while a request is listening or a reply is playing --
 * the remote's own voice is no wake word -- and starts afresh after.
 *
 * In a build without voice (see services/voice.h), or one without the
 * model, init() says false and nothing here does anything.
 */
namespace services::wake_word {

/** Once, after services::voice::init(). False without a microphone or a
 *  model. Listening starts off. */
bool init();

/** Listen for the wake word, or stop. */
void setListening(bool on);
bool listening();

/** The phrase, for the screen and the log: "Hey Jarvis". */
const char* phrase();

/** True once per time the wake word was heard. `position` is where in the
 *  microphone's stream it ended -- a little before where it was heard, by
 *  config::kWakeWordPreRollMs -- for hw::audio::captureStartAt(), so the
 *  request hears what was said straight after it. */
bool takeDetection(uint32_t& position);

/** How the detector has been doing since the last call, for the log. */
struct Stats {
  /** Times the model ran, one per 30 ms of sound. */
  uint32_t runs = 0;
  uint32_t mean_us = 0;
  uint32_t max_us = 0;
  /** The highest of the model's smoothed confidence, 0..255; it fires at
   *  config::kWakeWordCutoff. */
  uint8_t max_score = 0;
  uint32_t detections = 0;
  /** Microphone samples it fell too far behind to read. */
  uint32_t dropped = 0;
};
void takeStats(Stats& out);

}  // namespace services::wake_word
