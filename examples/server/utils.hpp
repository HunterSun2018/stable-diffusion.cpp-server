#pragma once

#include <cstddef>
#include "json.hpp"
using json = nlohmann::json;

#if SERVER_VERBOSE != 1
#define LOG_VERBOSE(MSG, ...)
#else
#define LOG_VERBOSE(MSG, ...)                                            \
    do                                                                   \
    {                                                                    \
        if (server_verbose)                                              \
        {                                                                \
            server_log("VERB", __func__, __LINE__, MSG, __VA_ARGS__); \
        }                                                                \
    } while (0)
#endif

#define LOG_ERROR(  MSG, ...) server_log("ERR",  __func__, __LINE__, MSG, __VA_ARGS__)
#define LOG_WARNING(MSG, ...) server_log("WARN", __func__, __LINE__, MSG, __VA_ARGS__)
#define LOG_INFO(   MSG, ...) server_log("INFO", __func__, __LINE__, MSG, __VA_ARGS__)

// https://community.openai.com/t/openai-chat-list-of-error-codes-and-types/357791/11
enum error_type {
    ERROR_TYPE_INVALID_REQUEST,
    ERROR_TYPE_AUTHENTICATION,
    ERROR_TYPE_SERVER,
    ERROR_TYPE_NOT_FOUND,
    ERROR_TYPE_PERMISSION,
    ERROR_TYPE_UNAVAILABLE, // custom error
    ERROR_TYPE_NOT_SUPPORTED, // custom error
};

static inline void server_log(const char* level, const char* function, int line, const char* message, const nlohmann::ordered_json& extra);

template <typename T>
static T json_value(const json& body, const std::string& key, const T& default_value) {
    // Fallback null to default value
    if (body.contains(key) && !body.at(key).is_null()) {
        try {
            return body.value(key, default_value);
        }
        catch (nlohmann::json_abi_v3_11_3::detail::type_error const&) {
            std::string message = "Wrong type supplied for parameter '" + key + "'. Expected '" + typeid(default_value).name() + "', using default value.";
            server_log("WARN", __func__, __LINE__, message.c_str(), body);
            return default_value;
        }
    }
    else {
        return default_value;
    }
}

static inline void server_log(const char* level, const char* function, int line, const char* message, const nlohmann::ordered_json& extra) {
    std::stringstream ss_tid;
    ss_tid << std::this_thread::get_id();
    json log = nlohmann::ordered_json{
        {"tid",       ss_tid.str()},
        {"timestamp", time(nullptr)},
    };

    if (true) {
        log.merge_patch({
            {"level",    level},
            {"function", function},
            {"line",     line},
            {"msg",      message},
            });

        if (!extra.empty()) {
            log.merge_patch(extra);
        }

        printf("%s\n", log.dump(-1, ' ', false, json::error_handler_t::replace).c_str());
    }
    else {
        char buf[1024];
        snprintf(buf, 1024, "%4s [%24s] %s", level, function, message);

        if (!extra.empty()) {
            log.merge_patch(extra);
        }
        std::stringstream ss;
        ss << buf << " |";
        for (const auto& el : log.items())
        {
            const std::string value = el.value().dump(-1, ' ', false, json::error_handler_t::replace);
            ss << " " << el.key() << "=" << value;
        }

        const std::string str = ss.str();
        printf("%.*s\n", (int)str.size(), str.data());
    }
    fflush(stdout);
}

static json format_error_response(const std::string& message, const enum error_type type) {
    std::string type_str;
    int code = 500;
    switch (type) {
    case ERROR_TYPE_INVALID_REQUEST:
        type_str = "invalid_request_error";
        code = 400;
        break;
    case ERROR_TYPE_AUTHENTICATION:
        type_str = "authentication_error";
        code = 401;
        break;
    case ERROR_TYPE_NOT_FOUND:
        type_str = "not_found_error";
        code = 404;
        break;
    case ERROR_TYPE_SERVER:
        type_str = "server_error";
        code = 500;
        break;
    case ERROR_TYPE_PERMISSION:
        type_str = "permission_error";
        code = 403;
        break;
    case ERROR_TYPE_NOT_SUPPORTED:
        type_str = "not_supported_error";
        code = 501;
        break;
    case ERROR_TYPE_UNAVAILABLE:
        type_str = "unavailable_error";
        code = 503;
        break;
    }
    return json{
        {"code", code},
        {"message", message},
        {"type", type_str},
    };
}

namespace Hash
{
    template<typename T>
    void hash_combine(size_t& seed,
        const T& val)
    {
        seed ^= std::hash<T>()(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    template <typename T>
    void hash_value(size_t& seed,
        const T& val)
    {
        hash_combine(seed, val);
    }

    template <typename T, typename... Types>
    void hash_value(size_t& seed,
        const T& firstArg,
        const Types&... args)
    {
        hash_combine(seed, firstArg);
        hash_value(seed, args...);
    }

    template <typename... Types>
    size_t hash(const Types&... args)
    {
        size_t seed = 0;
        hash_value(seed, args...);
        return seed;
    }
}