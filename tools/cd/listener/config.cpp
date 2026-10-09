#include "listener.hpp"
#include "listener_config.hpp"
#include <arpa/inet.h>
#include <spa/utils/json.h>
#include <pwd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

namespace stroom::cd
{
    namespace
    {
        constexpr size_t   config_bytes     = 4096;
        constexpr unsigned delete_character = 127;

        /**
         * @brief Remove leading JSON whitespace from a borrowed input view.
         * @param input Remaining configuration bytes, advanced in place.
         */
        void whitespace(std::string_view& input)
        {
            auto first = input.find_first_not_of(" \t\r\n");
            input.remove_prefix(first == std::string_view::npos ? input.size() : first);
        }

        /**
         * @brief Consume an optional JSON separator after whitespace.
         * @param input Remaining configuration bytes, advanced on a match.
         * @param expected Required separator character.
         * @return True when the separator was consumed.
         */
        bool take(std::string_view& input, char expected)
        {
            whitespace(input);

            if (input.empty() || input.front() != expected)
            {
                return false;
            }

            input.remove_prefix(1);
            return true;
        }

        /**
         * @brief Read one scalar JSON token without accepting skipped punctuation.
         * @param input Remaining configuration bytes, advanced past the token.
         * @return Token borrowing the original configuration storage.
         * @throws std::runtime_error On a malformed or missing token.
         */
        std::string_view token(std::string_view& input)
        {
            whitespace(input);
            spa_json parser{};
            spa_json_init(&parser, input.data(), input.size());
            const char* value  = nullptr;
            int         length = spa_json_next(&parser, &value);

            if (length <= 0 || value != input.data())
            {
                throw std::runtime_error("Expected a JSON value");
            }

            auto result = input.substr(0, static_cast<size_t>(length));
            input.remove_prefix(result.size());
            return result;
        }

        /**
         * @brief Decode a quoted configuration key or IPv4 string.
         * @param value Complete JSON string token.
         * @return Owned decoded text without embedded control characters.
         * @throws std::runtime_error On invalid string syntax or control characters.
         */
        std::string text(std::string_view value)
        {
            std::array<char, config_bytes + 1> decoded{};

            if (value.size() < 2 || value.front() != '"' || value.back() != '"' ||
                value.find("\\u0000") != std::string_view::npos ||
                spa_json_parse_stringn(value.data(), static_cast<int>(value.size()), decoded.data(), decoded.size()) < 0)
            {
                throw std::runtime_error("Expected a JSON string without control characters");
            }

            std::string result(decoded.data());

            for (unsigned char character : result)
            {
                if (character < ' ' || character == delete_character)
                {
                    throw std::runtime_error("Control character in configuration");
                }
            }

            return result;
        }

        /**
         * @brief Merge the supported flat JSON settings into listener options.
         * @param input Borrowed complete JSON configuration object.
         * @param result Options updated only for supplied settings.
         * @throws std::runtime_error On invalid syntax, unknown or duplicate settings, or invalid values.
         */
        void settings(std::string_view input, Options& result)
        {
            std::set<std::string> seen;

            if (!take(input, '{'))
            {
                throw std::runtime_error("Expected a JSON object");
            }

            if (!take(input, '}'))
            {
                for (;;)
                {
                    auto key = text(token(input));

                    if (!seen.insert(key).second)
                    {
                        throw std::runtime_error("Duplicate setting: " + key);
                    }

                    if (!take(input, ':'))
                    {
                        throw std::runtime_error("Expected ':' after setting name");
                    }

                    auto value = token(input);

                    if (key == "host")
                    {
                        result.host = value == "null" ? std::string{} : text(value);
                        in_addr address{};

                        if (value != "null" && inet_pton(AF_INET, result.host.c_str(), &address) != 1)
                        {
                            throw std::runtime_error("host must be an IPv4 address or null");
                        }
                    }
                    else if (key == "buffer_ms")
                    {
                        if (value.size() > 1 && value.front() == '0')
                        {
                            throw std::runtime_error("Invalid buffer_ms number");
                        }

                        result.buffer_ms = number(value, 0, max_buffer_ms);
                    }
                    else
                    {
                        throw std::runtime_error("Unknown setting: " + key);
                    }

                    if (take(input, '}'))
                    {
                        break;
                    }

                    if (!take(input, ','))
                    {
                        throw std::runtime_error("Expected ',' or '}' after setting");
                    }
                }
            }

            whitespace(input);

            if (!input.empty())
            {
                throw std::runtime_error("Unexpected data after configuration object");
            }
        }

