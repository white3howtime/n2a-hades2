/* Hades II (0100A00019DE0000) service-layer HLE that the D2R set does not cover.
 *
 * Every entry is HLE_FORCE: the SDK module defines these functions, and the SDK's own wrappers (each ends in nn::sf IPC,
 * i.e. SendSyncRequest) must never run on Android. Names come from the imports of Hades II's main module (v9) and were
 * checked against dodola's sources so none duplicates an existing entry (a duplicate aborts the loader).
 *
 * Not here on purpose:
 *  - nn::socket::InetHtonl/InetHtons: pure byte-order math, runs in the SDK.
 *  - nn::ro::ProtectRelro: executes svc 0x03, which hle_svc.c already handles.
 *  - nn::fs::GetDirectoryEntryCount: needs hle_fs.c's private directory handle; see hades/patches/.
 *  - EOS_*: never reached, see the EOS cut in patch_nso.py.
 */
#ifdef HADES_HLE_TEST
#include "hades_test/mock_env.h"
#else
#include "../host/hle.h"
#include "input_backend.h"
#include "services_platform.h"
#endif

/* nn::fs ResultOutOfRange (module 2, description 3005): a read that does not fit in the file. */
#define RES_FS_OUT_OF_RANGE ((3005u << 9) | 2u)

/* ---------- title setup ---------- */
void dm_android_svc_set_program_id(uint64_t);
void dm_android_loader_probe_allow_unused_force(void);

/* Runs when the library loads, before any module is mapped (both calls refuse to run afterwards).
 * The D2R HLE_FORCE entries that Hades II never references would otherwise abort the loader ("unreferenced entry"). */
__attribute__((constructor)) static void hades_title_setup(void) {
    dm_android_svc_set_program_id(0x0100A00019DE0000ull);
    dm_android_loader_probe_allow_unused_force();
}

/* ---------- nn::ro: only the EOSSDK NRO uses it, and EOS is cut; registration of game.nrr must not reach ldr:ro ---------- */
HLE_FORCE("_ZN2nn2ro10InitializeEv", "ldr:ro service; nothing is ever loaded", Result, h_ro_Initialize, void) { return RES_OK; }
HLE_FORCE("_ZN2nn2ro8FinalizeEv", "ldr:ro service", void, h_ro_Finalize, void) {}
HLE_FORCE("_ZN2nn2ro18RegisterModuleInfoEPNS0_16RegistrationInfoEPKv", "ldr:ro service; the NRR is not verified", Result, h_ro_Register, void *info, const void *nrr) {
    (void)info; (void)nrr; return RES_OK;
}
HLE_FORCE("_ZN2nn2ro20UnregisterModuleInfoEPNS0_16RegistrationInfoE", "ldr:ro service", void, h_ro_Unregister, void *info) { (void)info; }
/* Unreachable while the EOS cut is in place; failing is correct if it is ever reached (the caller aborts). */
HLE_FORCE("_ZN2nn2ro13GetBufferSizeEPmPKv", "ldr:ro service; NROs are not loaded", Result, h_ro_GetBufferSize, uint64_t *size, const void *nro) {
    (void)nro; if (size) *size = 0; return RES_FAIL;
}
HLE_FORCE("_ZN2nn2ro10LoadModuleEPNS0_6ModuleEPKvPvmib", "ldr:ro service; NROs are not loaded", Result, h_ro_LoadModule,
          void *module, const void *nro, void *buf, uint64_t size, int flag, int fixed) {
    (void)module; (void)nro; (void)buf; (void)size; (void)flag; (void)fixed; return RES_FAIL;
}
HLE_FORCE("_ZN2nn2ro12UnloadModuleEPNS0_6ModuleE", "ldr:ro service", void, h_ro_Unload, void *module) { (void)module; }

