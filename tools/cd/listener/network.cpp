#include "listener.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <set>
#include <sstream>

namespace stroom::cd
{
    namespace
    {
        constexpr unsigned discovery_ms = 1200, discovery_repeat_ms = 300;
        constexpr int      keepalive_seconds       = 5;
        constexpr size_t   disconnect_reason_bytes = 40;

        /** @brief Own a nonblocking IPv4 socket across exceptions and cancellation. */
        class Socket
        {
            public:
            /**
             * @brief Create an owned nonblocking IPv4 socket with close-on-exec enabled.
             * @param type Socket kind, such as SOCK_STREAM or SOCK_DGRAM.
             * @throws std::runtime_error If socket creation fails.
             */
            explicit Socket(int type)
                : fd(socket(AF_INET, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0))
            {
                if (fd < 0)
                {
                    throw std::runtime_error(std::string("Cannot open socket: ") + std::strerror(errno));
                }
            }

            /** @brief Close the owned descriptor. */
            ~Socket()
            {
                close(fd);
            }

            /** @brief Disable copying of the owned descriptor. */
            Socket(const Socket&) = delete;

            /** @brief Disable copy assignment of the owned descriptor. */
            Socket& operator=(const Socket&) = delete;
            int     fd;
        };

        /**
         * @brief Build a numeric IPv4 socket endpoint.
         * @param host Borrowed dotted-decimal IPv4 address.
         * @param port Host-order port, validated by the caller.
         * @return IPv4 endpoint with network-order address and port.
         * @throws std::runtime_error If host is not a numeric IPv4 address.
         */
        sockaddr_in address(const std::string& host, unsigned port)
        {
            sockaddr_in result{};
            result.sin_family = AF_INET;
            result.sin_port   = htons(port);

            if (inet_pton(AF_INET, host.c_str(), &result.sin_addr) != 1)
            {
                throw std::runtime_error("Invalid IPv4 address");
            }

            return result;
        }

        /**
         * @brief Poll socket readiness while servicing cancellation and health checks.
         * @param fd Borrowed socket descriptor.
         * @param events Requested poll event flags.
         * @param deadline Monotonic deadline in milliseconds, or zero for an indefinite wait.
         * @param tick Callback invoked before each poll; its exceptions propagate.
         * @return True for socket activity, including hangup or error; false at the deadline.
         * @throws std::runtime_error On a polling failure other than interrupted system calls.
         */
        bool wait(int fd, short events, uint64_t deadline, const Tick& tick)
        {
            for (;;)
            {
                tick();
                auto now = milliseconds();

                if (deadline && now >= deadline)
                {
                    return false;
                }

                int    timeout = deadline ? static_cast<int>(std::min<uint64_t>(socket_poll_ms, deadline - now)) : socket_poll_ms;
                pollfd poller{ fd, events, 0 };
                int    result = poll(&poller, 1, timeout);

                if (result > 0)
                {
                    return true;
                }

                if (result < 0 && errno != EINTR)
                {
                    throw std::runtime_error("Socket poll failed");
                }
            }
        }

        /**
         * @brief Collect broadcasts for active IPv4 LAN interfaces.
         * @return Unique interface broadcast addresses plus the limited broadcast address.
         * @throws std::runtime_error If interface enumeration fails.
         */
        std::set<std::string> broadcasts()
        {
            std::set<std::string> result{ "255.255.255.255" };
            ifaddrs*              raw = nullptr;

            if (getifaddrs(&raw) != 0)
            {
                throw std::runtime_error("Cannot enumerate LAN interfaces");
            }

            std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> interfaces(raw, freeifaddrs);

            for (auto* item = raw; item; item = item->ifa_next)
            {
                if (!item->ifa_addr || !item->ifa_broadaddr || item->ifa_addr->sa_family != AF_INET ||
                    !(item->ifa_flags & IFF_UP) || !(item->ifa_flags & IFF_BROADCAST) || (item->ifa_flags & IFF_LOOPBACK))
                {
                    continue;
                }

                auto* peer = reinterpret_cast<sockaddr_in*>(item->ifa_broadaddr);
                char  text[INET_ADDRSTRLEN]{};

                if (inet_ntop(AF_INET, &peer->sin_addr, text, sizeof(text)))
                {
                    result.insert(text);
                }
            }

            return result;
        }

