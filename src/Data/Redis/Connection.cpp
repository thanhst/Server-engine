#include "Connection.h"

#include <boost/asio.hpp>

#include <atomic>
#include <array>
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

#if defined(SERVERENGINE_WITH_REDIS)
#include <hiredis/hiredis.h>
#endif

namespace serverengine::data::redis {
namespace asio = boost::asio;
using Tcp = asio::ip::tcp;

#if defined(SERVERENGINE_WITH_REDIS)
namespace {
struct ReplyDeleter { void operator()(redisReply* reply) const noexcept { freeReplyObject(reply); } };
struct ReaderDeleter { void operator()(redisReader* reader) const noexcept { redisReaderFree(reader); } };
struct CommandDeleter { void operator()(char* command) const noexcept { redisFreeCommand(command); } };
using Reply = std::unique_ptr<redisReply, ReplyDeleter>;
}

// Hiredis handles binary-safe command encoding and RESP parsing. Asio owns
// transport, a deadline for the WHOLE command, and stop cancellation. In
// particular, a peer sending one byte periodically cannot extend the deadline.
class Connection::Impl {
public:
    explicit Impl(const Options& options) : options_(options), socket_(io_), timer_(io_) {}

    void cancel() noexcept
    {
        cancelled_.store(true);
        try {
            asio::post(io_, [this] { finish(SE_STOPPED); close(); });
        } catch (...) {
            // Allocation failure still leaves the already armed finite deadline.
        }
    }

    void execute(Request& request)
    {
        bool write_may_have_started = false;
        try {
            auto& result = request.completion;
            if (cancelled_.load()) {
                result.fail(SE_STOPPED, "Redis request cancelled before execution");
                return;
            }
            if (!socket_.is_open()) {
                const auto connected = connect();
                if (connected != SE_OK) {
                    result.fail(connected, "Redis connection failed or was cancelled");
                    return;
                }
                if (!options_.password.empty()) {
                    std::vector<std::string_view> auth{"AUTH"};
                    if (!options_.username.empty()) auth.push_back(options_.username);
                    auth.push_back(options_.password);
                    const auto authenticated = command(auth, 4096);
                    if (authenticated != SE_OK || !is_ok()) {
                        close();
                        result.fail(authenticated == SE_OK ? SE_IO_ERROR : authenticated,
                            "Redis authentication failed or was cancelled");
                        return;
                    }
                }
            }

            const std::string_view key(reinterpret_cast<const char*>(request.key.data()), request.key.size());
            // std::string_view(nullptr, 0) is avoided even for an empty value.
            const std::string_view value(request.value.empty() ? "" :
                reinterpret_cast<const char*>(request.value.data()), request.value.size());
            std::vector<std::string_view> arguments;
            std::string ttl;
            if (result.operation == SE_REDIS_GET) arguments = {"GET", key};
            else if (result.operation == SE_REDIS_DELETE) arguments = {"DEL", key};
            else {
                arguments = {"SET", key, value};
                if (request.ttl_ms != 0) {
                    ttl = std::to_string(request.ttl_ms);
                    arguments.push_back("PX");
                    arguments.push_back(ttl);
                }
            }
            if (cancelled_.load()) {
                result.fail(SE_STOPPED, "Redis request cancelled before execution");
                return;
            }
            // From this point failures are conservatively ambiguous for writes.
            // No automatic retry can accidentally apply an operation twice.
            write_may_have_started = result.operation != SE_REDIS_GET;
            const auto status = command(arguments, options_.max_value_bytes + UINT64_C(4096));
            if (status != SE_OK) {
                const auto outcome = write_may_have_started ? SE_OUTCOME_UNKNOWN : status;
                result.fail(outcome, write_may_have_started ?
                    "Redis write outcome unknown; reconcile before retrying" :
                    "Redis read failed, exceeded its limit, timed out or was cancelled");
                return;
            }
            if (reply_->type == REDIS_REPLY_ERROR) {
                result.fail(SE_IO_ERROR, "Redis rejected the command; check ACL and key type");
                return;
            }
            if (result.operation == SE_REDIS_GET) {
                if (reply_->type == REDIS_REPLY_NIL) return;
                if (reply_->type != REDIS_REPLY_STRING) {
                    close();
                    result.fail(SE_IO_ERROR, "Redis GET returned an unexpected reply type");
                    return;
                }
                if (reply_->len > options_.max_value_bytes) {
                    result.fail(SE_RESULT_TOO_LARGE, "Redis value exceeds max_value_bytes");
                    return;
                }
                if (reply_->len != 0) {
                    const auto* begin = reinterpret_cast<const std::uint8_t*>(reply_->str);
                    result.value.assign(begin, begin + reply_->len);
                }
                result.found = true;
            } else if (result.operation == SE_REDIS_DELETE) {
                if (reply_->type != REDIS_REPLY_INTEGER || reply_->integer < 0 || reply_->integer > 1) {
                    close();
                    result.fail(SE_OUTCOME_UNKNOWN, "Redis DELETE returned an unexpected reply");
                    return;
                }
                result.affected_count = static_cast<std::uint64_t>(reply_->integer);
            } else if (is_ok()) result.affected_count = 1;
            else {
                close();
                result.fail(SE_OUTCOME_UNKNOWN, "Redis SET returned an unexpected reply");
            }
        } catch (...) {
            finish(SE_INTERNAL_ERROR);
            close();
            drain();
            request.completion.fail(write_may_have_started ? SE_OUTCOME_UNKNOWN : SE_INTERNAL_ERROR,
                write_may_have_started ? "Redis write outcome unknown after a local failure" :
                "Redis client failed locally");
        }
    }

private:
    void close() noexcept
    {
        boost::system::error_code ignored;
        socket_.close(ignored);
    }

