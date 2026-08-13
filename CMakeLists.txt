cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use: cmake --preset vs2022-x64")
endif()

project(ServerEngine LANGUAGES CXX)

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
)

target_include_directories(ServerEngineNet
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ServerEngineNet
    PUBLIC
        ServerEngineCore
        ServerEnginePort
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

add_executable(ServerEngine
    ServerEngine.cpp
)

add_executable(ServerEngineTcpTest
    tests/test.cpp
)

target_link_libraries(ServerEngine
    PRIVATE
        ServerEngineRuntime
        ServerEngineCore
        ServerEngineNet
        ServerEnginePort
        ServerEngineSecurity
)

target_link_libraries(ServerEngineTcpTest
    PRIVATE
        ServerEnginePort
)

foreach(target_name IN ITEMS ServerEnginePort ServerEngineCore ServerEngineNet ServerEngineSecurity ServerEngineRuntime ServerEngine ServerEngineTcpTest)
    target_compile_features(${target_name} PUBLIC cxx_std_17)
    set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_definitions(${target_name} PRIVATE
            UNICODE
            _UNICODE
            _CONSOLE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>:WIN32>
        )

        target_compile_options(${target_name} PRIVATE
            /W3
            /permissive-
            /sdl
            $<$<CONFIG:Release>:/Gy>
            $<$<CONFIG:Release>:/GL>
            $<$<CONFIG:Release>:/Oi>
        )
    endif()
endforeach()

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

if(MSVC)
    target_link_options(ServerEngine PRIVATE
        /SUBSYSTEM:CONSOLE
        $<$<CONFIG:Release>:/LTCG>
    )
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ServerEngine)
