/*
 * iOS 16.7.16 PosterBoard readiness compatibility.
 *
 * The public getter can lazily create a switcher configuration before the
 * extension provider's first update. A later prewarm then hits the original
 * first-initialization assertion. Keep reads unavailable until that update
 * completes; the XPC handlers already turn nil into their normal NSError.
 * No state flags, configuration objects or assertions are modified here.
 */
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*Getter)(void *, void *);
static Getter original_getter;
static ptrdiff_t lock_offset;
static ptrdiff_t initialized_offset;
static void (*lock_state)(uint32_t *);
static void (*unlock_state)(uint32_t *);
static unsigned int reported_wait;
static unsigned int reported_ready;

static int
verified_framework(void)
{
    /* UUID from the 20H392 arm64 framework in the captured crash report. */
    static const unsigned char expected_uuid[16] = {
        0xfb, 0xd3, 0x2e, 0xac, 0xfb, 0xbf, 0x3f, 0x56,
        0xa0, 0xba, 0x5b, 0xd4, 0x4d, 0x2e, 0x27, 0x25,
    };
    for (uint32_t index = 0; index < _dyld_image_count(); ++index) {
        const char *name = _dyld_get_image_name(index);
        if (!name || strcmp(name, "/System/Library/PrivateFrameworks/PosterBoard.framework/PosterBoard"))
            continue;
        const struct mach_header_64 *header = (const void *)_dyld_get_image_header(index);
        if (!header || header->magic != MH_MAGIC_64 || header->cputype != CPU_TYPE_ARM64 ||
            header->ncmds > 4096 || header->sizeofcmds > 16 * 1024 * 1024)
            return 0;
        const unsigned char *cursor = (const void *)(header + 1);
        size_t remaining = header->sizeofcmds;
        for (uint32_t command = 0; command < header->ncmds; ++command) {
            if (remaining < sizeof(struct load_command))
                return 0;
            const struct load_command *load = (const void *)cursor;
            if (load->cmdsize < sizeof(*load) || load->cmdsize > remaining)
                return 0;
            if (load->cmd == LC_UUID) {
                if (load->cmdsize < sizeof(struct uuid_command))
                    return 0;
                return !memcmp(((const struct uuid_command *)load)->uuid, expected_uuid, 16);
            }
            cursor += load->cmdsize;
            remaining -= load->cmdsize;
        }
    }
    return 0;
}

static void *
ready_switcher_configuration(void *object, void *selector)
{
    unsigned char *bytes = object;
    uint32_t *lock = (uint32_t *)(bytes + lock_offset);
    lock_state(lock);
    int initialized = bytes[initialized_offset] != 0;
    unlock_state(lock);
    if (!initialized) {
        if (!__atomic_exchange_n(&reported_wait, 1, __ATOMIC_RELAXED))
            fprintf(stderr, "SweetPosterBoard: configuration read deferred until initial extensions load\n");
        return NULL;
    }
    if (!__atomic_exchange_n(&reported_ready, 1, __ATOMIC_RELAXED))
        fprintf(stderr, "SweetPosterBoard: initial extensions loaded; forwarding configuration reads\n");
    return original_getter(object, selector);
}

__attribute__((constructor)) static void
install_readiness_guard(void)
{
    const char *enabled = getenv("SWEET_POSTERBOARD_READINESS");
    if (strcmp(getprogname(), "PosterBoard"))
        return;
    fprintf(stderr, "SweetPosterBoard: constructor entered; readiness opt-in=%s\n",
            enabled ? enabled : "unset");
    if (!enabled || strcmp(enabled, "1"))
        return;
    if (!verified_framework()) {
        fprintf(stderr, "SweetPosterBoard: unverified framework UUID; readiness guard not installed\n");
        return;
    }

    void *(*get_class)(const char *) = dlsym(RTLD_DEFAULT, "objc_getClass");
    void *(*get_ivar)(void *, const char *) = dlsym(RTLD_DEFAULT, "class_getInstanceVariable");
    ptrdiff_t (*get_offset)(void *) = dlsym(RTLD_DEFAULT, "ivar_getOffset");
    const char *(*get_type)(void *) = dlsym(RTLD_DEFAULT, "ivar_getTypeEncoding");
    size_t (*get_size)(void *) = dlsym(RTLD_DEFAULT, "class_getInstanceSize");
    void *(*get_selector)(const char *) = dlsym(RTLD_DEFAULT, "sel_registerName");
    void *(*get_method)(void *, void *) = dlsym(RTLD_DEFAULT, "class_getInstanceMethod");
    unsigned int (*get_arguments)(void *) = dlsym(RTLD_DEFAULT, "method_getNumberOfArguments");
    void (*get_return_type)(void *, char *, size_t) = dlsym(RTLD_DEFAULT, "method_getReturnType");
    Getter (*get_implementation)(void *) = dlsym(RTLD_DEFAULT, "method_getImplementation");
    Getter (*set_implementation)(void *, Getter) = dlsym(RTLD_DEFAULT, "method_setImplementation");
    lock_state = dlsym(RTLD_DEFAULT, "os_unfair_lock_lock");
    unlock_state = dlsym(RTLD_DEFAULT, "os_unfair_lock_unlock");
    if (!get_class || !get_ivar || !get_offset || !get_type || !get_size ||
        !get_selector || !get_method || !get_arguments || !get_return_type ||
        !get_implementation || !set_implementation || !lock_state || !unlock_state)
        goto unsupported;

    void *klass = get_class("PBFPosterExtensionDataStore");
    if (!klass)
        goto unsupported;
    void *lock_ivar = get_ivar(klass, "_stateLock");
    void *initialized_ivar = get_ivar(klass, "_stateLock_initialExtensionsLoaded");
    void *method = get_method(klass, get_selector("switcherConfiguration"));
    if (!lock_ivar || !initialized_ivar || !method || get_arguments(method) != 2)
        goto unsupported;
    const char *lock_type = get_type(lock_ivar);
    const char *initialized_type = get_type(initialized_ivar);
    char return_type[16] = {0};
    get_return_type(method, return_type, sizeof(return_type));
    if (!lock_type || !strstr(lock_type, "os_unfair_lock") ||
        !initialized_type || strcmp(initialized_type, "B") || strcmp(return_type, "@"))
        goto unsupported;

    lock_offset = get_offset(lock_ivar);
    initialized_offset = get_offset(initialized_ivar);
    size_t size = get_size(klass);
    if (lock_offset < (ptrdiff_t)sizeof(void *) || lock_offset % sizeof(uint32_t) ||
        (size_t)lock_offset + sizeof(uint32_t) > size ||
        initialized_offset < (ptrdiff_t)sizeof(void *) || (size_t)initialized_offset >= size ||
        (initialized_offset >= lock_offset && initialized_offset < lock_offset + (ptrdiff_t)sizeof(uint32_t)))
        goto unsupported;
    original_getter = get_implementation(method);
    if (!original_getter || original_getter == ready_switcher_configuration)
        goto unsupported;
    original_getter = set_implementation(method, ready_switcher_configuration);
    fprintf(stderr, "SweetPosterBoard: initial-extension readiness guard installed\n");
    return;

unsupported:
    fprintf(stderr, "SweetPosterBoard: unverified runtime contract; readiness guard not installed\n");
}
