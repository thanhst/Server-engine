add_executable(ServerEngineWebServer
    main.cpp WebServer.h WebServer.cpp ProfileStore.h ProfileStore.cpp SignalRoom.h SignalRoom.cpp)
target_link_libraries(ServerEngineWebServer PRIVATE ServerEngineDll)
serverengine_target_defaults(ServerEngineWebServer)
set_target_properties(ServerEngineWebServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
    VS_DEBUGGER_WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
