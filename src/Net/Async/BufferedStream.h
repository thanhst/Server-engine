#pragma once

#include <boost/beast/core/buffered_read_stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket/teardown.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <utility>

namespace serverengine::net::async {

// Preserve data read past the HTTP upgrade headers for the WebSocket parser.
// Beast's direct buffered accept has a small internal handshake buffer; this
// adapter lets us parse bounded HTTP headers separately without losing frames.
template<class Stream>
class BufferedStream : public boost::beast::buffered_read_stream<Stream, boost::beast::flat_buffer> {
public:
    using boost::beast::buffered_read_stream<Stream, boost::beast::flat_buffer>::buffered_read_stream;
};

// WebSocket teardown is an ADL customization point. Forward through our adapter
// so both the TCP and TLS layers retain Beast's own close-handshake behavior.
template<class Stream>
void teardown(boost::beast::role_type role, BufferedStream<Stream>& stream,
    boost::system::error_code& error)
{
    using boost::beast::websocket::teardown;
    teardown(role, stream.next_layer(), error);
}

template<class Stream, class Handler>
void async_teardown(boost::beast::role_type role, BufferedStream<Stream>& stream, Handler&& handler)
{
    using boost::beast::websocket::async_teardown;
    async_teardown(role, stream.next_layer(), std::forward<Handler>(handler));
}

} // namespace serverengine::net::async
