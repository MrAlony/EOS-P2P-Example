/**
 * EOS Testing - P2P Manager Implementation
 */

#include "eos_testing/p2p/p2p_manager.hpp"
#include "eos_testing/core/platform.hpp"
#include "eos_testing/auth/auth_manager.hpp"
#include <iostream>
#include <cstring>

namespace eos_testing {

P2PManager& P2PManager::instance() {
    static P2PManager instance;
    return instance;
}

bool P2PManager::initialize(const P2PConfig& config) {
    if (m_initialized) {
        std::cout << "[P2P] Already initialized\n";
        return true;
    }
    
    if (!AuthManager::instance().is_logged_in()) {
        std::cout << "[P2P] Error: Must be logged in before initializing P2P\n";
        return false;
    }
    
    m_config = config;
    
#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] P2P initialized with socket: " << config.socket_name << "\n";
    std::cout << "[EOS-STUB] Relay enabled: " << (config.allow_relay ? "yes" : "no") << "\n";
    m_initialized = true;
    return true;
#else
    auto platform = Platform::instance().get_handle();
    if (!platform) {
        std::cout << "[P2P] Error: Platform not initialized\n";
        return false;
    }
    
    register_callbacks();
    m_initialized = true;
    std::cout << "[EOS] P2P initialized\n";
    return true;
#endif
}

void P2PManager::shutdown() {
    if (!m_initialized) return;
    
#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] P2P shutdown\n";
#else
    disconnect_all();
    unregister_callbacks();
#endif
    
    m_connections.clear();
    m_initialized = false;
}

void P2PManager::accept_connections(EOS_ProductUserId peer_id) {
    if (!m_initialized) return;
    
#ifdef EOS_STUB_MODE
    if (peer_id) {
        std::cout << "[EOS-STUB] Accepting connections from specific peer\n";
    } else {
        std::cout << "[EOS-STUB] Accepting connections from all peers\n";
    }
#else
    auto platform = Platform::instance().get_handle();
    if (!platform) return;

    if (!peer_id) {
        std::cout << "[EOS] Ready to accept incoming peer connection requests on socket '"
                  << m_config.socket_name << "'\n";
        return;
    }
    
    EOS_P2P_AcceptConnectionOptions options = {};
    options.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    options.LocalUserId = AuthManager::instance().get_product_user_id();
    options.RemoteUserId = peer_id;
    
    EOS_P2P_SocketId socket_id = {};
    socket_id.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    strncpy(socket_id.SocketName, m_config.socket_name.c_str(), 
            sizeof(socket_id.SocketName) - 1);
    options.SocketId = &socket_id;
    
    EOS_P2P_AcceptConnection(EOS_Platform_GetP2PInterface(platform), &options);
#endif
}

void P2PManager::connect_to_peer(EOS_ProductUserId peer_id) {
    if (!m_initialized || !peer_id) return;
    
#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Connecting to peer: " << peer_id << "\n";
    
    // Simulate connection
    PeerConnection conn;
    conn.peer_id = peer_id;
    conn.display_name = "StubPeer";
    conn.status = ConnectionStatus::Connected;
    conn.is_relay = false;
    conn.ping_ms = 25;
    
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        m_connections[peer_id] = conn;
    }
    
    std::cout << "[EOS-STUB] Connected to peer (simulated)\n";
    
    if (on_connection_established) {
        on_connection_established(peer_id, ConnectionStatus::Connected);
    }
#else
    // Send a "hello" packet to initiate connection
    uint8_t hello[] = {0x01}; // Connection request marker
    send_packet(peer_id, hello, sizeof(hello), 0, PacketReliability::ReliableOrdered);
    
    // Mark as connecting
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        PeerConnection conn;
        conn.peer_id = peer_id;
        conn.status = ConnectionStatus::Connecting;
        m_connections[peer_id] = conn;
    }
#endif
}

