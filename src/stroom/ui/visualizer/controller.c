#include "ui/visualizer/controller.h"

int visualizer_input(MilkdropRuntime* state, unsigned pressed)
{
    int reset = 0;

    if (pressed & INPUT_TRIANGLE)
    {
        state->feedback = !state->feedback;
        reset           = 1;
    }

    if (pressed & INPUT_LEFT)
    {
        director_move(&state->director, -1);
    }
    else if (pressed & (INPUT_RIGHT | INPUT_R1))
    {
        director_move(&state->director, 1);
    }

    if (pressed & INPUT_L1)
    {
        director_set_mode(&state->director, state->director.mode == DIRECTOR_FIXED ? DIRECTOR_SHUFFLE : DIRECTOR_FIXED);
    }

    return reset;
}
