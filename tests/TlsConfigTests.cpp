#include "Net/Async/TlsContext.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;
using MemoryBio = std::unique_ptr<BIO, decltype(&BIO_free)>;
using serverengine::net::ListenerConfig;
using serverengine::net::async::make_tls_context;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

Key make_key(bool rsa = false)
{
    Key key(rsa ? EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", std::size_t{2048})
                : EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
    check(key != nullptr, "generate test key");
    return key;
}

Certificate make_certificate(EVP_PKEY* key)
{
    Certificate certificate(X509_new(), X509_free);
    check(certificate != nullptr, "allocate test certificate");
    check(X509_set_version(certificate.get(), 2) == 1, "set certificate version");
    check(ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) == 1,
        "set certificate serial");
    check(X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) != nullptr,
        "set certificate start");
    check(X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600) != nullptr,
        "set certificate expiry");
    check(X509_set_pubkey(certificate.get(), key) == 1, "set certificate public key");
    X509_NAME* subject = X509_get_subject_name(certificate.get());
    const unsigned char hostname[] = "localhost";
    check(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
        hostname, -1, -1, 0) == 1, "set certificate subject");
    check(X509_set_issuer_name(certificate.get(), subject) == 1, "set issuer");
    std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> names(
        X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, "DNS:localhost"),
        X509_EXTENSION_free);
    check(names && X509_add_ext(certificate.get(), names.get(), -1) == 1,
        "set localhost subject alternative name");
    check(X509_sign(certificate.get(), key, EVP_sha256()) > 0, "sign test certificate");
    return certificate;
}

// Fixtures are generated at test time; no reusable private key is stored in git.
class PemFiles final {
public:
    PemFiles(const PemFiles&) = delete;
    PemFiles& operator=(const PemFiles&) = delete;

    PemFiles()
    {
        const auto sequence = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto base = std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            const auto suffix = std::to_string(sequence) + "-" + std::to_string(attempt);
            const auto candidate = base / std::filesystem::u8path(
                std::string(u8"ServerEngine-\uD14C\uC2A4\uD2B8-") + suffix);
            if (std::filesystem::create_directory(candidate)) {
                directory_ = candidate;
                return;
            }
        }
        throw std::runtime_error("create unique TLS fixture directory");
    }

    ~PemFiles()
    {
        std::error_code ignored;
        for (const auto& file : files_) std::filesystem::remove(file, ignored);
        std::filesystem::remove(directory_, ignored);
    }

    std::string certificate(X509* certificate)
    {
        MemoryBio pem(BIO_new(BIO_s_mem()), BIO_free);
        check(pem != nullptr, "allocate certificate BIO");
        check(PEM_write_bio_X509(pem.get(), certificate) == 1, "write certificate PEM");
        return write_bio(pem.get());
    }

    std::string key(EVP_PKEY* key, bool encrypted = false)
    {
        MemoryBio pem(BIO_new(BIO_s_mem()), BIO_free);
        check(pem != nullptr, "allocate key BIO");
        const unsigned char password[] = "fixture-password-do-not-print";
        check(PEM_write_bio_PrivateKey(pem.get(), key,
            encrypted ? EVP_aes_256_cbc() : nullptr,
            encrypted ? password : nullptr,
            encrypted ? static_cast<int>(sizeof(password) - 1) : 0, nullptr, nullptr) == 1,
            "write private key PEM");
        return write_bio(pem.get());
    }

    std::string bytes(const std::string& bytes)
    {
        const auto file = directory_ / std::filesystem::u8path(
            std::string(u8"\uC778\uC99D\uC11C-") + std::to_string(files_.size()) + ".pem");
        files_.push_back(file);
        std::ofstream output(file, std::ios::binary);
        check(static_cast<bool>(output.write(bytes.data(),
            static_cast<std::streamsize>(bytes.size()))), "write TLS fixture file");
        output.close();
        check(static_cast<bool>(output), "close TLS fixture file");
        return file.u8string();
    }

private:
    std::string write_bio(BIO* pem)
    {
        const char* buffer = nullptr;
        const long size = BIO_get_mem_data(pem, &buffer);
        check(size > 0 && buffer, "read fixture PEM from BIO");
        return bytes(std::string(buffer, static_cast<std::size_t>(size)));
    }

    std::filesystem::path directory_;
    std::vector<std::filesystem::path> files_;
};

