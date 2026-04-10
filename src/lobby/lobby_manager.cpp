/**
 * EOS Testing - Lobby Manager Implementation
 */

#include "eos_testing/lobby/lobby_manager.hpp"
#include "eos_testing/core/platform.hpp"
#include "eos_testing/auth/auth_manager.hpp"
#include "eos_testing/utils/eos_id.hpp"
#include <iostream>
#include <algorithm>
#include <unordered_map>

namespace eos_testing {

#ifndef EOS_STUB_MODE
namespace {

void populate_lobby_from_details(EOS_HLobbyDetails lobby_details, LobbyInfo& lobby) {
    if (!lobby_details) {
        return;
    }

    EOS_LobbyDetails_Info* info = nullptr;
    EOS_LobbyDetails_CopyInfoOptions info_opts = {};
    info_opts.ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST;

    if (EOS_LobbyDetails_CopyInfo(lobby_details, &info_opts, &info) == EOS_EResult::EOS_Success) {
        if (info->LobbyId) {
            lobby.lobby_id = info->LobbyId;
        }
        lobby.owner_id = info->LobbyOwnerUserId;
        lobby.owner_id_string = product_user_id_to_string(info->LobbyOwnerUserId);
        lobby.max_members = info->MaxMembers;
        EOS_LobbyDetails_Info_Release(info);
    }

    EOS_LobbyDetails_GetMemberCountOptions member_count_opts = {};
    member_count_opts.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
    lobby.current_members = EOS_LobbyDetails_GetMemberCount(lobby_details, &member_count_opts);

    lobby.members.clear();
    for (uint32_t i = 0; i < lobby.current_members; i++) {
        EOS_LobbyDetails_GetMemberByIndexOptions member_opts = {};
        member_opts.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST;
        member_opts.MemberIndex = i;

        EOS_ProductUserId member_id = EOS_LobbyDetails_GetMemberByIndex(lobby_details, &member_opts);
        if (!member_id) {
            continue;
        }

        LobbyMember member;
        member.user_id = member_id;
        member.user_id_string = product_user_id_to_string(member_id);
        member.is_owner = !member.user_id_string.empty() && member.user_id_string == lobby.owner_id_string;
        lobby.members.push_back(member);
    }
}

} // namespace
#endif

LobbyManager& LobbyManager::instance() {
    static LobbyManager instance;
    return instance;
}

void LobbyManager::create_lobby(const CreateLobbyOptions& options, CreateLobbyCallback callback) {
    if (!AuthManager::instance().is_logged_in()) {
        if (callback) callback(false, "", "Not logged in");
        return;
    }

    if (m_current_lobby.has_value()) {
        if (callback) callback(false, "", "Already in a lobby");
        return;
    }

#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Creating lobby: " << options.lobby_name << "\n";

    // Create stub lobby
    LobbyInfo lobby;
    lobby.lobby_id = "stub-lobby-" + std::to_string(rand());
    lobby.lobby_name = options.lobby_name;
    lobby.owner_id = AuthManager::instance().get_product_user_id();
    lobby.owner_id_string = product_user_id_to_string(lobby.owner_id);
    lobby.max_members = options.max_members;
    lobby.current_members = 1;
    lobby.permission = options.permission;
    lobby.allow_join_in_progress = options.allow_join_in_progress;
    lobby.attributes = options.attributes;

    // Add self as member
    LobbyMember self_member;
    self_member.user_id = AuthManager::instance().get_product_user_id();
    self_member.user_id_string = product_user_id_to_string(self_member.user_id);
    self_member.display_name = AuthManager::instance().get_display_name();
    self_member.is_owner = true;
    lobby.members.push_back(self_member);

    m_current_lobby = lobby;

    std::cout << "[EOS-STUB] Lobby created: " << lobby.lobby_id << "\n";

    if (callback) callback(true, lobby.lobby_id, "");
#else
    auto platform = Platform::instance().get_handle();
    if (!platform) {
        if (callback) callback(false, "", "Platform not initialized");
        return;
    }

    EOS_Lobby_CreateLobbyOptions create_options = {};
    create_options.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
    create_options.LocalUserId = AuthManager::instance().get_product_user_id();
    create_options.MaxLobbyMembers = options.max_members;
    create_options.bPresenceEnabled = options.presence_enabled ? EOS_TRUE : EOS_FALSE;
    create_options.bAllowInvites = EOS_TRUE;
    create_options.BucketId = options.bucket_id.c_str();

    switch (options.permission) {
        case LobbyPermission::PublicAdvertised:
            create_options.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
            break;
        case LobbyPermission::JoinViaPresence:
            create_options.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_JOINVIAPRESENCE;
            break;
        case LobbyPermission::InviteOnly:
            create_options.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_INVITEONLY;
            break;
    }

    // Store callback for async completion
    struct CallbackData {
        LobbyManager* manager;
        CreateLobbyCallback callback;
        CreateLobbyOptions options;
    };
    auto* cb_data = new CallbackData{this, callback, options};

    EOS_Lobby_CreateLobby(EOS_Platform_GetLobbyInterface(platform), &create_options, cb_data,
        [](const EOS_Lobby_CreateLobbyCallbackInfo* data) {
            auto* cb_data = static_cast<CallbackData*>(data->ClientData);

            if (data->ResultCode == EOS_EResult::EOS_Success) {
                std::string lobby_id = data->LobbyId;

                // Set up lobby info
                LobbyInfo lobby;
                lobby.lobby_id = lobby_id;
                lobby.lobby_name = cb_data->options.lobby_name;
                lobby.owner_id = AuthManager::instance().get_product_user_id();
                lobby.owner_id_string = product_user_id_to_string(lobby.owner_id);
                lobby.max_members = cb_data->options.max_members;
                lobby.current_members = 1;
                lobby.permission = cb_data->options.permission;
                lobby.attributes = cb_data->options.attributes;

                LobbyMember self_member;
                self_member.user_id = AuthManager::instance().get_product_user_id();
                self_member.user_id_string = product_user_id_to_string(self_member.user_id);
                self_member.display_name = AuthManager::instance().get_display_name();
                self_member.is_owner = true;
                lobby.members.push_back(self_member);

                cb_data->manager->m_current_lobby = lobby;
                cb_data->manager->register_callbacks();

                if (cb_data->callback) cb_data->callback(true, lobby_id, "");
            } else {
                if (cb_data->callback) cb_data->callback(false, "", "Failed to create lobby");
            }

            delete cb_data;
        }
    );
#endif
}

void LobbyManager::join_lobby(const std::string& lobby_id, JoinLobbyCallback callback) {
    if (!AuthManager::instance().is_logged_in()) {
        if (callback) callback(false, {}, "Not logged in");
        return;
    }

    if (m_current_lobby.has_value()) {
        if (callback) callback(false, {}, "Already in a lobby");
        return;
    }

#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Joining lobby: " << lobby_id << "\n";

    // Create stub joined lobby
    LobbyInfo lobby;
    lobby.lobby_id = lobby_id;
    lobby.lobby_name = "Joined Lobby";
    lobby.max_members = 8;
    lobby.current_members = 2;

    LobbyMember self_member;
    self_member.user_id = AuthManager::instance().get_product_user_id();
    self_member.user_id_string = product_user_id_to_string(self_member.user_id);
    self_member.display_name = AuthManager::instance().get_display_name();
    self_member.is_owner = false;
    lobby.members.push_back(self_member);

    m_current_lobby = lobby;

    std::cout << "[EOS-STUB] Joined lobby successfully\n";

    if (callback) callback(true, lobby, "");
#else
    auto platform = Platform::instance().get_handle();
    if (!platform) {
        if (callback) callback(false, {}, "Platform not initialized");
        return;
    }

    // First need to get lobby details handle from search, then join
    EOS_Lobby_JoinLobbyOptions join_options = {};
    join_options.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
    join_options.LocalUserId = AuthManager::instance().get_product_user_id();
    join_options.bPresenceEnabled = EOS_FALSE;

    // We need a LobbyDetailsHandle - store lobby_id in pending state
    m_pending_join_lobby_id = lobby_id;

    struct CallbackData {
        LobbyManager* manager;
        JoinLobbyCallback callback;
        std::string lobby_id;
    };
    auto* cb_data = new CallbackData{this, callback, lobby_id};

    // Create a lobby search to get the details handle
    EOS_HLobbySearch search_handle = nullptr;
    EOS_Lobby_CreateLobbySearchOptions search_create_options = {};
    search_create_options.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
    search_create_options.MaxResults = 1;

    EOS_EResult result = EOS_Lobby_CreateLobbySearch(
        EOS_Platform_GetLobbyInterface(platform),
        &search_create_options,
        &search_handle
    );

    if (result != EOS_EResult::EOS_Success) {
        if (callback) callback(false, {}, "Failed to create lobby search");
        delete cb_data;
        return;
    }

    // Set lobby ID filter
    EOS_LobbySearch_SetLobbyIdOptions set_id_options = {};
    set_id_options.ApiVersion = EOS_LOBBYSEARCH_SETLOBBYID_API_LATEST;
    set_id_options.LobbyId = lobby_id.c_str();
    EOS_LobbySearch_SetLobbyId(search_handle, &set_id_options);

    // Store search handle in callback data
    struct JoinSearchData {
        LobbyManager* manager;
        JoinLobbyCallback callback;
        std::string lobby_id;
        EOS_HLobbySearch search_handle;
    };
    auto* join_data = new JoinSearchData{this, callback, lobby_id, search_handle};

    EOS_LobbySearch_FindOptions find_options = {};
    find_options.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
    find_options.LocalUserId = AuthManager::instance().get_product_user_id();

    EOS_LobbySearch_Find(search_handle, &find_options, join_data,
        [](const EOS_LobbySearch_FindCallbackInfo* data) {
            auto* join_data = static_cast<JoinSearchData*>(data->ClientData);

            if (data->ResultCode != EOS_EResult::EOS_Success) {
                if (join_data->callback) join_data->callback(false, {}, "Lobby search failed");
                EOS_LobbySearch_Release(join_data->search_handle);
                delete join_data;
                return;
            }

            // Get the lobby details
            EOS_LobbySearch_GetSearchResultCountOptions count_options = {};
            count_options.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
            uint32_t count = EOS_LobbySearch_GetSearchResultCount(join_data->search_handle, &count_options);

            if (count == 0) {
                if (join_data->callback) join_data->callback(false, {}, "Lobby not found");
                EOS_LobbySearch_Release(join_data->search_handle);
                delete join_data;
                return;
            }

            // Get the details handle
            EOS_HLobbyDetails details_handle = nullptr;
            EOS_LobbySearch_CopySearchResultByIndexOptions copy_options = {};
            copy_options.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
            copy_options.LobbyIndex = 0;

            EOS_EResult copy_result = EOS_LobbySearch_CopySearchResultByIndex(
                join_data->search_handle, &copy_options, &details_handle);

            EOS_LobbySearch_Release(join_data->search_handle);

            if (copy_result != EOS_EResult::EOS_Success || !details_handle) {
                if (join_data->callback) join_data->callback(false, {}, "Failed to get lobby details");
                delete join_data;
                return;
            }

            // Now join with the details handle
            auto platform = Platform::instance().get_handle();

            EOS_Lobby_JoinLobbyOptions join_options = {};
            join_options.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
            join_options.LobbyDetailsHandle = details_handle;
            join_options.LocalUserId = AuthManager::instance().get_product_user_id();
            join_options.bPresenceEnabled = EOS_FALSE;

            struct JoinFinalData {
                LobbyManager* manager;
                JoinLobbyCallback callback;
                std::string lobby_id;
                EOS_HLobbyDetails details_handle;
            };
            auto* final_data = new JoinFinalData{join_data->manager, join_data->callback,
                                                   join_data->lobby_id, details_handle};
            delete join_data;

            EOS_Lobby_JoinLobby(EOS_Platform_GetLobbyInterface(platform), &join_options, final_data,
                [](const EOS_Lobby_JoinLobbyCallbackInfo* data) {
                    auto* final_data = static_cast<JoinFinalData*>(data->ClientData);

                    if (data->ResultCode == EOS_EResult::EOS_Success) {
                        std::cout << "[EOS] Joined lobby: " << data->LobbyId << "\n";

                        // Build lobby info
                        LobbyInfo lobby;
                        lobby.lobby_id = data->LobbyId;
                        populate_lobby_from_details(final_data->details_handle, lobby);

                        final_data->manager->m_current_lobby = lobby;
                        final_data->manager->register_callbacks();
                        final_data->manager->refresh_lobby_info();

                        if (final_data->callback) {
                            if (final_data->manager->m_current_lobby.has_value()) {
                                final_data->callback(true, *final_data->manager->m_current_lobby, "");
                            } else {
                                final_data->callback(true, lobby, "");
                            }
                        }
                    } else {
                        std::cout << "[EOS] Failed to join lobby: " << (int)data->ResultCode << "\n";
                        if (final_data->callback) final_data->callback(false, {}, "Failed to join lobby");
                    }

                    EOS_LobbyDetails_Release(final_data->details_handle);
                    delete final_data;
                }
            );
        }
    );

    delete cb_data;
#endif
}

void LobbyManager::leave_lobby(LeaveLobbyCallback callback) {
    if (!m_current_lobby.has_value()) {
        if (callback) callback(true);
        return;
    }

#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Leaving lobby: " << m_current_lobby->lobby_id << "\n";
    m_current_lobby.reset();
    if (callback) callback(true);
#else
    auto platform = Platform::instance().get_handle();
    if (!platform) {
        if (callback) callback(false);
        return;
    }

    EOS_Lobby_LeaveLobbyOptions leave_options = {};
    leave_options.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
    leave_options.LocalUserId = AuthManager::instance().get_product_user_id();
    leave_options.LobbyId = m_current_lobby->lobby_id.c_str();

    struct CallbackData {
        LobbyManager* manager;
        LeaveLobbyCallback callback;
    };
    auto* cb_data = new CallbackData{this, callback};

    EOS_Lobby_LeaveLobby(EOS_Platform_GetLobbyInterface(platform), &leave_options, cb_data,
        [](const EOS_Lobby_LeaveLobbyCallbackInfo* data) {
            auto* cb_data = static_cast<CallbackData*>(data->ClientData);

            cb_data->manager->unregister_callbacks();
            cb_data->manager->m_current_lobby.reset();

            if (cb_data->callback) {
                cb_data->callback(data->ResultCode == EOS_EResult::EOS_Success);
            }

            delete cb_data;
        }
    );
#endif
}

void LobbyManager::search_lobbies(const std::string& bucket_id,
                                   uint32_t max_results,
                                   const std::unordered_map<std::string, std::string>& filters,
                                   SearchLobbyCallback callback) {
#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Searching for lobbies (max " << max_results << ")\n";

    // Return some fake results
    std::vector<LobbySearchResult> results;

    LobbySearchResult result1;
    result1.lobby_id = "stub-lobby-001";
    result1.lobby_name = "Fun Game Room";
    result1.current_members = 3;
    result1.max_members = 8;
    results.push_back(result1);

    LobbySearchResult result2;
    result2.lobby_id = "stub-lobby-002";
    result2.lobby_name = "Competitive Match";
    result2.current_members = 6;
    result2.max_members = 8;
    results.push_back(result2);

    std::cout << "[EOS-STUB] Found " << results.size() << " lobbies\n";

    if (callback) callback(true, results);
#else
    auto platform = Platform::instance().get_handle();
    if (!platform) {
        if (callback) callback(false, {});
        return;
    }

    // Create lobby search
    EOS_HLobbySearch search_handle = nullptr;
    EOS_Lobby_CreateLobbySearchOptions search_options = {};
    search_options.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
    search_options.MaxResults = max_results;

    EOS_EResult result = EOS_Lobby_CreateLobbySearch(
        EOS_Platform_GetLobbyInterface(platform),
        &search_options,
        &search_handle
    );

    if (result != EOS_EResult::EOS_Success) {
        std::cout << "[EOS] Failed to create lobby search: " << (int)result << "\n";
        if (callback) callback(false, {});
        return;
    }

    // Set bucket_id parameter - this is required to find lobbies
    if (!bucket_id.empty()) {
        EOS_LobbySearch_SetParameterOptions bucket_param = {};
        bucket_param.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;

        EOS_Lobby_AttributeData bucket_attr = {};
        bucket_attr.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
        bucket_attr.Key = "bucket";
        bucket_attr.ValueType = EOS_EAttributeType::EOS_AT_STRING;
        bucket_attr.Value.AsUtf8 = bucket_id.c_str();

        bucket_param.Parameter = &bucket_attr;
        bucket_param.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;

        EOS_LobbySearch_SetParameter(search_handle, &bucket_param);
    }

    // Add filters if any
    for (const auto& filter : filters) {
        EOS_LobbySearch_SetParameterOptions param_options = {};
        param_options.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;

        EOS_Lobby_AttributeData attr_data = {};
        attr_data.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
        attr_data.Key = filter.first.c_str();
        attr_data.ValueType = EOS_EAttributeType::EOS_AT_STRING;
        attr_data.Value.AsUtf8 = filter.second.c_str();

        param_options.Parameter = &attr_data;
        param_options.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;

        EOS_LobbySearch_SetParameter(search_handle, &param_options);
    }

    struct SearchCallbackData {
        SearchLobbyCallback callback;
        EOS_HLobbySearch search_handle;
    };
    auto* cb_data = new SearchCallbackData{callback, search_handle};

    EOS_LobbySearch_FindOptions find_options = {};
    find_options.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
    find_options.LocalUserId = AuthManager::instance().get_product_user_id();

    EOS_LobbySearch_Find(search_handle, &find_options, cb_data,
        [](const EOS_LobbySearch_FindCallbackInfo* data) {
            auto* cb_data = static_cast<SearchCallbackData*>(data->ClientData);

            std::vector<LobbySearchResult> results;

            if (data->ResultCode == EOS_EResult::EOS_Success) {
                EOS_LobbySearch_GetSearchResultCountOptions count_options = {};
                count_options.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
                uint32_t count = EOS_LobbySearch_GetSearchResultCount(cb_data->search_handle, &count_options);

                std::cout << "[EOS] Found " << count << " lobbies\n";

                for (uint32_t i = 0; i < count; i++) {
                    EOS_HLobbyDetails details = nullptr;
                    EOS_LobbySearch_CopySearchResultByIndexOptions copy_options = {};
                    copy_options.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
                    copy_options.LobbyIndex = i;

                    if (EOS_LobbySearch_CopySearchResultByIndex(cb_data->search_handle, &copy_options, &details) == EOS_EResult::EOS_Success) {
                        EOS_LobbyDetails_Info* info = nullptr;
                        EOS_LobbyDetails_CopyInfoOptions info_options = {};
                        info_options.ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST;

                        if (EOS_LobbyDetails_CopyInfo(details, &info_options, &info) == EOS_EResult::EOS_Success) {
                            LobbySearchResult result;
                            result.lobby_id = info->LobbyId;
                            result.owner_id = info->LobbyOwnerUserId;
                            result.owner_id_string = product_user_id_to_string(info->LobbyOwnerUserId);
                            result.max_members = info->MaxMembers;

                            // Get member count
                            EOS_LobbyDetails_GetMemberCountOptions member_opts = {};
                            member_opts.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
                            result.current_members = EOS_LobbyDetails_GetMemberCount(details, &member_opts);

                            // Try to get lobby name from attributes
                            EOS_LobbyDetails_GetAttributeCountOptions attr_count_opts = {};
                            attr_count_opts.ApiVersion = EOS_LOBBYDETAILS_GETATTRIBUTECOUNT_API_LATEST;
                            uint32_t attr_count = EOS_LobbyDetails_GetAttributeCount(details, &attr_count_opts);

                            for (uint32_t j = 0; j < attr_count; j++) {
                                EOS_Lobby_Attribute* attr = nullptr;
                                EOS_LobbyDetails_CopyAttributeByIndexOptions attr_opts = {};
                                attr_opts.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYINDEX_API_LATEST;
                                attr_opts.AttrIndex = j;

                                if (EOS_LobbyDetails_CopyAttributeByIndex(details, &attr_opts, &attr) == EOS_EResult::EOS_Success) {
                                    if (attr->Data && attr->Data->Key) {
                                        std::string key = attr->Data->Key;
                                        if (attr->Data->ValueType == EOS_EAttributeType::EOS_AT_STRING && attr->Data->Value.AsUtf8) {
                                            result.attributes[key] = attr->Data->Value.AsUtf8;
                                        }
                                    }
                                    EOS_Lobby_Attribute_Release(attr);
                                }
                            }

                            result.lobby_name = result.attributes.count("name") ? result.attributes["name"] : "Lobby";

                            results.push_back(result);
                            EOS_LobbyDetails_Info_Release(info);
                        }
                        EOS_LobbyDetails_Release(details);
                    }
                }
            } else {
                std::cout << "[EOS] Lobby search failed: " << (int)data->ResultCode << "\n";
            }

            EOS_LobbySearch_Release(cb_data->search_handle);

            if (cb_data->callback) cb_data->callback(data->ResultCode == EOS_EResult::EOS_Success, results);
            delete cb_data;
        }
    );
#endif
}

void LobbyManager::set_lobby_attribute(const std::string& key, const std::string& value) {
    if (!m_current_lobby.has_value() || !is_owner()) return;

#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Set lobby attribute: " << key << " = " << value << "\n";
    m_current_lobby->attributes[key] = value;
    if (on_lobby_updated) on_lobby_updated(*m_current_lobby);
#else
    // Real EOS implementation
#endif
}

void LobbyManager::set_member_attribute(const std::string& key, const std::string& value) {
    if (!m_current_lobby.has_value()) return;

#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Set member attribute: " << key << " = " << value << "\n";
    auto user_id = AuthManager::instance().get_product_user_id();
    for (auto& member : m_current_lobby->members) {
        if (member.user_id == user_id) {
            member.attributes[key] = value;
            break;
        }
    }
    if (on_lobby_updated) on_lobby_updated(*m_current_lobby);
#else
    // Real EOS implementation
#endif
}

void LobbyManager::set_ready(bool ready) {
    set_member_attribute("ready", ready ? "true" : "false");

    if (m_current_lobby.has_value()) {
        auto user_id = AuthManager::instance().get_product_user_id();
        for (auto& member : m_current_lobby->members) {
            if (member.user_id == user_id) {
                member.is_ready = ready;
                break;
            }
        }
    }
}

void LobbyManager::kick_member(EOS_ProductUserId user_id) {
    if (!m_current_lobby.has_value() || !is_owner()) return;

#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Kicked member: " << user_id << "\n";
    auto& members = m_current_lobby->members;
    members.erase(std::remove_if(members.begin(), members.end(),
        [user_id](const LobbyMember& m) { return m.user_id == user_id; }), members.end());
    m_current_lobby->current_members = static_cast<uint32_t>(members.size());
    if (on_member_left) on_member_left(m_current_lobby->lobby_id, user_id);
#else
    // Real EOS implementation
#endif
}

void LobbyManager::promote_member(EOS_ProductUserId user_id) {
    if (!m_current_lobby.has_value() || !is_owner()) return;

#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Promoted member: " << user_id << "\n";
    for (auto& member : m_current_lobby->members) {
        member.is_owner = (member.user_id == user_id);
    }
    m_current_lobby->owner_id = user_id;
    if (on_lobby_updated) on_lobby_updated(*m_current_lobby);
#else
    // Real EOS implementation
#endif
}

void LobbyManager::send_chat_message(const std::string& message) {
    if (!m_current_lobby.has_value()) return;

#ifdef EOS_STUB_MODE
    std::cout << "[EOS-STUB] Chat: " << AuthManager::instance().get_display_name()
              << ": " << message << "\n";
    if (on_chat_message) {
        on_chat_message(AuthManager::instance().get_display_name(), message);
    }
#else
    // Real EOS doesn't have built-in chat - would use P2P or custom solution
#endif
}

bool LobbyManager::is_owner() const {
    if (!m_current_lobby.has_value()) return false;
    return product_user_ids_equal(m_current_lobby->owner_id, AuthManager::instance().get_product_user_id());
}

bool LobbyManager::all_members_ready() const {
    if (!m_current_lobby.has_value()) return false;

    for (const auto& member : m_current_lobby->members) {
        if (!member.is_ready && !member.is_owner) {
            return false;
        }
    }
    return true;
}

void LobbyManager::sync_current_lobby() {
    refresh_lobby_info();
}

void LobbyManager::register_callbacks() {
    if (m_callbacks_registered) return;

#ifndef EOS_STUB_MODE
    auto platform = Platform::instance().get_handle();
    if (!platform) return;

    auto lobby_interface = EOS_Platform_GetLobbyInterface(platform);

    EOS_Lobby_AddNotifyLobbyUpdateReceivedOptions update_options = {};
    update_options.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYUPDATERECEIVED_API_LATEST;
    m_lobby_update_notification_id = EOS_Lobby_AddNotifyLobbyUpdateReceived(
        lobby_interface,
        &update_options,
        this,
        [](const EOS_Lobby_LobbyUpdateReceivedCallbackInfo* data) {
            auto* self = static_cast<LobbyManager*>(data->ClientData);
            if (!self->m_current_lobby.has_value() || self->m_current_lobby->lobby_id != data->LobbyId) {
                return;
            }
            self->refresh_lobby_info();
            if (self->on_lobby_updated && self->m_current_lobby.has_value()) {
                self->on_lobby_updated(*self->m_current_lobby);
            }
        }
    );

    EOS_Lobby_AddNotifyLobbyMemberUpdateReceivedOptions member_update_options = {};
    member_update_options.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERUPDATERECEIVED_API_LATEST;
    m_lobby_member_update_notification_id = EOS_Lobby_AddNotifyLobbyMemberUpdateReceived(
        lobby_interface,
        &member_update_options,
        this,
        [](const EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo* data) {
            auto* self = static_cast<LobbyManager*>(data->ClientData);
            if (!self->m_current_lobby.has_value() || self->m_current_lobby->lobby_id != data->LobbyId) {
                return;
            }
            self->refresh_lobby_info();
            if (self->on_lobby_updated && self->m_current_lobby.has_value()) {
                self->on_lobby_updated(*self->m_current_lobby);
            }
        }
    );

    EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions status_options = {};
    status_options.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST;
    m_lobby_member_status_notification_id = EOS_Lobby_AddNotifyLobbyMemberStatusReceived(
        lobby_interface,
        &status_options,
        this,
        [](const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* data) {
            auto* self = static_cast<LobbyManager*>(data->ClientData);
            if (!self->m_current_lobby.has_value() || self->m_current_lobby->lobby_id != data->LobbyId) {
                return;
            }

            self->refresh_lobby_info();

            switch (data->CurrentStatus) {
                case EOS_ELobbyMemberStatus::EOS_LMS_JOINED:
                    if (self->on_member_joined && self->m_current_lobby.has_value()) {
                        auto it = std::find_if(
                            self->m_current_lobby->members.begin(),
                            self->m_current_lobby->members.end(),
                            [data](const LobbyMember& member) { return product_user_ids_equal(member.user_id, data->TargetUserId); }
                        );
                        LobbyMember member;
                        if (it != self->m_current_lobby->members.end()) {
                            member = *it;
                        } else {
                            member.user_id = data->TargetUserId;
                            member.user_id_string = product_user_id_to_string(data->TargetUserId);
                        }
                        self->on_member_joined(data->LobbyId, member);
                    }
                    break;
                case EOS_ELobbyMemberStatus::EOS_LMS_LEFT:
                case EOS_ELobbyMemberStatus::EOS_LMS_DISCONNECTED:
                case EOS_ELobbyMemberStatus::EOS_LMS_KICKED:
                case EOS_ELobbyMemberStatus::EOS_LMS_CLOSED:
                    if (self->on_member_left) {
                        self->on_member_left(data->LobbyId, data->TargetUserId);
                    }
                    break;
                case EOS_ELobbyMemberStatus::EOS_LMS_PROMOTED:
                    if (self->on_lobby_updated && self->m_current_lobby.has_value()) {
                        self->on_lobby_updated(*self->m_current_lobby);
                    }
                    break;
            }
        }
    );
#endif

    m_callbacks_registered = true;
}

void LobbyManager::unregister_callbacks() {
    if (!m_callbacks_registered) return;

#ifndef EOS_STUB_MODE
    auto platform = Platform::instance().get_handle();
    if (platform) {
        auto lobby_interface = EOS_Platform_GetLobbyInterface(platform);
        if (m_lobby_update_notification_id != EOS_INVALID_NOTIFICATIONID) {
            EOS_Lobby_RemoveNotifyLobbyUpdateReceived(lobby_interface, m_lobby_update_notification_id);
            m_lobby_update_notification_id = EOS_INVALID_NOTIFICATIONID;
        }
        if (m_lobby_member_update_notification_id != EOS_INVALID_NOTIFICATIONID) {
            EOS_Lobby_RemoveNotifyLobbyMemberUpdateReceived(lobby_interface, m_lobby_member_update_notification_id);
            m_lobby_member_update_notification_id = EOS_INVALID_NOTIFICATIONID;
        }
        if (m_lobby_member_status_notification_id != EOS_INVALID_NOTIFICATIONID) {
            EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(lobby_interface, m_lobby_member_status_notification_id);
            m_lobby_member_status_notification_id = EOS_INVALID_NOTIFICATIONID;
        }
    }
#endif

    m_callbacks_registered = false;
}

void LobbyManager::refresh_lobby_info() {
#ifndef EOS_STUB_MODE
    if (!m_current_lobby.has_value()) return;

    auto platform = Platform::instance().get_handle();
    if (!platform) return;

    std::unordered_map<std::string, LobbyMember> previous_members;
    for (const auto& member : m_current_lobby->members) {
        auto key = member.user_id_string;
        if (key.empty()) {
            key = product_user_id_to_string(member.user_id);
        }
        if (!key.empty()) {
            previous_members[key] = member;
        }
    }

    EOS_Lobby_CopyLobbyDetailsHandleOptions copy_opts = {};
    copy_opts.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
    copy_opts.LobbyId = m_current_lobby->lobby_id.c_str();
    copy_opts.LocalUserId = AuthManager::instance().get_product_user_id();

    EOS_HLobbyDetails lobby_details = nullptr;
    if (EOS_Lobby_CopyLobbyDetailsHandle(EOS_Platform_GetLobbyInterface(platform), &copy_opts, &lobby_details) != EOS_EResult::EOS_Success) {
        return;
    }

    LobbyInfo refreshed = *m_current_lobby;
    populate_lobby_from_details(lobby_details, refreshed);

    for (auto& member : refreshed.members) {
        auto previous = previous_members.find(member.user_id_string);
        if (previous != previous_members.end()) {
            member.display_name = previous->second.display_name;
            member.attributes = previous->second.attributes;
            member.is_ready = previous->second.is_ready;
            member.is_owner = previous->second.is_owner || member.is_owner;
        }

        if (product_user_ids_equal(member.user_id, AuthManager::instance().get_product_user_id()) && member.display_name.empty()) {
            member.display_name = AuthManager::instance().get_display_name();
        }
    }

    m_current_lobby = refreshed;
    EOS_LobbyDetails_Release(lobby_details);
#endif
}

} // namespace eos_testing
