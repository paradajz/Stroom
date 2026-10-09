#pragma once

#ifndef STROOM_DIAGNOSTICS
#define STROOM_DIAGNOSTICS 0
#endif

#if STROOM_DIAGNOSTICS
#include <stdio.h>

#define STROOM_LOG(format, ...) printf("stroom: " format "\n", ##__VA_ARGS__)
#else
#define STROOM_LOG(...) ((void)0)
#endif
