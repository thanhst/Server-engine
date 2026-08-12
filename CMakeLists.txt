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
    src/Port/Clock.cpp
    src/Port/Process.cpp
)

target_include_directories(ServerEnginePort
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(ServerEngineCore STATIC
    include/ServerEngine/Core/Config.h
    include/ServerEngine/Core/Logger.h
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

add_executable(ServerEngine
    ServerEngine.cpp
)

target_link_libraries(ServerEngine
    PRIVATE
        ServerEngineCore
        ServerEnginePort
)

foreach(target_name IN ITEMS ServerEnginePort ServerEngineCore ServerEngine)
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

if(MSVC)
    target_link_options(ServerEngine PRIVATE
        /SUBSYSTEM:CONSOLE
        $<$<CONFIG:Release>:/LTCG>
    )
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ServerEngine)
