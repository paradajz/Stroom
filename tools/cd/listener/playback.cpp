#include "listener.hpp"
#include "realtime.hpp"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

namespace stroom::cd
{
    /** @brief Own PipeWire resources and callback-only state; all diagnostics and RTKit calls run outside the callback. */
    struct Playback::Impl
    {
        Realtime              realtime;
        Buffer                buffer;
        pw_thread_loop*       loop   = nullptr;
        pw_stream*            stream = nullptr;
        std::atomic<pid_t>    tid{ 0 };
        std::atomic<bool>     enabled{ false }, failed{ false }, drained{ false };
        std::atomic<uint64_t> queued_frames{ 0 };
        uint64_t              created = milliseconds(), next_verify = 0;
        uint64_t              reported_underruns = 0, recovery_at = 0;
        unsigned              reserve_ms;
        bool                  granted     = false;
        bool                  initialized = false;

        /**
         * @brief Prepare real-time limits and allocate the callback's PCM ring.
         * @param reserve Startup and recovery threshold in milliseconds.
         * @throws SetupError On a RealtimeKit or CPU-time-limit setup failure.
         * @throws std::runtime_error If reserve is outside the supported range.
         */
        explicit Impl(unsigned reserve)
            : buffer(reserve)
            , reserve_ms(reserve)
        {}

        /** @brief Stop callbacks before releasing their stream and ring. */
        ~Impl()
        {
            enabled.store(false);

            if (loop)
            {
                pw_thread_loop_stop(loop);

                if (stream)
                {
                    pw_stream_destroy(stream);
                }

                pw_thread_loop_destroy(loop);
            }

            if (initialized)
            {
                pw_deinit();
            }
        }

        /**
         * @brief Publish a PipeWire stream error for the ordinary receiver thread.
         * @param userdata Borrowed Impl, kept alive until the stream is destroyed.
         * @param previous Previous stream state.
         * @param state New stream state.
         * @note The error-text argument is unused.
         */
        static void state_changed(void* userdata, pw_stream_state previous, pw_stream_state state, const char*)
        {
            if (state == PW_STREAM_STATE_ERROR || (state == PW_STREAM_STATE_UNCONNECTED && previous != PW_STREAM_STATE_UNCONNECTED))
            {
                static_cast<Impl*>(userdata)->failed.store(true);
            }
        }

        /**
         * @brief Publish completion of a clean PipeWire drain.
         * @param userdata Borrowed Impl, kept alive until the stream is destroyed.
         */
        static void drain_complete(void* userdata)
        {
            static_cast<Impl*>(userdata)->drained.store(true);
        }

        /**
         * @brief Fill one PipeWire buffer from the single-consumer PCM ring.
         * @param userdata Borrowed Impl, kept alive until the stream is destroyed.
         * @note Runs on the audio thread. Uses bounded copies and lock-free atomics;
         * publishes consumed frames after submitting the buffer to PipeWire.
         */
        static void process(void* userdata)
        {
            auto&                          self     = *static_cast<Impl*>(userdata);
            static thread_local const auto current  = static_cast<pid_t>(syscall(SYS_gettid));
            pid_t                          expected = 0;

            if (!self.tid.compare_exchange_strong(expected, current) && expected != current)
            {
                self.enabled.store(false);
                self.failed.store(true);
            }

            auto* buffer = pw_stream_dequeue_buffer(self.stream);

            if (!buffer)
            {
                return;
            }

            auto* storage = buffer->buffer;

            if (!storage->n_datas || !storage->datas[0].data || !storage->datas[0].chunk)
            {
                self.failed.store(true);
                pw_stream_queue_buffer(self.stream, buffer);
                return;
            }

            auto&  data   = storage->datas[0];
            size_t frames = data.maxsize / frame_bytes;

            if (buffer->requested)
            {
                frames = std::min<size_t>(frames, buffer->requested);
            }

            bool enabled = self.enabled.load(std::memory_order_acquire) && !self.failed.load(std::memory_order_acquire);
            self.buffer.render(static_cast<uint8_t*>(data.data), frames, enabled);
            data.chunk->offset = 0;
            data.chunk->stride = frame_bytes;
            data.chunk->size   = frames * frame_bytes;

            if (pw_stream_queue_buffer(self.stream, buffer) < 0)
            {
                self.failed.store(true);
            }

            // Publish completion after the final PCM has reached PipeWire's
            // queue, so the control thread cannot flush it while it is copied.
            self.queued_frames.store(self.buffer.played(), std::memory_order_release);
        }

