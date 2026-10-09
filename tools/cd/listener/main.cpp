#include "listener.hpp"
#include <csignal>
#include <chrono>
#include <future>
#include <iostream>
#include <optional>
#include <thread>

namespace
{
    constexpr unsigned         shutdown_poll_ms = 20, reconnect_ms = 200;
    volatile std::sig_atomic_t stopping = 0;

    /**
     * @brief Record a shutdown request from the signal handler.
     * @param signal Received signal number, retained for the process exit status.
     */
    void stop(int signal)
    {
        stopping = signal;
    }

    /**
     * @brief Check the process's cooperative shutdown flag.
     * @throws stroom::cd::Cancelled After a shutdown signal has been recorded.
     */
    void cancelled()
    {
        if (stopping)
        {
            throw stroom::cd::Cancelled{};
        }
    }

    /**
     * @brief Wait before reconnecting while checking for shutdown signals.
     * @param milliseconds Requested delay in monotonic milliseconds.
     * @throws stroom::cd::Cancelled On a recorded shutdown request.
     */
    void delay(unsigned milliseconds)
    {
        auto until = stroom::cd::milliseconds() + milliseconds;

        while (stroom::cd::milliseconds() < until)
        {
            cancelled();
            std::this_thread::sleep_for(std::chrono::milliseconds(shutdown_poll_ms));
        }
    }

    /** @brief Print the native CLI and playback requirements without opening an audio service. */
    void help()
    {
        std::cout << "Usage: stroom-cd-listener [options]\n"
                  << "Receive Stroom CD audio directly through PipeWire on Linux.\n\n"
                  << "  --config FILE        Read a specific listener JSON configuration\n"
                  << "  --check-config       Validate configuration, print its path and exit\n"
                  << "  --list               List discovered consoles without opening audio\n"
                  << "  --help, -h           Show help\n\n"
                  << "Set connection and buffering preferences in listener.json.\n"
                  << "Playback requires PipeWire and a verified RealtimeKit audio-thread grant.\n";
    }
}    // namespace

/**
 * @brief Run listener discovery, session validation and playback.
 * @param argc Argument count, including the executable name.
 * @param argv Borrowed command-line strings.
 * @return Zero on clean completion, one on failure, or 128 plus the shutdown signal.
 */
int main(int argc, char** argv)
{
    using namespace stroom::cd;
    try
    {
        auto configuration = options(argc, argv);

        if (configuration.help)
        {
            help();
            return 0;
        }

        if (configuration.check_config)
        {
            std::cout << configuration.config << '\n';
            return 0;
        }

        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        std::signal(SIGPIPE, SIG_IGN);
        std::string previous;
        // Write changed listener messages to standard error.
        auto log = [&](const std::string& text)
        {
            if (text != previous)
            {
                std::cerr << text << '\n';
            }

            previous = text;
        };

        std::optional<Source> selected;

        if (!configuration.host.empty())
        {
            selected = Source{ configuration.host };
        }

        for (;;)
        {
            try
            {
                if (!selected || configuration.list)
                {
                    auto sources = discover(configuration, cancelled);

                    if (configuration.list)
                    {
                        for (const auto& source : sources)
                        {
                            std::cout << "Stroom CD\t" << source.host << ':' << CD_STREAM_PORT << '\n';
                        }

                        if (sources.empty())
                        {
                            std::cerr << "No CD discovery replies received. Set host in " << configuration.config << ".\n";
                        }

                        return 0;
                    }

                    if (sources.empty())
                    {
                        throw std::runtime_error("Waiting for a Stroom CD source...");
                    }

                    if (sources.size() > 1)
                    {
                        std::string message = "Multiple PS2 sources found; set host in " + configuration.config + ":";

                        for (const auto& source : sources)
                        {
                            message += " " + source.host;
                        }

                        throw std::runtime_error(message);
                    }

                    selected = sources.front();
                }

                Source source = *selected;

                std::future<std::string>  sender;
                std::unique_ptr<Playback> playback;

                uint64_t first = 0, last = 0, received = 0, max_gap = 0, next_report = milliseconds() + stats_interval_ms;

                // Check playback health and report ready asynchronous diagnostics.
                auto tick = [&]
                {
                    cancelled();

                    if (playback)
                    {
                        playback->check();
                    }

                    if (sender.valid() && sender.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
                    {
                        auto text = sender.get();

                        if (!text.empty())
                        {
                            std::cerr << text;
                        }
                    }

                    if (milliseconds() >= next_report)
                    {
                        auto now = milliseconds();

                        if (first)
                        {
                            std::cerr << "CD timing: received " << received << " frames in " << now - first << " ms; max arrival gap " << max_gap << " ms\n";
                        }

                        if (playback)
                        {
                            std::cerr << playback->statistics() << '\n';
                        }

                        if (!sender.valid())
                        {
                            sender = std::async(std::launch::async, [source]
                                                {
                                                    return diagnostics(source);
                                                });
                        }

                        next_report = now + stats_interval_ms;
                    }
                };

                receive(source, [&](unsigned type, const uint8_t* bytes, size_t size)
                        {
                            if (type == CD_STREAM_FORMAT)
                            {
                                log("Connected to " + source.host + ':' + std::to_string(CD_STREAM_PORT) + "; waiting for CD audio...");
                            }
                            else if (type == CD_STREAM_PCM)
                            {
                                auto now = milliseconds();

                                if (last)
                                {
                                    max_gap = std::max(max_gap, now - last);
                                }

                                if (!first)
                                {
                                    first = now;
                                }

                                last = now;
                                received += size / frame_bytes;

                                if (!playback)
                                {
                                    log("Buffering " + std::to_string(configuration.buffer_ms) + " ms of CD audio...");
                                    playback = std::make_unique<Playback>(configuration.buffer_ms);
                                    log("Listening to " + source.host + ':' + std::to_string(CD_STREAM_PORT));
                                }

                                playback->push(bytes, size, tick);
                            }
                        },
                        tick);

                if (playback)
                {
                    playback->finish(tick);

                    std::cerr << playback->statistics() << '\n';
                }

                delay(reconnect_ms);
            }
            catch (const SetupError&)
            {
                throw;
            }
            catch (const Cancelled&)
            {
                throw;
            }
            catch (const std::exception& error)
            {
                std::string message = error.what();

                if (message == "Console unavailable" || message == "CD stream handshake timed out")
                {
                    if (configuration.host.empty())
                    {
                        selected.reset();
                    }

                    log(configuration.host.empty() ? "Waiting for a Stroom CD source..." : "Waiting for PS2 at " + configuration.host + ':' + std::to_string(CD_STREAM_PORT) + "...");
                }
                else
                {
                    log(message);

                    if (selected)
                    {
                        std::cerr << diagnostics(*selected);
                    }
                }

                delay(milliseconds_per_second);
            }
        }
    }
    catch (const Cancelled&)
    {
        return 128 + stopping;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
