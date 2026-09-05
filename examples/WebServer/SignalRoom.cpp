#include "SignalRoom.h"
#include <utility>

SignalRoom::SignalRoom(Send send, Disconnect disconnect)
    : send_(std::move(send)), disconnect_(std::move(disconnect)) {}

void SignalRoom::opened(std::uint64_t session)
{
    if (members_[0] && members_[1]) { disconnect_(session); return; }
    const auto slot = members_[0] ? 1 : 0;
    members_[slot] = session;
    if (!send_(session, "WAIT")) { closed(session); return; }
    if (members_[0] && members_[1] && !send_(members_[0], "OFFER")) closed(members_[0]);
}

void SignalRoom::message(std::uint64_t session, std::string_view payload)
{
    if (session != members_[0] && session != members_[1]) { disconnect_(session); return; }
    if (payload == "PING") { if (!send_(session, "PONG")) closed(session); return; }
    if (payload.size() <= 7 || payload.size() > 65536 || payload.substr(0, 7) != "SIGNAL\n") {
        disconnect_(session); closed(session); return;
    }
    const auto other = members_[0] == session ? members_[1] : members_[0];
    if (other && !send_(other, payload)) { disconnect_(other); closed(other); }
}

void SignalRoom::closed(std::uint64_t session)
{
    if (session != members_[0] && session != members_[1]) return;
    const auto other = members_[0] == session ? members_[1] : members_[0];
    members_ = {}; // End the entire call: old SDP cannot enter a new pairing.
    if (other) disconnect_(other);
}
