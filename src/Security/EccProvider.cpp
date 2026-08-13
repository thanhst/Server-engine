#include <ServerEngine/Security/EccProvider.h>

#include <ServerEngine/Port/Platform.h>

#if SERVERENGINE_OS_WINDOWS
#include <windows.h>
#include <bcrypt.h>
#endif

namespace serverengine::security {

EccProviderStatus check_ecc_p256_provider()
{
#if SERVERENGINE_OS_WINDOWS
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    const auto status = ::BCryptOpenAlgorithmProvider(
        &algorithm,
        BCRYPT_ECDH_P256_ALGORITHM,
        nullptr,
        0);

    if (algorithm != nullptr) {
        ::BCryptCloseAlgorithmProvider(algorithm, 0);
    }

    if (status == 0) {
        return EccProviderStatus{true, "Windows CNG ECDH P-256", "available"};
    }

    return EccProviderStatus{false, "Windows CNG ECDH P-256", "BCryptOpenAlgorithmProvider failed"};
#else
    return EccProviderStatus{false, "unavailable", "ECC provider is not implemented for this platform yet"};
#endif
}

} // namespace serverengine::security
