#include "listener.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

namespace stroom::cd
{
    /** @brief Paced test sink for the production receiver. It records consumed frames, excluding generated silence. */
    struct Playback::Impl
    {
        Buffer            buffer;
        std::atomic<bool> stopping{ false };
        std::thread       worker;
        std::ofstream     output;

        /**
         * @brief Create a paced recording sink for receiver lifecycle checks.
         * @param reserve Startup and recovery threshold in milliseconds.
         * @throws SetupError On an injected setup denial or recording-file failure.
         * @throws std::runtime_error If reserve is outside the supported range.
         */
        explicit Impl(unsigned reserve)
            : buffer(reserve)
        {
            if (std::getenv("STROOM_TEST_AUDIO_ERROR"))
            {
                throw SetupError("Injected real-time scheduling denial");
            }

            if (const char* file = std::getenv("STROOM_TEST_PCM"))
            {
                output.open(file, std::ios::binary | std::ios::app);

                if (!output)
                {
                    throw SetupError("Cannot open test PCM recording");
                }

                std::ofstream(std::string(file) + ".started").put('1');
            }

            worker = std::thread([this]
                                 {
                                     std::array<uint8_t, 480 * frame_bytes> pcm{};

                                     while (!stopping.load())
                                     {
                                         size_t frames = buffer.render(pcm.data(), 480, true);

                                         if (frames && output.is_open())
                                         {
                                             output.write(reinterpret_cast<char*>(pcm.data()), frames * frame_bytes);
                                             output.flush();
                                         }

                                         std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                     }
                                 });
        }

        /** @brief Join the simulated callback before releasing its ring and recording. */
        ~Impl()
        {
            stopping.store(true);

            if (worker.joinable())
            {
                worker.join();
            }
        }
    };

    /**
     * @brief Start the receiver test's paced PCM recording adapter.
     * @note This adapter can inject a setup denial or record consumed PCM to a test file.
     */
    Playback::Playback(unsigned reserve_ms)
        : state(std::make_unique<Impl>(reserve_ms))
    {}

    /** @brief Join the test consumer and release its ring and recording. */
    Playback::~Playback() = default;

    /** @brief Accept a health check for the test adapter's ordinary-priority worker. */
    void Playback::check()
    {}

    /**
     * @brief Queue PCM for the recording adapter with bounded backpressure.
     * @note The paced test consumer provides ring space; tick handles cancellation
     * while waiting. The test's space-wait deadline is five seconds.
     */
    void Playback::push(const uint8_t* data, size_t size, const std::function<void()>& tick)
    {
        auto deadline = milliseconds() + 5000;

        while (!state->buffer.push(data, size))
        {
            tick();

            if (milliseconds() >= deadline)
            {
                throw SetupError("Test sink cannot keep pace");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    /**
     * @brief Drain the test ring and join its recording worker.
     * @note Successful return makes the recording readable by integration assertions.
     */
    void Playback::finish(const std::function<void()>& tick)
    {
        state->buffer.end();
        auto deadline = milliseconds() + 5000;

        while (!state->buffer.empty_at_end())
        {
            tick();

            if (milliseconds() >= deadline)
            {
                throw SetupError("Test sink drain timed out");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        // The consumer publishes its ring read before writing the recording.
        // Join it before the test process can inspect clean-completion output.
        state->stopping.store(true);

        if (state->worker.joinable())
        {
            state->worker.join();
        }
    }

    /**
     * @brief Format the test adapter's queue occupancy and underrun count.
     */
    std::string Playback::statistics() const
    {
        return "PC playback: queued " + std::to_string(state->buffer.queued() * 1000 / AUDIO_RATE) + " ms; underruns " + std::to_string(state->buffer.underruns());
    }
}    // namespace stroom::cd
