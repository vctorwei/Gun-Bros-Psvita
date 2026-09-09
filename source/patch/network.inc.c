/* Offline CNGS/GameSpy behavior and keepalive continuation guards. */

static int patch_log_label_is_essential(const char *label) {
    return label &&
           (strstr(label, "CStoreAggregator") ||
            strstr(label, "CBrother") ||
            strstr(label, "CPlayer") ||
            strstr(label, "Store"));
}

static void patch_log_once(const char *label, const char *result) {
    enum { max_slots = 64, max_per_label = 6 };
    static struct {
        const char *label;
        int count;
    } slots[max_slots];
    int i;
    int slot = -1;

    if (!patch_log_label_is_essential(label)) {
        return;
    }

    for (i = 0; i < max_slots; i++) {
        if (slots[i].label == label) {
            slot = i;
            break;
        }

        if (!slots[i].label && slot < 0) {
            slot = i;
        }
    }

    if (slot >= 0 && !slots[slot].label) {
        slots[slot].label = label;
    }

    if (slot < 0 || slots[slot].count < max_per_label) {
        sceClibPrintf("[PATCH-CNGS] %s -> %s\n",
                      label ? label : "(null)",
                      result ? result : "(null)");
    }

    if (slot >= 0) {
        slots[slot].count++;
    }
}

#define CNGS_INT_STUB(fn, label, value) \
    static int fn(void) { \
        patch_log_once(label, value ? "true" : "false"); \
        return value; \
    }

#define CNGS_VOID_STUB(fn, label) \
    static void fn(void) { \
        patch_log_once(label, "skip"); \
    }

// Report a coherent offline CNGS environment. Claiming valid online credentials
// while server request/update hooks are no-ops leaves boot parked on splash.
CNGS_VOID_STUB(cngs_handle_update_stub, "CNGS::HandleUpdate")
CNGS_VOID_STUB(cngs_session_tick_stub, "CNGSSession::tick")
CNGS_INT_STUB(cngs_session_key_valid_stub, "CNGSSession::isSessionKeyValid", 0)
CNGS_INT_STUB(cngs_local_user_authenticated_stub, "CNGSLocalUser::isAuthenticated", 0)
CNGS_INT_STUB(cngs_local_user_load_credentials_stub, "CNGSLocalUser::LoadCredentials", 0)
CNGS_INT_STUB(cngs_local_user_register_stub, "CNGSLocalUser::RegisterUser", 0)
CNGS_INT_STUB(cngs_local_user_validate_stub, "CNGSLocalUser::ValidateUser", 0)
CNGS_INT_STUB(cngs_local_user_associate_stub, "CNGSLocalUser::AssociateUser", 0)
CNGS_INT_STUB(cngs_local_user_update_stub, "CNGSLocalUser::UpdateUserInfo", 0)
CNGS_INT_STUB(cngs_local_credentials_exists_stub, "CNGSLocalUser::CredentialsFileExists", 0)
CNGS_INT_STUB(cngs_user_load_credentials_stub, "CNGSUser::LoadCredentials", 0)
CNGS_INT_STUB(cngs_user_credentials_exists_stub, "CNGSUser::CredentialsFileExists", 0)
CNGS_INT_STUB(cngs_user_credentials_valid_stub, "CNGSUserCredentials::isValid", 0)
CNGS_INT_STUB(cngs_user_credentials_read_stub, "CNGSUserCredentials::readFromFile", 0)
CNGS_INT_STUB(cngs_user_credentials_last_exists_stub, "CNGSUserCredentials::lastPlayerExists", 0)
CNGS_INT_STUB(cngs_user_credentials_get_last_stub, "CNGSUserCredentials::getLastPlayer", 0)
CNGS_INT_STUB(cngs_session_config_read_stub, "CNGSSessionConfig::readFromFile", 0)
CNGS_VOID_STUB(cngs_account_handle_update_stub, "CNGSAccountManager::HandleUpdate")
CNGS_INT_STUB(cngs_account_send_message_stub, "CNGSAccountManager::SendMessageToServer", 0)
CNGS_INT_STUB(cngs_account_send_object_stub, "CNGSAccountManager::SendMessageObjectToServer", 0)
CNGS_VOID_STUB(content_tracker_load_from_server_stub, "CContentTracker::LoadFromServer")
CNGS_INT_STUB(content_tracker_save_to_server_stub, "CContentTracker::SaveToServer", 0)
CNGS_INT_STUB(cngs_content_get_stub, "CNGSContentManager::GetContent", 0)
CNGS_INT_STUB(cngs_content_get_self_stub, "CNGSContentManager::GetContentSelf", 0)
CNGS_INT_STUB(cngs_content_get_friend_stub, "CNGSContentManager::GetContentFriend", 0)
CNGS_VOID_STUB(cngs_json_handle_update_stub, "CNGSJSONData::HandleUpdate")
CNGS_INT_STUB(cngs_json_load_server_stub, "CNGSJSONData::LoadFromServer", 0)
CNGS_VOID_STUB(cngs_content_handle_update_stub, "CNGSContentManager::HandleUpdate")
CNGS_INT_STUB(cngs_content_status_stub, "CNGSContentManager::getContentManagerStatus", 0)
CNGS_VOID_STUB(gunbros_update_online_status_stub, "CGunBros::UpdateOnlineStatus")
CNGS_INT_STUB(menu_greeting_offline_stub, "CMenuGreeting::IsInOfflineMode", 1)
CNGS_INT_STUB(cngs_applet_wifi_stub, "CApplet::IsUserOnWiFi", 0)
CNGS_INT_STUB(cngs_callback_wifi_stub, "GluPlatformCallbackJNI::IsUserOnWiFi", 0)

