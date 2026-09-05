add_executable(ServerEngineGameServer
    main.cpp GameServer.h GameServer.cpp PlayerStore.h PlayerStore.cpp)
target_link_libraries(ServerEngineGameServer PRIVATE ServerEngineDll)
serverengine_target_defaults(ServerEngineGameServer)
set_target_properties(ServerEngineGameServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
    VS_DEBUGGER_WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