void P2PManager::disconnect_from_peer(EOS_ProductUserId peer_id) {
    if (!m_initialized || !peer_id) return;
    
#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Disconnecting from peer: " << peer_id << "\n";
    
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        m_connections.erase(peer_id);
    }
    
    if (on_connection_closed) {
        on_connection_closed(peer_id, ConnectionStatus::Disconnected);
    }
#else
    auto platform = Platform::instance().get_handle();
    if (!platform) return;
    
    EOS_P2P_CloseConnectionOptions options = {};
    options.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
    options.LocalUserId = AuthManager::instance().get_product_user_id();
    options.RemoteUserId = peer_id;
    
    EOS_P2P_SocketId socket_id = {};
    socket_id.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    strncpy(socket_id.SocketName, m_config.socket_name.c_str(),
            sizeof(socket_id.SocketName) - 1);
    options.SocketId = &socket_id;
    
    EOS_P2P_CloseConnection(EOS_Platform_GetP2PInterface(platform), &options);
    
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        m_connections.erase(peer_id);
    }
#endif
}

void P2PManager::disconnect_all() {
    std::vector<EOS_ProductUserId> peers;
    
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        for (const auto& pair : m_connections) {
            peers.push_back(pair.first);
        }
    }
    
    for (auto peer_id : peers) {
        disconnect_from_peer(peer_id);
    }
}

bool P2PManager::send_packet(EOS_ProductUserId peer_id,
                              const void* data,
                              uint32_t size,
                              uint8_t channel,
                              PacketReliability reliability) {
    if (!m_initialized || !peer_id || !data || size == 0) return false;
    
    if (size > m_config.max_packet_size) {
        std::cout << "[P2P] Error: Packet too large (" << size << " > " 
                  << m_config.max_packet_size << ")\n";
        return false;
    }
    
#ifdef EOS_STUB_MODE
    // Just pretend we sent it
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        auto it = m_connections.find(peer_id);
        if (it != m_connections.end()) {
            it->second.bytes_sent += size;
        }
    }
    return true;
#else
    auto platform = Platform::instance().get_handle();
    if (!platform) return false;
    
    EOS_P2P_SendPacketOptions options = {};
    options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    options.LocalUserId = AuthManager::instance().get_product_user_id();
    options.RemoteUserId = peer_id;
    options.Channel = channel;
    options.DataLengthBytes = size;
    options.Data = data;
    options.bAllowDelayedDelivery = EOS_TRUE;
    
    EOS_P2P_SocketId socket_id = {};
    socket_id.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    strncpy(socket_id.SocketName, m_config.socket_name.c_str(),
            sizeof(socket_id.SocketName) - 1);
    options.SocketId = &socket_id;
    
    switch (reliability) {
        case PacketReliability::UnreliableUnordered:
            options.Reliability = EOS_EPacketReliability::EOS_PR_UnreliableUnordered;
            break;
        case PacketReliability::ReliableUnordered:
            options.Reliability = EOS_EPacketReliability::EOS_PR_ReliableUnordered;
            break;
        case PacketReliability::ReliableOrdered:
            options.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
            break;
    }
    
    EOS_EResult result = EOS_P2P_SendPacket(EOS_Platform_GetP2PInterface(platform), &options);
    
    if (result == EOS_EResult::EOS_Success) {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        auto it = m_connections.find(peer_id);
        if (it != m_connections.end()) {
            it->second.bytes_sent += size;
        }
        return true;
    }
    
    return false;
#endif
}

void P2PManager::broadcast_packet(const void* data,
                                   uint32_t size,
                                   uint8_t channel,
                                   PacketReliability reliability) {
    std::vector<EOS_ProductUserId> peers;
    
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        for (const auto& pair : m_connections) {
            if (pair.second.status == ConnectionStatus::Connected) {
                peers.push_back(pair.first);
            }
        }
    }
    
    for (auto peer_id : peers) {
        send_packet(peer_id, data, size, channel, reliability);
    }
}

