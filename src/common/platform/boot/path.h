#pragma once

#include <stddef.h>
#include <string.h>

/**
 * @brief Resolve the executable path from launcher arguments or the DEV1 default.
 * @param argc Launcher argument count.
 * @param argv Launcher arguments; may be NULL when absent.
 * @return Borrowed nonempty argv[0], or the static mc0:/BOOT/BOOT.ELF fallback.
 */
static inline const char* platform_boot_executable(int argc, char** argv)
{
    return argc > 0 && argv && argv[0] && argv[0][0] ? argv[0] : "mc0:/BOOT/BOOT.ELF";
}

/**
 * @brief Build a settings path beside the executable, independent of the working directory.
 * @param path Destination buffer.
 * @param capacity Destination capacity in bytes.
 * @param executable Null-terminated boot path; its last slash or device colon determines the prefix.
 * @param name Null-terminated settings filename.
 * @return Zero on success, minus one when the destination is too small.
 */
static inline int platform_boot_config_path(char* path, size_t capacity, const char* executable, const char* name)
{
    const char* separator = strrchr(executable, '/');

    if (!separator)
    {
        separator = strrchr(executable, ':');
    }

    size_t prefix = separator ? (size_t)(separator - executable + 1) : 0;
    size_t bytes  = strlen(name) + 1;

    if (prefix > capacity || bytes > capacity - prefix)
    {
        return -1;
    }

    memcpy(path, executable, prefix);
    memcpy(path + prefix, name, bytes);

    return 0;
}
