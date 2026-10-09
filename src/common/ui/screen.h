#pragma once

/**
 * @brief Screen selected by application routing and drawn by frame composition.
 */
typedef enum
{
    UI_SCREEN_DETECTING,      /**< Waiting background while identifying a disc. */
    UI_SCREEN_WAITING,        /**< Waiting for an available audio source. */
    UI_SCREEN_CD_PLAYER,      /**< CD controls and audio display. */
    UI_SCREEN_NETWORK_PLAYER, /**< Network metadata and audio display. */
    UI_SCREEN_VISUALIZER      /**< MilkDrop artwork and optional overlays. */
} UiScreen;
