// Minimal mirrors of the EOS SDK types ayame touches.
//
// Field layouts follow the public EOS SDK ABI, which is append-only: field
// offsets never move between SDK versions. Structs are declared only up to
// the last field we read or write. ApiVersions always come from the game's
// own options; the only struct we construct (CreateDeviceId options) uses
// EOS_CONNECT_CREATEDEVICEID_API_LATEST.
#pragma once

#include <stdint.h>

typedef int32_t EOS_EResult;
typedef int32_t EOS_Bool;
typedef void *EOS_HConnect;
typedef void *EOS_HLobby;
typedef void *EOS_HIntegratedPlatformOptionsContainer;
typedef void *EOS_ProductUserId;
typedef void *EOS_ContinuanceToken;
typedef int64_t EOS_NotificationId;

#define EOS_SUCCESS 0
#define EOS_DUPLICATE_NOT_ALLOWED 24 // device id already exists
#define EOS_NOT_FOUND 6
#define EOS_INVALID_PARAMETERS 11

#define EOS_ECT_STEAM_APP_TICKET 0
#define EOS_ECT_STEAM_SESSION_TICKET 13
#define EOS_ECT_DEVICEID_ACCESS_TOKEN 10

#define EOS_CONNECT_CREATEDEVICEID_API_LATEST 1
#define EOS_CONNECT_CREDENTIALS_API_LATEST 1
#define EOS_CONNECT_USERLOGININFO_API_LATEST 2

#define EOS_LOBBY_CREATELOBBY_API_LATEST 6
#define EOS_LOBBY_JOINLOBBY_API_LATEST 3
#define EOS_LOBBY_JOINLOBBYBYID_API_LATEST 2

#pragma pack(push, 8)

typedef struct {
    int32_t ApiVersion;
    EOS_EResult ResultCode;
    void *ClientData;
} EOS_GenericCallbackInfo_Head; // common callback prefix (ResultCode at +4)

typedef struct {
    int32_t ApiVersion;
    const char *Token;
    int32_t Type; // EOS_EExternalCredentialType
} EOS_Connect_Credentials;

typedef struct {
    int32_t ApiVersion;
    const char *DisplayName;
    const char *NsaIdToken; // v2+; null on every non-Switch platform
} EOS_Connect_UserLoginInfo;

typedef struct {
    int32_t ApiVersion;
    const EOS_Connect_Credentials *Credentials;
    const EOS_Connect_UserLoginInfo *UserLoginInfo;
} EOS_Connect_LoginOptions;

typedef struct {
    EOS_EResult ResultCode;
    void *ClientData;
    EOS_ProductUserId LocalUserId;
    void *ContinuanceToken;
} EOS_Connect_LoginCallbackInfo;

typedef struct {
    int32_t ApiVersion;
    const char *DeviceModel;
} EOS_Connect_CreateDeviceIdOptions;

typedef struct {
    EOS_EResult ResultCode;
    void *ClientData;
} EOS_Connect_CreateDeviceIdCallbackInfo;

// Partial views of lobby options — only the prefix up to bPresenceEnabled.
typedef struct {
    int32_t ApiVersion;
    EOS_ProductUserId LocalUserId;
    uint32_t MaxLobbyMembers;
    int32_t PermissionLevel;
    EOS_Bool bPresenceEnabled; // v2+
} EOS_Lobby_CreateLobbyOptions_Partial;

typedef struct {
    int32_t ApiVersion;
    void *LobbyDetailsHandle;
    EOS_ProductUserId LocalUserId;
    EOS_Bool bPresenceEnabled; // v2+
} EOS_Lobby_JoinLobbyOptions_Partial;

typedef struct {
    int32_t ApiVersion;
    const char *LobbyId;
    EOS_ProductUserId LocalUserId;
    EOS_Bool bPresenceEnabled; // v1+
} EOS_Lobby_JoinLobbyByIdOptions_Partial;

#pragma pack(pop)

typedef void (*EOS_Connect_OnLoginCb)(const EOS_Connect_LoginCallbackInfo *);
typedef void (*EOS_Connect_OnCreateDeviceIdCb)(const EOS_Connect_CreateDeviceIdCallbackInfo *);

typedef void (*EOS_Connect_Login_t)(EOS_HConnect, const EOS_Connect_LoginOptions *, void *,
                                    EOS_Connect_OnLoginCb);
typedef void (*EOS_Connect_CreateDeviceId_t)(EOS_HConnect, const EOS_Connect_CreateDeviceIdOptions *,
                                             void *, EOS_Connect_OnCreateDeviceIdCb);
typedef void (*EOS_Connect_DeleteDeviceId_t)(EOS_HConnect, const void *, void *,
                                             void (*)(const void *));
typedef EOS_EResult (*EOS_IPOContainer_Add_t)(EOS_HIntegratedPlatformOptionsContainer, const void *);
// CreateLobby / JoinLobby / JoinLobbyById share shape (handle, opts, cd, completion).
typedef void (*EOS_Lobby_OpFn_t)(EOS_HLobby, const void *, void *, void *);
typedef void *(*EOS_Lobby_CreateLobbySearch_t)(EOS_HLobby, const void *);

typedef EOS_EResult (*EOS_Initialize_t)(const void *);
typedef EOS_EResult (*EOS_Shutdown_t)(void);
