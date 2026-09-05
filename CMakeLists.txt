cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use: cmake --preset vs2022-x64")
endif()

# vcpkg reads manifest features during project(), so select optional drivers first.
option(SE_WITH_REDIS "Enable the optional Redis GET/SET/DELETE connector" OFF)
option(SE_WITH_GAME_TRANSPORT "Enable reliable/unreliable UDP game endpoints (Valve GNS)" OFF)
if(SE_WITH_REDIS)
    list(APPEND VCPKG_MANIFEST_FEATURES redis)
    list(REMOVE_DUPLICATES VCPKG_MANIFEST_FEATURES)
endif()
if(SE_WITH_GAME_TRANSPORT)
    list(APPEND VCPKG_MANIFEST_FEATURES game-transport)
    list(REMOVE_DUPLICATES VCPKG_MANIFEST_FEATURES)
endif()
project(ServerEngine VERSION 1.2.0 LANGUAGES C CXX)

option(SE_BUILD_DLL "Build the general-purpose C ABI shared library" ON)
option(SE_BUILD_GAME_EXAMPLE "Build the DLL host example with SQLite" ON)
option(SE_BUILD_WEB_EXAMPLE "Build the HTTPS/API/WebRTC signaling host" ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/CompilerOptions.cmake)
include(CTest)
find_package(Threads REQUIRED)

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_library(ServerEnginePort STATIC
    include/ServerEngine/Port/Clock.h
    include/ServerEngine/Port/Platform.h
    include/ServerEngine/Port/Process.h
    include/ServerEngine/Port/Socket.h
    src/Port/Clock.cpp
    src/Port/Process.cpp
    src/Port/Socket.cpp
)

target_include_directories(ServerEnginePort
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(ServerEngineCore STATIC
    include/ServerEngine/Core/Buffer.h
    include/ServerEngine/Core/Config.h
    include/ServerEngine/Core/Logger.h
    src/Core/Buffer.cpp
    src/Core/Config.cpp
    src/Core/Logger.cpp
)

target_include_directories(ServerEngineCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ServerEngineCore
    PRIVATE
        ServerEnginePort
)

add_library(ServerEngineNet STATIC
    include/ServerEngine/Net/Endpoint.h
    include/ServerEngine/Net/ITcpServer.h
    include/ServerEngine/Net/TcpBackend.h
    include/ServerEngine/Net/TcpIocpServer.h
    include/ServerEngine/Net/TcpServer.h
    include/ServerEngine/Net/TcpTypes.h
    include/ServerEngine/Net/TransportKind.h
    src/Net/Endpoint.cpp
    src/Net/TcpBackend.cpp
    src/Net/TcpIocpServer.cpp
    src/Net/TcpServer.cpp
    src/Net/TransportKind.cpp
    src/Net/Detail/LineFramer.h
    src/Net/Threaded/TcpServerImpl.h
    src/Net/Threaded/TcpServerImpl.cpp
    src/Net/Threaded/ClientConnection.h
    src/Net/Threaded/ClientConnection.cpp
    src/Net/Threaded/SocketOperations.h
    src/Net/Threaded/SocketOperations.cpp
    src/Net/Iocp/WinSockSupport.h
    src/Net/Iocp/WinSockSupport.cpp
    src/Net/Iocp/CompletionPort.h
    src/Net/Iocp/CompletionPort.cpp
    src/Net/Iocp/Connection.h
    src/Net/Iocp/Connection.cpp
    src/Net/Iocp/Operation.h
    src/Net/Iocp/Server.h
    src/Net/Iocp/ServerLifecycle.cpp
    src/Net/Iocp/ServerAccept.cpp
    src/Net/Iocp/ServerIo.cpp
    src/Net/Iocp/ServerConnections.cpp
)

target_include_directories(ServerEngineNet
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ServerEngineNet
    PUBLIC
        ServerEngineCore
        ServerEnginePort
        Threads::Threads
)

add_library(ServerEngineSecurity STATIC
    include/ServerEngine/Security/CredentialVerifier.h
    include/ServerEngine/Security/EccProvider.h
    include/ServerEngine/Security/SecurityMode.h
    src/Security/CredentialVerifier.cpp
    src/Security/EccProvider.cpp
    src/Security/SecurityMode.cpp
)

target_include_directories(ServerEngineSecurity
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ServerEngineSecurity
    PUBLIC
        ServerEnginePort
)

add_library(ServerEngineRuntime STATIC
    include/ServerEngine/Runtime/ConnectionHub.h
    include/ServerEngine/Runtime/IMessageHandler.h
    include/ServerEngine/Runtime/Server.h
    include/ServerEngine/Runtime/ServerOptions.h
    include/ServerEngine/Runtime/Session.h
    src/Runtime/ConnectionHub.cpp
    src/Runtime/IMessageHandler.cpp
    src/Runtime/Server.cpp
    src/Runtime/SessionRegistry.h
    src/Runtime/SessionRegistry.cpp
    src/Runtime/TcpListener.h
    src/Runtime/TcpListener.cpp
    src/Runtime/Session.cpp
)

target_include_directories(ServerEngineRuntime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ServerEngineRuntime
    PUBLIC
        ServerEngineCore
        ServerEngineNet
        ServerEngineSecurity
)

foreach(target_name IN ITEMS ServerEnginePort ServerEngineCore ServerEngineNet ServerEngineSecurity ServerEngineRuntime)
    serverengine_target_defaults(${target_name})
endforeach()

add_subdirectory(apps/EchoServer)
if(SE_BUILD_DLL)
    find_package(Boost 1.85 REQUIRED CONFIG)
    find_package(OpenSSL 3.0 REQUIRED)
    add_subdirectory(src/Net/Async)
    add_subdirectory(src/Net/Game)
    add_subdirectory(src/Runtime/Host)
    add_library(ServerEngineNetwork INTERFACE)
    target_link_libraries(ServerEngineNetwork INTERFACE ServerEngineHost)
    add_subdirectory(src/Data)
    add_subdirectory(src/Abi)
    if(SE_BUILD_GAME_EXAMPLE)
        add_subdirectory(examples/GameServer)
    endif()
    if(SE_BUILD_WEB_EXAMPLE)
        add_subdirectory(examples/WebServer)
    endif()
    if(SE_WITH_REDIS)
        add_subdirectory(examples/RedisCache)
    endif()
    if(SE_WITH_GAME_TRANSPORT)
        add_subdirectory(examples/GameTransport)
    endif()
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(WIN32)
    target_link_libraries(ServerEnginePort
        PUBLIC
            ws2_32
    )
    target_link_libraries(ServerEngineSecurity
        PUBLIC
            bcrypt
    )
endif()


set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ServerEngine)
