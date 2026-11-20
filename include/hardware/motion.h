#pragma once

/**
 * The board's QMI8658 accelerometer, on a board that has one
 * (BOARD_HAS_QMI8658 in its header): a lift to wake the screen, and the puck
 * turned face down.
 *
 * Like battery.h this is readings, not meaning. On a board without the IMU
 * every call is a harmless no-op: motionInit() says false and nothing ever
 * moves.
 */
namespace hw {

/** Idempotent; after touchInit(), which starts the bus the IMU is on. Turns
 *  the accelerometer on in its low-power mode, gyro off. False on a board
 *  without one, or when it does not answer. */
bool motionInit();

/** True once each time the puck moves after lying still for a moment: picked
 *  up, nudged, or turned back face up. Never while it lies face down. Reads
 *  the bus at most every 100 ms, so call it as often as touch is read. */
bool motionTakeLift();

/** True once each time the puck has lain face down for about a second; again
 *  only after it has been turned up in between. */
bool motionTakeFaceDown();

/** The last reading in g, screen-out positive on z, for the portal's /imu.
 *  False with no IMU. */
bool motionLast(float& x, float& y, float& z);

}  // namespace hw
