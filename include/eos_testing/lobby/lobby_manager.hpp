#pragma once

/**
 * EOS Testing - Lobby Manager
 * 
 * Handles multiplayer lobby functionality:
 * - Creating/destroying lobbies
 * - Joining/leaving lobbies
 * - Lobby search and matchmaking
 * - Real-time lobby updates
 * - Member management (kick, promote, etc.)
 * 
 * This is essential for Crab Game-style party games where
 * players gather before matches start.
 */

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <optional>

#ifndef EOS_STUB_MODE
    #include <eos_sdk.h>
    #include <eos_lobby.h>
#else
    // Stub types when SDK not available
    using EOS_ProductUserId = void*;
    using EOS_LobbyId = const char*;
#endif

namespace eos_testing {

/**
 * Lobby permission level
 */
enum class LobbyPermission {
    PublicAdvertised,   // Anyone can find and join
    JoinViaPresence,    // Friends can join via presence
    InviteOnly          // Invite required
};

/**
 * Lobby member info
 */
struct LobbyMember {
    EOS_ProductUserId user_id = nullptr;
    std::string display_name;
    bool is_owner = false;
    bool is_ready = false;
    
    // Custom attributes per member (e.g., selected character, team)
    std::unordered_map<std::string, std::string> attributes;
};

/**
 * Lobby info
 */
struct LobbyInfo {
    std::string lobby_id;
    std::string lobby_name;
    EOS_ProductUserId owner_id = nullptr;
    
    uint32_t max_members = 8;
    uint32_t current_members = 0;
    
    LobbyPermission permission = LobbyPermission::PublicAdvertised;
    bool allow_join_in_progress = true;
    
    // Custom lobby attributes (e.g., game mode, map)
    std::unordered_map<std::string, std::string> attributes;
    
    // Members currently in lobby
    std::vector<LobbyMember> members;
};

/**
 * Lobby search result
 */
struct LobbySearchResult {
    std::string lobby_id;
    std::string lobby_name;
    uint32_t current_members = 0;
    uint32_t max_members = 0;
    std::unordered_map<std::string, std::string> attributes;
};

/**
 * Lobby creation options
 */
struct CreateLobbyOptions {
    std::string lobby_name = "My Lobby";
    std::string bucket_id = "default:region";  // Required by EOS: "game_mode:region" format
    uint32_t max_members = 8;
    LobbyPermission permission = LobbyPermission::PublicAdvertised;
    bool allow_join_in_progress = true;
    bool presence_enabled = true;
    
    // Initial lobby attributes
    std::unordered_map<std::string, std::string> attributes;
};

/**
 * Callback types
 */
using CreateLobbyCallback = std::function<void(bool success, const std::string& lobby_id, const std::string& error)>;
using JoinLobbyCallback = std::function<void(bool success, const LobbyInfo& lobby, const std::string& error)>;
using LeaveLobbyCallback = std::function<void(bool success)>;
using SearchLobbyCallback = std::function<void(bool success, const std::vector<LobbySearchResult>& results)>;
using LobbyUpdateCallback = std::function<void(const LobbyInfo& updated_lobby)>;
using MemberJoinCallback = std::function<void(const std::string& lobby_id, const LobbyMember& member)>;
using MemberLeaveCallback = std::function<void(const std::string& lobby_id, EOS_ProductUserId user_id)>;

/**
 * Lobby Manager
 * 
 * Central manager for all lobby operations.
 * Supports creating, joining, and managing multiplayer lobbies.
 */
class LobbyManager {
public:
    static LobbyManager& instance();
    
    // Delete copy/move
    LobbyManager(const LobbyManager&) = delete;
    LobbyManager& operator=(const LobbyManager&) = delete;
    
    /**
     * Create a new lobby.
     * The creator automatically becomes the lobby owner.
     * 
     * @param options Lobby creation options
     * @param callback Called when creation completes
     */
    void create_lobby(const CreateLobbyOptions& options, CreateLobbyCallback callback);
    
    /**
     * Join an existing lobby by ID.
     * 
     * @param lobby_id ID of the lobby to join
     * @param callback Called when join completes
     */
    void join_lobby(const std::string& lobby_id, JoinLobbyCallback callback);
    
    /**
     * Leave the current lobby.
     * If owner leaves, ownership transfers to another member.
     * 
     * @param callback Called when leave completes
     */
    void leave_lobby(LeaveLobbyCallback callback);
    
    /**
     * Search for public lobbies.
     * 
     * @param bucket_id Bucket to search in (must match lobby's bucket_id)
     * @param max_results Maximum number of results to return
     * @param filters Optional attribute filters (key-value pairs)
     * @param callback Called with search results
     */
    void search_lobbies(const std::string& bucket_id,
                        uint32_t max_results,
                        const std::unordered_map<std::string, std::string>& filters,
                        SearchLobbyCallback callback);
    
    /**
     * Update a lobby attribute (owner only).
     * 
     * @param key Attribute key
     * @param value Attribute value
     */
    void set_lobby_attribute(const std::string& key, const std::string& value);
    
    /**
     * Update local member attribute.
     * 
     * @param key Attribute key
     * @param value Attribute value
     */
    void set_member_attribute(const std::string& key, const std::string& value);
    
    /**
     * Set ready status for local member.
     * 
     * @param ready Whether the local player is ready
     */
    void set_ready(bool ready);
    
    /**
     * Kick a member from the lobby (owner only).
     * 
     * @param user_id User to kick
     */
    void kick_member(EOS_ProductUserId user_id);
    
    /**
     * Promote a member to owner (owner only).
     * 
     * @param user_id User to promote
     */
    void promote_member(EOS_ProductUserId user_id);
    
    /**
     * Send a lobby chat message.
     * 
     * @param message Message to send
     */
    void send_chat_message(const std::string& message);
    
    /**
     * Check if we're currently in a lobby.
     */
    bool is_in_lobby() const { return m_current_lobby.has_value(); }
    
    /**
     * Check if we're the lobby owner.
     */
    bool is_owner() const;
    
    /**
     * Get the current lobby info.
     */
    std::optional<LobbyInfo> get_current_lobby() const { return m_current_lobby; }
    
    /**
     * Check if all members are ready.
     */
    bool all_members_ready() const;
    
    // Event callbacks - set these to receive lobby events
    MemberJoinCallback on_member_joined;
    MemberLeaveCallback on_member_left;
    LobbyUpdateCallback on_lobby_updated;
    std::function<void(const std::string& sender, const std::string& message)> on_chat_message;

private:
    LobbyManager() = default;
    
    void register_callbacks();
    void unregister_callbacks();
    void refresh_lobby_info();
    
    std::optional<LobbyInfo> m_current_lobby;
    bool m_callbacks_registered = false;
    std::string m_pending_join_lobby_id;
};

} // namespace eos_testing
