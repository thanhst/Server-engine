#include <ServerEngine/Security/CredentialVerifier.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using serverengine::security::constant_time_equals;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void reject_both_orders(std::string_view left, std::string_view right)
{
    check(!constant_time_equals(left, right), "reject different credentials");
    check(!constant_time_equals(right, left), "reject different credentials in reverse order");
}

void verify_length_differences_cannot_disappear()
{
    // Regression: an 8-bit length accumulator lost these length differences.
    // Padded zero bytes then matched the old loop's out-of-range zero padding.
    const std::string zeros256(256, '\0');
    const std::string zeros512(512, '\0');
    reject_both_orders({}, zeros256);
    reject_both_orders({}, zeros512);
    reject_both_orders(zeros256, zeros512);

    const std::string credential = "test-credential";
    reject_both_orders(credential, credential + zeros256);
    reject_both_orders(credential, credential + zeros512);
    reject_both_orders(credential, credential + std::string(1, '\0'));
}

void verify_binary_and_normal_comparisons()
{
    check(constant_time_equals({}, {}), "equal empty credentials");
    check(constant_time_equals("test-credential", "test-credential"), "equal text credentials");
    const std::string binary("A\0\x80\xff", 4);
    check(constant_time_equals(binary, binary), "preserve all binary bytes");
    const std::string long_credential(512, '\0');
    check(constant_time_equals(long_credential, long_credential), "equal long binary credentials");

    reject_both_orders("credential", "Credential");
    reject_both_orders("credential", "credenTial");
    reject_both_orders("credential", "credentiaL");
    reject_both_orders("credential", "credentials");
    reject_both_orders({}, "credential");
    reject_both_orders(binary, std::string("A\0\x80\xfe", 4));
}

} // namespace

int main()
{
    try {
        verify_length_differences_cannot_disappear();
        verify_binary_and_normal_comparisons();
        std::cout << "PASS credential comparison\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
