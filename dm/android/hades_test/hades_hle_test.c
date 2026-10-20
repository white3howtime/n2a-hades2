/* Host test for hades_hle.c.
 *   python -m ziglang cc -std=gnu11 -Wall -Wextra -DHADES_HLE_TEST -o hades_hle_test hades_hle.c hades_test/hades_hle_test.c
 *   ./hades_hle_test <dm/android/hades_test dir>
 * Behaviour is checked against mocks; registry checks use name lists taken from the real Hades II binaries and dodola's sources. */
#include "mock_env.h"
#include <stdarg.h>
#include <setjmp.h>
#include <errno.h>

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---------- registry ---------- */
#define MAXREG 256
static struct { const char *name, *why; void *fn; } reg[MAXREG];
static int nreg;
void mock_register(const char *name, void *fn, const char *why) {
    if (nreg == MAXREG) { puts("registry full"); exit(2); }
    reg[nreg].name = name; reg[nreg].fn = fn; reg[nreg].why = why; nreg++;
}
void *mock_force_lookup(const char *name) { for (int i = 0; i < nreg; i++) if (!strcmp(reg[i].name, name)) return reg[i].fn; return NULL; }
static void *F(const char *name) { void *p = mock_force_lookup(name); if (!p) { printf("FAIL no such entry: %s\n", name); exit(1); } return p; }

/* ---------- mocks ---------- */
static jmp_buf die_jmp; static int die_armed, die_hit;
void die(const char *fmt, ...) { (void)fmt; die_hit = 1; if (die_armed) longjmp(die_jmp, 1); puts("unexpected die"); exit(2); }
#define EXPECT_DIE(stmt) do { die_hit = 0; die_armed = 1; if (!setjmp(die_jmp)) { stmt; } die_armed = 0; CHECK(die_hit); } while (0)

static const uint8_t *file_data; static size_t file_len; static int pread_fail, pread_calls, pread_chunk = 1 << 30;
ssize_t dm_data_pread(int fd, void *buf, size_t n, off_t off) {
    (void)fd; pread_calls++;
    if (pread_fail) return -1;
    if ((size_t)off >= file_len) return 0;
    size_t k = file_len - (size_t)off; if (k > n) k = n; if (k > (size_t)pread_chunk) k = (size_t)pread_chunk;
    memcpy(buf, file_data + off, k); return (ssize_t)k;
}
static uint32_t npad_id, npad_style; static int npad_err, bind_id = -1, bind_auto = -1; static void *bind_ev;
int dm_android_input_read_npad(dm_android_npad_state *o, uint32_t id, uint32_t style) {
    npad_id = id; npad_style = style; if (!npad_err) { memset(o, 0, sizeof *o); o->buttons = 0x55; } return npad_err;
}
int dm_android_input_bind_style_event(uint32_t id, void *ev, int autoclear) { bind_id = (int)id; bind_ev = ev; bind_auto = autoclear; return 0; }
static int acc_fail, acc_closed; static uint64_t acc_closed_h[3];
int dm_android_platform_account_uid(uint64_t uid[2]) { if (acc_fail) return 1; uid[0] = 0x1111; uid[1] = 0x2222; return 0; }
int dm_android_platform_account_close(uint64_t h[3]) { acc_closed++; memcpy(acc_closed_h, h, sizeof acc_closed_h); return 0; }
static void *notif_event = (void *)0x1234;
void *dm_android_oe_notification_event(void) { return notif_event; }
static uint64_t program_id; static int allow_unused;
void dm_android_svc_set_program_id(uint64_t id) { program_id = id; }
void dm_android_loader_probe_allow_unused_force(void) { allow_unused = 1; }

/* ---------- name lists ---------- */
static char **load_lines(const char *dir, const char *file, int *n) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, file);
    FILE *f = fopen(path, "rb"); if (!f) { printf("cannot open %s\n", path); exit(2); }
    char **v = NULL; int cnt = 0; char line[4096];
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line); while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!l) continue;
        v = realloc(v, (size_t)(cnt + 1) * sizeof *v); v[cnt++] = strdup(line);
    }
    fclose(f); *n = cnt; return v;
}
static int has(char **v, int n, const char *s) { for (int i = 0; i < n; i++) if (!strcmp(v[i], s)) return 1; return 0; }

