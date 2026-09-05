#include "TlsContext.h"

#include <boost/asio/buffer.hpp>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

namespace serverengine::net::async {
namespace {

using TlsContext = boost::asio::ssl::context;
constexpr std::streamoff max_pem_file_bytes = 1024 * 1024;

// Avoid retaining an extra plaintext copy of private-key PEM after OpenSSL has
// parsed it. This also clears partially read data if file loading fails.
struct PemBuffer final {
    std::vector<char> bytes;

    PemBuffer() = default;
    PemBuffer(const PemBuffer&) = delete;
    PemBuffer& operator=(const PemBuffer&) = delete;

    ~PemBuffer()
    {
        if (!bytes.empty()) {
            OPENSSL_cleanse(bytes.data(), bytes.size());
        }
    }
};

bool read_pem_file(const std::string& utf8_path, PemBuffer& output)
{
    if (utf8_path.empty() || utf8_path.find('\0') != std::string::npos) {
        return false;
    }

    // OpenSSL's narrow file APIs cannot reliably open Korean/Unicode Windows
    // paths. Let std::filesystem convert UTF-8 and load OpenSSL from memory.
    std::ifstream input(std::filesystem::u8path(utf8_path),
        std::ios::binary | std::ios::ate);
    if (!input) return false;
    const std::streamoff size = input.tellg();
    if (size <= 0 || size > max_pem_file_bytes) return false;

    output.bytes.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    return static_cast<bool>(input.read(output.bytes.data(),
        static_cast<std::streamsize>(output.bytes.size())));
}

std::shared_ptr<TlsContext> reject(std::string* error, const char* message)
{
    // Do not expose OpenSSL's diagnostic stack, file contents, or private paths
    // through the public API. The named stage still makes failures traceable.
    ERR_clear_error();
    if (error) *error = message;
    return nullptr;
}

bool configure_tls_policy(TlsContext& context)
{
    SSL_CTX* native = context.native_handle();
    if (SSL_CTX_set_min_proto_version(native, TLS1_3_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(native, TLS1_3_VERSION) != 1) {
        return false;
    }

    // ECDH creates per-connection keys; AES/ChaCha encrypt the actual bytes.
    // Certificate signing and ephemeral key exchange are separate operations.
    if (SSL_CTX_set1_groups_list(native, "P-256:P-384") != 1 ||
        SSL_CTX_set_ciphersuites(native,
            "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:"
            "TLS_AES_128_GCM_SHA256") != 1) {
        return false;
    }

    // Never accept replayable 0-RTT application messages (e.g. purchases or moves).
    if (SSL_CTX_set_max_early_data(native, 0) != 1) return false;
    context.set_options(TlsContext::no_compression);
    return true;
}

} // namespace

std::shared_ptr<TlsContext> make_tls_context(
    const ListenerConfig& config, std::string* error)
{
    if (error) error->clear();
    if (config.security != ChannelSecurity::Tls ||
        (config.protocol != Protocol::Tcp && config.protocol != Protocol::WebSocket &&
         config.protocol != Protocol::Http)) {
        return reject(error, "TLS requires a TCP, WebSocket or HTTP listener.");
    }

    try {
        PemBuffer certificate;
        PemBuffer private_key;
        if (!read_pem_file(config.certificate_chain_file, certificate)) {
            return reject(error,
                "TLS certificate chain file is unreadable, empty, or exceeds 1 MiB.");
        }
        if (!read_pem_file(config.private_key_file, private_key)) {
            return reject(error,
                "TLS private key file is unreadable, empty, or exceeds 1 MiB.");
        }

        auto context = std::make_shared<TlsContext>(TlsContext::tls_server);
        if (!configure_tls_policy(*context)) {
            return reject(error, "TLS 1.3 ECC/AEAD policy could not be configured.");
        }

        // A DLL must never prompt on stdin for a PEM password. This API accepts
        // unencrypted PEM keys protected by the service account's file ACLs.
        SSL_CTX_set_default_passwd_cb(context->native_handle(),
            [](char*, int, int, void*) -> int { return 0; });

        boost::system::error_code load_error;
        context->use_certificate_chain(boost::asio::buffer(certificate.bytes), load_error);
        if (load_error) {
            return reject(error, "TLS certificate chain is not valid PEM.");
        }
        context->use_private_key(boost::asio::buffer(private_key.bytes),
            TlsContext::pem, load_error);
        if (load_error || SSL_CTX_check_private_key(context->native_handle()) != 1) {
            return reject(error,
                "TLS private key must be unencrypted PEM matching the certificate.");
        }

        EVP_PKEY* key = SSL_CTX_get0_privatekey(context->native_handle());
        if (!key || EVP_PKEY_is_a(key, "EC") != 1 || EVP_PKEY_get_bits(key) < 256) {
            return reject(error, "TLS requires an EC server certificate of at least 256 bits.");
        }

        // Server authentication is completed by the client: it must validate
        // the certificate chain AND the intended server hostname. This listener
        // does not request client certificates; game login remains host logic.
        ERR_clear_error();
        return context;
    } catch (const std::filesystem::filesystem_error&) {
        return reject(error, "TLS PEM path is not valid UTF-8 or cannot be accessed.");
    } catch (const std::exception&) {
        return reject(error, "TLS context initialization failed.");
    }
}

} // namespace serverengine::net::async
