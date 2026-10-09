#pragma once

#include "contracts/audio.h"
#include "contracts/cd_stream.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stroom::cd
{
    constexpr unsigned frame_bytes       = CD_STREAM_CHANNELS * CD_STREAM_BITS / 8;
    constexpr unsigned default_buffer_ms = 500, max_buffer_ms = 2000;
    constexpr unsigned ring_frames             = 131072;
    constexpr unsigned milliseconds_per_second = 1000;
    constexpr unsigned io_deadline_ms = 5000, socket_poll_ms = 100, udp_reply_ms = 200;
    constexpr unsigned stats_interval_ms = 10000, udp_bytes = 1024;

    /** @brief Fatal playback setup or scheduling failure; do not retry silently. */
    struct SetupError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /** @brief Cooperative shutdown requested by the owning command. */
    struct Cancelled : std::exception
    {
    };

    /** @brief IPv4 console address; discovery and audio use CD_STREAM_PORT. */
    struct Source
    {
        std::string host;
    };

    /** @brief File settings, configuration path and one-shot listener actions. */
    struct Options
    {
        std::string host, config;
        unsigned    buffer_ms = default_buffer_ms;
        bool        list = false, help = false, check_config = false;
    };

    /**
     * @brief Read the monotonic clock used by listener deadlines.
     * @return Milliseconds since the steady clock's implementation-defined epoch.
     */
    uint64_t milliseconds();

    /**
     * @brief Load listener settings from the selected file and parse one-shot actions.
     * @param argc Argument count, including the executable name.
     * @param argv Borrowed argument strings, with the executable name at index zero.
     * @return Validated options and an absolute config path; help skips config I/O.
     * @throws std::runtime_error On invalid options, configuration or file I/O failure.
     * @note Creates a missing default config; explicitly selected files must exist.
     */
    Options options(int argc, char** argv);

    /**
     * @brief Parse an unsigned decimal option within inclusive bounds.
     * @param value Borrowed decimal text; every character must belong to the number.
     * @param minimum Smallest accepted value.
     * @param maximum Largest accepted value.
     * @return Parsed value.
     * @throws std::runtime_error On empty, malformed, overflowing or out-of-range input.
     */
    unsigned number(std::string_view value, unsigned minimum, unsigned maximum);

    /**
     * @brief Check a discovery reply's service, version and audio format.
     * @param packet Borrowed reply, limited to udp_bytes bytes.
     * @return True for a supported reply advertising CD_STREAM_PORT.
     * @note The caller obtains the console address from the UDP sender.
     */
    bool discovery_reply(std::string_view packet);

    /**
     * @brief Extract the first matching field token from a bounded JSON object.
     * @param packet Borrowed object text, limited to udp_bytes bytes.
     * @param key Null-terminated field name.
     * @return A view into packet, or an empty view when extraction fails.
     * @note The returned view remains valid while packet's backing storage is valid.
     */
    std::string_view json_field(std::string_view packet, const char* key);

    /**
     * @brief Decode a JSON string field used by discovery or diagnostics.
     * @param packet Borrowed object text, limited to udp_bytes bytes.
     * @param key Null-terminated field name.
     * @return An owned decoded string, or an empty string for a missing or invalid field.
     */
    std::string json_string(std::string_view packet, const char* key);

    /**
     * @brief Parse a signed decimal integer field used by diagnostics.
     * @param packet Borrowed object text, limited to udp_bytes bytes.
     * @param key Null-terminated field name.
     * @return Parsed signed 64-bit value.
     * @throws std::runtime_error On a missing, malformed or overflowing field.
     */
    int64_t json_integer(std::string_view packet, const char* key);

    /** @brief Incrementally validate records across arbitrary TCP fragmentation. No record exceeds the shared payload bound. */
    class Decoder
    {
        public:
        /**
         * @brief Validate incoming bytes and synchronously deliver complete records.
         * @param data Borrowed input bytes, valid for this call.
         * @param size Number of input bytes.
         * @param consume Callback receiving record type, borrowed payload and payload size.
         * Payload storage remains valid until the callback returns.
         * @throws std::runtime_error On invalid ordering, size, format or trailing data.
         * @note Discard the decoder after an exception, including one from consume.
         */
        void feed(const uint8_t* data, size_t size, const std::function<void(unsigned, const uint8_t*, size_t)>& consume);

        /**
         * @brief Check that input completed with an explicit END record.
         * @throws std::runtime_error If END is missing or a record is incomplete.
         */
        void finish() const;

        /**
         * @brief Check whether an explicit END record has been validated.
         * @return True after a valid END record.
         */
        bool ended() const
        {
            return input_ended;
        }

        private:
        std::array<uint8_t, CD_STREAM_HEADER_BYTES + CD_STREAM_MAX_PAYLOAD_BYTES> record_bytes{};
        size_t                                                                    used_bytes = 0, needed_bytes = CD_STREAM_HEADER_BYTES;
        bool                                                                      formatted = false, input_ended = false;
    };

    /** @brief Bounded SPSC PCM ring. One network producer and one audio consumer; no allocation or locks in render(). */
    class Buffer
    {
        public:
        /**
         * @brief Allocate and touch the fixed PCM ring before audio starts.
         * @param reserve_ms Startup and recovery threshold, zero through max_buffer_ms.
         * @throws std::runtime_error If reserve_ms is outside the supported range.
         */
        explicit Buffer(unsigned reserve_ms);

        /**
         * @brief Copy complete little-endian stereo frames into available ring space.
         * @param data Borrowed PCM bytes, valid for this call.
         * @param bytes Byte count, divisible by frame_bytes.
         * @return True if all frames were copied; false if free space is insufficient.
         * @throws std::runtime_error On a partial frame or a write after end().
         * @pre Calls belong to the single producer thread.
         */
        bool push(const uint8_t* data, size_t bytes);

        /**
         * @brief Copy queued audio and fill the remainder of the output with silence.
         * @param destination Writable storage for frames multiplied by frame_bytes bytes.
         * @param frames Number of output frames requested.
         * @param enabled Whether audio scheduling has been verified; false emits only silence.
         * @return Number of source frames consumed, excluding generated silence.
         * @pre Calls belong to the single consumer thread.
         */
        size_t render(uint8_t* destination, size_t frames, bool enabled);

        /**
         * @brief Mark clean input completion so a short reserve can drain.
         * @pre Call from the producer after its final push().
         */
        void end();

        /**
         * @brief Sample the ring's queued source-frame count.
         * @return A frame-count snapshot; concurrent reads and writes can advance it.
         */
        size_t queued() const;

        /**
         * @brief Check whether the consumer has copied all cleanly completed input.
         * @return True after end() when the ring contains no remaining source frames.
         * @note This reports ring consumption; the output device may still hold audio.
         */
        bool empty_at_end() const;

        /**
         * @brief Read the count of transitions into recovery buffering.
         * @return Underrun count, excluding initial startup and clean completion.
         */
        uint64_t underruns() const
        {
            return underrun_count.load();
        }

        /**
         * @brief Read the count of source frames copied by the consumer.
         * @return Consumed source frames, excluding generated silence.
         * @note The count measures ring consumption rather than device playback.
         */
        uint64_t played() const
        {
            return read_frames.load();
        }

        private:
        std::vector<uint8_t> pcm_data;
        const size_t         target_frames;
        alignas(64) std::atomic<uint64_t> written_frames{ 0 };
        alignas(64) std::atomic<uint64_t> read_frames{ 0 };
        std::atomic<bool>     input_ended{ false };
        std::atomic<uint64_t> underrun_count{ 0 };
        bool                  is_playing = false;
    };

    /** @brief Native PipeWire session. Destroying it cancels queued audio and joins callbacks before freeing storage. */
    class Playback
    {
        public:
        /**
         * @brief Prepare real-time limits and open a native PipeWire playback stream.
         * @param reserve_ms Startup and recovery threshold, zero through max_buffer_ms.
         * @throws SetupError On a scheduling setup or PipeWire initialization failure.
         * @throws std::runtime_error If reserve_ms is outside the supported range.
         * @note check() verifies the callback's priority before queued PCM is consumed.
         */
        explicit Playback(unsigned reserve_ms);

        /** @brief Disconnect and release callbacks before destroying their buffer. */
        ~Playback();

        /** @brief Disable copying of the owned stream and callback storage. */
        Playback(const Playback&) = delete;

        /** @brief Disable copy assignment of the owned stream and callback storage. */
        Playback& operator=(const Playback&) = delete;

        /**
         * @brief Publish source audio, waiting for ring space when needed.
         * @param data Borrowed PCM bytes, valid until the call returns.
         * @param size Byte count, divisible by frame_bytes.
         * @param tick Cooperative cancellation and health callback used while waiting.
         * @throws SetupError On playback failure or a five-second space-wait timeout.
         * @throws std::runtime_error On a partial frame or a write after finish().
         * @note Exceptions from tick propagate. Call from the single receiver thread.
         */
        void push(const uint8_t* data, size_t size, const std::function<void()>& tick);

        /**
         * @brief Acquire and periodically verify audio-thread scheduling.
         * @throws SetupError On a denied or revoked grant, stream failure, changed
         * callback thread, or failure to observe a callback within five seconds.
         * @note Call regularly from the ordinary receiver thread during playback.
         */
        void check();

        /**
         * @brief Drain all source frames through PipeWire after clean input completion.
         * @param tick Cooperative cancellation and health callback used while waiting.
         * @throws SetupError On playback failure or a five-second drain timeout.
         * @note Exceptions from tick propagate. Call from the receiver after its final push().
         */
        void finish(const std::function<void()>& tick);

        /**
         * @brief Format the ring's occupancy, consumed-frame count and underruns.
         * @return Owned diagnostic text suitable for ordinary-thread logging.
         */
        std::string statistics() const;

        private:
        struct Impl;
        std::unique_ptr<Impl> state;
    };

    using Tick = std::function<void()>;

    /**
     * @brief Discover unique IPv4 consoles through local subnet broadcasts.
     * @param configuration Optional console host for a directed discovery query.
     * @param tick Cooperative cancellation and health callback used during polling.
     * @return Up to 32 console addresses, sorted by sender address.
     * @throws std::runtime_error On socket, interface enumeration or polling failure.
     * @note Discovery runs for discovery_ms; exceptions from tick propagate.
     */
    std::vector<Source> discover(const Options& configuration, const Tick& tick);

    /**
     * @brief Subscribe to a console and deliver one validated audio session.
     * @param source Numeric IPv4 console address; TCP uses CD_STREAM_PORT.
     * @param consume Synchronous record callback; payload storage is valid until it returns.
     * @param tick Cooperative cancellation and health callback used during socket polling.
     * @throws std::runtime_error On connection failure, handshake timeout, interrupted
     * input, malformed records or EOF before END.
     * @note The handshake has a five-second deadline. After FORMAT, TCP keepalive
     * detects lost peers while an idle stream can remain connected.
     * Callback exceptions propagate, and the owned socket closes on every exit.
     */
    void receive(const Source& source, const std::function<void(unsigned, const uint8_t*, size_t)>& consume, const Tick& tick);

    /**
     * @brief Query the console's source counters and saved TCP stall snapshot.
     * @param source Numeric IPv4 console address; UDP uses CD_STREAM_PORT.
     * @return Owned diagnostic lines for valid replies; unavailable replies are omitted.
     * @note Each of the two queries has a udp_reply_ms reply deadline. Run on an
     * ordinary diagnostics thread because the queries wait for replies.
     */
    std::string diagnostics(const Source& source);
}    // namespace stroom::cd
