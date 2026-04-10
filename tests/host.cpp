/**
 * EOS Testing - Host Application
 *
 * Creates a lobby and waits for clients to connect.
 * Once a client joins, establishes P2P connection and exchanges messages.
 *
 * Usage: eos_host.exe
 */

#include "eos_testing/eos_testing.hpp"
#include "../config/credentials.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

using namespace eos_testing;

std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

// Simple packet types for our test
enum class PacketType : uint8_t {
    Ping = 1,
    Pong = 2,
    Chat = 3
};

struct TestPacket {
    PacketType type;
    uint32_t sequence;
    char message[256];
};

int main() {
    std::signal(SIGINT, signal_handler);
    std::cout << std::unitbuf;

    std::cout << "==============================================\n";
    std::cout << "         EOS P2P Test - HOST MODE\n";
    std::cout << "==============================================\n\n";

    // Initialize platform
    std::cout << "[HOST] Initializing EOS Platform...\n";

    PlatformConfig config;
    config.product_name = config::PRODUCT_NAME;
    config.product_version = config::PRODUCT_VERSION;
    config.product_id = config::PRODUCT_ID;
    config.sandbox_id = config::SANDBOX_ID;
    config.deployment_id = config::DEPLOYMENT_ID;
    config.client_id = config::CLIENT_ID;
    config.client_secret = config::CLIENT_SECRET;

    bool init_done = false;
    eos_testing::initialize(config, [&](bool success, const std::string& msg) {
        if (success) {
            std::cout << "[HOST] Platform initialized!\n";
        } else {
            std::cout << "[HOST] Platform init failed: " << msg << "\n";
            g_running = false;
        }
        init_done = true;
    });

    while (!init_done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (!Platform::instance().is_ready()) {
        return 1;
    }

    // Login with Device ID
    std::cout << "[HOST] Logging in...\n";

    bool login_done = false;
    EOS_ProductUserId my_user_id = nullptr;
    std::string my_user_id_string;

    // Device ID storage is shared per local OS profile, so always rotate it
    // for deterministic two-process local testing regardless of launch order.
    AuthManager::instance().login_device_id_with_model("Host", "HostPC", true, [&](const AuthResult& result) {
        if (result.success) {
            std::cout << "[HOST] Logged in as '" << result.display_name << "'\n";
            my_user_id = AuthManager::instance().get_product_user_id();
            my_user_id_string = product_user_id_to_string(my_user_id);
            std::cout << "[HOST] User ID: " << (my_user_id_string.empty() ? "<unknown>" : my_user_id_string) << "\n";
        } else {
            std::cout << "[HOST] Login failed: " << result.error_message << "\n";
            g_running = false;
        }
        login_done = true;
    });

    while (!login_done && g_running) {
        Platform::instance().tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (!g_running) return 1;

    // Initialize P2P
    std::cout << "[HOST] Initializing P2P...\n";

    P2PConfig p2p_config;
    p2p_config.socket_name = "P2PTestSocket";
    p2p_config.allow_relay = true;

    if (!P2PManager::instance().initialize(p2p_config)) {
        std::cout << "[HOST] P2P init failed!\n";
        return 1;
    }

    // Track connected client
    EOS_ProductUserId connected_client = nullptr;
    EOS_ProductUserId pending_client = nullptr;
    uint32_t ping_sequence = 0;
    uint32_t pings_sent = 0;
    uint32_t pongs_received = 0;
    bool ping_limit_reached = false;
    std::chrono::steady_clock::time_point ping_limit_reached_at;

    auto is_self = [&](EOS_ProductUserId user_id, const std::string& user_id_string = {}) {
        if (!user_id_string.empty() && !my_user_id_string.empty()) {
            return user_id_string == my_user_id_string;
        }
        return product_user_ids_equal(user_id, my_user_id);
    };

    auto try_connect_to_client = [&]() {
        auto lobby = LobbyManager::instance().get_current_lobby();
        if (!lobby.has_value()) {
            return;
        }

        for (const auto& member : lobby->members) {
            if (!member.user_id || is_self(member.user_id, member.user_id_string)) {
                continue;
            }
            if (product_user_ids_equal(connected_client, member.user_id) ||
                product_user_ids_equal(pending_client, member.user_id)) {
                return;
            }

            pending_client = member.user_id;
            std::cout << "[HOST] Attempting P2P connection to client...\n";
            P2PManager::instance().connect_to_peer(member.user_id);
            return;
        }
    };

    // Set up P2P callbacks
    P2PManager::instance().on_connection_established = [&](EOS_ProductUserId peer, ConnectionStatus status) {
        if (status == ConnectionStatus::Connected && !is_self(peer)) {
            std::cout << "[HOST] Client connected via P2P!\n";
            connected_client = peer;
            pending_client = nullptr;
        }
    };

    P2PManager::instance().on_connection_closed = [&](EOS_ProductUserId peer, ConnectionStatus status) {
        std::cout << "[HOST] Client disconnected.\n";
        if (product_user_ids_equal(peer, connected_client)) {
            connected_client = nullptr;
        }
        if (product_user_ids_equal(peer, pending_client)) {
            pending_client = nullptr;
        }
    };

    P2PManager::instance().on_packet_received = [&](const IncomingPacket& packet) {
        if (packet.data.size() >= sizeof(TestPacket)) {
            TestPacket* pkt = (TestPacket*)packet.data.data();

            switch (pkt->type) {
                case PacketType::Pong:
                    pongs_received++;
                    std::cout << "[HOST] Received PONG #" << pkt->sequence
                              << " (RTT measured by client)\n";
                    break;

                case PacketType::Chat:
                    std::cout << "[HOST] Client says: " << pkt->message << "\n";
                    break;

                default:
                    break;
            }
        }
    };

    // Accept all incoming connections
    P2PManager::instance().accept_connections();
    std::cout << "[HOST] Accepting P2P connections...\n";

    // Register lobby callbacks before creating the lobby so we cannot miss
    // a fast client join that races immediately after creation succeeds.
    LobbyManager::instance().on_member_joined = [&](const std::string& lid, const LobbyMember& member) {
        if (is_self(member.user_id, member.user_id_string)) {
            return;
        }
        std::cout << "[HOST] Player joined lobby.\n";
        try_connect_to_client();
    };

    LobbyManager::instance().on_member_left = [&](const std::string& lid, EOS_ProductUserId user_id) {
        std::cout << "[HOST] Player left lobby.\n";
    };

    LobbyManager::instance().on_lobby_updated = [&](const LobbyInfo& lobby) {
        if (lobby.current_members > 1 && !connected_client) {
            try_connect_to_client();
        }
    };

    // Create a lobby so client can find us
    std::cout << "[HOST] Creating lobby...\n";

    bool lobby_created = false;
    std::string lobby_id;

    CreateLobbyOptions lobby_opts;
    lobby_opts.lobby_name = "P2P Test Lobby";
    lobby_opts.bucket_id = "p2ptest:global";
    lobby_opts.max_members = 2;
    lobby_opts.permission = LobbyPermission::PublicAdvertised;
    lobby_opts.attributes["test"] = "true";

    LobbyManager::instance().create_lobby(lobby_opts, [&](bool success, const std::string& id, const std::string& error) {
        if (success) {
            lobby_id = id;
            std::cout << "[HOST] Lobby created: " << id << "\n";
            std::cout << "[HOST] Waiting for client to join...\n";
        } else {
            std::cout << "[HOST] Failed to create lobby: " << error << "\n";
            g_running = false;
        }
        lobby_created = true;
    });

    while (!lobby_created && g_running) {
        Platform::instance().tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Main loop
    std::cout << "\n[HOST] Running... Press Ctrl+C to stop.\n";
    std::cout << "[HOST] Will send 10 PINGs total, one every 2 seconds once client connects.\n\n";

    auto last_ping = std::chrono::steady_clock::now();

    while (g_running) {
        Platform::instance().tick();
        LobbyManager::instance().sync_current_lobby();
        P2PManager::instance().receive_packets();
        if (!connected_client) {
            try_connect_to_client();
        }

        // Send periodic pings if client is connected
        if (connected_client && !ping_limit_reached) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_ping).count();

            if (elapsed >= 2) {
                TestPacket ping;
                ping.type = PacketType::Ping;
                ping.sequence = ++ping_sequence;
                snprintf(ping.message, sizeof(ping.message), "Ping from host!");

                if (P2PManager::instance().send_packet(connected_client, &ping, sizeof(ping),
                                                        0, PacketReliability::ReliableOrdered)) {
                    pings_sent++;
                    std::cout << "[HOST] Sent PING #" << ping.sequence << "\n";
                    if (pings_sent >= 10) {
                        ping_limit_reached = true;
                        ping_limit_reached_at = now;
                        std::cout << "[HOST] Reached ping limit of 10.\n";
                    }
                }

                last_ping = now;
            }
        }

        if (ping_limit_reached) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_after_limit = std::chrono::duration_cast<std::chrono::seconds>(now - ping_limit_reached_at).count();
            if (elapsed_after_limit >= 2) {
                g_running = false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Cleanup
    std::cout << "\n[HOST] Shutting down...\n";
    std::cout << "[HOST] Stats: " << pings_sent << " pings sent, " << pongs_received << " pongs received\n";

    LobbyManager::instance().leave_lobby(nullptr);
    P2PManager::instance().shutdown();
    eos_testing::shutdown();

    std::cout << "[HOST] Done.\n";
    return 0;
}
