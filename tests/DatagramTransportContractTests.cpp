#include <ServerEngine/Cpp/DatagramTransport.h>
#include <ServerEngine/Cpp/GameTransport.h>

#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
using serverengine::sdk::DatagramEndpoint;
static_assert(std::is_same_v<serverengine::sdk::GameEndpoint, DatagramEndpoint>,
    "Legacy endpoint alias preserves source compatibility");
static_assert(std::is_same_v<serverengine::sdk::GameDelivery, serverengine::sdk::DatagramDelivery>,
    "Legacy delivery alias preserves source compatibility");
static_assert(std::is_same_v<serverengine::sdk::GameEvent, serverengine::sdk::DatagramEvent>,
    "Legacy event alias preserves source compatibility");
static_assert(std::is_same_v<serverengine::sdk::GameTransportError, serverengine::sdk::DatagramTransportError>,
    "Legacy error alias preserves source compatibility");
static_assert(!std::is_copy_constructible_v<DatagramEndpoint>, "Endpoints cannot share accidental ownership");
static_assert(std::is_nothrow_move_constructible_v<DatagramEndpoint>, "Endpoint move transfers ownership");
static_assert(std::is_nothrow_destructible_v<DatagramEndpoint>, "Endpoint destruction cannot throw");

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void invalid_options()
{
    auto options = DatagramEndpoint::default_options();
    se_datagram_handle output = 123;
    check(se_datagram_create(nullptr, &output, nullptr) == SE_INVALID_ARGUMENT && output == 0, "null options clear handle");
    check(se_datagram_create(&options, nullptr, nullptr) == SE_INVALID_ARGUMENT, "null output rejected");
    options.struct_size--;
    check(se_datagram_create(&options, &output, nullptr) == SE_INVALID_ARGUMENT, "wrong options size rejected");
    options = DatagramEndpoint::default_options();
    options.abi_version++;
    check(se_datagram_create(&options, &output, nullptr) == SE_INVALID_ARGUMENT, "wrong ABI rejected");
    options = DatagramEndpoint::default_options();
    options.flags = 2;
    check(se_datagram_create(&options, &output, nullptr) == SE_INVALID_ARGUMENT, "unknown flags rejected");
    options = DatagramEndpoint::default_options();
    options.reserved[0] = 1;
    check(se_datagram_create(&options, &output, nullptr) == SE_INVALID_ARGUMENT, "reserved fields rejected");
    options = DatagramEndpoint::default_options();
    options.max_peers = 0;
    check(se_datagram_create(&options, &output, nullptr) == SE_INVALID_ARGUMENT, "zero peer limit rejected");
    options = DatagramEndpoint::default_options();
    options.max_message_bytes = 63;
    check(se_datagram_create(&options, &output, nullptr) == SE_INVALID_ARGUMENT, "native minimum message size enforced");
    options = DatagramEndpoint::default_options();
    options.max_message_bytes = 64;
    options.max_send_queue_bytes = 4095;
    check(se_datagram_create(&options, &output, nullptr) == SE_INVALID_ARGUMENT, "native minimum send budget enforced");
}

void disabled_contract()
{
    std::uint64_t peer = 123;
    se_datagram_event event;
    se_datagram_event_init(&event);
    check(se_datagram_listen(0, "127.0.0.1", 9002, nullptr) == SE_NOT_SUPPORTED, "disabled listen");
    check(se_datagram_connect(0, "127.0.0.1", 9002, &peer, nullptr) == SE_NOT_SUPPORTED && peer == 0,
        "disabled connect clears output");
    check(se_datagram_send(0, 1, SE_DATAGRAM_RELIABLE_ORDERED, "x", 1, nullptr) == SE_NOT_SUPPORTED, "disabled send");
    check(se_datagram_poll(0, &event, nullptr, 0, 0, nullptr) == SE_NOT_SUPPORTED, "disabled poll");
    check(se_datagram_disconnect(0, 1, nullptr) == SE_NOT_SUPPORTED, "disabled disconnect");
    check(se_datagram_destroy(0, nullptr) == SE_NOT_SUPPORTED, "disabled destroy");
}

