#pragma once

#include <ServerEngine/C/ServerEngine.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <string_view>

namespace serverengine::abi {

inline se_status fail(se_error* error, se_status code, std::string_view message) noexcept
{
    if (error != nullptr) {
        error->code = code;
        const auto length = (std::min)(message.size(), sizeof(error->message) - 1);
        if (length != 0) {
            std::memcpy(error->message, message.data(), length);
        }
        error->message[length] = '\0';
    }
    return code;
}

// Every fallible export uses this boundary. No C++ exception escapes to C/C#.
template<class Function>
se_status protect(se_error* error, Function&& function) noexcept
{
    if (error != nullptr) {
        *error = {};
    }
    try {
        return function();
    } catch (const std::exception& exception) {
        return fail(error, SE_INTERNAL_ERROR, exception.what());
    } catch (...) {
        return fail(error, SE_INTERNAL_ERROR, "unhandled engine error");
    }
}

} // namespace serverengine::abi
