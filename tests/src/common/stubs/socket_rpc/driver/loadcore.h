#pragma once

#include <stdint.h>

/**
 * @brief Host substitute for the library lookup descriptor.
 */
typedef struct
{
    uint16_t version; /**< Requested library version. */
    char     name[8]; /**< Requested library name. */
} iop_library_t;

void* QueryLibraryEntryTable(iop_library_t* library);