    void finish(se_status status) noexcept
    {
        if (finished_) return;
        finished_ = true;
        status_ = status;
        // Current Asio has only the throwing timer cancel(). Keep finish()
        // non-throwing so socket cleanup and completion delivery still happen.
        try { (void)timer_.cancel(); } catch (...) {}
        if (status != SE_OK) close();
    }

    template<class Handler>
    auto guarded(Handler handler)
    {
        return [this, handler = std::move(handler)](auto... arguments) noexcept {
            if (finished_) return;
            try { handler(arguments...); }
            catch (...) { finish(SE_INTERNAL_ERROR); }
        };
    }

    void begin(std::uint32_t timeout_ms)
    {
        finished_ = false;
        status_ = SE_OK;
        timer_.expires_after(std::chrono::milliseconds(timeout_ms));
        timer_.async_wait(guarded([this](boost::system::error_code error) {
            if (!error) finish(SE_TIMEOUT);
        }));
    }

    void drain()
    {
        io_.restart();
        io_.run();
    }

    se_status connect()
    {
        begin(options_.connect_timeout_ms);
        socket_.async_connect(Tcp::endpoint(asio::ip::make_address(options_.address), options_.port),
            guarded([this](boost::system::error_code error) { finish(error ? SE_IO_ERROR : SE_OK); }));
        drain();
        if (cancelled_.load()) { close(); return SE_STOPPED; }
        return status_;
    }

    bool is_ok() const noexcept
    {
        return reply_ && reply_->type == REDIS_REPLY_STATUS && reply_->len == 2
            && std::memcmp(reply_->str, "OK", 2) == 0;
    }

    se_status command(const std::vector<std::string_view>& arguments, std::uint64_t maximum_reply)
    {
        if (cancelled_.load()) return SE_STOPPED;
        std::vector<const char*> argv;
        std::vector<std::size_t> lengths;
        for (const auto argument : arguments) { argv.push_back(argument.data()); lengths.push_back(argument.size()); }
        char* encoded = nullptr;
        const auto encoded_size = redisFormatCommandArgv(&encoded, static_cast<int>(argv.size()),
            argv.data(), lengths.data());
        command_.reset(encoded);
        if (encoded_size < 0 || !command_) return SE_INTERNAL_ERROR;
        reader_.reset(redisReaderCreate());
        if (!reader_) return SE_INTERNAL_ERROR;
        // This API only accepts RESP2 scalar replies, never arrays/push streams.
        reader_->maxelements = 1;
        reply_.reset();
        received_ = 0;
        reply_limit_ = maximum_reply;
        bulk_limit_ = maximum_reply > 4096 ? maximum_reply - 4096 : 0;
        header_.clear();
        header_checked_ = false;
        begin(options_.command_timeout_ms);
        asio::async_write(socket_, asio::buffer(command_.get(), static_cast<std::size_t>(encoded_size)),
            guarded([this](boost::system::error_code error, std::size_t) {
                if (error) finish(SE_IO_ERROR);
                else read_reply();
            }));
        drain();
        command_.reset();
        return status_;
    }