        /**
         * @brief Send one UDP diagnostics request to the selected console.
         * @param source Numeric IPv4 console address and UDP port.
         * @param request Null-terminated request text, borrowed for this call.
         * @return An owned bounded reply with the expected service identity, or empty
         * text on send failure, a receive error or the reply deadline.
         * @throws std::runtime_error On socket creation, address validation or polling failure.
         */
        std::string query(const Source& source, const char* request)
        {
            Socket socket(SOCK_DGRAM);
            auto   peer = address(source.host, CD_STREAM_PORT);

            if (connect(socket.fd, reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) != 0 || send(socket.fd, request, std::strlen(request), 0) < 0)
            {
                return {};
            }

            auto deadline = milliseconds() + udp_reply_ms;

            while (wait(socket.fd, POLLIN, deadline, []
                        {
                        }))
            {
                std::array<char, udp_bytes + 1> packet{};
                ssize_t                         size = recv(socket.fd, packet.data(), packet.size(), 0);

                if (size > 0 && size <= 1024)
                {
                    std::string reply(packet.data(), size);

                    if (json_string(reply, "service") == CD_STREAM_SERVICE)
                    {
                        return reply;
                    }
                }
                else if (size < 0 && errno != EAGAIN && errno != EINTR)
                {
                    return {};
                }
            }

            return {};
        }

        /**
         * @brief Append validated unsigned counters to a diagnostic stream.
         * @param output Stream receiving names and decimal values; earlier text is retained.
         * @param object Borrowed JSON object containing the counters.
         * @param names Ordered, null-terminated field names.
         * @throws std::runtime_error On a missing, invalid or out-of-range counter.
         */
        void counters(std::ostringstream& output, std::string_view object, const std::vector<const char*>& names)
        {
            for (auto* name : names)
            {
                auto value = json_integer(object, name);

                if (value < 0 || value > UINT32_MAX)
                {
                    throw std::runtime_error("Invalid diagnostic counter");
                }

                output << ' ' << name << '=' << value;
            }
        }
    }    // namespace

    std::vector<Source> discover(const Options& configuration, const Tick& tick)
    {
        Socket socket(SOCK_DGRAM);
        int    enabled = 1;

        if (setsockopt(socket.fd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) != 0)
        {
            throw std::runtime_error("Cannot enable discovery broadcast");
        }

        auto local = address("0.0.0.0", 0);

        if (bind(socket.fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0)
        {
            throw std::runtime_error("Cannot bind discovery socket");
        }

        auto                destinations = configuration.host.empty() ? broadcasts() : std::set<std::string>{ configuration.host };
        auto                deadline = milliseconds() + discovery_ms, next_send = uint64_t{ 0 };
        std::vector<Source> sources;

        while (milliseconds() < deadline)
        {
            tick();

            if (milliseconds() >= next_send)
            {
                for (const auto& destination : destinations)
                {
                    auto peer = address(destination, CD_STREAM_PORT);
                    sendto(socket.fd, CD_STREAM_DISCOVER, std::strlen(CD_STREAM_DISCOVER), 0, reinterpret_cast<sockaddr*>(&peer), sizeof(peer));
                }

                next_send = milliseconds() + discovery_repeat_ms;
            }

            if (!wait(socket.fd, POLLIN, std::min(deadline, next_send), tick))
            {
                continue;
            }

            std::array<char, udp_bytes + 1> packet{};
            sockaddr_in                     peer{};
            socklen_t                       peer_size = sizeof(peer);
            ssize_t                         size      = recvfrom(socket.fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&peer), &peer_size);

            if (size <= 0 || size > 1024 || ntohs(peer.sin_port) != CD_STREAM_PORT || !discovery_reply({ packet.data(), static_cast<size_t>(size) }))
            {
                continue;
            }

            char host[INET_ADDRSTRLEN]{};

            if (!inet_ntop(AF_INET, &peer.sin_addr, host, sizeof(host)))
            {
                continue;
            }

            bool known = std::any_of(sources.begin(), sources.end(), [&](const Source& source)
                                     {
                                         return source.host == host;
                                     });

            if (!known && sources.size() < 32)
            {
                sources.push_back({ host });
            }
        }

        std::sort(sources.begin(), sources.end(), [](const Source& a, const Source& b)
                  {
                      return a.host < b.host;
                  });
        return sources;
    }