/* gti2SocketConnectionsThink maps every live GameSpy connection through this
 * callback. Its original body calls gti2SendKeepAlive at +0x110. The Vita
 * port has no GameSpy transport, so skip the connection tick before it can
 * enter that callback chain. Returning zero tells TableMapSafe2 to continue
 * iterating without closing or touching the connection. */
static int gti2_connection_think_offline_stub(void *connection, unsigned int now) {
    static unsigned int log_count;

    if (log_count < 4) {
        sceClibPrintf("[FIX-NET] gti2ConnectionThink skipped offline connection=%p now=%u\n",
                      connection, now);
        log_count++;
    }
    return 0;
}

/* CGameSpyMatchmaker::Update calls gt2Think directly every frame. Keep the
 * high-level Update method intact: besides pumping transport it advances the
 * login/failure state observed by CProfileManager's resource-loader callback.
 * Replacing the whole method with a no-op leaves that callback permanently
 * not-ready and parks the loader on its first type-3 node. Only stop the
 * unsupported low-level socket pump here. */
static void gt2_think_offline_stub(void *socket) {
    static unsigned int log_count;

    if (log_count < 4) {
        sceClibPrintf("[FIX-NET] gt2Think skipped offline socket=%p\n", socket);
        log_count++;
    }
}


static void patch_gti2_keepalive_offline_arm_stub(void) {
    uintptr_t entry = so_symbol(&so_mod, "gti2SendKeepAlive");
    static const uint32_t code[2] = {
        0xe3a00000u, /* mov r0, #0 */
        0xe12fff1eu, /* bx lr */
    };

    /* This is an offline-only transport.  Patch its real ARM entry in place
     * rather than through a second trampoline: the faulting PC was inside
     * this routine after it was called with a null connection. */
    if (!entry || (entry & 1u)) {
        sceClibPrintf("[PATCH-NET] gti2SendKeepAlive ARM entry missing/bad: %p\n",
                      (void *)entry);
        return;
    }

    kuKernelCpuUnrestrictedMemcpy((void *)entry, code, sizeof(code));
    kuKernelFlushCaches((void *)entry, 8);

    sceClibPrintf("[PATCH-NET] gti2SendKeepAlive forced offline at %p words=%08x %08x\n",
                  (void *)entry,
                  *(volatile uint32_t *)entry,
                  *(volatile uint32_t *)(entry + 4));
}

static void patch_gti2_keepalive_after_append_guard(void) {
    uintptr_t entry = so_symbol(&so_mod, "gti2SendKeepAlive");
    uintptr_t patch_at;
    uint32_t first;
    uint32_t second;
    uint32_t code[2];

    /* Some offline callback returns resume inside this ARM routine rather than
     * at its entry. At +0x70, overwrite the final array load/ArrayAppend pair
     * that leads to the known +0x78 r4 dereference, then use the function's
     * own return-zero epilogue at +0x9c. */
    if (!entry || (entry & 1u)) {
        sceClibPrintf("[PATCH-NET] gti2SendKeepAlive ARM continuation missing/bad: %p\n",
                      (void *)entry);
        return;
    }

    patch_at = entry + 0x70u;
    first = *(volatile uint32_t *)patch_at;
    second = *(volatile uint32_t *)(patch_at + 4);
    if (first != 0xe5940060u || second != 0xebffd47cu) {
        sceClibPrintf("[PATCH-NET] gti2SendKeepAlive +0x70 unexpected words at %p: %08x %08x; forcing verified offline guard\n",
                      (void *)patch_at, first, second);
    }

    code[0] = 0xe51ff004u;       /* ldr pc, [pc, #-4] */
    code[1] = (uint32_t)(entry + 0x9cu); /* mov r0, #0; epilogue */
    kuKernelCpuUnrestrictedMemcpy((void *)patch_at, code, sizeof(code));
    kuKernelFlushCaches((void *)patch_at, sizeof(code));

    sceClibPrintf("[PATCH-NET] gti2SendKeepAlive +0x70 direct epilogue guard at %p -> %p words=%08x %08x\n",
                  (void *)patch_at, (void *)(entry + 0x9cu),
                  *(volatile uint32_t *)patch_at,
                  *(volatile uint32_t *)(patch_at + 4));
}

