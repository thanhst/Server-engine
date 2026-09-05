#pragma once

#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

// A short-lived, explicitly trusted localhost certificate. Generated only when
// the user runs the test; no private key or persistent trust changes in source.
class HttpTestCertificate final {
public:
    HttpTestCertificate()
    {
        using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
        using Cert = std::unique_ptr<X509, decltype(&X509_free)>;
        Key key(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
        Cert cert(X509_new(), X509_free);
        require(key && cert, "allocate HTTPS fixture key/certificate");
        require(X509_set_version(cert.get(), 2) == 1 &&
            ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1) == 1 &&
            X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60) &&
            X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600) &&
            X509_set_pubkey(cert.get(), key.get()) == 1, "configure HTTPS fixture certificate");
        auto* subject = X509_get_subject_name(cert.get());
        const unsigned char hostname[] = "localhost";
        require(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
            hostname, -1, -1, 0) == 1 && X509_set_issuer_name(cert.get(), subject) == 1,
            "configure HTTPS fixture subject");
        std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> san(
            X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, "DNS:localhost"), X509_EXTENSION_free);
        require(san && X509_add_ext(cert.get(), san.get(), -1) == 1 &&
            X509_sign(cert.get(), key.get(), EVP_sha256()) > 0, "sign HTTPS fixture");

        certificate_pem = pem([&](BIO* bio) { return PEM_write_bio_X509(bio, cert.get()); });
        const auto private_pem = pem([&](BIO* bio) {
            return PEM_write_bio_PrivateKey(bio, key.get(), nullptr, nullptr, 0, nullptr, nullptr);
        });
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt != 100; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("ServerEngine-http-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) { directory_ = candidate; break; }
        }
        require(!directory_.empty(), "create unique HTTPS fixture directory");
        try {
            certificate_file = write("certificate.pem", certificate_pem);
            key_file = write("private.pem", private_pem);
        } catch (...) { cleanup(); throw; }
    }

    ~HttpTestCertificate() { cleanup(); }
    HttpTestCertificate(const HttpTestCertificate&) = delete;
    HttpTestCertificate& operator=(const HttpTestCertificate&) = delete;

    std::string certificate_file;
    std::string key_file;
    std::string certificate_pem;

private:
    static void require(bool okay, const char* message)
    {
        if (!okay) throw std::runtime_error(message);
    }

    template<class Write>
    static std::string pem(Write write)
    {
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
        require(bio && write(bio.get()) == 1, "encode HTTPS fixture PEM");
        const char* bytes{};
        const auto size = BIO_get_mem_data(bio.get(), &bytes);
        require(size > 0 && bytes, "read HTTPS fixture PEM");
        return {bytes, static_cast<std::size_t>(size)};
    }

    std::string write(const char* name, const std::string& bytes)
    {
        const auto path = directory_ / name;
        std::ofstream file(path, std::ios::binary);
        require(static_cast<bool>(file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))),
            "write HTTPS fixture PEM");
        file.close();
        require(static_cast<bool>(file), "close HTTPS fixture PEM");
        return path.u8string();
    }

    void cleanup() noexcept
    {
        if (directory_.empty()) return;
        std::error_code ignored;
        std::filesystem::remove(directory_ / "certificate.pem", ignored);
        std::filesystem::remove(directory_ / "private.pem", ignored);
        std::filesystem::remove(directory_, ignored);
    }

    std::filesystem::path directory_;
};
