find_package(Boost 1.85 REQUIRED CONFIG)
find_package(OpenSSL 3.0 REQUIRED)

add_library(ServerEngineTransport STATIC
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/Net/TransportService.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/Net/HttpTypes.h
    TransportService.cpp
    WorkerContext.h WorkerContext.cpp
    Connection.h ConnectionState.h ConnectionState.cpp
    Listener.h StreamTypes.h BufferedStream.h
    TcpListener.h TcpListener.cpp
    TcpConnection.h WebSocketConnection.h
    HttpConnection.h HttpRequestCodec.h HttpRequestCodec.cpp
    HttpResponseValidation.h HttpResponseValidation.cpp
    UdpListener.h UdpListener.cpp
    UdpPeer.h UdpPeer.cpp
    TlsContext.h TlsContext.cpp
)
target_include_directories(ServerEngineTransport PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ServerEngineTransport PUBLIC
    ServerEngineCore Threads::Threads Boost::headers OpenSSL::SSL OpenSSL::Crypto)
target_compile_definitions(ServerEngineTransport PUBLIC BOOST_ALL_NO_LIB)
serverengine_target_defaults(ServerEngineTransport)
if(MSVC)
    # Instantiating the TCP/TLS/HTTP/WebSocket templates can exceed the COFF
    # section limit in x64 Debug builds (C1128). Keep full debug information.
    target_compile_options(ServerEngineTransport PRIVATE /bigobj)
endif()
if(WIN32)
    target_link_libraries(ServerEngineTransport PUBLIC ws2_32 mswsock)
endif()