static void test_registry(const char *dir) {
    int nrel, ndod, nimp;
    char **rel = load_lines(dir, "refs_reloc_names.txt", &nrel);
    char **dod = load_lines(dir, "refs_dodola_names.txt", &ndod);
    char **imp = load_lines(dir, "refs_main_imports.txt", &nimp);
    int own = 0;
    for (int i = 0; i < nreg; i++) {
        if (!strcmp(reg[i].why, "test stand-in")) continue;
        own++;
        CHECK(reg[i].why && reg[i].why[0]);                          /* every takeover states its reason */
        CHECK(has(imp, nimp, reg[i].name));                          /* Hades II's main imports it */
        CHECK(has(rel, nrel, reg[i].name));                          /* some module relocates against it (else the loader reports an unreferenced entry) */
        CHECK(!has(dod, ndod, reg[i].name));                         /* dodola does not already own it (duplicates abort the loader) */
        for (int j = i + 1; j < nreg; j++) CHECK(strcmp(reg[i].name, reg[j].name));   /* unique within this file */
    }
    CHECK(own > 40);
    /* things that must stay with the SDK */
    CHECK(!mock_force_lookup("_ZN2nn6socket9InetHtonlEj"));
    CHECK(!mock_force_lookup("_ZN2nn2ro12ProtectRelroEPKvS2_S2_S2_S2_"));
    CHECK(!mock_force_lookup("_ZN2nn2fs22GetDirectoryEntryCountEPlNS0_15DirectoryHandleE"));
}

/* ---------- behaviour ---------- */
static void test_setup(void) { CHECK(program_id == 0x0100A00019DE0000ull); CHECK(allow_unused == 1); }

static void test_ro(void) {
    Result (*init)(void) = F("_ZN2nn2ro10InitializeEv");
    Result (*reg_)(void *, const void *) = F("_ZN2nn2ro18RegisterModuleInfoEPNS0_16RegistrationInfoEPKv");
    Result (*gbs)(uint64_t *, const void *) = F("_ZN2nn2ro13GetBufferSizeEPmPKv");
    Result (*load)(void *, const void *, void *, uint64_t, int, int) = F("_ZN2nn2ro10LoadModuleEPNS0_6ModuleEPKvPvmib");
    void (*unreg)(void *) = F("_ZN2nn2ro20UnregisterModuleInfoEPNS0_16RegistrationInfoE");
    void (*fin)(void) = F("_ZN2nn2ro8FinalizeEv");
    CHECK(init() == RES_OK); CHECK(reg_(NULL, NULL) == RES_OK); unreg(NULL); fin();
    uint64_t sz = 77; CHECK(gbs(&sz, NULL) != RES_OK && sz == 0); CHECK(gbs(NULL, NULL) != RES_OK);
    CHECK(load(NULL, NULL, NULL, 0, 0, 0) != RES_OK);
}

static void test_fs(void) {
    Result (*rd)(uint64_t, int64_t, void *, uint64_t) = F("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm");
    static uint8_t data[10000]; for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 7 + 3);
    file_data = data; file_len = sizeof data; pread_fail = 0; pread_chunk = 1 << 30;
    uint8_t buf[10000];
    CHECK(rd(5, 0, buf, 10000) == RES_OK && !memcmp(buf, data, 10000));
    CHECK(rd(5, 123, buf, 400) == RES_OK && !memcmp(buf, data + 123, 400));
    CHECK(rd(5, 9999, buf, 1) == RES_OK && buf[0] == data[9999]);
    CHECK(rd(5, 5, buf, 0) == RES_OK);                                   /* zero bytes: nothing to read */
    pread_calls = 0; pread_chunk = 64;                                   /* short reads are looped until complete */
    CHECK(rd(5, 1000, buf, 1000) == RES_OK && !memcmp(buf, data + 1000, 1000) && pread_calls == 16);
    pread_chunk = 1 << 30;
    CHECK(rd(5, 9990, buf, 11) == ((3005u << 9) | 2u));                  /* one byte past the end: ResultOutOfRange */
    CHECK(rd(5, 10000, buf, 1) == ((3005u << 9) | 2u));
    CHECK(rd(5, 20000, buf, 1) == ((3005u << 9) | 2u));
    pread_fail = 1; CHECK(rd(5, 0, buf, 8) == RES_FAIL); pread_fail = 0;
    /* the HLE'd ReadFile is called by the game with the fd as the handle: it must reach pread with that fd untouched */
    void (*cache)(void *, uint64_t) = F("_ZN2nn2fs32EnableDefaultGlobalFileDataCacheEPvm"); cache(NULL, 0x2000000);
    Result (*create)(int, int64_t, int64_t) = F("_ZN2nn2fs18CreateCacheStorageEill"); CHECK(create(0, 4 << 20, 0) == RES_OK);
    Result (*mount)(const char *, int) = F("_ZN2nn2fs17MountCacheStorageEPKci"); CHECK(mount("steamcache", 0) == RES_OK);
    Result (*cmax)(int *, int64_t *) = F("_ZN2nn2fs18GetCacheStorageMaxEPiPl"); int im = 9; int64_t sm = 9; CHECK(cmax(&im, &sm) == RES_OK && im == 0 && sm == 0);
    Result (*open)(uint64_t *) = F("_ZN2nn2fs20OpenCacheStorageListEPNS0_22CacheStorageListHandleE"); uint64_t h = 9; CHECK(open(&h) == RES_OK && h == 0);
    Result (*rl)(int *, void *, uint64_t, int) = F("_ZN2nn2fs20ReadCacheStorageListEPiPNS0_16CacheStorageInfoENS0_22CacheStorageListHandleEi"); int c = 9; CHECK(rl(&c, NULL, 0, 8) == RES_OK && c == 0);
    void (*cl)(uint64_t) = F("_ZN2nn2fs21CloseCacheStorageListENS0_22CacheStorageListHandleE"); cl(0);
}

