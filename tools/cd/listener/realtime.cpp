#include "realtime.hpp"
#include <dbus/dbus.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>

namespace stroom::cd
{
    namespace
    {
        constexpr int         rtkit_timeout_ms = 2000;
        constexpr const char* service          = "org.freedesktop.RealtimeKit1";
        constexpr const char* path             = "/org/freedesktop/RealtimeKit1";
        using Message                          = std::unique_ptr<DBusMessage, decltype(&dbus_message_unref)>;

        /** @brief Own one setup-only system-bus connection and its error state. */
        struct Bus
        {
            DBusError       error      = DBUS_ERROR_INIT;
            DBusConnection* connection = nullptr;

            /**
             * @brief Open a private system-bus connection for RealtimeKit setup.
             * @throws SetupError If the connection cannot be opened.
             */
            Bus()
            {
                connection = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error);

                if (!connection)
                {
                    std::string message = error.message ? error.message : "system bus unavailable";
                    dbus_error_free(&error);
                    throw SetupError("RealtimeKit: " + message);
                }

                dbus_connection_set_exit_on_disconnect(connection, false);
            }

            /** @brief Release setup-only D-Bus resources. */
            ~Bus()
            {
                dbus_connection_close(connection);
                dbus_connection_unref(connection);
                dbus_error_free(&error);
            }

            /**
             * @brief Call RealtimeKit with a two-second reply timeout.
             * @param request Message whose ownership is consumed, including on failure.
             * @return Owned reply message.
             * @throws SetupError On a null request, failed call or missing reply.
             */
            Message call(DBusMessage* request)
            {
                if (!request)
                {
                    throw SetupError("Cannot allocate RealtimeKit request");
                }

                Message owned(request, dbus_message_unref);
                Message reply(dbus_connection_send_with_reply_and_block(connection, request, rtkit_timeout_ms, &error), dbus_message_unref);

                if (!reply)
                {
                    throw SetupError(std::string("RealtimeKit: ") + (error.message ? error.message : "request failed"));
                }

                return reply;
            }

            /**
             * @brief Read a typed integer property from RealtimeKit.
             * @param name Borrowed, null-terminated property name.
             * @param type Expected D-Bus type: DBUS_TYPE_INT32 or DBUS_TYPE_INT64.
             * @return Property value widened to a signed 64-bit integer.
             * @throws SetupError On a failed request or a mismatched reply type.
             */
            int64_t property(const char* name, int type)
            {
                auto* request = dbus_message_new_method_call(service, path, "org.freedesktop.DBus.Properties", "Get");

                if (!request)
                {
                    throw SetupError("Cannot allocate RealtimeKit property request");
                }

                if (!dbus_message_append_args(request, DBUS_TYPE_STRING, &service, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID))
                {
                    dbus_message_unref(request);
                    throw SetupError("Cannot populate RealtimeKit property request");
                }

                auto            reply = call(request);
                DBusMessageIter outer{}, value{};

                if (!dbus_message_iter_init(reply.get(), &outer) || dbus_message_iter_get_arg_type(&outer) != DBUS_TYPE_VARIANT)
                {
                    throw SetupError("Invalid RealtimeKit property reply");
                }

                dbus_message_iter_recurse(&outer, &value);

                if (dbus_message_iter_get_arg_type(&value) != type)
                {
                    throw SetupError("Invalid RealtimeKit property type");
                }

                if (type == DBUS_TYPE_INT32)
                {
                    dbus_int32_t number = 0;
                    dbus_message_iter_get_basic(&value, &number);
                    return number;
                }

                dbus_int64_t number = 0;
                dbus_message_iter_get_basic(&value, &number);
                return number;
            }
        };
    }    // namespace

    Realtime::Realtime()
    {
        Bus  bus;
        auto maximum      = bus.property("MaxRealtimePriority", DBUS_TYPE_INT32);
        auto maximum_time = bus.property("RTTimeUSecMax", DBUS_TYPE_INT64);

        if (maximum < priority || maximum_time <= 0)
        {
            throw SetupError("RealtimeKit limits do not permit audio scheduling");
        }

        rlimit limit{};

        if (getrlimit(RLIMIT_RTTIME, &limit) != 0)
        {
            throw SetupError("Cannot read the RT CPU-time limit");
        }

        limit.rlim_max = std::min(limit.rlim_max, static_cast<rlim_t>(maximum_time));
        limit.rlim_cur = std::min(limit.rlim_cur, limit.rlim_max);

        if (!limit.rlim_cur || setrlimit(RLIMIT_RTTIME, &limit) != 0)
        {
            throw SetupError("Cannot bound the RT CPU-time limit");
        }
    }

    void Realtime::acquire(pid_t tid)
    {
        Bus   bus;
        auto* request = dbus_message_new_method_call(service, path, service, "MakeThreadRealtimeWithPID");

        if (!request)
        {
            throw SetupError("Cannot allocate RealtimeKit scheduling request");
        }

        dbus_uint64_t process = getpid(), thread = tid;
        dbus_uint32_t requested = priority;

        if (!dbus_message_append_args(request, DBUS_TYPE_UINT64, &process, DBUS_TYPE_UINT64, &thread, DBUS_TYPE_UINT32, &requested, DBUS_TYPE_INVALID))
        {
            dbus_message_unref(request);
            throw SetupError("Cannot populate RealtimeKit scheduling request");
        }

        bus.call(request);
        verify(tid);
    }

    void Realtime::verify(pid_t tid)
    {
        sched_param actual{};
        int         policy = sched_getscheduler(tid);

        if (tid <= 0 || policy != (SCHED_RR | SCHED_RESET_ON_FORK) || sched_getparam(tid, &actual) != 0 || actual.sched_priority != priority)
        {
            throw SetupError("Audio real-time priority is missing or revoked");
        }
    }
}    // namespace stroom::cd
