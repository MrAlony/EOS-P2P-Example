/**
 * EOS Testing - Test Client
 *
 * A simple test application that demonstrates all EOS functionality.
 * Runs through authentication, lobby, P2P, and voice chat.
 *
 * SETUP: Edit config/credentials.hpp with your Epic Developer Portal credentials!
 */

#include "eos_testing/eos_testing.hpp"
#include "../config/credentials.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace eos_testing;

void print_header(const std::string& title) {
    std::cout << "\n========================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "========================================\n\n";
}

void test_authentication() {
    print_header("Testing Authentication");

    auto& auth = AuthManager::instance();

    std::cout << "Logging in with Device ID...\n";

    bool login_complete = false;
    auth.login_device_id("TestPlayer", [&](const AuthResult& result) {
        if (result.success) {
            std::cout << "SUCCESS: Logged in as '" << result.display_name << "'\n";
            std::cout << "  Product User ID: " << result.product_user_id << "\n";
        } else {
            std::cout << "FAILED: " << result.error_message << "\n";
        }
        login_complete = true;
    });

    // Wait for callback (in stub mode this is synchronous)
    while (!login_complete) {
        Platform::instance().tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "Is logged in: " << (auth.is_logged_in() ? "YES" : "NO") << "\n";
}

void test_lobby() {
    print_header("Testing Lobby System");

    auto& lobby = LobbyManager::instance();

    // Create a lobby
    std::cout << "Creating lobby...\n";

    CreateLobbyOptions options;
    options.lobby_name = "Test Game Room";
    options.bucket_id = "deathmatch:global";  // Required: game_mode:region format
    options.max_members = 8;
    options.permission = LobbyPermission::PublicAdvertised;
    options.attributes["game_mode"] = "deathmatch";
    options.attributes["map"] = "arena_01";

    bool create_complete = false;
    lobby.create_lobby(options, [&](bool success, const std::string& lobby_id, const std::string& error) {
        if (success) {
            std::cout << "SUCCESS: Created lobby '" << lobby_id << "'\n";
        } else {
            std::cout << "FAILED: " << error << "\n";
        }
        create_complete = true;
    });

    while (!create_complete) {
        Platform::instance().tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Test lobby operations
    if (lobby.is_in_lobby()) {
        std::cout << "Is owner: " << (lobby.is_owner() ? "YES" : "NO") << "\n";

        // Set some attributes
        lobby.set_lobby_attribute("status", "waiting");
        lobby.set_member_attribute("character", "ninja");
        lobby.set_ready(true);

        std::cout << "All members ready: " << (lobby.all_members_ready() ? "YES" : "NO") << "\n";

        // Search for lobbies
        std::cout << "\nSearching for public lobbies...\n";

        bool search_complete = false;
        lobby.search_lobbies("deathmatch:global", 10, {}, [&](bool success, const std::vector<LobbySearchResult>& results) {
            if (success) {
                std::cout << "Found " << results.size() << " lobbies:\n";
                for (const auto& result : results) {
                    std::cout << "  - " << result.lobby_name << " ("
                              << result.current_members << "/" << result.max_members << ")\n";
                }
            }
            search_complete = true;
        });

        while (!search_complete) {
            Platform::instance().tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
}

void test_p2p() {
    print_header("Testing P2P Networking");

    auto& p2p = P2PManager::instance();

    // Initialize P2P
    P2PConfig config;
    config.socket_name = "TestGameSocket";
    config.allow_relay = true;

    if (p2p.initialize(config)) {
        std::cout << "SUCCESS: P2P initialized\n";
        std::cout << "  Socket: " << config.socket_name << "\n";
        std::cout << "  Max packet size: " << config.max_packet_size << " bytes\n";
    } else {
        std::cout << "FAILED: P2P initialization failed\n";
        return;
    }

    // Accept incoming connections
    p2p.accept_connections();
    std::cout << "Accepting connections from all peers\n";

    // Set up callbacks
    p2p.on_connection_established = [](EOS_ProductUserId peer, ConnectionStatus status) {
        std::cout << "CALLBACK: Peer connected!\n";
    };

    p2p.on_packet_received = [](const IncomingPacket& packet) {
        std::cout << "CALLBACK: Received " << packet.data.size()
                  << " bytes on channel " << (int)packet.channel << "\n";
    };

    EOS_ProductUserId test_peer = nullptr;
#ifdef EOS_STUB_MODE
    test_peer = reinterpret_cast<EOS_ProductUserId>(static_cast<uintptr_t>(0xDEADBEEF));
    std::cout << "Connecting to fake peer...\n";
    p2p.connect_to_peer(test_peer);
#else
    std::cout << "Skipping fake peer connect in real EOS mode\n";
#endif

    // Process for a bit
    for (int i = 0; i < 5; i++) {
        Platform::instance().tick();
        p2p.receive_packets();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Test sending a packet
    if (test_peer && p2p.is_connected_to(test_peer)) {
        std::cout << "Sending test packet...\n";

        const char* message = "Hello, peer!";
        bool sent = p2p.send_packet(test_peer, message, strlen(message) + 1,
                                     0, PacketReliability::ReliableOrdered);
        std::cout << "Packet sent: " << (sent ? "YES" : "NO") << "\n";

        // Test broadcast
        std::cout << "Broadcasting to all peers...\n";
        p2p.broadcast_packet(message, strlen(message) + 1, 0, PacketReliability::UnreliableUnordered);
    }

    std::cout << "Connected peers: " << p2p.get_peer_count() << "\n";
}

void test_voice() {
    print_header("Testing Voice Chat");

    auto& voice = VoiceManager::instance();

    if (voice.initialize()) {
        std::cout << "SUCCESS: Voice chat initialized\n";
    } else {
        std::cout << "FAILED: Voice chat initialization failed\n";
        return;
    }

    // Join a voice room
    std::cout << "Joining voice room...\n";

    bool join_complete = false;
    voice.join_room("test-lobby-room", [&](bool success, const std::string& room_name) {
        if (success) {
            std::cout << "SUCCESS: Joined voice room '" << room_name << "'\n";
        } else {
            std::cout << "FAILED: Could not join voice room\n";
        }
        join_complete = true;
    });

    while (!join_complete) {
        Platform::instance().tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Test voice controls
    if (voice.is_in_room()) {
        std::cout << "Testing voice controls...\n";

        voice.set_input_mode(VoiceInputMode::PushToTalk);
        voice.set_push_to_talk(true);
        std::cout << "  PTT pressed - transmitting: " << (voice.is_transmitting() ? "YES" : "NO") << "\n";

        voice.set_push_to_talk(false);
        std::cout << "  PTT released - transmitting: " << (voice.is_transmitting() ? "YES" : "NO") << "\n";

        voice.set_self_mute(true);
        std::cout << "  Self muted: " << (voice.is_self_muted() ? "YES" : "NO") << "\n";

        voice.set_input_volume(0.8f);
        voice.set_output_volume(0.9f);

        auto participants = voice.get_participants();
        std::cout << "  Participants: " << participants.size() << "\n";
    }
}

void test_matchmaking() {
    print_header("Testing Matchmaking");

    auto& mm = MatchmakingManager::instance();

    // Create a session as host
    std::cout << "Creating game session...\n";

    bool create_complete = false;
    std::unordered_map<std::string, std::string> attrs;
    attrs["game_mode"] = "battle_royale";
    attrs["region"] = "us-east";

    mm.create_session("Epic Battle Room", 16, attrs, [&](bool success, const SessionInfo& session, const std::string& error) {
        if (success) {
            std::cout << "SUCCESS: Created session '" << session.session_name << "'\n";
            std::cout << "  Session ID: " << session.session_id << "\n";
            std::cout << "  Max players: " << session.max_players << "\n";
        } else {
            std::cout << "FAILED: " << error << "\n";
        }
        create_complete = true;
    });

    while (!create_complete) {
        Platform::instance().tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Test session operations
    if (mm.is_in_session()) {
        std::cout << "Is host: " << (mm.is_host() ? "YES" : "NO") << "\n";
        std::cout << "Status: " << static_cast<int>(mm.get_status()) << "\n";

        // Start the match
        std::cout << "\nStarting match...\n";
        mm.on_match_started = []() {
            std::cout << "CALLBACK: Match started!\n";
        };

        bool start_complete = false;
        mm.start_match([&](bool success, const std::string& error) {
            std::cout << "Match start: " << (success ? "SUCCESS" : "FAILED") << "\n";
            start_complete = true;
        });

        while (!start_complete) {
            Platform::instance().tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        // End the match
        std::cout << "Ending match...\n";
        mm.on_match_ended = []() {
            std::cout << "CALLBACK: Match ended!\n";
        };

        bool end_complete = false;
        mm.end_match([&](bool success, const std::string& error) {
            std::cout << "Match end: " << (success ? "SUCCESS" : "FAILED") << "\n";
            end_complete = true;
        });

        while (!end_complete) {
            Platform::instance().tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
}

int main() {
    std::cout << "==============================================\n";
    std::cout << "    EOS Testing - Proof of Concept Client\n";
    std::cout << "==============================================\n";

    // Check if credentials are configured
    if (!config::credentials_configured()) {
        std::cout << "\n";
        std::cout << "ERROR: Credentials not configured!\n";
        std::cout << "\n";
        std::cout << "Please edit config/credentials.hpp with your Epic Developer Portal credentials.\n";
        std::cout << "See the guide below:\n";
        std::cout << "\n";
        std::cout << "1. Go to https://dev.epicgames.com/portal\n";
        std::cout << "2. Create/select your product\n";
        std::cout << "3. Go to Product Settings to find your IDs\n";
        std::cout << "4. Create a Client with 'Peer2Peer' policy type\n";
        std::cout << "5. Copy all IDs to config/credentials.hpp\n";
        std::cout << "\n";
        std::cout << "Running in STUB MODE for demo...\n";
    }

    // Initialize platform
    print_header("Initializing EOS Platform");

    PlatformConfig config;
    config.product_name = config::PRODUCT_NAME;
    config.product_version = config::PRODUCT_VERSION;
    config.product_id = config::PRODUCT_ID;
    config.sandbox_id = config::SANDBOX_ID;
    config.deployment_id = config::DEPLOYMENT_ID;
    config.client_id = config::CLIENT_ID;
    config.client_secret = config::CLIENT_SECRET;

    bool init_complete = false;
    eos_testing::initialize(config, [&](bool success, const std::string& message) {
        std::cout << "Platform init: " << (success ? "SUCCESS" : "FAILED") << "\n";
        std::cout << "  " << message << "\n";
        init_complete = true;
    });

    while (!init_complete) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (!Platform::instance().is_ready()) {
        std::cout << "Platform not ready, exiting.\n";
        return 1;
    }

    // Run tests
    test_authentication();
    test_lobby();
    test_p2p();
    test_voice();
    test_matchmaking();

    // Cleanup
    print_header("Shutting Down");

    LobbyManager::instance().leave_lobby(nullptr);
    VoiceManager::instance().leave_room(nullptr);
    P2PManager::instance().shutdown();
    MatchmakingManager::instance().leave_session(nullptr);
    eos_testing::shutdown();

    std::cout << "All tests complete!\n";

#ifdef EOS_STUB_MODE
    std::cout << "\nNote: This ran in STUB MODE.\n";
    std::cout << "SDK found but credentials may not be configured.\n";
#else
    std::cout << "\nRan with REAL EOS SDK!\n";
#endif

    if (!config::credentials_configured()) {
        std::cout << "\nReminder: Edit config/credentials.hpp with real credentials for production use.\n";
    }

    return 0;
}