/* ---------- nn::fs ---------- */
HLE_FORCE("_ZN2nn2fs32EnableDefaultGlobalFileDataCacheEPvm", "fsp-srv; reads go straight to pread", void, h_fs_EnableCache, void *buf, uint64_t size) {
    (void)buf; (void)size;
}
/* ReadFile without an out-size: the whole range must be readable. FileHandle is the host fd (hle_fs.c). */
HLE_FORCE("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm", "fsp-srv -> pread(2); fd is the FileHandle, as in hle_fs.c", Result, h_fs_ReadFileExact,
          uint64_t h, int64_t off, void *buf, uint64_t size) {
    uint64_t got = 0;
    while (got < size) {
        ssize_t r = dm_data_pread((int)h, (uint8_t *)buf + got, (size_t)(size - got), (off_t)(off + (int64_t)got));
        if (r < 0) return RES_FAIL;
        if (r == 0) return RES_FS_OUT_OF_RANGE;
        got += (uint64_t)r;
    }
    return RES_OK;
}
/* Cache storage is only used by the cloud-save device (mount name "steamcache"), which is not created offline.
 * If it is ever reached: creation succeeds, nothing is mounted (later steamcache:/ accesses report ordinary file errors), and the list is empty. */
HLE_FORCE("_ZN2nn2fs18CreateCacheStorageEill", "fsp-srv; cache storage is not provided", Result, h_fs_CreateCache, int index, int64_t size, int64_t journal) {
    (void)index; (void)size; (void)journal; return RES_OK;
}
HLE_FORCE("_ZN2nn2fs17MountCacheStorageEPKci", "fsp-srv; cache storage is not provided", Result, h_fs_MountCache, const char *name, int index) {
    (void)name; (void)index; return RES_OK;
}
HLE_FORCE("_ZN2nn2fs18GetCacheStorageMaxEPiPl", "fsp-srv; cache storage is not provided", Result, h_fs_CacheMax, int *index_max, int64_t *size_max) {
    if (index_max) *index_max = 0;
    if (size_max) *size_max = 0;
    return RES_OK;
}
HLE_FORCE("_ZN2nn2fs20OpenCacheStorageListEPNS0_22CacheStorageListHandleE", "fsp-srv; cache storage is not provided", Result, h_fs_OpenCacheList, uint64_t *handle) {
    if (handle) *handle = 0;
    return RES_OK;
}
HLE_FORCE("_ZN2nn2fs20ReadCacheStorageListEPiPNS0_16CacheStorageInfoENS0_22CacheStorageListHandleEi", "fsp-srv; the list is empty", Result, h_fs_ReadCacheList,
          int *count, void *infos, uint64_t handle, int max) {
    (void)infos; (void)handle; (void)max; if (count) *count = 0; return RES_OK;
}
HLE_FORCE("_ZN2nn2fs21CloseCacheStorageListENS0_22CacheStorageListHandleE", "fsp-srv", void, h_fs_CloseCacheList, uint64_t handle) { (void)handle; }

/* ---------- nn::oe ---------- */
HLE_FORCE("_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE", "apm service; the host does not retune the CPU", void, h_oe_CpuBoost, int mode) { (void)mode; }
/* Same event object hle_oe.c signals when it queues a notification; the SDK getter would hand out an event nothing initialised. */
extern void *dm_android_oe_notification_event(void);
HLE_FORCE("_ZN2nn2oe27GetNotificationMessageEventEv", "appletOE; event bound to hle_oe.c's notification queue", void *, h_oe_NotifEvent, void) {
    return dm_android_oe_notification_event();
}

/* ---------- nn::hid ---------- */
static void hid_require(int err, const char *what) { if (err) die("Hades HID %s failed: %d", what, err); }
/* JoyDual shares NpadFullKeyState's layout. The logical pad is reported as FullKey (input_backend.h), so this only
 * runs if the game asks for JoyDual anyway; it then gets the same state. */
