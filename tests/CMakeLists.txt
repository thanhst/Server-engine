# The legacy TCP client needs a separately running server and is not in CTest.
# Registered tests create their own fixtures.
add_executable(ServerEngineTcpTest test.cpp)
target_link_libraries(ServerEngineTcpTest PRIVATE ServerEnginePort)
serverengine_target_defaults(ServerEngineTcpTest)
set_target_properties(ServerEngineTcpTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_executable(ServerEngineLineFramerTests LineFramerTests.cpp)
target_include_directories(ServerEngineLineFramerTests PRIVATE ${PROJECT_SOURCE_DIR}/src)
serverengine_target_defaults(ServerEngineLineFramerTests)
add_test(NAME LineFramer COMMAND ServerEngineLineFramerTests)

add_executable(ServerEngineRuntimeTests RuntimeTests.cpp)
target_include_directories(ServerEngineRuntimeTests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ServerEngineRuntimeTests PRIVATE ServerEngineRuntime)
serverengine_target_defaults(ServerEngineRuntimeTests)
add_test(NAME RuntimeSessions COMMAND ServerEngineRuntimeTests)

if(WIN32)
    add_executable(ServerEngineIocpCallbackTests IocpCallbackTests.cpp)
    target_include_directories(ServerEngineIocpCallbackTests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(ServerEngineIocpCallbackTests PRIVATE ServerEngineNet)
    serverengine_target_defaults(ServerEngineIocpCallbackTests)
    add_test(NAME IocpCallbacks COMMAND ServerEngineIocpCallbackTests)
endif()

add_executable(ServerEngineEchoAppTests EchoAppTests.cpp)
target_link_libraries(ServerEngineEchoAppTests PRIVATE ServerEngineEchoApp)
serverengine_target_defaults(ServerEngineEchoAppTests)
add_test(NAME EchoApp COMMAND ServerEngineEchoAppTests)

add_executable(ServerEngineCredentialTests CredentialVerifierTests.cpp)
target_link_libraries(ServerEngineCredentialTests PRIVATE ServerEngineSecurity)
serverengine_target_defaults(ServerEngineCredentialTests)
add_test(NAME Credentials COMMAND ServerEngineCredentialTests)
