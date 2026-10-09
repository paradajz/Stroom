#include "contracts/milkdrop.h"
#include "app/config.h"
#include "util/ipv4.h"
#include <ctype.h>
#include <string.h>

#define CONFIG_LINE_BYTES      256
#define CONFIG_PATH_BYTES      1024
#define CONFIG_RATE_BYTES      12
#define CONFIG_IP_PRESENT      1u
#define CONFIG_NETMASK_PRESENT 2u
#define CONFIG_GATEWAY_PRESENT 4u

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
    AppConfig   config = app_config_defaults();
    char        path[CONFIG_PATH_BYTES];
    const char* separator = strrchr(executable, '/');

    if (!separator)
    {
        separator = strrchr(executable, ':');
    }

    size_t prefix = separator ? (size_t)(separator - executable + 1) : 0;

    if (prefix + sizeof("STROOM.DAT") > sizeof(path))
    {
        return config;
    }

    memcpy(path, executable, prefix);
    memcpy(path + prefix, "STROOM.DAT", sizeof("STROOM.DAT"));

    FILE* file = fopen(path, "r");

    if (file)
    {
        app_config_read(&config, file);
        fclose(file);
    }

    return config;
}
