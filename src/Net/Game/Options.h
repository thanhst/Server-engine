#pragma once

#include <ServerEngine/C/GameTransport.h>

namespace serverengine::net::game {

bool valid_options(const se_game_options* options) noexcept;
uint32_t native_receive_bytes(const se_game_options& options) noexcept;
uint32_t native_receive_count(const se_game_options& options) noexcept;
se_game_event empty_event() noexcept;

} // namespace serverengine::net::game
