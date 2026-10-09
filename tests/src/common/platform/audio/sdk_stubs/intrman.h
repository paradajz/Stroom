/**
 * @brief Substitute CpuSuspendIntr in this host fixture.
 */
void CpuSuspendIntr(int* state);

/**
 * @brief Substitute CpuResumeIntr in this host fixture.
 */
void CpuResumeIntr(int state);

/**
 * @brief Substitute CpuEnableIntr in this host fixture.
 */
void CpuEnableIntr(void);

/**
 * @brief Substitute FlushDcache in this host fixture.
 */
void FlushDcache(void);
