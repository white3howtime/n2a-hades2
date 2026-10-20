/* Host-side stand-ins for the parts of dodola's headers that hades_hle.c uses (HADES_HLE_TEST builds only).
 * HLE_FORCE registers into a table at start-up so the test can look entries up by mangled name, as the loader does. */
#ifndef HADES_MOCK_ENV_H
#define HADES_MOCK_ENV_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef uint32_t Result;
#define RES_OK 0u
#define RES_FAIL 0x2A2u

typedef struct { const char *name; void *fn; const char *why; } hle_force_ent;
void mock_register(const char *name, void *fn, const char *why);
void *mock_force_lookup(const char *name);

#define HLE_FORCE(mangled, why, ret, cname, ...)                                                    \
    static ret cname(__VA_ARGS__);                                                                  \
    __attribute__((constructor)) static void reg_##cname(void) { mock_register(mangled, (void *)cname, why); } \
    static ret cname(__VA_ARGS__)

void die(const char *fmt, ...) __attribute__((noreturn));
void *guest_sym(const char *name);
ssize_t dm_data_pread(int fd, void *buf, size_t n, off_t off);

typedef struct { int64_t sampling; uint64_t buttons; int32_t lx, ly, rx, ry; uint32_t attributes, reserved; } dm_android_npad_state;
int dm_android_input_read_npad(dm_android_npad_state *out, uint32_t id, uint32_t style);
int dm_android_input_bind_style_event(uint32_t id, void *event, int autoclear);
int dm_android_platform_account_uid(uint64_t uid[2]);
int dm_android_platform_account_close(uint64_t handle[3]);
#endif
