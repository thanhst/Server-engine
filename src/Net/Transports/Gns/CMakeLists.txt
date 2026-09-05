# Always build options/ABI support, including when the optional driver is off.
add_library(ServerEngineGns STATIC
    Api.h Options.h Options.cpp)
target_include_directories(ServerEngineGns
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ServerEngineGns PRIVATE Threads::Threads)
serverengine_target_defaults(ServerEngineGns)

if(SE_WITH_GNS)
    find_package(GameNetworkingSockets CONFIG REQUIRED)
    if(TARGET GameNetworkingSockets::static)
        target_link_libraries(ServerEngineGns PRIVATE GameNetworkingSockets::static)
    elseif(TARGET GameNetworkingSockets::shared)
        target_link_libraries(ServerEngineGns PRIVATE GameNetworkingSockets::shared)
    else()
        message(FATAL_ERROR "GameNetworkingSockets static/shared imported target is missing")
    endif()
    target_sources(ServerEngineGns PRIVATE NativeConfig.h NativeConfig.cpp
        Endpoint.h Endpoint.cpp EndpointEvents.cpp Runtime.cpp)
    # Transitive define also selects real dispatch in the DLL ABI translation unit.
    target_compile_definitions(ServerEngineGns PUBLIC SERVERENGINE_WITH_GNS)
endif()
