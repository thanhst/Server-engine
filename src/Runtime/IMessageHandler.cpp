#include <ServerEngine/Runtime/IMessageHandler.h>

namespace serverengine::runtime {

void IMessageHandler::on_session_started(Session&)
{
}

void IMessageHandler::on_session_stopped(Session&)
{
}

void IMessageHandler::on_error(std::string_view)
{
}

} // namespace serverengine::runtime