HLE_FORCE("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj", "hid shared memory; Android logical actions", void, h_hid_GetJoyDual, dm_android_npad_state *out, const uint32_t *id) {
    if (!id) die("GetNpadState(JoyDual) null id");
    hid_require(dm_android_input_read_npad(out, *id, 1), "GetNpadState(JoyDual)");
}
HLE_FORCE("_ZN2nn3hid27BindNpadStyleSetUpdateEventERKjPNS_2os15SystemEventTypeENS3_14EventClearModeE", "hid service; event owned by input_backend.c", void, h_hid_BindStyleEvent,
          const uint32_t *id, void *event, int clear_mode) {
    if (!id) die("BindNpadStyleSetUpdateEvent null id");
    hid_require(dm_android_input_bind_style_event(*id, event, clear_mode == 1), "BindNpadStyleSetUpdateEvent");   /* EventClearMode: 0 manual, 1 auto */
}
HLE_FORCE("_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE", "hid service; Joy-Con orientation does not apply", void, h_hid_JoyHold, int type) { (void)type; }
/* Controller applet: report one player on pad 1 without showing anything. ControllerSupportResultInfo = {int playerCount; uint32 selectedId}. */
HLE_FORCE("_ZN2nn3hid21ShowControllerSupportEPNS0_27ControllerSupportResultInfoERKNS0_20ControllerSupportArgE", "controller applet; none on Android", Result, h_hid_ShowSupport,
          int32_t *result, const void *arg) {
    (void)arg;
    if (result) { result[0] = 1; result[1] = 0; }
    return RES_OK;
}
HLE_FORCE("_ZN2nn3hid22GetVibrationDeviceInfoEPNS0_19VibrationDeviceInfoERKNS0_21VibrationDeviceHandleE", "hid service; GetVibrationDeviceHandles returns none", Result, h_hid_VibInfo,
          void *info, const void *handle) { (void)info; (void)handle; return RES_FAIL; }

/* ---------- nn::account (one fixed local user, same as hle_account.c) ---------- */
typedef struct { uint64_t v[2]; } HUid;
typedef struct { uint64_t v[3]; } HUserHandle;
HLE_FORCE("_ZN2nn7account12ListAllUsersEPiPNS0_3UidEi", "acc:u0; one local user", Result, h_acc_ListAll, int *count, HUid *out, int max) {
    int n = 0;
    if (max >= 1 && out) {
        if (dm_android_platform_account_uid(out[0].v)) die("Android local account is not initialized");
        n = 1;
    }
    if (count) *count = n;
    return RES_OK;
}
HLE_FORCE("_ZN2nn7account17GetLastOpenedUserEPNS0_3UidE", "acc:u0; the one local user", Result, h_acc_LastOpened, HUid *out) {
    if (dm_android_platform_account_uid(out->v)) die("Android local account is not initialized");
    return RES_OK;
}
HLE_FORCE("_ZN2nn7account32IsNetworkServiceAccountAvailableEPbRKNS0_10UserHandleE", "acc:u0; online accounts are out of scope", Result, h_acc_NsaAvail, uint8_t *out, const HUserHandle *h) {
    (void)h; if (out) *out = 0; return RES_OK;
}
/* D2R registers the pointer overload as a plain HLE; Hades II calls the reference overload (a different symbol). */
HLE_FORCE("_ZN2nn7account9CloseUserERKNS0_10UserHandleE", "acc:u0; local account bookkeeping", void, h_acc_CloseUserRef, const HUserHandle *h) {
    if (dm_android_platform_account_close((uint64_t *)(uintptr_t)h->v)) die("Android local account close received an invalid handle");
}

