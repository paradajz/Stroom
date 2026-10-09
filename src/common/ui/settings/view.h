#pragma once

#include "ui/settings/controller.h"
#include <gsKit.h>

/**
 * @brief Draw the active settings page and highlight its selected row.
 * @param gs Graphics context.
 * @param settings Settings and menu navigation.
 * @param director Preset selection and availability.
 * @param player Current playback program draft.
 * @param cd_available Nonzero when CD playback settings are available.
 * @param listening Nonzero for a silent network-listening session.
 */
void ui_settings_draw(GSGLOBAL* gs, const AppSettings* settings, const Director* director, const PlayerState* player, int cd_available, int listening);
