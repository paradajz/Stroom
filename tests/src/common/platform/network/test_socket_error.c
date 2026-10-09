#include "platform/network/socket_error.h"
#include "platform/network/socket_options.h"
#include "unity.h"
#include <errno.h>
#include <string.h>

static int       queries;
static int       query_error;
static int       pending_error;
static socklen_t reply_size;

int getsockopt(int fd, int level, int option, void* value, socklen_t* size)
{
    ++queries;
    TEST_ASSERT_EQUAL_INT(7, fd);
    TEST_ASSERT_EQUAL_INT(PLATFORM_SOCKET_LEVEL, level);
    TEST_ASSERT_EQUAL_INT(SO_ERROR, option);
    TEST_ASSERT_EQUAL_UINT(sizeof(int), *size);

    if (query_error)
    {
        errno = query_error;

        return -1;
    }

    memcpy(value, &pending_error, sizeof(pending_error));

    *size = reply_size;

    return 0;
}

/**
 * @brief Native errors require no socket query or retry-policy interpretation.
 */
static void native_errors(void)
{
    const int errors[] = { EAGAIN, EINTR, EINPROGRESS, EALREADY, ENOTCONN, ECONNRESET };

    for (unsigned i = 0; i < sizeof(errors) / sizeof(*errors); ++i)
    {
        errno = errors[i];

        TEST_ASSERT_EQUAL_INT(errors[i], platform_socket_error(7));
        TEST_ASSERT_EQUAL_INT(errors[i], errno);
    }

    TEST_ASSERT_EQUAL_INT(0, queries);
}

/**
 * @brief Recover both transient and terminal errors, including no pending error.
 */
static void collapsed_errors(void)
{
    const int errors[] = { 0, EAGAIN, EINTR, EINPROGRESS, EALREADY, ENOTCONN, ECONNRESET };

    for (unsigned i = 0; i < sizeof(errors) / sizeof(*errors); ++i)
    {
        errno         = ENFILE;
        pending_error = errors[i];

        TEST_ASSERT_EQUAL_INT(errors[i], platform_socket_error(7));
        TEST_ASSERT_EQUAL_INT(errors[i], errno);
    }

    TEST_ASSERT_EQUAL_INT(sizeof(errors) / sizeof(*errors), queries);
}

/**
 * @brief A failed query must not become a retryable socket result.
 */
static void query_failures(void)
{
    const int errors[] = { EAGAIN, EINTR, EBADF };

    for (unsigned i = 0; i < sizeof(errors) / sizeof(*errors); ++i)
    {
        errno       = ENFILE;
        query_error = errors[i];

        TEST_ASSERT_EQUAL_INT(PS2_SOCKET_QUERY_ERROR_GET_OPTION, platform_socket_error(7));
        TEST_ASSERT_EQUAL_INT(errors[i], errno);
    }
}

/**
 * @brief Reject malformed replies even when their value would permit retry.
 */
static void malformed_replies(void)
{
    const socklen_t sizes[] = { 0, sizeof(int) - 1, sizeof(int) + 1 };

    for (unsigned i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i)
    {
        errno         = ENFILE;
        pending_error = EAGAIN;
        reply_size    = sizes[i];

        TEST_ASSERT_EQUAL_INT(PS2_SOCKET_QUERY_ERROR_INVALID_REPLY, platform_socket_error(7));
        TEST_ASSERT_EQUAL_INT(EIO, errno);
    }
}

/**
 * @brief Reset the socket query substitute.
 */
void setUp(void)
{
    queries = query_error = pending_error = 0;
    reply_size                            = sizeof(int);
    errno                                 = 0;
}

/**
 * @brief No external resources are owned by this suite.
 */
void tearDown(void)
{}

/**
 * @brief Run socket normalization regressions.
 * @return Number of failed tests.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(native_errors);
    RUN_TEST(collapsed_errors);
    RUN_TEST(query_failures);
    RUN_TEST(malformed_replies);

    return UNITY_END();
}
