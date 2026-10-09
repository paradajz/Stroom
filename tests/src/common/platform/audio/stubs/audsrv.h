#pragma once

#define AUDSRV_IRX 0x870884e
#define MAX_VOLUME 0x3fff

/**
 * @brief Minimal audsrv format used by the output adapter.
 */
typedef struct
{
    int freq;     /**< Sample rate. */
    int bits;     /**< Bits per sample. */
    int channels; /**< Channel count. */
} audsrv_fmt_t;

typedef int (*audsrv_callback_t)(void*);

/**
 * @brief Simulate service startup.
 * @return Zero on success.
 */
int audsrv_init(void);
int audsrv_on_fillbuf(int amount, audsrv_callback_t callback, void* argument);

/**
 * @brief Simulate service shutdown.
 * @return Zero on success.
 */
int audsrv_quit(void);

/**
 * @brief Set the test format.
 * @param format Requested format.
 * @return Zero on success.
 */
int audsrv_set_format(audsrv_fmt_t* format);

/**
 * @brief Record output volume.
 * @param volume Requested volume.
 * @return Zero on success.
 */
int audsrv_set_volume(int volume);

/**
 * @brief Record output stop.
 * @return Zero on success.
 */
int audsrv_stop_audio(void);

/**
 * @brief Read fixture capacity.
 * @return Free bytes or a negative error.
 */
int audsrv_available(void);

/**
 * @brief Read fixture queue depth.
 * @return Queued bytes or a negative error.
 */
int audsrv_queued(void);

/**
 * @brief Record a sound write.
 * @param data PCM bytes.
 * @param size Byte count.
 * @return Accepted bytes.
 */
int audsrv_play_audio(const char* data, int size);
