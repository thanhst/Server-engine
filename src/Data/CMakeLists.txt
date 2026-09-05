# Data has no dependency on a network listener or ServerHost. Each service can
# be used by a console worker without opening any application listening socket.
find_package(SQLite3 3.37 REQUIRED)
add_library(ServerEngineSql STATIC
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/Sql.h
    Sql/Types.h Sql/Driver.h Sql/Request.cpp Sql/Result.cpp
    Sql/SqliteDriver.h Sql/SqliteDriver.cpp Sql/SqliteStatement.cpp
    Sql/Service.h Sql/Service.cpp Sql/Registry.cpp)
target_include_directories(ServerEngineSql PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ServerEngineSql PRIVATE SQLite::SQLite3 Threads::Threads)
serverengine_target_defaults(ServerEngineSql)

add_library(ServerEngineRedis STATIC
    ${PROJECT_SOURCE_DIR}/include/ServerEngine/C/Redis.h
    Redis/Types.h Redis/Service.h Redis/Service.cpp Redis/Connection.h Redis/Connection.cpp)
target_include_directories(ServerEngineRedis PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ServerEngineRedis PRIVATE Boost::headers Threads::Threads)
target_compile_definitions(ServerEngineRedis PRIVATE BOOST_ALL_NO_LIB)
if(WIN32)
    target_link_libraries(ServerEngineRedis PRIVATE ws2_32 mswsock)
endif()
if(SE_WITH_REDIS)
    find_package(hiredis CONFIG REQUIRED)
    target_compile_definitions(ServerEngineRedis PRIVATE SERVERENGINE_WITH_REDIS)
    target_link_libraries(ServerEngineRedis PRIVATE hiredis::hiredis)
endif()
serverengine_target_defaults(ServerEngineRedis)

add_library(ServerEngineData INTERFACE)
target_link_libraries(ServerEngineData INTERFACE ServerEngineSql ServerEngineRedis)