    bool check_header(const char* bytes, std::size_t size)
    {
        if (header_checked_) return true;
        for (std::size_t i = 0; i < size; ++i) {
            if (header_.size() >= 4096) { finish(SE_RESULT_TOO_LARGE); return false; }
            header_.push_back(bytes[i]);
            const auto count = header_.size();
            if (count < 2 || header_[count - 2] != '\r' || header_[count - 1] != '\n') continue;
            header_checked_ = true;
            if (header_[0] == '$') {
                std::int64_t length = 0;
                const auto end = header_.data() + count - 2;
                const auto parsed = std::from_chars(header_.data() + 1, end, length);
                if (parsed.ec != std::errc{} || parsed.ptr != end || length < -1) {
                    finish(SE_IO_ERROR);
                    return false;
                }
                // Validate the advertised bulk length before hiredis sees it,
                // including when a peer sends only a giant length header.
                if (length >= 0 && static_cast<std::uint64_t>(length) > bulk_limit_) {
                    finish(SE_RESULT_TOO_LARGE);
                    return false;
                }
            }
            return true;
        }
        return true;
    }

    void read_reply()
    {
        socket_.async_read_some(asio::buffer(incoming_),
            guarded([this](boost::system::error_code error, std::size_t size) {
                if (error) { finish(SE_IO_ERROR); return; }
                if (received_ == 0 && incoming_[0] != '+' && incoming_[0] != '-'
                    && incoming_[0] != ':' && incoming_[0] != '$') {
                    finish(SE_IO_ERROR);
                    return;
                }
                // Check BEFORE feeding hiredis: neither a huge GET nor a bad
                // endpoint may accumulate an unbounded response in its parser.
                if (size > reply_limit_ - received_) { finish(SE_RESULT_TOO_LARGE); return; }
                received_ += size;
                if (!check_header(incoming_.data(), size)) return;
                if (redisReaderFeed(reader_.get(), incoming_.data(), size) != REDIS_OK) {
                    finish(SE_IO_ERROR);
                    return;
                }
                void* parsed = nullptr;
                if (redisReaderGetReply(reader_.get(), &parsed) != REDIS_OK) {
                    finish(SE_IO_ERROR);
                    return;
                }
                if (parsed != nullptr) {
                    reply_.reset(static_cast<redisReply*>(parsed));
                    // Only one command is outstanding; trailing unsolicited
                    // bytes cannot be interpreted as the next request's reply.
                    if (reader_->len != reader_->pos) { finish(SE_IO_ERROR); return; }
                    finish(SE_OK);
                } else read_reply();
            }));
    }

    const Options& options_;
    asio::io_context io_;
    Tcp::socket socket_;
    asio::steady_timer timer_;
    std::atomic<bool> cancelled_{false};
    bool finished_ = true;
    se_status status_ = SE_OK;
    std::array<char, 4096> incoming_{};
    std::uint64_t received_ = 0;
    std::uint64_t reply_limit_ = 0;
    std::uint64_t bulk_limit_ = 0;
    std::string header_;
    bool header_checked_ = false;
    std::unique_ptr<char, CommandDeleter> command_;
    std::unique_ptr<redisReader, ReaderDeleter> reader_;
    Reply reply_;
};
#else
class Connection::Impl {
public:
    explicit Impl(const Options&) {}
    void execute(Request& request) { request.completion.fail(SE_NOT_SUPPORTED, "Redis support was not compiled"); }
    void cancel() noexcept {}
};
#endif

Connection::Connection(const Options& options) : impl_(std::make_unique<Impl>(options)) {}
Connection::~Connection() = default;
void Connection::execute(Request& request) { impl_->execute(request); }
void Connection::cancel() noexcept { impl_->cancel(); }
bool Connection::supported() noexcept
{
#if defined(SERVERENGINE_WITH_REDIS)
    return true;
#else
    return false;
#endif
}
bool Connection::valid_address(const std::string& address) noexcept
{
    boost::system::error_code error;
    asio::ip::make_address(address, error);
    return !error;
}

} // namespace serverengine::data::redis
