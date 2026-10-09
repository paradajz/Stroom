#include "listener.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>

using namespace stroom::cd;

/**
 * @brief Append a network-order word to a wire fixture.
 * @param data Destination byte vector.
 * @param value Unsigned word to append.
 */
static void append_word(std::vector<uint8_t>& data, unsigned value)
{
    for (int shift : { 24, 16, 8, 0 })
    {
        data.push_back((value >> shift) & 255);
    }
}

/**
 * @brief Construct one record for decoder interoperability tests.
 * @param type Shared wire record type.
 * @param data Borrowed payload bytes to copy into the record.
 * @return Owned header and payload bytes.
 */
static std::vector<uint8_t> record(unsigned type, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> result{ 'S', 'C', 'D', '1' };
    append_word(result, type);
    append_word(result, data.size());
    result.insert(result.end(), data.begin(), data.end());

    return result;
}

/**
 * @brief Assert that decoding or completion rejects malformed input.
 * @param input Complete wire fixture expected to fail validation.
 */
static void rejected(const std::vector<uint8_t>& input)
{
    bool failed = false;

    try
    {
        Decoder decoder;

        decoder.feed(input.data(), input.size(), [](unsigned, const uint8_t*, size_t)
                     {
                     });
        decoder.finish();
    }
    catch (const std::runtime_error&)
    {
        failed = true;
    }

    assert(failed);
}

/**
 * @brief Check framing, reserve recovery, clean drains and concurrent ring wraparound.
 * @return Zero after all assertions pass.
 */
int main()
{
    std::vector<uint8_t> format;
    append_word(format, AUDIO_RATE);
    append_word(format, CD_STREAM_CHANNELS);
    append_word(format, CD_STREAM_BITS);

    auto initial = record(CD_STREAM_FORMAT, format);

    const std::vector<uint8_t> expected_format = { 0x53, 0x43, 0x44, 0x31, 0, 0, 0, 2, 0, 0, 0, 12, 0, 0, 0xbb, 0x80, 0, 0, 0, 2, 0, 0, 0, 16 };
    assert(initial == expected_format);
    assert(std::string(CD_STREAM_REQUEST) == "STROOM CD 1\n");

    std::vector<uint8_t> pcm(CD_STREAM_MAX_PAYLOAD_BYTES);

    for (size_t i = 0; i < pcm.size(); ++i)
    {
        pcm[i] = (i * 37) & 255;
    }

    auto audio = record(CD_STREAM_PCM, pcm), end = record(CD_STREAM_END, {});
    auto stream = initial;

    stream.insert(stream.end(), audio.begin(), audio.end());
    stream.insert(stream.end(), end.begin(), end.end());

    for (size_t split = 1; split <= stream.size(); ++split)
    {
        Decoder decoder;

        std::vector<uint8_t> received;

        auto consume = [&](unsigned type, const uint8_t* bytes, size_t length)
        {
            if (type == CD_STREAM_PCM)
            {
                received.insert(received.end(), bytes, bytes + length);
            }
        };

        decoder.feed(stream.data(), split, consume);
        decoder.feed(stream.data() + split, stream.size() - split, consume);
        decoder.finish();
        assert(received == pcm);
    }

    rejected(audio);
    rejected(initial);
    rejected(record(CD_STREAM_END, {}));

    auto bad = initial;

    bad.insert(bad.end(), initial.begin(), initial.end());
    rejected(bad);

    bad = stream;

    bad.push_back(0);
    rejected(bad);

    bad    = stream;
    bad[0] = 'X';

    rejected(bad);

    bad    = initial;
    bad[8] = 1;

    rejected(bad);

    bad                         = initial;
    bad[CD_STREAM_HEADER_BYTES] = 1;

    rejected(bad);

    for (size_t length = 1; length < CD_STREAM_HEADER_BYTES + CD_STREAM_FORMAT_BYTES; ++length)
    {
        rejected({ stream.begin(), stream.begin() + length });
    }

    bad            = initial;
    auto unaligned = record(CD_STREAM_PCM, { 1, 2, 3 });

    bad.insert(bad.end(), unaligned.begin(), unaligned.end());
    rejected(bad);

    Buffer               buffer(500);
    std::vector<uint8_t> output(1024 * frame_bytes, 99);
    assert(buffer.push(pcm.data(), pcm.size()));
    assert(buffer.render(output.data(), 1024, true) == 0);
    assert(std::all_of(output.begin(), output.end(), [](uint8_t byte)
                       {
                           return byte == 0;
                       }));
    buffer.end();
    assert(buffer.render(output.data(), 1024, false) == 0);
    assert(buffer.queued() == pcm.size() / frame_bytes);
    assert(buffer.render(output.data(), 1024, true) == pcm.size() / frame_bytes);
    assert(std::equal(pcm.begin(), pcm.end(), output.begin()));
    assert(buffer.empty_at_end());
    assert(buffer.underruns() == 0);

    Buffer recovery(10);
    assert(recovery.push(pcm.data(), pcm.size()));
    assert(recovery.render(output.data(), 1024, true) == pcm.size() / frame_bytes);
    assert(recovery.underruns() == 1);
    assert(recovery.render(output.data(), 1024, true) == 0);
    assert(recovery.underruns() == 1);
    assert(recovery.push(pcm.data(), 4));
    assert(recovery.render(output.data(), 1024, true) == 0);
    recovery.end();
    assert(recovery.render(output.data(), 1024, true) == 1);
    assert(recovery.empty_at_end());
    assert(recovery.underruns() == 1);

    Buffer             concurrent(0);
    constexpr uint32_t count = ring_frames * 8;
    std::thread        producer([&]
                                {
                             for (uint32_t value = 1; value <= count; ++value)
                             {
                                 while (!concurrent.push(reinterpret_cast<uint8_t*>(&value), sizeof(value)))
                                 {
                                     std::this_thread::yield();
                                 }
                             }

                             concurrent.end();
                                });

    uint32_t expected = 1;

    std::array<uint32_t, 193> received{};

    while (!concurrent.empty_at_end())
    {
        size_t amount = concurrent.render(reinterpret_cast<uint8_t*>(received.data()), received.size(), true);

        for (size_t i = 0; i < amount; ++i)
        {
            assert(received[i] == expected++);
        }
    }

    producer.join();
    assert(expected == count + 1);
    assert(concurrent.played() == count);
    std::vector<uint8_t> too_large((ring_frames + 1) * frame_bytes);
    Buffer               bounded(2000);
    assert(!bounded.push(too_large.data(), too_large.size()));
    assert(bounded.queued() == 0);

    const std::string discovery = "{\"service\":\"stroom-cd\",\"version\":1,\"port\":12891,\"sample_rate\":48000,\"channels\":2,\"bits\":16,\"stats\":{\"source\":{\"blocks\":4294967295}}}";

    assert(discovery_reply(discovery));
    assert(!discovery_reply(discovery + std::string(1025, ' ')));
    assert(!discovery_reply("{\"service\":\"wrong\"}"));
    assert(json_integer(json_field(json_field(discovery, "stats"), "source"), "blocks") == UINT32_MAX);
    std::cout << "Native framing, cancellation reserve, short drain, underrun recovery and concurrent ring wrap passed\n";
}
