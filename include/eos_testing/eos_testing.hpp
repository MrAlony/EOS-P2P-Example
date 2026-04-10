#pragma once

/**
 * EOS Testing - Main Include Header
 * 
 * Include this single header to get access to all EOS Testing functionality.
 */

#include "core/platform.hpp"
#include "auth/auth_manager.hpp"
#include "lobby/lobby_manager.hpp"
#include "p2p/p2p_manager.hpp"
#include "voice/voice_manager.hpp"
#include "matchmaking/matchmaking_manager.hpp"
#include "utils/eos_id.hpp"

namespace eos_testing {

/**
 * Convenience function to initialize the entire EOS stack.
 * 
 * @param config Platform configuration
 * @param callback Called when initialization completes
 */
inline void initialize(const PlatformConfig& config, 
                       std::function<void(bool success, const std::string& message)> callback) {
    Platform::instance().initialize(config, callback);
}

/**
 * Convenience function to shut down the entire EOS stack.
 */
inline void shutdown() {
    VoiceManager::instance().shutdown();
    P2PManager::instance().shutdown();
    Platform::instance().shutdown();
}

/**
 * Tick all EOS subsystems. Call this every frame.
 */
inline void tick() {
    Platform::instance().tick();
}

} // namespace eos_testing