        /**
         * @brief Resolve the login account's default listener configuration path.
         * @return Path under an absolute XDG_CONFIG_HOME or the account's home directory.
         * @throws std::runtime_error When no absolute home directory can be determined.
         */
        std::filesystem::path default_path()
        {
            const char* xdg = std::getenv("XDG_CONFIG_HOME");

            if (xdg && std::filesystem::path(xdg).is_absolute())
            {
                return std::filesystem::path(xdg) / "stroom/listener.json";
            }

            const char* home = std::getenv("HOME");

            if (!home || !std::filesystem::path(home).is_absolute())
            {
                auto* account = getpwuid(getuid());
                home          = account ? account->pw_dir : nullptr;
            }

            if (!home || !std::filesystem::path(home).is_absolute())
            {
                throw std::runtime_error("Cannot determine listener config directory");
            }

            return std::filesystem::path(home) / ".config/stroom/listener.json";
        }

        /**
         * @brief Create the default config without replacing an existing file.
         * @param path Absolute default config destination.
         * @throws std::runtime_error On directory, creation or write failure.
         */
        void initialize(const std::filesystem::path& path)
        {
            std::filesystem::create_directories(path.parent_path());
            std::string temporary = path.string() + ".XXXXXX";
            int         fd        = mkstemp(temporary.data());

            if (fd < 0)
            {
                throw std::runtime_error("Cannot create default configuration");
            }

            std::string_view remaining(listener_config_template);
            bool             failed = false;

            while (!remaining.empty())
            {
                auto written = write(fd, remaining.data(), remaining.size());

                if (written < 0 && errno == EINTR)
                {
                    continue;
                }

                if (written <= 0)
                {
                    failed = true;
                    break;
                }

                remaining.remove_prefix(static_cast<size_t>(written));
            }

            failed = close(fd) != 0 || failed;

            if (!failed && link(temporary.c_str(), path.c_str()) != 0 && errno != EEXIST)
            {
                failed = true;
            }

            unlink(temporary.c_str());

            if (failed)
            {
                throw std::runtime_error("Cannot write default configuration");
            }
        }

        /**
         * @brief Read a regular configuration file with a fixed size ceiling.
         * @param path Existing JSON configuration path.
         * @return Owned configuration text, at most config_bytes bytes.
         * @throws std::runtime_error On an unreadable, nonregular or oversized file.
         */
        std::string read(const std::filesystem::path& path)
        {
            if (!std::filesystem::is_regular_file(path))
            {
                throw std::runtime_error("Configuration file does not exist or is not a regular file");
            }

            std::ifstream input(path, std::ios::binary);

            if (!input)
            {
                throw std::runtime_error("Cannot read configuration file");
            }

            std::array<char, config_bytes + 1> bytes{};
            input.read(bytes.data(), bytes.size());
            auto count = input.gcount();

            if (input.bad() || count > static_cast<std::streamsize>(config_bytes))
            {
                throw std::runtime_error("Configuration exceeds 4 KiB or could not be read");
            }

            return { bytes.data(), static_cast<size_t>(count) };
        }

        /**
         * @brief Select the configuration file and one-shot listener actions.
         * @param argc Argument count including the executable.
         * @param argv Borrowed argument strings.
         * @param result Selected configuration path and action flags.
         * @throws std::runtime_error On unknown options, missing arguments or invalid values.
         */
        void arguments(int argc, char** argv, Options& result)
        {
            for (int i = 1; i < argc; ++i)
            {
                std::string key(argv[i]), value;
                auto        equals    = key.find('=');
                bool        has_value = equals != std::string::npos;

                if (has_value)
                {
                    value = key.substr(equals + 1);
                    key.resize(equals);
                }

                if (!has_value && key == "--config")
                {
                    if (++i == argc)
                    {
                        throw std::runtime_error("Missing value for " + key);
                    }

                    value     = argv[i];
                    has_value = true;
                }

                if (key == "--config")
                {
                    if (value.empty())
                    {
                        throw std::runtime_error("Missing configuration path");
                    }

                    result.config = value;
                }
                else if (!has_value && key == "--check-config")
                {
                    result.check_config = true;
                }
                else if (!has_value && key == "--list")
                {
                    result.list = true;
                }
                else if (!has_value && (key == "--help" || key == "-h"))
                {
                    result.help = true;
                }
                else
                {
                    throw std::runtime_error("Unknown option: " + key);
                }
            }
        }

    }    // namespace

    Options options(int argc, char** argv)
    {
        Options selected;
        arguments(argc, argv, selected);

        if (selected.help)
        {
            return selected;
        }

        bool    explicit_path = !selected.config.empty();
        auto    path          = std::filesystem::absolute(explicit_path ? std::filesystem::path(selected.config) : default_path()).lexically_normal();
        Options result        = selected;
        try
        {
            settings(listener_config_template, result);

            if (!explicit_path && !std::filesystem::exists(path))
            {
                initialize(path);
            }

            settings(read(path), result);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error("Listener configuration " + path.string() + ": " + error.what());
        }

        result.config = path.string();
        return result;
    }
}    // namespace stroom::cd