static void patch_gti2_keepalive_return_guard(void) {
    uintptr_t entry = so_symbol(&so_mod, "gti2SendKeepAlive");
    uintptr_t patch_at;
    uint32_t first;
    uint32_t second;
    static const uint32_t code[3] = {
        0xe3a00000u, /* mov r0, #0 */
        0xe28dd01cu, /* add sp, sp, #0x1c */
        0xe8bd80f0u, /* pop {r4, r5, r6, r7, pc} */
    };

    /* The hooked GetGameObject callback returns to +0x78. Do not resume the
     * original r4+0x60 load after an offline safe miss; return failure using
     * the original ARM stack layout instead. */
    if (!entry || (entry & 1u)) {
        sceClibPrintf("[PATCH-NET] gti2SendKeepAlive return continuation missing/bad: %p\n",
                      (void *)entry);
        return;
    }

    patch_at = entry + 0x78u;
    first = *(volatile uint32_t *)patch_at;
    second = *(volatile uint32_t *)(patch_at + 4);
    if (first != 0xe5940060u || second != 0xebffd36cu) {
        sceClibPrintf("[PATCH-NET] gti2SendKeepAlive +0x78 unexpected words at %p: %08x %08x; forcing verified return epilogue\n",
                      (void *)patch_at, first, second);
    }

    kuKernelCpuUnrestrictedMemcpy((void *)patch_at, code, sizeof(code));
    kuKernelFlushCaches((void *)patch_at, sizeof(code));

    sceClibPrintf("[PATCH-NET] gti2SendKeepAlive +0x78 direct return guard at %p words=%08x %08x %08x\n",
                  (void *)patch_at,
                  *(volatile uint32_t *)patch_at,
                  *(volatile uint32_t *)(patch_at + 4),
                  *(volatile uint32_t *)(patch_at + 8));
}

static void ensure_gti2_keepalive_offline_guards(const char *source) {
    static uintptr_t entry;
    static int looked_up;
    static unsigned int repair_count;
    static const uint32_t entry_code[2] = {
        0xe3a00000u, /* mov r0, #0 */
        0xe12fff1eu, /* bx lr */
    };
    static const uint32_t continuation_code[3] = {
        0xe3a00000u, /* mov r0, #0 */
        0xe28dd01cu, /* add sp, sp, #0x1c */
        0xe8bd80f0u, /* pop {r4, r5, r6, r7, pc} */
    };
    uint32_t redirect_code[2];
    int entry_ok;
    int redirect_ok;
    int continuation_ok;

    /* Called from CGunBros::Update every frame. Symbol lookup still walks the
     * dynamic table after a hash miss/fallback, while this module cannot move
     * after relocation, so resolve the guard address exactly once. */
    if (!looked_up) {
        entry = so_symbol(&so_mod, "gti2SendKeepAlive");
        looked_up = 1;
    }

    if (!entry || (entry & 1u)) {
        return;
    }

    redirect_code[0] = 0xe51ff004u; /* ldr pc, [pc, #-4] */
    redirect_code[1] = (uint32_t)(entry + 0x9cu);

    entry_ok = *(volatile uint32_t *)(entry + 0x00u) == entry_code[0] &&
               *(volatile uint32_t *)(entry + 0x04u) == entry_code[1];
    redirect_ok = *(volatile uint32_t *)(entry + 0x70u) == redirect_code[0] &&
                  *(volatile uint32_t *)(entry + 0x74u) == redirect_code[1];
    continuation_ok =
        *(volatile uint32_t *)(entry + 0x78u) == continuation_code[0] &&
        *(volatile uint32_t *)(entry + 0x7cu) == continuation_code[1] &&
        *(volatile uint32_t *)(entry + 0x80u) == continuation_code[2];

    if (entry_ok && redirect_ok && continuation_ok) {
        return;
    }

    /* Patch the observed crash continuation first. A stale callback may be
     * about to return directly to +0x78 and therefore bypass both parents and
     * the function entry. */
    kuKernelCpuUnrestrictedMemcpy((void *)(entry + 0x78u),
                                  continuation_code,
                                  sizeof(continuation_code));
    kuKernelCpuUnrestrictedMemcpy((void *)(entry + 0x70u),
                                  redirect_code,
                                  sizeof(redirect_code));
    kuKernelCpuUnrestrictedMemcpy((void *)entry,
                                  entry_code,
                                  sizeof(entry_code));
    kuKernelFlushCaches((void *)entry, 0x84u);

    if (repair_count < 8u) {
        sceClibPrintf("[FIX-NET] reasserted gti2SendKeepAlive guards source=%s entry=%p previous(entry=%d redirect=%d continuation=%d)\n",
                      source ? source : "?", (void *)entry,
                      entry_ok, redirect_ok, continuation_ok);
    }
    repair_count++;
}