/* ---------- nn::audio ---------- */
/* AudioOut: Hades II opens the default device; the AAudio sink in audio_aaudio.inc registers OpenAudioOut(name). Forward to it. */
#ifdef HADES_HLE_TEST
#define HADES_FORCE_LOOKUP mock_force_lookup
#else
extern const hle_force_ent __start_dm_force[] __attribute__((weak));
extern const hle_force_ent __stop_dm_force[] __attribute__((weak));
static void *hades_force_lookup(const char *name) {
    for (const hle_force_ent *e = __start_dm_force; e < __stop_dm_force; e++) if (!strcmp(e->name, name)) return e->fn;
    return NULL;
}
#define HADES_FORCE_LOOKUP hades_force_lookup
#endif
typedef Result (*open_audio_out_fn)(void *out, void *event, const char *name, const uint8_t *param);
HLE_FORCE("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventERKNS0_17AudioOutParameterE", "audio renderer service; default device = the AAudio sink", Result, h_au_OpenDefault,
          void *out, void *event, const uint8_t *param) {
    static open_audio_out_fn open_named;
    if (!open_named) open_named = (open_audio_out_fn)HADES_FORCE_LOOKUP("_ZN2nn5audio12OpenAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventEPKcRKNS0_17AudioOutParameterE");
    if (!open_named) die("Hades: OpenAudioOut sink is not registered");
    return open_named(out, event, "DeviceOut", param);
}
/* AudioIn (microphone): no devices. ListAudioIns reports zero, so the rest is only a safety net. */
HLE_FORCE("_ZN2nn5audio12ListAudioInsEPNS0_11AudioInInfoEi", "audin service; no capture devices", int, h_au_ListIns, void *info, int max) { (void)info; (void)max; return 0; }
HLE_FORCE("_ZN2nn5audio11OpenAudioInEPNS0_7AudioInEPKcRKNS0_16AudioInParameterE", "audin service; no capture devices", Result, h_au_OpenIn, void *in, const char *name, const void *param) {
    (void)in; (void)name; (void)param; return RES_FAIL;
}
HLE_FORCE("_ZN2nn5audio12StartAudioInEPNS0_7AudioInE", "audin service", Result, h_au_StartIn, void *in) { (void)in; return RES_FAIL; }
HLE_FORCE("_ZN2nn5audio11StopAudioInEPNS0_7AudioInE", "audin service", void, h_au_StopIn, void *in) { (void)in; }
HLE_FORCE("_ZN2nn5audio12CloseAudioInEPNS0_7AudioInE", "audin service", void, h_au_CloseIn, void *in) { (void)in; }
HLE_FORCE("_ZN2nn5audio19AppendAudioInBufferEPNS0_7AudioInEPNS0_13AudioInBufferE", "audin service", unsigned, h_au_AppendIn, void *in, void *buf) {
    (void)in; (void)buf; return 0;
}
HLE_FORCE("_ZN2nn5audio24GetReleasedAudioInBufferEPNS0_7AudioInE", "audin service", void *, h_au_ReleasedIn, void *in) { (void)in; return NULL; }

/* ---------- nn::nifm / nn::socket / nn::ssl / nn::web: offline, same policy as hle_net.c ---------- */
HLE_FORCE("_ZN2nn4nifm27SubmitNetworkRequestAndWaitEv", "nifm service; offline, IsNetworkAvailable stays false", void, h_nifm_SubmitWait, void) {}
HLE_FORCE("_ZN2nn6socket8ShutdownEiNS0_14ShutdownMethodE", "bsd service (cut): -1", int, h_sk_Shutdown, int s, int how) { (void)s; (void)how; return -1; }
/* nn::ssl: Context::Create is already covered by hle_net.c. */
#define HADES_SSL_FAIL(mangled, cname, ...) HLE_FORCE(mangled, "ssl service (cut): failure", Result, cname, __VA_ARGS__)
HADES_SSL_FAIL("_ZN2nn3ssl7Context15ImportServerPkiEPmPKcjNS0_17CertificateFormatE", h_ssl_ImportServer, void *self, uint64_t *id, const char *cert, unsigned size, int fmt) {
    (void)self; (void)id; (void)cert; (void)size; (void)fmt; return RES_FAIL;
}
HADES_SSL_FAIL("_ZN2nn3ssl7Context15ImportClientPkiEPmPKcS4_jj", h_ssl_ImportClient, void *self, uint64_t *id, const char *pki, const char *pass, unsigned psize, unsigned size) {
    (void)self; (void)id; (void)pki; (void)pass; (void)psize; (void)size; return RES_FAIL;
}
HADES_SSL_FAIL("_ZN2nn3ssl7Context9ImportCrlEPmPKcj", h_ssl_ImportCrl, void *self, uint64_t *id, const char *crl, unsigned size) {
    (void)self; (void)id; (void)crl; (void)size; return RES_FAIL;
}
HADES_SSL_FAIL("_ZN2nn3ssl10Connection19SetServerCertBufferEPcj", h_ssl_SetCertBuf, void *self, char *buf, unsigned size) {
    (void)self; (void)buf; (void)size; return RES_FAIL;
}
HADES_SSL_FAIL("_ZN2nn3ssl10Connection19GetVerifyCertErrorsEPNS_6ResultEPjS4_j", h_ssl_VerifyErrors, void *self, uint32_t *errors, uint32_t *count, uint32_t *total, unsigned max) {
    (void)self; (void)errors; (void)count; (void)total; (void)max; return RES_FAIL;
}
HLE_FORCE("_ZN2nn3web11ShowWebPageEPNS0_18WebPageReturnValueERKNS0_14ShowWebPageArgE", "web applet; none on Android", Result, h_web_Show, void *ret, const void *arg) {
    (void)ret; (void)arg; return RES_FAIL;
}

