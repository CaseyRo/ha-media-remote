#pragma once

#include <LovyanGFX.hpp>

namespace ui::toast {

/** The toast ui::showToast() asked for, if it is still showing: a pill
 *  across the top of now playing, where the rooms chip sits, and over it.
 *  Drawn last in a compose, so it is in every frame of now playing for as
 *  long as it shows. */
void draw(lgfx::LovyanGFX& gfx);

}  // namespace ui::toast
