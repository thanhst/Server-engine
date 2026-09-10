add_executable(ServerEngineGameClient main.cpp)
target_link_libraries(ServerEngineGameClient PRIVATE Boost::headers OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_compile_definitions(ServerEngineGameClient PRIVATE BOOST_ALL_NO_LIB)
serverengine_target_defaults(ServerEngineGameClient)
if(WIN32)
    target_link_libraries(ServerEngineGameClient PRIVATE ws2_32)
endif()
set_target_properties(ServerEngineGameClient PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
    VS_DEBUGGER_WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
