#include <stdint.h>

#define kBUSCLKBY256 576000

/**
 * @brief Substitute GetTimerSystemTime in this host fixture.
 */
uint64_t GetTimerSystemTime(void);
