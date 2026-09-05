# Sample application code is shared with its protocol tests, not with the engine.
add_library(ServerEngineEchoApp STATIC
    AppConfig.h AppConfig.cpp
    EchoHandler.h EchoHandler.cpp
    Application.h Application.cpp
    StartupDiagnostics.h StartupDiagnostics.cpp
)
target_include_directories(ServerEngineEchoApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ServerEngineEchoApp PUBLIC ServerEngineRuntime)
serverengine_target_defaults(ServerEngineEchoApp)

add_executable(ServerEngine main.cpp)
target_link_libraries(ServerEngine PRIVATE ServerEngineEchoApp)
serverengine_target_defaults(ServerEngine)
if(MSVC)
    target_link_options(ServerEngine PRIVATE
        /SUBSYSTEM:CONSOLE
        $<$<CONFIG:Release>:/LTCG>
    )
endif()

# Keep the original executable path, including for the existing client script.
set_target_properties(ServerEngine PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set_property(TARGET ServerEngine PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