        /**
         * @brief Connect an S16LE stereo source to the default PipeWire output.
         * @throws SetupError On control-loop, stream, connection or worker-start failure.
         * @note The owning Impl releases partially initialized resources on failure.
         */
        void open()
        {
            pw_init(nullptr, nullptr);
            initialized = true;
            loop        = pw_thread_loop_new("stroom-control", nullptr);

            if (!loop)
            {
                throw SetupError("Cannot create PipeWire control loop");
            }

            static const pw_stream_events events = []
            {
                pw_stream_events result{};
                result.version       = PW_VERSION_STREAM_EVENTS;
                result.state_changed = state_changed;
                result.process       = process;
                result.drained       = drain_complete;
                return result;
            }();
            stream = pw_stream_new_simple(pw_thread_loop_get_loop(loop), "Stroom CD", pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Music", PW_KEY_NODE_NAME, "stroom-cd-listener", PW_KEY_NODE_LATENCY, "1024/48000", nullptr), &events, this);

            if (!stream)
            {
                throw SetupError("Cannot create PipeWire playback stream");
            }

            std::array<uint8_t, 1024> storage{};
            spa_pod_builder           builder = SPA_POD_BUILDER_INIT(storage.data(), storage.size());
            spa_audio_info_raw        format{};
            format.format             = SPA_AUDIO_FORMAT_S16_LE;
            format.rate               = AUDIO_RATE;
            format.channels           = CD_STREAM_CHANNELS;
            format.position[0]        = SPA_AUDIO_CHANNEL_FL;
            format.position[1]        = SPA_AUDIO_CHANNEL_FR;
            const spa_pod* parameters = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format);
            auto           flags      = static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);

            if (pw_stream_connect(stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, &parameters, 1) < 0)
            {
                throw SetupError("Cannot connect to PipeWire; check the desktop PipeWire service");
            }

            if (pw_thread_loop_start(loop) < 0)
            {
                throw SetupError("Cannot start PipeWire playback");
            }
        }
    };

    Playback::Playback(unsigned reserve_ms)
        : state(std::make_unique<Impl>(reserve_ms))
    {
        state->open();
    }

    Playback::~Playback() = default;

    void Playback::check()
    {
        if (state->failed.load())
        {
            throw SetupError("PipeWire playback failed or its audio thread changed");
        }

        auto tid = state->tid.load(std::memory_order_acquire);
        auto now = milliseconds();

        if (!state->granted)
        {
            if (!tid)
            {
                if (now - state->created > io_deadline_ms)
                {
                    throw SetupError("No PipeWire audio callback; check the selected sound output");
                }

                return;
            }

            state->realtime.acquire(tid);
            state->granted = true;
            state->enabled.store(true, std::memory_order_release);
            state->next_verify = milliseconds() + milliseconds_per_second;
            std::cerr << "Audio scheduling: SCHED_RR priority=5 verified (tid=" << tid << ")\n";
        }
        else if (now >= state->next_verify)
        {
            state->realtime.verify(tid);
            state->next_verify = now + milliseconds_per_second;
        }

        auto underruns = state->buffer.underruns();

        if (underruns != state->reported_underruns)
        {
            state->reported_underruns = underruns;
            state->recovery_at        = state->buffer.played();
            std::cerr << "Rebuffering " << state->reserve_ms << " ms of CD audio after an underrun...\n";
        }
        else if (state->recovery_at && state->buffer.played() > state->recovery_at)
        {
            state->recovery_at = 0;
            std::cerr << "CD audio ready; resuming playback.\n";
        }
    }

    void Playback::push(const uint8_t* data, size_t size, const std::function<void()>& tick)
    {
        auto deadline = milliseconds() + io_deadline_ms;

        while (!state->buffer.push(data, size))
        {
            tick();
            check();

            if (milliseconds() >= deadline)
            {
                throw SetupError("PC playback cannot keep pace");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    void Playback::finish(const std::function<void()>& tick)
    {
        state->buffer.end();
        auto deadline = milliseconds() + io_deadline_ms;

        while (!state->buffer.empty_at_end() || state->queued_frames.load(std::memory_order_acquire) != state->buffer.played())
        {
            tick();
            check();

            if (milliseconds() >= deadline)
            {
                throw SetupError("PipeWire did not consume the final audio");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        pw_thread_loop_lock(state->loop);
        int result = pw_stream_flush(state->stream, true);
        pw_thread_loop_unlock(state->loop);

        if (result < 0)
        {
            throw SetupError("Cannot drain PipeWire playback");
        }

        while (!state->drained.load())
        {
            tick();
            check();

            if (milliseconds() >= deadline)
            {
                throw SetupError("PipeWire did not finish playback");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    std::string Playback::statistics() const
    {
        std::ostringstream output;
        output << "PC playback: queued " << (state->buffer.queued() * milliseconds_per_second / AUDIO_RATE) << " ms; played " << state->buffer.played()
               << " frames; underruns " << state->buffer.underruns();
        return output.str();
    }
}    // namespace stroom::cd
