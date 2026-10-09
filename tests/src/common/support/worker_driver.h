#pragma once

/**
 * @brief Control worker reset in the lifecycle fixture.
 * Drive the entry/argument supplied through the public platform worker contract. Production translation units remain separate; no implementation symbols are exposed.
 */
void test_worker_reset(void);

/**
 * @brief Control worker run in the lifecycle fixture.
 */
void test_worker_run(void);

/**
 * @brief Control worker stop in the lifecycle fixture.
 */
void test_worker_stop(void);

/**
 * @brief Control worker opens in the lifecycle fixture.
 */
unsigned test_worker_opens(void);

/**
 * @brief Control worker priority in the lifecycle fixture.
 */
int test_worker_priority(void);
