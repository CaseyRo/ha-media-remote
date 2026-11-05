#pragma once

/**
 * The battery and the PWR key, as a board's AXP2101 power manager reports
 * them, on a board that has one (BOARD_HAS_AXP2101 in its header).
 *
 * Like encoder.h this is readings, not meaning. On a board without a power
 * manager every call is a harmless no-op: batteryInit() says false, there is
 * never a battery, and nothing ever charges.
 */
namespace hw {

/** Idempotent; after touchInit(), which starts the bus the power manager is
 *  on. Turns the fuel gauge on if it is off, arms the PWR key's short press,
 *  and logs the first reading. False
 *  on a board without a power manager. */
bool batteryInit();

/** Charge in percent, 0 to 100, from the power manager's fuel gauge; -1 with
 *  no power manager, no battery, or no answer. Reads the bus each call, so
 *  call it from the loop, as touch is read. */
int batteryPercent();

/** True while the battery is charging. */
bool batteryCharging();

/** True once for each short press of the PWR key since the last call; a
 *  long press stays the power manager's own, switching the board off. Reads
 *  the bus at most every 100 ms, so call it as often as touch is read. */
bool powerKeyTakePress();

}  // namespace hw