/* ---------- nn::codec ---------- */
/* FMOD (statically linked in main) builds its Opus decoder in a ladder: hardware class first, software class when the hardware Initialize returns
 * exactly 8 (main+0x1cef2c / +0x1cf014), any other non-zero value is a hard error (28). So the hardware initialisers return 8. Before that FMOD
 * sizes one work buffer as max(hardware, software) and asks the hardware size from the hwopus service; the SDK would open "sm:" for it, so the
 * hardware size queries are answered with the software sizes. */
#define HADES_HWOPUS_NOT_SUPPORTED 8u
static uint64_t hw_size(const char *software, int a, int b, int c, int d) {
    uint64_t (*fn)(int, int, int, int) = guest_sym(software);
    if (!fn) die("Hades codec: software work-buffer query %s is missing", software);
    return fn(a, b, c, d);
}
HLE_FORCE("_ZN2nn5codec36GetHardwareOpusDecoderWorkBufferSizeEii", "hwopus service; software size", uint64_t, h_codec_HwSize2, int rate, int channels) {
    return hw_size("_ZN2nn5codec28GetOpusDecoderWorkBufferSizeEii", rate, channels, 0, 0);
}
HLE_FORCE("_ZN2nn5codec36GetHardwareOpusDecoderWorkBufferSizeEiii", "hwopus service; software size", uint64_t, h_codec_HwSize3, int rate, int channels, int option) {
    (void)option; return hw_size("_ZN2nn5codec28GetOpusDecoderWorkBufferSizeEii", rate, channels, 0, 0);
}
HLE_FORCE("_ZN2nn5codec47GetHardwareOpusMultiStreamDecoderWorkBufferSizeEiiii", "hwopus service; software size", uint64_t, h_codec_HwMultiSize4, int rate, int channels, int total, int stereo) {
    return hw_size("_ZN2nn5codec39GetOpusMultiStreamDecoderWorkBufferSizeEiiii", rate, channels, total, stereo);
}
HLE_FORCE("_ZN2nn5codec47GetHardwareOpusMultiStreamDecoderWorkBufferSizeEiiiii", "hwopus service; software size", uint64_t, h_codec_HwMultiSize5, int rate, int channels, int total, int stereo, int option) {
    (void)option; return hw_size("_ZN2nn5codec39GetOpusMultiStreamDecoderWorkBufferSizeEiiii", rate, channels, total, stereo);
}
HLE_FORCE("_ZN2nn5codec29InitializeHardwareOpusDecoderEPNS0_23HardwareOpusDecoderTypeEiiPvmi", "hwopus service; FMOD falls back to software on 8", Result, h_codec_HwInit,
          void *dec, int rate, int channels, void *work, uint64_t size, int option) {
    (void)dec; (void)rate; (void)channels; (void)work; (void)size; (void)option; return HADES_HWOPUS_NOT_SUPPORTED;
}
HLE_FORCE("_ZN2nn5codec40InitializeHardwareOpusMultiStreamDecoderEPNS0_34HardwareOpusMultiStreamDecoderTypeEiiiiPKhPvmi", "hwopus service; FMOD falls back to software on 8", Result, h_codec_HwInitMulti,
          void *dec, int rate, int channels, int total, int stereo, const uint8_t *map, void *work, uint64_t size, int option) {
    (void)dec; (void)rate; (void)channels; (void)total; (void)stereo; (void)map; (void)work; (void)size; (void)option; return HADES_HWOPUS_NOT_SUPPORTED;
}