static void test_oe(void) {
    void (*boost)(int) = F("_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE"); boost(1);
    void *(*ev)(void) = F("_ZN2nn2oe27GetNotificationMessageEventEv");
    CHECK(ev() == notif_event); notif_event = (void *)0x9999; CHECK(ev() == (void *)0x9999);
}

static void test_hid(void) {
    void (*joy)(dm_android_npad_state *, const uint32_t *) = F("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj");
    dm_android_npad_state st; uint32_t id = 3;
    npad_err = 0; joy(&st, &id); CHECK(npad_id == 3 && npad_style == 1 && st.buttons == 0x55);
    npad_err = 5; EXPECT_DIE(joy(&st, &id)); npad_err = 0;
    EXPECT_DIE(joy(&st, NULL));
    void (*bind)(const uint32_t *, void *, int) = F("_ZN2nn3hid27BindNpadStyleSetUpdateEventERKjPNS_2os15SystemEventTypeENS3_14EventClearModeE");
    int ev; id = 0x20; bind(&id, &ev, 1); CHECK(bind_id == 0x20 && bind_ev == &ev && bind_auto == 1);
    bind(&id, &ev, 0); CHECK(bind_auto == 0);
    EXPECT_DIE(bind(NULL, &ev, 0));
    void (*hold)(int) = F("_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE"); hold(1);
    Result (*show)(int32_t *, const void *) = F("_ZN2nn3hid21ShowControllerSupportEPNS0_27ControllerSupportResultInfoERKNS0_20ControllerSupportArgE");
    int32_t ri[2] = {-1, -1}; CHECK(show(ri, NULL) == RES_OK && ri[0] == 1 && ri[1] == 0); CHECK(show(NULL, NULL) == RES_OK);
    Result (*vib)(void *, const void *) = F("_ZN2nn3hid22GetVibrationDeviceInfoEPNS0_19VibrationDeviceInfoERKNS0_21VibrationDeviceHandleE"); CHECK(vib(NULL, NULL) != RES_OK);
}

static void test_account(void) {
    typedef struct { uint64_t v[2]; } U; typedef struct { uint64_t v[3]; } H;
    Result (*all)(int *, U *, int) = F("_ZN2nn7account12ListAllUsersEPiPNS0_3UidEi");
    U u[2]; int n = -1; acc_fail = 0;
    CHECK(all(&n, u, 2) == RES_OK && n == 1 && u[0].v[0] == 0x1111 && u[0].v[1] == 0x2222);
    n = -1; CHECK(all(&n, u, 1) == RES_OK && n == 1);
    n = -1; CHECK(all(&n, u, 0) == RES_OK && n == 0);                       /* no room: report none */
    n = -1; CHECK(all(&n, NULL, 4) == RES_OK && n == 0);
    acc_fail = 1; EXPECT_DIE(all(&n, u, 1)); acc_fail = 0;
    Result (*last)(U *) = F("_ZN2nn7account17GetLastOpenedUserEPNS0_3UidE");
    U l = {{0, 0}}; CHECK(last(&l) == RES_OK && l.v[0] == 0x1111);
    acc_fail = 1; EXPECT_DIE(last(&l)); acc_fail = 0;
    Result (*nsa)(uint8_t *, const H *) = F("_ZN2nn7account32IsNetworkServiceAccountAvailableEPbRKNS0_10UserHandleE");
    uint8_t b = 1; H h = {{1, 2, 3}}; CHECK(nsa(&b, &h) == RES_OK && b == 0);
    void (*close)(const H *) = F("_ZN2nn7account9CloseUserERKNS0_10UserHandleE");
    acc_closed = 0; close(&h); CHECK(acc_closed == 1 && acc_closed_h[0] == 1 && acc_closed_h[2] == 3);
}

