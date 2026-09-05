add_library(ServerEngineDll SHARED
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/ServerEngine.h
    Boundary.h HandleRegistry.h HandleRegistry.cpp
    Options.h Options.cpp
    Lifecycle.cpp Messaging.cpp
    Http.cpp Sql.cpp Redis.cpp DatagramTransport.cpp GameTransport.cpp
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/Http.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/Sql.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/Redis.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/Cpp/Engine.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/DatagramTransport.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/Cpp/DatagramTransport.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/GameTransport.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/Cpp/GameTransport.h
)
target_include_directories(ServerEngineDll
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ServerEngineDll PRIVATE ServerEngineNetwork ServerEngineData ServerEngineGns)
target_compile_definitions(ServerEngineDll PRIVATE SERVERENGINE_DLL_BUILD)
serverengine_target_defaults(ServerEngineDll)
set_target_properties(ServerEngineDll PROPERTIES
    OUTPUT_NAME ServerEngine
    PDB_NAME ServerEngineDll
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1)
if(WIN32)
    target_sources(ServerEngineDll PRIVATE ServerEngine.def)
endif()
if(MSVC)
    target_link_options(ServerEngineDll PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

# The SDK exposes C headers, a header-only C++ wrapper and the DLL/import library.
# Deploy matching OpenSSL runtime DLLs too when using dynamic dependency builds.
install(TARGETS ServerEngineDll RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(FILES
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/ServerEngine.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/Http.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/Sql.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/Redis.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/DatagramTransport.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/GameTransport.h
    DESTINATION include/ServerEngine/C)
install(FILES ${PROJECT_SOURCE_DIR}/include/ServerEngine/Cpp/Engine.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/Cpp/DatagramTransport.h
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/Cpp/GameTransport.h DESTINATION include/ServerEngine/Cpp)
