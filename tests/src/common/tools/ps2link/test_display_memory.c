#include "unity.h"
#include <dmaKit.h>
#include <stddef.h>
#include <stdint.h>

static void*    allocation;
static void*    released;
static void*    submitted;
static size_t   alignment, allocated_bytes;
static unsigned channel_seen, size_seen, sequence, flushed_at, submitted_at;

void* __wrap_gsKit_alloc_ucab(int size);
void  __wrap_gsKit_free_ucab(void* data);
void  __wrap_dmaKit_send_ucab(u16 channel, void* data, u32 size);
void  __wrap_dmaKit_send_chain_ucab(u16 channel, void* data);

void setUp(void)
{
    released = submitted = NULL;
    sequence = flushed_at = submitted_at = 0;
}

void tearDown(void)
{}

void* launcher_test_memalign(size_t boundary, size_t bytes)
{
    alignment       = boundary;
    allocated_bytes = bytes;

    return allocation;
}

void launcher_test_free(void* data)
{
    released = data;
}

void FlushCache(int operation)
{
    TEST_ASSERT_EQUAL_INT(0, operation);

    flushed_at = ++sequence;
}

void dmaKit_send(u16 channel, void* data, u32 size)
{
    channel_seen = channel;
    submitted    = data;
    size_seen    = size;
    submitted_at = ++sequence;
}

void dmaKit_send_chain(u16 channel, void* data, u32 size)
{
    dmaKit_send(channel, data, size);
}

static void low_memory_packets_keep_their_real_addresses(void)
{
    /* A low-resident launcher buffer must never become 0x30094000. */
    allocation = (void*)(uintptr_t)0x00094000;

    void* data = __wrap_gsKit_alloc_ucab(512);

    TEST_ASSERT_EQUAL_PTR(allocation, data);
    TEST_ASSERT_EQUAL_UINT(64, alignment);
    TEST_ASSERT_EQUAL_UINT(512, allocated_bytes);
    __wrap_dmaKit_send_ucab(2, data, 19);
    TEST_ASSERT_EQUAL_PTR(allocation, submitted);
    TEST_ASSERT_EQUAL_UINT(2, channel_seen);
    TEST_ASSERT_EQUAL_UINT(19, size_seen);
    __wrap_gsKit_free_ucab(data);
    TEST_ASSERT_EQUAL_PTR(allocation, released);
}

static void chains_flush_before_submitting_without_rebasing(void)
{
    void* data = (void*)(uintptr_t)0x00098000;

    __wrap_dmaKit_send_chain_ucab(2, data);
    TEST_ASSERT_EQUAL_UINT(1, flushed_at);
    TEST_ASSERT_EQUAL_UINT(2, submitted_at);
    TEST_ASSERT_EQUAL_PTR(data, submitted);
    TEST_ASSERT_EQUAL_UINT(2, channel_seen);
    TEST_ASSERT_EQUAL_UINT(0, size_seen);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(low_memory_packets_keep_their_real_addresses);
    RUN_TEST(chains_flush_before_submitting_without_rebasing);

    return UNITY_END();
}
