#pragma once

/**
 * @brief Substitute CpuSuspendIntr in this host fixture.
 */
int CpuSuspendIntr(int*);

/**
 * @brief Substitute CpuResumeIntr in this host fixture.
 */
int CpuResumeIntr(int);
