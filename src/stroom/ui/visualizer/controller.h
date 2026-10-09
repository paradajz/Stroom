#pragma once

#include "platform/input/input.h"
#include "milkdrop/runtime.h"

/**
 * @brief Apply visualization controls and preset browsing.
 *
 * @param state Visualizer to update.
 * @param pressed Newly pressed INPUT_* bits.
 * @return Nonzero if the caller must clear hardware feedback history.
 */
int visualizer_input(MilkdropRuntime* state, unsigned pressed);
