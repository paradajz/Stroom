#pragma once

/* Drive the entry/argument supplied through the public platform worker contract.
 * Production translation units remain separate; no implementation symbols are exposed. */
void     test_worker_reset(void);
void     test_worker_run(void);
void     test_worker_stop(void);
unsigned test_worker_opens(void);
int      test_worker_priority(void);
