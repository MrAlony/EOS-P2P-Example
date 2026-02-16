#pragma once

/**
 * EOS Testing - Authentication Manager
 * 
 * Handles all authentication flows:
 * - Epic Games Account login (for cosmetics, friends, etc.)
 * - Device ID login (for anonymous play)
 * - Connect interface (for game services - P2P, lobbies, etc.)
 */

#include <string>
#include <functional>
#include <vector>

#ifndef EOS_STUB_MODE
    #include <eos_sdk.h>
    #include <eos_auth.h>
    #include <eos_connect.h>
#else
    // Stub types when SDK not available
    using EOS_ProductUserId = void*;
    using EOS_EpicAccountId = void*;
#endif

namespace eos_testing {

/**
 * Authentication result
 */
struct AuthResult {
    bool success = false;
    std::string error_message;
    std::string display_name;
    
    // Valid if success = true
    EOS_ProductUserId product_user_id = nullptr;
    EOS_EpicAccountId epic_account_id = nullptr;
};

/**
 * Authentication method
 */
enum class AuthMethod {
    DeviceId,           // Anonymous auth using device ID (easiest for testing)
    EpicAccount,        // Full Epic Games account login
    Developer,          // Developer portal credentials (dev only)
    ExchangeCode,       // Exchange code from launcher
    PersistentAuth,     // Previously saved auth token
};

/**
 * Callback types
 */
using AuthCallback = std::function<void(const AuthResult& result)>;
using LogoutCallback = std::function<void(bool success)>;

/**
 * Authentication Manager
 * 
 * Manages user authentication with Epic Online Services.
 * For P2P and lobbies, you need to be logged in via the Connect interface.
 */
class AuthManager {
public:
    static AuthManager& instance();
    
    // Delete copy/move
    AuthManager(const AuthManager&) = delete;
    AuthManager& operator=(const AuthManager&) = delete;
    
    /**
     * Login with Device ID (anonymous auth).
     * Best for testing - no Epic account required.
     * Creates a unique identity per device.
     * 
     * @param display_name Display name for this user
     * @param callback Called when login completes
     */
    void login_device_id(const std::string& display_name, AuthCallback callback);
    
    /**
     * Login with Device ID using a specific device model.
     * 
     * @param display_name Display name for this user
     * @param device_model Device model identifier (e.g., "HostPC", "ClientPC")
     * @param callback Called when login completes
     */
    void login_device_id_with_model(const std::string& display_name, 
                                     const std::string& device_model,
                                     AuthCallback callback);
    
    /**
     * Login with Device ID, optionally deleting existing device ID first.
     * Use delete_existing=true to create a new identity on the same machine.
     * 
     * @param display_name Display name for this user
     * @param device_model Device model identifier
     * @param delete_existing If true, deletes existing device ID first
     * @param callback Called when login completes
     */
    void login_device_id_with_model(const std::string& display_name, 
                                     const std::string& device_model,
                                     bool delete_existing,
                                     AuthCallback callback);
    
    /**
     * Login with Developer credentials.
     * Uses Developer Authentication Tool from Epic.
     * 
     * @param host Host:Port of Developer Auth Tool (e.g., "localhost:6547")
     * @param credential_name Credential name from Dev Auth Tool
     * @param callback Called when login completes
     */
    void login_developer(const std::string& host, 
                         const std::string& credential_name,
                         AuthCallback callback);
    
    /**
     * Login with Epic Games Account (opens browser).
     * Required for social features like friends list.
     * 
     * @param callback Called when login completes
     */
    void login_epic_account(AuthCallback callback);
    
    /**
     * Logout the current user.
     * 
     * @param callback Called when logout completes
     */
    void logout(LogoutCallback callback);
    
    /**
     * Check if a user is currently logged in.
     */
    bool is_logged_in() const { return m_logged_in; }
    
    /**
     * Get the current user's display name.
     */
    const std::string& get_display_name() const { return m_display_name; }
    
    /**
     * Get the current Product User ID (for game services).
     */
    EOS_ProductUserId get_product_user_id() const { return m_product_user_id; }
    
    /**
     * Get the current Epic Account ID (for social features).
     * May be nullptr if logged in anonymously.
     */
    EOS_EpicAccountId get_epic_account_id() const { return m_epic_account_id; }

private:
    AuthManager() = default;
    
    // Internal login flow helpers
    void connect_login(AuthCallback callback);
    void create_device_id(const std::string& display_name, AuthCallback callback);
    void delete_device_id_then_create(const std::string& display_name, AuthCallback callback);
    
    bool m_logged_in = false;
    std::string m_display_name;
    std::string m_device_model;
    EOS_ProductUserId m_product_user_id = nullptr;
    EOS_EpicAccountId m_epic_account_id = nullptr;
    
    // Pending callbacks (stored during async operations)
    AuthCallback m_pending_callback;
};

} // namespace eos_testing
