#include "contracts/milkdrop.h"
#include "app/config.h"
#include "platform/boot/path.h"
#include "util/ipv4.h"
#include <ctype.h>
#include <string.h>

#define CONFIG_LINE_BYTES      256
#define CONFIG_PATH_BYTES      1024
#define CONFIG_RATE_BYTES      12
#define CONFIG_IP_PRESENT      1u
#define CONFIG_NETMASK_PRESENT 2u
#define CONFIG_GATEWAY_PRESENT 4u

/**
 * @brief Trim a mutable setting line and return its first nonspace byte.
 * @param text Null-terminated buffer; trailing whitespace is replaced by a terminator.
 * @return Borrowed pointer within text.
 */
static char* trim(char* text)
{
    while (isspace((unsigned char)*text))
    {
        ++text;
    }

    char* end = text + strlen(text);

    while (end > text && isspace((unsigned char)end[-1]))
    {
        *--end = 0;
    }

    return text;
}

/**
 * @brief Apply an on/off setting while leaving invalid values unchanged.
 * @param value Null-terminated setting value.
 * @param setting Destination boolean.
 */
static void boolean(const char* value, int* setting)
{
    if (!strcmp(value, "on"))
    {
        *setting = 1;
    }
    else if (!strcmp(value, "off"))
    {
        *setting = 0;
    }
}

/**
 * @brief Parse a complete dotted IPv4 setting without trailing characters.
 * @param value Null-terminated setting value.
 * @param result Receives host-order address bits only on success.
 * @return One on success, zero for invalid input.
 */
static int address(const char* value, uint32_t* result)
{
    uint32_t parsed;

    if (util_ipv4_parse(&value, &parsed) != 0 || *value)
    {
        return 0;
    }

    *result = parsed;

    return 1;
}

AppConfig app_config_defaults(void)
{
    return (AppConfig){ .show_panels = 1, .levels = 1, .muted = 0, .cd_autoplay = 1, .frame_rate = MILKDROP_FPS_BASELINE };
}

void app_config_read(AppConfig* config, FILE* file)
{
    char          line[CONFIG_LINE_BYTES];
    NetworkConfig network   = config->network;
    unsigned      addresses = 0;

    while (fgets(line, sizeof(line), file))
    {
        /* Discard an oversized line in full, including a possible valid prefix. */

        if (!strchr(line, '\n') && !feof(file))
        {
            int ch;

            while ((ch = fgetc(file)) != '\n' && ch != EOF)
            {
            }

            if (ch == EOF)
            {
                break;
            }

            continue;
        }

        char* comment = strchr(line, '#');

        if (comment)
        {
            *comment = 0;
        }

        char* key   = trim(line);
        char* value = strchr(key, '=');

        if (!value)
        {
            continue;
        }

        *value++ = 0;
        key      = trim(key);
        value    = trim(value);

        if (!strcmp(key, "ip"))
        {
            if (address(value, &network.ip))
            {
                addresses |= CONFIG_IP_PRESENT;
            }
        }
        else if (!strcmp(key, "netmask"))
        {
            if (address(value, &network.netmask))
            {
                addresses |= CONFIG_NETMASK_PRESENT;
            }
        }
        else if (!strcmp(key, "gateway"))
        {
            if (address(value, &network.gateway))
            {
                addresses |= CONFIG_GATEWAY_PRESENT;
            }
        }
        else if (!strcmp(key, "lookup_host"))
        {
            uint32_t parsed;

            if (!*value || (strlen(value) < sizeof(config->lookup_host) && address(value, &parsed) && parsed))
            {
                snprintf(config->lookup_host, sizeof(config->lookup_host), "%s", value);
            }
        }
        else if (!strcmp(key, "display"))
        {
            if (!strcmp(value, "panels"))
            {
                config->show_panels = 1;
            }
            else if (!strcmp(value, "fullscreen"))
            {
                config->show_panels = 0;
            }
        }
        else if (!strcmp(key, "frame_rate"))
        {
            const int rates[] = { MILKDROP_FPS_BASELINE, MILKDROP_FPS_HIGH };

            for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
            {
                char text[CONFIG_RATE_BYTES];

                snprintf(text, sizeof(text), "%d", rates[i]);

                if (!strcmp(value, text))
                {
                    config->frame_rate = rates[i];

                    break;
                }
            }
        }
        else if (!strcmp(key, "levels"))
        {
            boolean(value, &config->levels);
        }
        else if (!strcmp(key, "mute"))
        {
            boolean(value, &config->muted);
        }
        else if (!strcmp(key, "cd_autoplay"))
        {
            boolean(value, &config->cd_autoplay);
        }
    }

    /* Static addressing is a complete group; otherwise retain automatic setup. */
    uint32_t inverse_mask = ~network.netmask;

    if (addresses == (CONFIG_IP_PRESENT | CONFIG_NETMASK_PRESENT | CONFIG_GATEWAY_PRESENT) && network.ip && network.netmask && !(inverse_mask & (inverse_mask + 1)))
    {
        config->network = network;
    }
}

AppConfig app_config_load(const char* executable)
{
    AppConfig config = app_config_defaults();
    char      path[CONFIG_PATH_BYTES];

    if (platform_boot_config_path(path, sizeof(path), executable, "STROOM.DAT") != 0)
    {
        return config;
    }

    FILE* file = fopen(path, "r");

    if (file)
    {
        app_config_read(&config, file);
        fclose(file);
    }

    return config;
}