void enabled_contract()
{
    DatagramEndpoint original;
    const auto handle = original.handle();
    DatagramEndpoint endpoint(std::move(original));
    check(original.handle() == 0 && endpoint.handle() == handle, "move preserves exactly one owner");
    bool moved_rejected = false;
    try { original.listen(9002); } catch (const std::logic_error&) { moved_rejected = true; }
    check(moved_rejected, "moved-from wrapper fails safely");

    check(se_datagram_listen(handle, "localhost", 9002, nullptr) == SE_INVALID_ARGUMENT, "numeric listen address required");
    check(se_datagram_listen(handle, "127.0.0.1", 0, nullptr) == SE_INVALID_ARGUMENT, "zero port rejected");
    check(se_datagram_listen(handle, "127.0.0.1", 65536, nullptr) == SE_INVALID_ARGUMENT, "oversized port rejected");
    std::uint64_t peer = 123;
    check(se_datagram_connect(handle, "203.0.113.1", 9002, &peer, nullptr) == SE_INVALID_ARGUMENT && peer == 0,
        "remote unauthenticated connection rejected by default");
    check(se_datagram_connect(handle, "127.0.0.1", 0, &peer, nullptr) == SE_INVALID_ARGUMENT && peer == 0,
        "invalid connect port clears peer");
    check(se_datagram_send(handle, 1, SE_DATAGRAM_RELIABLE_ORDERED, nullptr, 1, nullptr) == SE_INVALID_ARGUMENT,
        "null data with nonzero size rejected");
    check(se_datagram_send(handle, 1, 99, "x", 1, nullptr) == SE_INVALID_ARGUMENT, "unknown delivery mode rejected");
    check(se_datagram_send(handle, 1, SE_DATAGRAM_RELIABLE_ORDERED, "x", 1, nullptr) == SE_INVALID_HANDLE,
        "unknown peer send rejected");
    check(se_datagram_disconnect(handle, 1, nullptr) == SE_INVALID_HANDLE, "unknown peer disconnect rejected");
    se_datagram_event event;
    se_datagram_event_init(&event);
    check(se_datagram_poll(handle, &event, nullptr, 0, 0, nullptr) == SE_TIMEOUT, "empty poll is nonblocking");
    event.abi_version++;
    check(se_datagram_poll(handle, &event, nullptr, 0, 0, nullptr) == SE_INVALID_ARGUMENT, "event ABI validated");
    se_datagram_event_init(&event);
    check(se_datagram_poll(handle, &event, nullptr, 1, 0, nullptr) == SE_INVALID_ARGUMENT, "poll requires caller buffer");
    endpoint.close();
    check(se_datagram_destroy(handle, nullptr) == SE_INVALID_HANDLE, "stale endpoint rejected");
    DatagramEndpoint replacement;
    check(replacement.handle() != handle, "endpoint tokens not reused");
    check(se_datagram_poll(handle, &event, nullptr, 0, 0, nullptr) == SE_INVALID_HANDLE,
        "stale handle cannot reach replacement endpoint");
}
} // namespace

int main()
{
    try {
        check(se_datagram_get_abi_version() == SE_DATAGRAM_ABI_VERSION, "datagram ABI version");
        invalid_options();
        auto options = DatagramEndpoint::default_options();
        se_datagram_handle probe = 123;
        const auto status = se_datagram_create(&options, &probe, nullptr);
        if (status == SE_NOT_SUPPORTED) {
            check(probe == 0, "disabled create clears endpoint output");
            disabled_contract();
        } else {
            check(status == SE_OK && probe != 0, "enabled create succeeds");
            check(se_datagram_destroy(probe, nullptr) == SE_OK, "destroy capability probe");
            enabled_contract();
        }
        std::cout << "PASS datagram transport C ABI contracts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL datagram transport contract: " << error.what() << '\n';
        return 1;
    }
}
