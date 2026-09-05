#pragma once

// Compatibility aliases. New applications use DatagramTransport.h directly.
#include <ServerEngine/C/GameTransport.h>
#include <ServerEngine/Cpp/DatagramTransport.h>

namespace serverengine::sdk {
using GameDelivery = DatagramDelivery;
using GameTransportError = DatagramTransportError;
using GameEvent = DatagramEvent;
using GameEndpoint = DatagramEndpoint;
} // namespace serverengine::sdk
