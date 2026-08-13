#pragma once

#include <string>

namespace serverengine::security {

struct EccProviderStatus {
    bool available{false};
    std::string provider_name{};
    std::string detail{};
};

[[nodiscard]] EccProviderStatus check_ecc_p256_provider();

} // namespace serverengine::security
