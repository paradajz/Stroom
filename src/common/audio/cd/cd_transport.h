#pragma once

#include "audio/common/transport.h"

/**
 * @brief Submit a batch of shared transport requests to the CD worker.
 *
 * Queues commands in order, submits a nonempty program, then publishes scan
 * direction, including zero to release scanning. Each operation retains its
 * own generation validation and locking; the batch is not atomic.
 *
 * @param generation Disc generation from the snapshot used by the controller.
 * @param requests Non-NULL requests with valid counts; borrowed for this call
 * and copied by the backend. Left unchanged.
 */
void cd_transport_apply(unsigned generation, const AudioTransportRequests* requests);

/**
 * @brief Queue a transport or playback-mode command for the worker.
 *
 * @param generation Disc generation from the displayed status snapshot.
 * @param command Requested action.
 */
void cd_transport_command(unsigned generation, AudioTransportCommand command);

/**
 * @brief Copy a pending playback program for worker-side validation.
 *
 * @param generation Disc generation from the displayed status snapshot.
 * @param tracks Ordered one-based track numbers.
 * @param count Entry count, 1..99; other counts are ignored.
 */
void cd_transport_program(unsigned generation, const int* tracks, unsigned count);

/**
 * @brief Publish the requested scan direction without waiting for drive I/O.
 *
 * @param generation Disc generation from the displayed status snapshot.
 * @param direction Negative rewinds, zero releases, positive advances.
 */
void cd_transport_scan(unsigned generation, int direction);
