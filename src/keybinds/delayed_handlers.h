#pragma once

/**
 * @brief Stores the delayed keybind handlers, to avoid freezing the environment
 */
typedef struct DelayedHandlers {
    bool stop_reset;
} DelayedHandlers;