uint32_t P2PManager::receive_packets(uint32_t max_packets) {
    if (!m_initialized) return 0;
    
    uint32_t packets_received = 0;
    
#ifdef EOS_STUB_MODE
    // In stub mode, just process any queued test packets
    std::lock_guard<std::mutex> lock(m_packets_mutex);
    while (!m_incoming_packets.empty() && packets_received < max_packets) {
        IncomingPacket packet = std::move(m_incoming_packets.front());
        m_incoming_packets.pop();
        
        if (on_packet_received) {
            on_packet_received(packet);
        }
        
        packets_received++;
    }
#else
    auto platform = Platform::instance().get_handle();
    if (!platform) return 0;
    
    auto p2p = EOS_Platform_GetP2PInterface(platform);
    
    while (packets_received < max_packets) {
        // Check for next packet size
        EOS_P2P_GetNextReceivedPacketSizeOptions size_options = {};
        size_options.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
        size_options.LocalUserId = AuthManager::instance().get_product_user_id();
        
        uint32_t packet_size = 0;
        if (EOS_P2P_GetNextReceivedPacketSize(p2p, &size_options, &packet_size) 
            != EOS_EResult::EOS_Success) {
            break; // No more packets
        }
        
        // Receive the packet
        IncomingPacket packet;
        packet.data.resize(packet_size);
        
        EOS_P2P_ReceivePacketOptions recv_options = {};
        recv_options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
        recv_options.LocalUserId = AuthManager::instance().get_product_user_id();
        recv_options.MaxDataSizeBytes = packet_size;
        
        uint32_t bytes_received = 0;
        EOS_P2P_SocketId socket_id;
        
        EOS_EResult result = EOS_P2P_ReceivePacket(p2p, &recv_options,
            &packet.sender, &socket_id, &packet.channel,
            packet.data.data(), &bytes_received);
        
        if (result == EOS_EResult::EOS_Success) {
            packet.data.resize(bytes_received);
            
            // Check if this is a new peer we haven't seen before
            bool is_new_peer = false;
            {
                std::lock_guard<std::mutex> lock(m_connections_mutex);
                auto it = m_connections.find(packet.sender);
                if (it != m_connections.end()) {
                    it->second.bytes_received += bytes_received;
                } else {
                    // New peer! Add them to connections
                    std::cout << "[P2P] New peer detected from received packet - adding to connections\n";
                    PeerConnection conn;
                    conn.peer_id = packet.sender;
                    conn.status = ConnectionStatus::Connected;
                    conn.bytes_received = bytes_received;
                    m_connections[packet.sender] = conn;
                    is_new_peer = true;
                }
            }
            
            // Trigger connection callback for new peers
            if (is_new_peer && on_connection_established) {
                std::cout << "[P2P] Triggering on_connection_established for new peer\n";
                on_connection_established(packet.sender, ConnectionStatus::Connected);
            }
            
            if (on_packet_received) {
                on_packet_received(packet);
            }
            
            packets_received++;
        } else {
            break;
        }
    }
#endif
    
    return packets_received;
}