static const char *open_name; static const uint8_t *open_param; static void *open_out, *open_ev; static int open_calls;
static Result fake_open_audio_out(void *out, void *ev, const char *name, const uint8_t *param) { open_out = out; open_ev = ev; open_name = name; open_param = param; open_calls++; return RES_OK; }
/* the real OpenAudioOut is registered by audio_aaudio.inc; register a stand-in under its name */
__attribute__((constructor)) static void reg_fake_open(void) { mock_register("_ZN2nn5audio12OpenAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventEPKcRKNS0_17AudioOutParameterE", (void *)fake_open_audio_out, "test stand-in"); }

static void test_audio(void) {
    Result (*def)(void *, void *, const uint8_t *) = F("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventERKNS0_17AudioOutParameterE");
    uint8_t param[8] = {1}; int out, ev;
    CHECK(def(&out, &ev, param) == RES_OK); CHECK(open_calls == 1 && open_out == &out && open_ev == &ev && open_param == param && !strcmp(open_name, "DeviceOut"));
    CHECK(def(&out, &ev, param) == RES_OK && open_calls == 2);
    int (*list)(void *, int) = F("_ZN2nn5audio12ListAudioInsEPNS0_11AudioInInfoEi"); CHECK(list(NULL, 4) == 0);
    Result (*openin)(void *, const char *, const void *) = F("_ZN2nn5audio11OpenAudioInEPNS0_7AudioInEPKcRKNS0_16AudioInParameterE"); CHECK(openin(NULL, "x", NULL) != RES_OK);
    Result (*start)(void *) = F("_ZN2nn5audio12StartAudioInEPNS0_7AudioInE"); CHECK(start(NULL) != RES_OK);
    void (*stop)(void *) = F("_ZN2nn5audio11StopAudioInEPNS0_7AudioInE"); stop(NULL);
    void (*cl)(void *) = F("_ZN2nn5audio12CloseAudioInEPNS0_7AudioInE"); cl(NULL);
    unsigned (*app)(void *, void *) = F("_ZN2nn5audio19AppendAudioInBufferEPNS0_7AudioInEPNS0_13AudioInBufferE"); CHECK(app(NULL, NULL) == 0);
    void *(*rel)(void *) = F("_ZN2nn5audio24GetReleasedAudioInBufferEPNS0_7AudioInE"); CHECK(rel(NULL) == NULL);
}

static void test_net_misc(void) {
    void (*sub)(void) = F("_ZN2nn4nifm27SubmitNetworkRequestAndWaitEv"); sub();
    int (*sd)(int, int) = F("_ZN2nn6socket8ShutdownEiNS0_14ShutdownMethodE"); CHECK(sd(3, 2) == -1);
    const char *ssl[] = { "_ZN2nn3ssl7Context15ImportServerPkiEPmPKcjNS0_17CertificateFormatE", "_ZN2nn3ssl7Context15ImportClientPkiEPmPKcS4_jj",
        "_ZN2nn3ssl7Context9ImportCrlEPmPKcj", "_ZN2nn3ssl10Connection19SetServerCertBufferEPcj", "_ZN2nn3ssl10Connection19GetVerifyCertErrorsEPNS_6ResultEPjS4_j" };
    Result (*f5)(void *, void *, void *, void *, unsigned);
    for (unsigned i = 0; i < sizeof ssl / sizeof *ssl; i++) { f5 = F(ssl[i]); CHECK(f5(NULL, NULL, NULL, NULL, 0) == RES_FAIL); }
    Result (*web)(void *, const void *) = F("_ZN2nn3web11ShowWebPageEPNS0_18WebPageReturnValueERKNS0_14ShowWebPageArgE"); CHECK(web(NULL, NULL) != RES_OK);
}

static void test_codec(void) {
    Result (*hw)(void *, int, int, void *, uint64_t, int) = F("_ZN2nn5codec29InitializeHardwareOpusDecoderEPNS0_23HardwareOpusDecoderTypeEiiPvmi");
    CHECK(hw(NULL, 48000, 2, NULL, 0, 0) != RES_OK);
    Result (*hm)(void *, int, int, int, int, const uint8_t *, void *, uint64_t, int) = F("_ZN2nn5codec40InitializeHardwareOpusMultiStreamDecoderEPNS0_34HardwareOpusMultiStreamDecoderTypeEiiiiPKhPvmi");
    CHECK(hm(NULL, 48000, 6, 4, 2, NULL, NULL, 0, 0) != RES_OK);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    setvbuf(stdout, NULL, _IONBF, 0);
    test_registry(dir); test_setup(); test_ro(); test_fs(); test_oe(); test_hid(); test_account(); test_audio(); test_net_misc(); test_codec();
    printf("%d registered entries, %d checks, %d failures\n", nreg - 1 /* test stand-in */, checks, failures);
    return failures ? 1 : 0;
}
