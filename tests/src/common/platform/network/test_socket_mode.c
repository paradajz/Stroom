#include "platform/network/socket_mode.h"
#include <ps2sdkapi.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "unity.h"

static int sdk_result;

int _ps2sdk_ioctl(int fd, int request, void* data)
{
    TEST_ASSERT_EQUAL_INT(7, fd);
    TEST_ASSERT_EQUAL_INT(FIONBIO, request);
    TEST_ASSERT_EQUAL_UINT(1, *(unsigned long*)data);

    return sdk_result;
}

void setUp(void)
{
    sdk_result = 0;
    errno      = EAGAIN;
}

void tearDown(void)
{}

/** @brief Successful mode changes preserve errno, including positive SDK results. */
static void success_preserves_errno(void)
{
    TEST_ASSERT_TRUE(platform_socket_nonblocking(7) == 0);
    TEST_ASSERT_EQUAL_INT(EAGAIN, errno);

    sdk_result = 1;

    TEST_ASSERT_TRUE(platform_socket_nonblocking(7) == 0);
    TEST_ASSERT_EQUAL_INT(EAGAIN, errno);
}

/** @brief The SDK return code supplies errno instead of an unrelated previous failure. */
static void failure_sets_errno(void)
{
    const int errors[] = { ENOSYS, EBADF, EIO };

    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i)
    {
        sdk_result = -errors[i];
        errno      = EAGAIN;

        TEST_ASSERT_TRUE(!(platform_socket_nonblocking(7) == 0));
        TEST_ASSERT_EQUAL_INT(errors[i], errno);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(success_preserves_errno);
    RUN_TEST(failure_sets_errno);

    return UNITY_END();
}
