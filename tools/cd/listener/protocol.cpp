#include "listener.hpp"
#include <spa/utils/json.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>

namespace stroom::cd
{
    constexpr unsigned high_byte_shift = 24;
    static_assert(std::atomic<uint64_t>::is_always_lock_free, "The audio ring requires lock-free 64-bit atomics");

    uint64_t milliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    unsigned number(std::string_view value, unsigned minimum, unsigned maximum)
    {
        unsigned result = 0;
        auto     parsed = std::from_chars(value.data(), value.data() + value.size(), result);

        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result < minimum || result > maximum)
        {
            throw std::runtime_error("Invalid numeric option: " + std::string(value));
        }

        return result;
    }

    std::string_view json_field(std::string_view packet, const char* key)
    {
        spa_json root{}, object{};

        if (packet.empty() || packet.size() > udp_bytes)
        {
            return {};
        }

        spa_json_init(&root, packet.data(), packet.size());

        if (spa_json_enter_object(&root, &object) <= 0)
        {
            return {};
        }

        std::array<char, 128> name{};

        while (spa_json_get_string(&object, name.data(), name.size()) > 0)
        {
            const char* value  = nullptr;
            int         length = spa_json_next(&object, &value);

            if (length <= 0)
            {
                return {};
            }

            if (name.data() != std::string_view(key))
            {
                continue;
            }

            if (spa_json_is_container(value, length))
            {
                length = spa_json_container_len(&object, value, length);
            }

            if (length <= 0 || static_cast<size_t>(length) > static_cast<size_t>(packet.data() + packet.size() - value))
            {
                return {};
            }

            return { value, static_cast<size_t>(length) };
        }

        return {};
    }

    std::string json_string(std::string_view packet, const char* key)
    {
        auto                            value = json_field(packet, key);
        std::array<char, udp_bytes + 1> text{};

        if (value.empty() || !spa_json_is_string(value.data(), value.size()) || spa_json_parse_stringn(value.data(), value.size(), text.data(), text.size()) < 0)
        {
            return {};
        }

        return text.data();
    }

    int64_t json_integer(std::string_view packet, const char* key)
    {
        auto    value  = json_field(packet, key);
        int64_t number = 0;

        if (value.empty())
        {
            throw std::runtime_error("Missing counter");
        }

        auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);

        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        {
            throw std::runtime_error("Invalid counter");
        }

        return number;
    }

    bool discovery_reply(std::string_view packet)
    {
        try
        {
            auto advertised = json_integer(packet, "port");

            if (json_string(packet, "service") != CD_STREAM_SERVICE || json_integer(packet, "version") != CD_STREAM_VERSION ||
                json_integer(packet, "sample_rate") != AUDIO_RATE || json_integer(packet, "channels") != CD_STREAM_CHANNELS ||
                json_integer(packet, "bits") != CD_STREAM_BITS || advertised != CD_STREAM_PORT)
            {
                return false;
            }

            return true;
        }
        catch (const std::runtime_error&)
        {
            return false;
        }
    }

    /**
     * @brief Decode a network-order word from unaligned storage.
     * @param bytes Borrowed storage containing at least four readable bytes.
     * @return Decoded unsigned 32-bit value.
     */
    static uint32_t word(const uint8_t* bytes)
    {
        return (uint32_t(bytes[0]) << high_byte_shift) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | bytes[3];
    }

    void Decoder::feed(const uint8_t* data, size_t size, const std::function<void(unsigned, const uint8_t*, size_t)>& consume)
    {
        while (size)
        {
            if (input_ended)
            {
                throw std::runtime_error("Data after CD stream end");
            }

            size_t amount = std::min(size, needed_bytes - used_bytes);
            std::memcpy(record_bytes.data() + used_bytes, data, amount);
            used_bytes += amount;
            data += amount;
            size -= amount;

            if (used_bytes != needed_bytes)
            {
                continue;
            }

            auto type = word(record_bytes.data() + 4), payload = word(record_bytes.data() + 8);

            if (needed_bytes == CD_STREAM_HEADER_BYTES)
            {
                if (std::memcmp(record_bytes.data(), CD_STREAM_MAGIC, 4) != 0 || payload > CD_STREAM_MAX_PAYLOAD_BYTES)
                {
                    throw std::runtime_error("Invalid CD stream header or size");
                }

                needed_bytes += payload;

                if (payload)
                {
                    continue;
                }
            }

            const auto* pcm = record_bytes.data() + CD_STREAM_HEADER_BYTES;

            if (type == CD_STREAM_FORMAT && !formatted && payload == CD_STREAM_FORMAT_BYTES)
            {
                if (word(pcm) != AUDIO_RATE || word(pcm + 4) != CD_STREAM_CHANNELS || word(pcm + 8) != CD_STREAM_BITS)
                {
                    throw std::runtime_error("Unsupported CD audio format");
                }

                formatted = true;
            }
            else if (type == CD_STREAM_PCM && formatted && payload && payload % frame_bytes == 0)
            {
            }
            else if (type == CD_STREAM_END && formatted && payload == 0)
            {
                input_ended = true;
            }
            else
            {
                throw std::runtime_error("Unexpected CD stream record");
            }

            consume(type, pcm, payload);
            used_bytes   = 0;
            needed_bytes = CD_STREAM_HEADER_BYTES;
        }
    }

    void Decoder::finish() const
    {
        if (!input_ended || used_bytes)
        {
            throw std::runtime_error(used_bytes ? "Truncated CD stream" : "CD playback interrupted");
        }
    }

    Buffer::Buffer(unsigned reserve_ms)
        : pcm_data(ring_frames * frame_bytes, 0)
        , target_frames(size_t(AUDIO_RATE) * number(std::to_string(reserve_ms), 0, max_buffer_ms) / milliseconds_per_second)
    {}

    bool Buffer::push(const uint8_t* data, size_t bytes)
    {
        if (bytes % frame_bytes || input_ended.load())
        {
            throw std::runtime_error("Invalid PCM write");
        }

        size_t frames  = bytes / frame_bytes;
        auto   written = written_frames.load(std::memory_order_relaxed);

        if (frames > ring_frames - (written - read_frames.load(std::memory_order_acquire)))
        {
            return false;
        }

        size_t offset = (written % ring_frames) * frame_bytes;
        size_t first  = std::min(bytes, pcm_data.size() - offset);
        std::memcpy(pcm_data.data() + offset, data, first);
        std::memcpy(pcm_data.data(), data + first, bytes - first);
        written_frames.store(written + frames, std::memory_order_release);
        return true;
    }

    size_t Buffer::render(uint8_t* destination, size_t frames, bool enabled)
    {
        std::memset(destination, 0, frames * frame_bytes);

        if (!enabled)
        {
            return 0;
        }

        bool   ended     = input_ended.load(std::memory_order_acquire);
        auto   read      = read_frames.load(std::memory_order_relaxed);
        size_t available = written_frames.load(std::memory_order_acquire) - read;

        if (!is_playing && available && (available >= target_frames || ended))
        {
            is_playing = true;
        }

        if (!is_playing)
        {
            return 0;
        }

        size_t amount = std::min(frames, available);
        size_t offset = (read % ring_frames) * frame_bytes;
        size_t first  = std::min(amount * frame_bytes, pcm_data.size() - offset);
        std::memcpy(destination, pcm_data.data() + offset, first);
        std::memcpy(destination + first, pcm_data.data(), amount * frame_bytes - first);
        read_frames.store(read + amount, std::memory_order_release);

        if (amount < frames)
        {
            is_playing = false;

            if (!ended)
            {
                underrun_count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        return amount;
    }

    void Buffer::end()
    {
        input_ended.store(true, std::memory_order_release);
    }

    size_t Buffer::queued() const
    {
        auto read = read_frames.load(std::memory_order_acquire);
        return written_frames.load(std::memory_order_acquire) - read;
    }

    bool Buffer::empty_at_end() const
    {
        return input_ended.load(std::memory_order_acquire) && queued() == 0;
    }
}    // namespace stroom::cd