std::optional<PeerConnection> P2PManager::get_peer_connection(EOS_ProductUserId peer_id) const {
    std::lock_guard<std::mutex> lock(m_connections_mutex);
    auto it = m_connections.find(peer_id);
    if (it != m_connections.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<PeerConnection> P2PManager::get_all_connections() const {
    std::vector<PeerConnection> result;
    std::lock_guard<std::mutex> lock(m_connections_mutex);
    for (const auto& pair : m_connections) {
        result.push_back(pair.second);
    }
    return result;
}

bool P2PManager::is_connected_to(EOS_ProductUserId peer_id) const {
    std::lock_guard<std::mutex> lock(m_connections_mutex);
    auto it = m_connections.find(peer_id);
    return it != m_connections.end() && it->second.status == ConnectionStatus::Connected;
}

uint32_t P2PManager::get_peer_count() const {
    std::lock_guard<std::mutex> lock(m_connections_mutex);
    uint32_t count = 0;
    for (const auto& pair : m_connections) {
        if (pair.second.status == ConnectionStatus::Connected) {
            count++;
        }
    }
    return count;
}

void P2PManager::register_callbacks() {
#ifndef EOS_STUB_MODE
    auto platform = Platform::instance().get_handle();
    if (!platform) return;
    
    // Register for connection requests
    EOS_P2P_AddNotifyPeerConnectionRequestOptions request_options = {};
    request_options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
    request_options.LocalUserId = AuthManager::instance().get_product_user_id();
    
    EOS_P2P_SocketId socket_id = {};
    socket_id.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    strncpy(socket_id.SocketName, m_config.socket_name.c_str(),
            sizeof(socket_id.SocketName) - 1);
    request_options.SocketId = &socket_id;
    
    EOS_P2P_AddNotifyPeerConnectionRequest(EOS_Platform_GetP2PInterface(platform),
        &request_options, this,
        [](const EOS_P2P_OnIncomingConnectionRequestInfo* data) {
            auto* self = static_cast<P2PManager*>(data->ClientData);
            self->handle_connection_request(data->RemoteUserId);
        }
    );
    
    // Register for connection state changes
    EOS_P2P_AddNotifyPeerConnectionEstablishedOptions established_options = {};
    established_options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
    established_options.LocalUserId = AuthManager::instance().get_product_user_id();
    established_options.SocketId = &socket_id;
    
    EOS_P2P_AddNotifyPeerConnectionEstablished(EOS_Platform_GetP2PInterface(platform),
        &established_options, this,
        [](const EOS_P2P_OnPeerConnectionEstablishedInfo* data) {
            auto* self = static_cast<P2PManager*>(data->ClientData);
            self->handle_connection_established(data->RemoteUserId);
        }
    );
    
    // Register for connection closed
    EOS_P2P_AddNotifyPeerConnectionClosedOptions closed_options = {};
    closed_options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
    closed_options.LocalUserId = AuthManager::instance().get_product_user_id();
    closed_options.SocketId = &socket_id;
    
    EOS_P2P_AddNotifyPeerConnectionClosed(EOS_Platform_GetP2PInterface(platform),
        &closed_options, this,
        [](const EOS_P2P_OnRemoteConnectionClosedInfo* data) {
            auto* self = static_cast<P2PManager*>(data->ClientData);
            self->handle_connection_closed(data->RemoteUserId);
        }
    );
#endif
}

void P2PManager::unregister_callbacks() {
#ifndef EOS_STUB_MODE
    // Remove notification handlers
#endif
}

void P2PManager::handle_connection_request(EOS_ProductUserId peer_id) {
    std::cout << "[P2P] >>> Connection REQUEST callback triggered! <<<\n";
    // Auto-accept for now
    accept_connections(peer_id);
}

void P2PManager::handle_connection_established(EOS_ProductUserId peer_id) {
    std::cout << "[P2P] >>> Connection ESTABLISHED callback triggered! <<<\n";
    
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        auto it = m_connections.find(peer_id);
        if (it != m_connections.end()) {
            it->second.status = ConnectionStatus::Connected;
        } else {
            PeerConnection conn;
            conn.peer_id = peer_id;
            conn.status = ConnectionStatus::Connected;
            m_connections[peer_id] = conn;
        }
    }
    
    if (on_connection_established) {
        std::cout << "[P2P] Calling on_connection_established callback\n";
        on_connection_established(peer_id, ConnectionStatus::Connected);
    } else {
        std::cout << "[P2P] WARNING: on_connection_established callback is NULL!\n";
    }
}

void P2PManager::handle_connection_closed(EOS_ProductUserId peer_id) {
    std::cout << "[P2P] Connection closed with peer\n";
    
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        m_connections.erase(peer_id);
    }
    
    if (on_connection_closed) {
        on_connection_closed(peer_id, ConnectionStatus::Disconnected);
    }
}

} // namespace eos_testing
