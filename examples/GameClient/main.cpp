#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <openssl/ssl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using Tcp = asio::ip::tcp;
using Udp = asio::ip::udp;

enum class Mode { Unset, Tcp, Udp };

struct Options {
    Mode mode = Mode::Unset;
    std::string host;
    std::string port;
    std::string trust_file;
    std::vector<std::string> commands;
    std::uint32_t max_frame_bytes = 4096;
    std::chrono::milliseconds timeout{3000};
};

struct UsageError final : std::runtime_error {
    explicit UsageError(const std::string& message) : std::runtime_error(message) {}
};

std::string usage()
{
    return
        "Usage:\n"
        "  ServerEngineGameClient --tcp [--host localhost] [--port 9443]\n"
        "      [--trust certs/server-cert.pem] [commands...]\n"
        "  ServerEngineGameClient --udp [--host 127.0.0.1] [--port 9001] [commands...]\n"
        "\n"
        "Examples:\n"
        "  ServerEngineGameClient --tcp PING \"NAME Thanh\" STATS\n"
        "  ServerEngineGameClient --udp PING \"HELLO UDP\"\n"
        "\n"
        "TCP uses TLS 1.3 plus the DLL 4-byte big-endian frame.\n"
        "UDP sends one datagram per command and has no TLS.\n";
}

bool starts_with_option(std::string_view value)
{
    return value.size() >= 2 && value[0] == '-' && value[1] == '-';
}

std::string require_value(const std::vector<std::string>& args, std::size_t& index,
    std::string_view option)
{
    if (index + 1 >= args.size())
        throw UsageError(std::string(option) + " requires a value");
    ++index;
    if (starts_with_option(args[index]))
        throw UsageError(std::string(option) + " requires a value");
    return args[index];
}

std::uint32_t parse_u32(std::string_view value, std::string_view option,
    std::uint32_t minimum, std::uint32_t maximum)
{
    std::size_t used = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(std::string(value), &used, 10);
    } catch (const std::exception&) {
        throw UsageError(std::string(option) + " must be a decimal number");
    }
    if (used != value.size() || parsed < minimum || parsed > maximum)
        throw UsageError(std::string(option) + " is outside the supported range");
    return static_cast<std::uint32_t>(parsed);
}

Options parse_options(const std::vector<std::string>& args)
{
    Options options;
    for (std::size_t index = 1; index < args.size(); ++index) {
        const auto& arg = args[index];
        if (arg == "--help" || arg == "-h") {
            std::cout << usage();
            std::exit(0);
        } else if (arg == "--tcp") {
            if (options.mode != Mode::Unset) throw UsageError("Choose only one protocol");
            options.mode = Mode::Tcp;
        } else if (arg == "--udp") {
            if (options.mode != Mode::Unset) throw UsageError("Choose only one protocol");
            options.mode = Mode::Udp;
        } else if (arg == "--host") {
            options.host = require_value(args, index, "--host");
        } else if (arg == "--port") {
            const auto value = require_value(args, index, "--port");
            options.port = std::to_string(parse_u32(value, "--port", 1, 65535));
        } else if (arg == "--trust") {
            options.trust_file = require_value(args, index, "--trust");
        } else if (arg == "--timeout-ms") {
            const auto value = require_value(args, index, "--timeout-ms");
            options.timeout = std::chrono::milliseconds(parse_u32(value, "--timeout-ms", 1, 60000));
        } else if (arg == "--max-frame-bytes") {
            const auto value = require_value(args, index, "--max-frame-bytes");
            options.max_frame_bytes = parse_u32(value, "--max-frame-bytes", 1, 16 * 1024 * 1024);
        } else if (arg == "--") {
            while (++index < args.size()) options.commands.push_back(args[index]);
            break;
        } else if (starts_with_option(arg)) {
            throw UsageError("Unknown option: " + arg);
        } else {
            options.commands.push_back(arg);
        }
    }

    if (options.mode == Mode::Unset) throw UsageError("Choose --tcp or --udp");
    if (options.host.empty()) options.host = options.mode == Mode::Tcp ? "localhost" : "127.0.0.1";
    if (options.port.empty()) options.port = options.mode == Mode::Tcp ? "9443" : "9001";
    if (options.mode == Mode::Tcp && options.trust_file.empty())
        options.trust_file = "certs/server-cert.pem";
    return options;
}

std::vector<std::uint8_t> make_tcp_frame(std::string_view payload)
{
    if (payload.size() > (std::numeric_limits<std::uint32_t>::max)())
        throw std::length_error("TCP payload is too large");
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> frame(payload.size() + 4);
    frame[0] = static_cast<std::uint8_t>(size >> 24);
    frame[1] = static_cast<std::uint8_t>(size >> 16);
    frame[2] = static_cast<std::uint8_t>(size >> 8);
    frame[3] = static_cast<std::uint8_t>(size);
    std::copy(payload.begin(), payload.end(), frame.begin() + 4);
    return frame;
}

