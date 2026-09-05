#include "SignalRoom.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
}
int main()
{
    std::vector<std::pair<std::uint64_t, std::string>> sent;
    std::vector<std::uint64_t> disconnected;
    SignalRoom room([&](auto id, auto text) { sent.emplace_back(id, text); return true; },
        [&](auto id) { disconnected.push_back(id); });
    room.opened(10);
    check(sent.size() == 1 && sent.back().second == "WAIT", "First peer must wait");
    room.opened(20);
    check(sent.back().first == 10 && sent.back().second == "OFFER", "Exactly the first peer initiates SDP");
    const auto admitted_messages = sent.size();
    room.opened(30);
    check(disconnected.back() == 30 && sent.size() == admitted_messages, "Third peer must not receive room signaling");
    room.message(10, "SIGNAL\n{\"description\":{}}");
    check(sent.back().first == 20, "SDP must reach the other peer only");
    room.closed(10);
    check(disconnected.back() == 20, "Closing one participant must end the entire old pairing");
    const auto old_messages = sent.size();
    room.opened(40);
    room.message(20, "SIGNAL\n{\"candidate\":{}}");
    check(sent.size() == old_messages + 1 && disconnected.back() == 20, "Old signaling cannot enter a new pairing");
    room.message(40, "PING");
    check(sent.back().second == "PONG", "Heartbeat keeps signaling session alive during media flow");
    room.message(40, "unexpected payload");
    check(disconnected.back() == 40, "Unknown application commands must not be relayed");
    std::cout << "Signal room tests passed\n";
}