static void patch_cngs_online_stubs(void) {
    /* Guard the unsupported transport layer, but do not replace
     * CGameSpyMatchmaker::Update: its non-transport state transitions are
     * required to release the offline profile/login loader gate. */
    hook_symbol("gti2ConnectionThink",
                "gti2ConnectionThink",
                (uintptr_t)gti2_connection_think_offline_stub);
    hook_symbol("gt2Think",
                "gt2Think",
                (uintptr_t)gt2_think_offline_stub);
    hook_symbol("CNGS::HandleUpdate",
                "_ZN4CNGS12HandleUpdateEi",
                (uintptr_t)cngs_handle_update_stub);
    hook_symbol("CNGSSession::tick",
                "_ZN11CNGSSession4tickEj",
                (uintptr_t)cngs_session_tick_stub);
    hook_symbol("CNGSSession::isSessionKeyValid",
                "_ZN11CNGSSession17isSessionKeyValidEv",
                (uintptr_t)cngs_session_key_valid_stub);
    hook_symbol("CNGSLocalUser::isAuthenticated",
                "_ZN13CNGSLocalUser15isAuthenticatedEi",
                (uintptr_t)cngs_local_user_authenticated_stub);
    hook_symbol("CNGSLocalUser::LoadCredentials",
                "_ZN13CNGSLocalUser15LoadCredentialsEv",
                (uintptr_t)cngs_local_user_load_credentials_stub);
    hook_symbol("CNGSLocalUser::RegisterUser",
                "_ZN13CNGSLocalUser12RegisterUserEi",
                (uintptr_t)cngs_local_user_register_stub);
    hook_symbol("CNGSLocalUser::ValidateUser",
                "_ZN13CNGSLocalUser12ValidateUserEi",
                (uintptr_t)cngs_local_user_validate_stub);
    hook_symbol("CNGSLocalUser::AssociateUser",
                "_ZN13CNGSLocalUser13AssociateUserEi",
                (uintptr_t)cngs_local_user_associate_stub);
    hook_symbol("CNGSLocalUser::UpdateUserInfo",
                "_ZN13CNGSLocalUser14UpdateUserInfoEi",
                (uintptr_t)cngs_local_user_update_stub);
    hook_symbol("CNGSLocalUser::CredentialsFileExists",
                "_ZN13CNGSLocalUser21CredentialsFileExistsEv",
                (uintptr_t)cngs_local_credentials_exists_stub);
    hook_symbol("CNGSUser::LoadCredentials",
                "_ZN8CNGSUser15LoadCredentialsEPKw",
                (uintptr_t)cngs_user_load_credentials_stub);
    hook_symbol("CNGSUser::CredentialsFileExists",
                "_ZN8CNGSUser21CredentialsFileExistsEPKw",
                (uintptr_t)cngs_user_credentials_exists_stub);
    hook_symbol("CNGSUserCredentials::isValid",
                "_ZNK19CNGSUserCredentials7isValidEv",
                (uintptr_t)cngs_user_credentials_valid_stub);
    hook_symbol("CNGSUserCredentials::readFromFile",
                "_ZN19CNGSUserCredentials12readFromFileERN3com3glu8platform10components9CStrWCharEb",
                (uintptr_t)cngs_user_credentials_read_stub);
    hook_symbol("CNGSUserCredentials::lastPlayerExists",
                "_ZN19CNGSUserCredentials16lastPlayerExistsEv",
                (uintptr_t)cngs_user_credentials_last_exists_stub);
    hook_symbol("CNGSUserCredentials::getLastPlayer",
                "_ZN19CNGSUserCredentials13getLastPlayerERS_",
                (uintptr_t)cngs_user_credentials_get_last_stub);
    hook_symbol("CNGSSessionConfig::readFromFile",
                "_ZN17CNGSSessionConfig12readFromFileERN3com3glu8platform10components9CStrWCharES5_b",
                (uintptr_t)cngs_session_config_read_stub);
    hook_symbol("CNGSAccountManager::HandleUpdate",
                "_ZN18CNGSAccountManager12HandleUpdateEi",
                (uintptr_t)cngs_account_handle_update_stub);
    hook_symbol("CNGSAccountManager::SendMessageToServer",
                "_ZN18CNGSAccountManager19SendMessageToServerEP10CObjectMapPKcMS_FvS1_P25CNGSAccountManagerFunctorEPv",
                (uintptr_t)cngs_account_send_message_stub);
    hook_symbol("CNGSAccountManager::SendMessageObjectToServer",
                "_ZN18CNGSAccountManager25SendMessageObjectToServerEP16CObjectMapObjectPKcMS_FvP10CObjectMapP25CNGSAccountManagerFunctorEPv",
                (uintptr_t)cngs_account_send_object_stub);
    hook_symbol("CContentTracker::LoadFromServer",
                "_ZN15CContentTracker14LoadFromServerEPN3com3glu8platform10components5CHashE",
                (uintptr_t)content_tracker_load_from_server_stub);
    hook_symbol("CContentTracker::SaveToServer",
                "_ZN15CContentTracker12SaveToServerERN3com3glu8platform4core7CVectorIP13CNGSAttributeEE",
                (uintptr_t)content_tracker_save_to_server_stub);
    hook_symbol("CNGSContentManager::GetContent",
                "_ZN18CNGSContentManager10GetContentEP10CObjectMapP25CNGSContentRequestFunctor",
                (uintptr_t)cngs_content_get_stub);
    hook_symbol("CNGSContentManager::GetContentSelf",
                "_ZN18CNGSContentManager14GetContentSelfEPKc",
                (uintptr_t)cngs_content_get_self_stub);
    hook_symbol("CNGSContentManager::GetContentFriend",
                "_ZN18CNGSContentManager16GetContentFriendEiPKc",
                (uintptr_t)cngs_content_get_friend_stub);
    hook_symbol("CNGSContentManager::HandleUpdate",
                "_ZN18CNGSContentManager12HandleUpdateEi",
                (uintptr_t)cngs_content_handle_update_stub);
    hook_symbol("CNGSContentManager::getContentManagerStatus",
                "_ZN18CNGSContentManager23getContentManagerStatusEv",
                (uintptr_t)cngs_content_status_stub);
    hook_symbol("CNGSJSONData::HandleUpdate",
                "_ZN12CNGSJSONData12HandleUpdateEi",
                (uintptr_t)cngs_json_handle_update_stub);
    hook_symbol("CNGSJSONData::LoadFromServer(paths)",
                "_ZN12CNGSJSONData14LoadFromServerERKN3com3glu8platform10components9CStrWCharES6_S6_b",
                (uintptr_t)cngs_json_load_server_stub);
    hook_symbol("CNGSJSONData::LoadFromServer(path)",
                "_ZN12CNGSJSONData14LoadFromServerERKN3com3glu8platform10components9CStrWCharEb",
                (uintptr_t)cngs_json_load_server_stub);
    hook_symbol("CNGSJSONData::LoadFromServer",
                "_ZN12CNGSJSONData14LoadFromServerEb",
                (uintptr_t)cngs_json_load_server_stub);
    hook_symbol("CApplet::IsUserOnWiFi",
                "_ZN7CApplet12IsUserOnWiFiEv",
                (uintptr_t)cngs_applet_wifi_stub);
    hook_symbol("GluPlatformCallbackJNI::IsUserOnWiFi",
                "_ZN22GluPlatformCallbackJNI12IsUserOnWiFiEv",
                (uintptr_t)cngs_callback_wifi_stub);
    hook_symbol("CGunBros::UpdateOnlineStatus",
                "_ZN8CGunBros18UpdateOnlineStatusEv",
                (uintptr_t)gunbros_update_online_status_stub);
    hook_symbol("CMenuGreeting::IsInOfflineMode",
                "_ZNK13CMenuGreeting15IsInOfflineModeEv",
                (uintptr_t)menu_greeting_offline_stub);
}
