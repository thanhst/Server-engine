#pragma once

#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>

namespace serverengine::net::async {

using PlainStream = boost::beast::tcp_stream;
using TlsStream = boost::asio::ssl::stream<PlainStream>;

template<class Stream>
void close_stream(Stream& stream)
{
    // Forced local close is bounded, including unresponsive peers. No graceful
    // delivery promise: callers must use their own application acknowledgment.
    boost::system::error_code ignored;
    auto& socket = boost::beast::get_lowest_layer(stream).socket();
    socket.cancel(ignored);
    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
}

} // namespace serverengine::net::async
