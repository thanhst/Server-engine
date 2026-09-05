add_library(ServerEngineHost STATIC
    ServerHost.h ServerHost.cpp
    EventQueue.h EventQueue.cpp
)
target_link_libraries(ServerEngineHost PUBLIC ServerEngineTransport)
serverengine_target_defaults(ServerEngineHost)
