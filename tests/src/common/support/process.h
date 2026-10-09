#pragma once

/**
 * @brief Run a case in a child process, terminating its process group on completion.
 * Preserves Unity failure output and bounds execution to roughly ten seconds.
 * @param scenario Case body; failures are reported to the parent Unity runner.
 */
void test_process_run(void (*scenario)(void));
