#pragma once

#include <cstdint>
#include <string>

#ifndef EOS_STUB_MODE
    #include <eos_sdk.h>
#else
    using EOS_ProductUserId = void*;
#endif

namespace eos_testing {

inline std::string product_user_id_to_string(EOS_ProductUserId user_id) {
    if (!user_id) {
        return {};
    }

#ifdef EOS_STUB_MODE
    return std::to_string(reinterpret_cast<uintptr_t>(user_id));
#else
    char buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
    int32_t buffer_length = sizeof(buffer);
    if (EOS_ProductUserId_ToString(user_id, buffer, &buffer_length) != EOS_EResult::EOS_Success) {
        return {};
    }
    return buffer;
#endif
}

inline bool product_user_ids_equal(EOS_ProductUserId a, EOS_ProductUserId b) {
    if (!a || !b) {
        return a == b;
    }

#ifdef EOS_STUB_MODE
    return a == b;
#else
    const auto a_string = product_user_id_to_string(a);
    const auto b_string = product_user_id_to_string(b);
    return !a_string.empty() && a_string == b_string;
#endif
}

} // namespace eos_testing
