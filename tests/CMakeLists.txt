# The legacy TCP client needs a separately running server and is not in CTest.
# Registered tests create their own fixtures; TransportLoopback uses local ports.
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

if(SE_BUILD_DLL)
    add_executable(ServerEngineDatagramTransportCHeaderTest DatagramTransportHeaderSmoke.c)
    target_link_libraries(ServerEngineDatagramTransportCHeaderTest PRIVATE ServerEngineDll)
    set_target_properties(ServerEngineDatagramTransportCHeaderTest PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    add_test(NAME DatagramTransportCHeader COMMAND ServerEngineDatagramTransportCHeaderTest)

    add_executable(ServerEngineDatagramTransportContractTests DatagramTransportContractTests.cpp)
    target_link_libraries(ServerEngineDatagramTransportContractTests PRIVATE ServerEngineDll Threads::Threads)
    serverengine_target_defaults(ServerEngineDatagramTransportContractTests)
    add_test(NAME DatagramTransportContracts COMMAND ServerEngineDatagramTransportContractTests)

    add_executable(ServerEngineDatagramTransportLoopbackTests DatagramTransportLoopbackTests.cpp)
    target_link_libraries(ServerEngineDatagramTransportLoopbackTests PRIVATE ServerEngineDll Threads::Threads)
    serverengine_target_defaults(ServerEngineDatagramTransportLoopbackTests)
    add_test(NAME DatagramTransportLoopback COMMAND ServerEngineDatagramTransportLoopbackTests)
    set_tests_properties(DatagramTransportLoopback PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
    set_tests_properties(DatagramTransportCHeader DatagramTransportContracts DatagramTransportLoopback PROPERTIES TIMEOUT 60)
    add_executable(ServerEngineDatagramTransportLossTests DatagramTransportLossTests.cpp)
    target_link_libraries(ServerEngineDatagramTransportLossTests PRIVATE ServerEngineDll Boost::headers Threads::Threads)
    target_compile_definitions(ServerEngineDatagramTransportLossTests PRIVATE BOOST_ALL_NO_LIB)
    if(WIN32)
        target_link_libraries(ServerEngineDatagramTransportLossTests PRIVATE ws2_32 mswsock)
    endif()
    serverengine_target_defaults(ServerEngineDatagramTransportLossTests)
    set_target_properties(ServerEngineDatagramTransportLossTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
    add_test(NAME DatagramTransportLoss COMMAND ServerEngineDatagramTransportLossTests)
    set_tests_properties(DatagramTransportLoss PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
    foreach(target IN ITEMS ServerEngineDatagramTransportCHeaderTest
            ServerEngineDatagramTransportContractTests ServerEngineDatagramTransportLoopbackTests)
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
    endforeach()

    add_executable(ServerEngineAbiCHeaderTest AbiHeaderSmoke.c)
    target_link_libraries(ServerEngineAbiCHeaderTest PRIVATE ServerEngineDll)
    set_target_properties(ServerEngineAbiCHeaderTest PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    add_test(NAME AbiCHeader COMMAND ServerEngineAbiCHeaderTest)

    add_executable(ServerEngineAbiTests AbiContractTests.cpp)
    target_link_libraries(ServerEngineAbiTests PRIVATE ServerEngineDll)
    serverengine_target_defaults(ServerEngineAbiTests)
    add_test(NAME AbiContracts COMMAND ServerEngineAbiTests)

    add_executable(ServerEngineEventQueueTests EventQueueTests.cpp)
    target_include_directories(ServerEngineEventQueueTests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(ServerEngineEventQueueTests PRIVATE ServerEngineHost)
    serverengine_target_defaults(ServerEngineEventQueueTests)
    add_test(NAME EventQueue COMMAND ServerEngineEventQueueTests)

    add_executable(ServerEngineTlsTests TlsConfigTests.cpp)
    target_include_directories(ServerEngineTlsTests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(ServerEngineTlsTests PRIVATE ServerEngineTransport)
    serverengine_target_defaults(ServerEngineTlsTests)
    add_test(NAME TlsSecurity COMMAND ServerEngineTlsTests)

    add_executable(ServerEngineTransportTests TransportLoopbackTests.cpp)
    target_link_libraries(ServerEngineTransportTests PRIVATE ServerEngineDll ServerEngineTransport)
    serverengine_target_defaults(ServerEngineTransportTests)
    add_test(NAME TransportLoopback COMMAND ServerEngineTransportTests)

    add_executable(ServerEngineHttpTests HttpLoopbackTests.cpp HttpTestCertificate.h)
    target_link_libraries(ServerEngineHttpTests PRIVATE ServerEngineDll ServerEngineTransport)
    serverengine_target_defaults(ServerEngineHttpTests)
    add_test(NAME HttpLoopback COMMAND ServerEngineHttpTests)

    add_executable(ServerEngineSqlTests SqlServiceTests.cpp)
    target_link_libraries(ServerEngineSqlTests PRIVATE ServerEngineDll)
    serverengine_target_defaults(ServerEngineSqlTests)
    add_test(NAME SqlService COMMAND ServerEngineSqlTests)

    add_executable(ServerEngineCppSdkTests CppSdkTests.cpp)
    target_link_libraries(ServerEngineCppSdkTests PRIVATE ServerEngineDll)
    serverengine_target_defaults(ServerEngineCppSdkTests)
    add_test(NAME CppSdkOwnership COMMAND ServerEngineCppSdkTests)

    add_executable(ServerEngineRedisContractTests RedisContractTests.cpp)
    target_link_libraries(ServerEngineRedisContractTests PRIVATE ServerEngineDll)
    serverengine_target_defaults(ServerEngineRedisContractTests)
    add_test(NAME RedisContracts COMMAND ServerEngineRedisContractTests)

    add_executable(ServerEngineRedisLoopbackTests RedisLoopbackTests.cpp)
    target_link_libraries(ServerEngineRedisLoopbackTests PRIVATE ServerEngineDll Boost::headers Threads::Threads)
    target_compile_definitions(ServerEngineRedisLoopbackTests PRIVATE BOOST_ALL_NO_LIB)
    if(WIN32)
        target_link_libraries(ServerEngineRedisLoopbackTests PRIVATE ws2_32 mswsock)
    endif()
    serverengine_target_defaults(ServerEngineRedisLoopbackTests)
    add_test(NAME RedisLoopback COMMAND ServerEngineRedisLoopbackTests)
    set_tests_properties(RedisLoopback PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(ServerEngineSignalRoomTests SignalRoomTests.cpp
        ${PROJECT_SOURCE_DIR}/examples/WebServer/SignalRoom.cpp)
    target_include_directories(ServerEngineSignalRoomTests PRIVATE ${PROJECT_SOURCE_DIR}/examples/WebServer)
    serverengine_target_defaults(ServerEngineSignalRoomTests)
    add_test(NAME SignalRoom COMMAND ServerEngineSignalRoomTests)

    # DLL and its dependent DLLs share the executable directory on Windows.
    foreach(target IN ITEMS ServerEngineAbiCHeaderTest ServerEngineAbiTests
            ServerEngineEventQueueTests ServerEngineTlsTests ServerEngineTransportTests
            ServerEngineHttpTests ServerEngineSqlTests ServerEngineCppSdkTests
            ServerEngineRedisContractTests ServerEngineRedisLoopbackTests ServerEngineSignalRoomTests)
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
    endforeach()
    set_tests_properties(AbiCHeader AbiContracts EventQueue TlsSecurity TransportLoopback PROPERTIES TIMEOUT 45)
    set_tests_properties(HttpLoopback SqlService CppSdkOwnership RedisContracts RedisLoopback SignalRoom PROPERTIES TIMEOUT 45)
    set_tests_properties(TransportLoopback HttpLoopback RedisLoopback PROPERTIES RUN_SERIAL TRUE)
endif()