    void receive(const Source& source, const std::function<void(unsigned, const uint8_t*, size_t)>& consume, const Tick& tick)
    {
        Socket socket(SOCK_STREAM);
        auto   peer     = address(source.host, CD_STREAM_PORT);
        auto   deadline = milliseconds() + io_deadline_ms;
        int    result   = connect(socket.fd, reinterpret_cast<sockaddr*>(&peer), sizeof(peer));

        if (result != 0 && errno != EINPROGRESS)
        {
            throw std::runtime_error("Console unavailable");
        }

        if (!wait(socket.fd, POLLOUT, deadline, tick))
        {
            throw std::runtime_error("CD stream handshake timed out");
        }

        int       error = 0;
        socklen_t size  = sizeof(error);

        if (getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error)
        {
            throw std::runtime_error("Console unavailable");
        }

        int enabled = 1, idle = keepalive_seconds, interval = keepalive_seconds, retries = 3;
        setsockopt(socket.fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
        setsockopt(socket.fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
        setsockopt(socket.fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
        setsockopt(socket.fd, IPPROTO_TCP, TCP_KEEPCNT, &retries, sizeof(retries));
        size_t sent = 0;

        while (sent < std::strlen(CD_STREAM_REQUEST))
        {
            if (!wait(socket.fd, POLLOUT, deadline, tick))
            {
                throw std::runtime_error("CD stream handshake timed out");
            }

            ssize_t count = send(socket.fd, CD_STREAM_REQUEST + sent, std::strlen(CD_STREAM_REQUEST) - sent, MSG_NOSIGNAL);

            if (count > 0)
            {
                sent += count;
            }
            else if (count == 0 || (errno != EAGAIN && errno != EINTR))
            {
                throw std::runtime_error("Cannot subscribe to CD stream");
            }
        }

        Decoder                    decoder;
        std::array<uint8_t, 16384> packet{};

        while (!decoder.ended())
        {
            if (!wait(socket.fd, POLLIN, deadline, tick))
            {
                throw std::runtime_error("CD stream handshake timed out");
            }

            ssize_t count = recv(socket.fd, packet.data(), packet.size(), 0);

            if (count > 0)
            {
                decoder.feed(packet.data(), count, [&](unsigned type, const uint8_t* bytes, size_t length)
                             {
                                 if (type == CD_STREAM_FORMAT)
                                 {
                                     deadline = 0;
                                 }

                                 consume(type, bytes, length);
                             });
            }
            else if (count == 0)
            {
                break;
            }
            else if (errno != EAGAIN && errno != EINTR)
            {
                throw std::runtime_error("CD playback interrupted");
            }
        }

        decoder.finish();
    }

    std::string diagnostics(const Source& source)
    {
        std::ostringstream output;
        try
        {
            auto reply = query(source, CD_STREAM_DISCOVER);

            if (discovery_reply(reply))
            {
                auto stats = json_field(reply, "stats"), producer = json_field(stats, "source");

                if (!producer.empty())
                {
                    output << "PS2 CD source (since playback reset):";
                    counters(output, producer, { "blocks", "max_gap_ms", "gaps", "drive_waits", "max_drive_wait_ms", "output_busy", "max_output_busy_ms", "silence_blocks" });
                    output << "; sender lifetime:";
                    counters(output, stats, { "send_retries", "max_lag_blocks", "max_poll_gap_ms", "disconnects" });
                    auto reason = json_string(stats, "last_disconnect");

                    if (!reason.empty() && reason.size() <= disconnect_reason_bytes && std::all_of(reason.begin(), reason.end(), [](char c)
                                                                                                   {
                                                                                                       return (c >= 'a' && c <= 'z') || c == '_';
                                                                                                   }))
                    {
                        output << "; last_disconnect=" << reason << " last_error=" << json_integer(stats, "last_error");
                    }

                    output << '\n';
                }
            }
        }
        catch (const std::runtime_error&)
        {
            output.str("");
        }

        try
        {
            auto reply = query(source, CD_STREAM_TRACE);

            if (json_integer(reply, "traceVersion") != 1)
            {
                return output.str();
            }

            auto id = json_integer(reply, "id"), available = json_integer(reply, "available");

            if (id < 0 || id > UINT32_MAX || available < 0 || available > 1)
            {
                return output.str();
            }

            if (available == 0)
            {
                output << "PS2 CD TCP diagnostics unavailable.\n";
            }
            else if (!id)
            {
                output << "PS2 CD TCP diagnostics available; no send stalls of 250 ms or more recorded.\n";
            }
            else
            {
                std::ostringstream trace;
                trace << "PS2 CD TCP stall #" << id << ':';
                counters(trace, reply, { "duration_ms", "sample_ms", "lag" });
                trace << "; TCP:";
                auto tcp = json_field(reply, "tcp");
                counters(trace, tcp, { "flags", "state", "send_space", "unsent_bytes", "unsent_segments", "unsent_ref", "unacked_bytes", "unacked_segments", "unacked_ref", "queue_length", "last_ack", "next_sequence", "last_written", "congestion_window", "peer_window", "retransmissions", "rto_ms", "persist_backoff" });
                auto timer = json_integer(tcp, "timer_ms");

                if (timer < -1 || timer > INT32_MAX)
                {
                    throw std::runtime_error("Invalid TCP timer");
                }

                trace << " timer_ms=" << timer;
                auto driver = json_field(reply, "driver");

                if (!driver.empty())
                {
                    trace << "; Ethernet status=" << json_integer(driver, "status");
                    counters(trace, driver, { "link_mode", "tx_dropped", "tx_errors", "tx_carrier", "tx_deferred", "tx_collisions", "tx_underruns", "rx_alloc_fail", "rx_overrun", "rx_bad_fcs" });
                }

                output << trace.str() << '\n';
            }
        }
        catch (const std::runtime_error&)
        {}
        return output.str();
    }
}    // namespace stroom::cd
