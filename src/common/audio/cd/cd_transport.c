#include "audio/cd/cd_transport.h"

void cd_transport_apply(unsigned generation, const AudioTransportRequests* requests)
{
    for (unsigned i = 0; i < requests->command_count; ++i)
    {
        cd_transport_command(generation, requests->commands[i]);
    }

    if (requests->program_count)
    {
        cd_transport_program(generation, requests->program, requests->program_count);
    }

    cd_transport_scan(generation, requests->scan_direction);
}