std::string read_tcp_frame(ssl::stream<Tcp::socket>& stream, std::uint32_t maximum_size)
{
    std::array<std::uint8_t, 4> header{};
    asio::read(stream, asio::buffer(header));
    const std::uint32_t size = (std::uint32_t(header[0]) << 24) |
        (std::uint32_t(header[1]) << 16) | (std::uint32_t(header[2]) << 8) | header[3];
    if (size > maximum_size) throw std::runtime_error("TCP reply exceeds --max-frame-bytes");
    std::string payload(size, '\0');
    if (!payload.empty()) asio::read(stream, asio::buffer(&payload[0], payload.size()));
    return payload;
}

template <typename Sender>
void run_command_loop(const std::vector<std::string>& commands, Sender&& sender)
{
    if (!commands.empty()) {
        for (const auto& command : commands) sender(command);
        return;
    }

    std::cout << "Enter commands. Empty line exits.\n";
    for (;;) {
        std::cout << "> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line) || line.empty()) break;
        sender(line);
    }
}

void write_exchange(std::string_view command, std::string_view reply)
{
    std::cout << "SEND " << command << '\n';
    std::cout << "RECV " << reply << '\n';
}

void run_tcp(const Options& options)
{
    const auto trust_path = std::filesystem::u8path(options.trust_file);
    if (!std::filesystem::exists(trust_path))
        throw std::runtime_error("Trust file not found: " + options.trust_file);

    asio::io_context io;
    ssl::context tls(ssl::context::tls_client);
    if (SSL_CTX_set_min_proto_version(tls.native_handle(), TLS1_3_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(tls.native_handle(), TLS1_3_VERSION) != 1)
        throw std::runtime_error("Failed to configure TLS 1.3 client policy");
    tls.set_verify_mode(ssl::verify_peer);
    tls.load_verify_file(options.trust_file);

    ssl::stream<Tcp::socket> stream(io, tls);
    stream.set_verify_callback(ssl::host_name_verification(options.host));

    Tcp::resolver resolver(io);
    asio::connect(stream.next_layer(), resolver.resolve(options.host, options.port));
    stream.handshake(ssl::stream_base::client);
    std::cout << "TCP TLS connected " << options.host << ':' << options.port << '\n';

    run_command_loop(options.commands, [&](std::string_view command) {
        const auto frame = make_tcp_frame(command);
        asio::write(stream, asio::buffer(frame));
        const auto reply = read_tcp_frame(stream, options.max_frame_bytes);
        write_exchange(command, reply);
    });
}

Udp::endpoint resolve_udp_endpoint(asio::io_context& io, const std::string& host,
    const std::string& port)
{
    Udp::resolver resolver(io);
    Udp::endpoint first;
    bool have_first = false;
    for (const auto& result : resolver.resolve(host, port)) {
        const auto endpoint = result.endpoint();
        if (!have_first) {
            first = endpoint;
            have_first = true;
        }
        if (endpoint.address().is_v4()) return endpoint;
    }
    if (!have_first) throw std::runtime_error("No UDP endpoint resolved");
    return first;
}

std::string read_udp_reply(Udp::socket& socket, const Udp::endpoint& expected_sender,
    std::chrono::milliseconds timeout)
{
    std::array<char, 4096> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        Udp::endpoint sender;
        boost::system::error_code error;
        const auto bytes = socket.receive_from(asio::buffer(buffer), sender, 0, error);
        if (!error) {
            if (sender == expected_sender) return std::string(buffer.data(), bytes);
            continue;
        }
        if (error == asio::error::message_size)
            throw std::runtime_error("UDP reply exceeds the client receive buffer");
        if (error != asio::error::would_block && error != asio::error::try_again)
            throw boost::system::system_error(error);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Timed out waiting for UDP reply");
}

void run_udp(const Options& options)
{
    asio::io_context io;
    const auto endpoint = resolve_udp_endpoint(io, options.host, options.port);
    Udp::socket socket(io);
    socket.open(endpoint.protocol());
    socket.bind(Udp::endpoint(endpoint.protocol(), 0));
    socket.non_blocking(true);
    std::cout << "UDP ready " << socket.local_endpoint() << " -> " << endpoint << '\n';

    run_command_loop(options.commands, [&](std::string_view command) {
        socket.send_to(asio::buffer(command.data(), command.size()), endpoint);
        const auto reply = read_udp_reply(socket, endpoint, options.timeout);
        write_exchange(command, reply);
    });
}

std::vector<std::string> collect_args(int argc, char** argv)
{
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) args.emplace_back(argv[index]);
    return args;
}

#if defined(_WIN32)
std::vector<std::string> collect_args(int argc, wchar_t** argv)
{
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index)
        args.push_back(std::filesystem::path(argv[index]).u8string());
    return args;
}
#endif

int run(const std::vector<std::string>& args)
{
    try {
        const auto options = parse_options(args);
        if (options.mode == Mode::Tcp) run_tcp(options);
        else run_udp(options);
        return 0;
    } catch (const UsageError& error) {
        std::cerr << error.what() << "\n\n" << usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "GameClient failed: " << error.what() << '\n';
        return 1;
    }
}
} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    return run(collect_args(argc, argv));
}
#else
int main(int argc, char** argv)
{
    return run(collect_args(argc, argv));
}
#endif