// Exercise OpenSSL's actual handshake in memory: no socket, port, or service is
// needed. Client trust and hostname verification are deliberately enabled.
bool handshake(SSL_CTX* server_context, X509* trusted_certificate,
    const char* hostname = "localhost", int version = TLS1_3_VERSION)
{
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> client_context(
        SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
    check(client_context != nullptr, "create test TLS client context");
    SSL_CTX_set_verify(client_context.get(), SSL_VERIFY_PEER, nullptr);
    check(SSL_CTX_set_min_proto_version(client_context.get(), version) == 1 &&
        SSL_CTX_set_max_proto_version(client_context.get(), version) == 1,
        "set client TLS version");
    if (trusted_certificate) {
        check(X509_STORE_add_cert(SSL_CTX_get_cert_store(client_context.get()),
            trusted_certificate) == 1, "trust this test certificate explicitly");
    }

    std::unique_ptr<SSL, decltype(&SSL_free)> client(SSL_new(client_context.get()), SSL_free);
    std::unique_ptr<SSL, decltype(&SSL_free)> server(SSL_new(server_context), SSL_free);
    check(client && server, "create test TLS connection pair");
    check(SSL_set1_host(client.get(), hostname) == 1, "enable hostname verification");
    BIO* client_bio = nullptr;
    BIO* server_bio = nullptr;
    check(BIO_new_bio_pair(&client_bio, 0, &server_bio, 0) == 1, "create paired memory BIOs");
    SSL_set_bio(client.get(), client_bio, client_bio);
    SSL_set_bio(server.get(), server_bio, server_bio);
    SSL_set_connect_state(client.get());
    SSL_set_accept_state(server.get());

    const auto step = [](SSL* connection) {
        ERR_clear_error();
        const int result = SSL_do_handshake(connection);
        if (result == 1) return 1;
        const int error = SSL_get_error(connection, result);
        return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE ? 0 : -1;
    };
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        const int client_result = step(client.get());
        if (client_result < 0) return false;
        const int server_result = step(server.get());
        if (server_result < 0) return false;
        if (client_result == 1 && server_result == 1) {
            check(SSL_get_verify_result(client.get()) == X509_V_OK, "verify server identity");
            check(SSL_version(client.get()) == TLS1_3_VERSION, "negotiate TLS 1.3");
            const int group = static_cast<int>(SSL_get_negotiated_group(client.get()));
            check(group == NID_X9_62_prime256v1 || group == NID_secp384r1,
                "negotiate configured ECDHE group");
            check(SSL_CIPHER_is_aead(SSL_get_current_cipher(client.get())) == 1,
                "negotiate authenticated payload encryption");
            return true;
        }
    }
    throw std::runtime_error("in-memory TLS handshake made no progress");
}

void verify_tls_policy_and_loading()
{
    PemFiles files;
    auto key = make_key();
    auto certificate = make_certificate(key.get());
    ListenerConfig config;
    config.certificate_chain_file = files.certificate(certificate.get());
    config.private_key_file = files.key(key.get());

    std::string error = "previous error";
    auto context = make_tls_context(config, &error);
    check(context != nullptr, "load matching EC certificate and key through Unicode paths");
    check(error.empty(), "successful load clears the previous error");
    SSL_CTX* native = context->native_handle();
    check(SSL_CTX_get_min_proto_version(native) == TLS1_3_VERSION, "reject older TLS");
    check(SSL_CTX_get_max_proto_version(native) == TLS1_3_VERSION, "pin TLS 1.3 policy");
    check(SSL_CTX_get_max_early_data(native) == 0, "reject replayable early data");
    check(handshake(native, certificate.get()), "trusted TLS 1.3 client connects");
    check(!handshake(native, nullptr), "client rejects an untrusted server certificate");
    check(!handshake(native, certificate.get(), "wrong.example"),
        "client rejects an incorrect server hostname");
    check(!handshake(native, certificate.get(), "localhost", TLS1_2_VERSION),
        "server rejects a TLS 1.2 client");

    config.protocol = serverengine::net::Protocol::WebSocket;
    check(make_tls_context(config, &error) != nullptr, "WebSocket shares TLS policy");
    config.protocol = serverengine::net::Protocol::Udp;
    check(make_tls_context(config, &error) == nullptr, "reject TLS on UDP");
    config.protocol = serverengine::net::Protocol::Tcp;

    auto other_key = make_key();
    config.private_key_file = files.key(other_key.get());
    check(make_tls_context(config, &error) == nullptr, "reject mismatched key");
    check(!error.empty(), "key mismatch has a diagnostic");
    config.private_key_file = files.key(key.get(), true);
    check(make_tls_context(config, &error) == nullptr, "reject encrypted key without prompting");
    check(error.find("fixture-password") == std::string::npos, "never disclose key password");

    config.private_key_file = files.bytes("PRIVATE-DATA-MARKER-invalid-PEM");
    check(make_tls_context(config, &error) == nullptr, "reject malformed private key");
    check(error.find("PRIVATE-DATA-MARKER") == std::string::npos, "never disclose PEM data");
    check(error.find(config.private_key_file) == std::string::npos, "never disclose key path");
    check(make_tls_context(config, nullptr) == nullptr, "error output is optional");

    config.private_key_file = files.bytes(std::string(1024 * 1024 + 1, 'A'));
    check(make_tls_context(config, &error) == nullptr, "bound PEM file size");
    config.private_key_file = files.key(key.get());
    config.certificate_chain_file = files.bytes("NOT-A-CERTIFICATE");
    check(make_tls_context(config, &error) == nullptr, "reject invalid certificate chain");

    auto rsa_key = make_key(true);
    auto rsa_certificate = make_certificate(rsa_key.get());
    config.certificate_chain_file = files.certificate(rsa_certificate.get());
    config.private_key_file = files.key(rsa_key.get());
    check(make_tls_context(config, &error) == nullptr, "require EC server identity");
}

} // namespace

int main()
{
    try {
        verify_tls_policy_and_loading();
        std::cout << "PASS TLS configuration\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
