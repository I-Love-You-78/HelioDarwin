/* Query the actual poster providers, or explicitly register the two stock
 * providers needed for the default wallpaper. Run on the matching iOS root. */
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void
operation_timed_out(int signal_number)
{
    (void)signal_number;
    static const char message[] =
        "SweetRegisterPosters: operation timed out; last checkpoint identifies the pending boundary\n";
    /* dlopen and synchronous XPC can block before any result exists. Keep the
     * failure path independent of their locks and of stdio's internal locks. */
    (void)write(STDERR_FILENO, message, sizeof(message) - 1);
    _exit(1);
}

static void
checkpoint(const char *phase)
{
    fprintf(stderr, "SweetRegisterPosters: %s\n", phase);
    alarm(125);
}

static int
verified_image(const char *path, const unsigned char expected[16])
{
    for (uint32_t index = 0; index < _dyld_image_count(); ++index) {
        const char *name = _dyld_get_image_name(index);
        if (!name || strcmp(name, path))
            continue;
        const struct mach_header_64 *header = (const void *)_dyld_get_image_header(index);
        if (!header || header->magic != MH_MAGIC_64 || header->cputype != CPU_TYPE_ARM64 ||
            header->ncmds > 4096 || header->sizeofcmds > 16 * 1024 * 1024)
            return 0;
        const unsigned char *cursor = (const void *)(header + 1);
        size_t remaining = header->sizeofcmds;
        for (uint32_t i = 0; i < header->ncmds; ++i) {
            if (remaining < sizeof(struct load_command)) return 0;
            const struct load_command *load = (const void *)cursor;
            if (load->cmdsize < sizeof(*load) || load->cmdsize > remaining) return 0;
            if (load->cmd == LC_UUID)
                return load->cmdsize >= sizeof(struct uuid_command) &&
                    !memcmp(((const struct uuid_command *)load)->uuid, expected, 16);
            cursor += load->cmdsize;
            remaining -= load->cmdsize;
        }
    }
    return 0;
}

static int
prewarm_posters(void)
{
    static const char path[] = "/System/Library/PrivateFrameworks/PosterBoardServices.framework/PosterBoardServices";
    static const unsigned char expected[16] = {
        0x4f,0xc9,0x6a,0x43,0x1b,0x54,0x34,0x8b,
        0x9b,0x78,0x13,0x48,0x51,0x97,0xe0,0x86,
    };
    checkpoint("loading PosterBoardServices");
    if (!dlopen(path, RTLD_NOW | RTLD_GLOBAL) || !verified_image(path, expected)) {
        fprintf(stderr, "Unverified PosterBoardServices; refusing prewarm API\n");
        return 1;
    }
    checkpoint("PosterBoardServices loaded and UUID verified");
    void *(*klass)(const char *) = dlsym(RTLD_DEFAULT, "objc_getClass");
    void *(*sel)(const char *) = dlsym(RTLD_DEFAULT, "sel_registerName");
    void *(*method)(void *, void *) = dlsym(RTLD_DEFAULT, "class_getInstanceMethod");
    void *(*send)(void *, void *) = dlsym(RTLD_DEFAULT, "objc_msgSend");
    const char *(*cstring)(void *, void *) = dlsym(RTLD_DEFAULT, "objc_msgSend");
    void (*prewarm)(void *, void *, void (^)(void *)) = dlsym(RTLD_DEFAULT, "objc_msgSend");
    void *(*semaphore_create)(long) = dlsym(RTLD_DEFAULT, "dispatch_semaphore_create");
    long (*semaphore_signal)(void *) = dlsym(RTLD_DEFAULT, "dispatch_semaphore_signal");
    long (*semaphore_wait)(void *, uint64_t) = dlsym(RTLD_DEFAULT, "dispatch_semaphore_wait");
    uint64_t (*dispatch_time_fn)(uint64_t, int64_t) = dlsym(RTLD_DEFAULT, "dispatch_time");
    void (*dispatch_release_fn)(void *) = dlsym(RTLD_DEFAULT, "dispatch_release");
    if (!klass || !sel || !method || !send || !cstring || !prewarm || !semaphore_create ||
        !semaphore_signal || !semaphore_wait || !dispatch_time_fn || !dispatch_release_fn)
    {
        fprintf(stderr, "Required PosterBoardServices runtime symbols are unavailable\n");
        return 1;
    }
    void *service_class = klass("PRSService");
    if (!service_class || !method(service_class, sel("prewarmWithCompletion:"))) {
        fprintf(stderr, "PRSService prewarm selector is unavailable\n");
        return 1;
    }
    struct prewarm_state { void *semaphore; int failed; };
    struct prewarm_state *state = calloc(1, sizeof(*state));
    if (!state) return 1;
    state->semaphore = semaphore_create(0);
    if (!state->semaphore) { free(state); return 1; }
    void *service = send(send(service_class, sel("alloc")), sel("init"));
    if (!service) {
        dispatch_release_fn(state->semaphore);
        free(state);
        return 1;
    }
    checkpoint("requesting PosterBoard prewarm");
    prewarm(service, sel("prewarmWithCompletion:"), ^(void *error) {
        state->failed = error != NULL;
        if (error) {
            const char *message = cstring(send(error, sel("description")), sel("UTF8String"));
            fprintf(stderr, "PosterBoard prewarm failed: %s\n", message ? message : "NSError description unavailable");
        }
        semaphore_signal(state->semaphore);
    });
    checkpoint("waiting for PosterBoard prewarm completion");
    long timed_out = semaphore_wait(state->semaphore, dispatch_time_fn(0, 120000000000LL));
    send(service, sel("release"));
    if (timed_out) {
        /* A late XPC completion still owns these callback references. The
         * short-lived process can release them on exit, not underneath it. */
        fprintf(stderr, "PosterBoard prewarm completion timed out\n");
        return 1;
    }
    int failed = state->failed;
    dispatch_release_fn(state->semaphore);
    free(state);
    if (!failed) puts("PosterBoard prewarm completion returned without NSError");
    return failed;
}

int
main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr, "SweetRegisterPosters: main entered pid=%d uid=%u euid=%u\n",
            getpid(), (unsigned int)getuid(), (unsigned int)geteuid());
    struct sigaction timeout_action = {0};
    timeout_action.sa_handler = operation_timed_out;
    sigemptyset(&timeout_action.sa_mask);
    if (sigaction(SIGALRM, &timeout_action, NULL)) {
        perror("SweetRegisterPosters: cannot install operation timeout");
        return 1;
    }
    int registering = 0, prewarming = 0;
    for (int index = 1; index < argc; ++index) {
        if (!strcmp(argv[index], "--register")) registering = 1;
        else if (!strcmp(argv[index], "--prewarm")) prewarming = 1;
        else {
            fprintf(stderr, "Usage: SweetRegisterPosters [--register] [--prewarm]\n");
            return 2;
        }
    }
    if (geteuid() != 501) {
        fprintf(stderr, "Run as mobile (uid 501), using the same ExtensionKit database as PosterBoard\n");
        return 1;
    }
    checkpoint("loading CoreServices");
    if (!dlopen("/System/Library/Frameworks/CoreServices.framework/CoreServices", RTLD_NOW | RTLD_GLOBAL)) {
        fprintf(stderr, "Cannot load CoreServices: %s\n", dlerror());
        return 1;
    }
    checkpoint("CoreServices loaded; loading ExtensionFoundation");
    if (!dlopen("/System/Library/Frameworks/ExtensionFoundation.framework/ExtensionFoundation", RTLD_NOW | RTLD_GLOBAL)) {
        fprintf(stderr, "Cannot load system registration/query frameworks: %s\n", dlerror());
        return 1;
    }
    static const unsigned char core_uuid[16] = {
        0x8d,0x42,0x10,0x3d,0xbd,0x59,0x30,0x8d,
        0x9d,0x2c,0x20,0x82,0xb3,0x97,0x38,0xe7,
    };
    if (!verified_image("/System/Library/Frameworks/CoreServices.framework/CoreServices", core_uuid)) {
        fprintf(stderr, "Unverified CoreServices UUID; refusing private registration API\n");
        return 1;
    }
    checkpoint("CoreServices UUID verified; resolving runtime contract");
    void *(*klass)(const char *) = dlsym(RTLD_DEFAULT, "objc_getClass");
    void *(*sel)(const char *) = dlsym(RTLD_DEFAULT, "sel_registerName");
    void *(*class_method)(void *, void *) = dlsym(RTLD_DEFAULT, "class_getClassMethod");
    void *(*instance_method)(void *, void *) = dlsym(RTLD_DEFAULT, "class_getInstanceMethod");
    void *(*send0)(void *, void *) = dlsym(RTLD_DEFAULT, "objc_msgSend");
    void *(*send1)(void *, void *, void *) = dlsym(RTLD_DEFAULT, "objc_msgSend");
    unsigned long (*send_count)(void *, void *) = dlsym(RTLD_DEFAULT, "objc_msgSend");
    void *(*send_index)(void *, void *, unsigned long) = dlsym(RTLD_DEFAULT, "objc_msgSend");
    const char *(*send_cstring)(void *, void *) = dlsym(RTLD_DEFAULT, "objc_msgSend");
    unsigned char (*send_bool)(void *, void *, void *) = dlsym(RTLD_DEFAULT, "objc_msgSend");
    void *(*pool_push)(void) = dlsym(RTLD_DEFAULT, "objc_autoreleasePoolPush");
    void (*pool_pop)(void *) = dlsym(RTLD_DEFAULT, "objc_autoreleasePoolPop");
    void *(*url_create)(void *, const unsigned char *, long, unsigned char) =
        dlsym(RTLD_DEFAULT, "CFURLCreateFromFileSystemRepresentation");
    void *(*string_create)(void *, const char *, uint32_t) = dlsym(RTLD_DEFAULT, "CFStringCreateWithCString");
    void (*release)(void *) = dlsym(RTLD_DEFAULT, "CFRelease");
    if (!klass || !sel || !class_method || !instance_method || !send0 || !send1 ||
        !send_count || !send_index || !send_cstring || !send_bool || !pool_push ||
        !pool_pop || !url_create || !string_create || !release) {
        fprintf(stderr, "Required registration/query runtime symbols are unavailable\n");
        return 1;
    }
    void *workspace_class = klass("LSApplicationWorkspace");
    void *query_class = klass("_EXQuery");
    void *controller = klass("_EXQueryController");
    void *array_class = klass("NSArray");
    if (!workspace_class || !query_class || !controller || !array_class ||
        !class_method(workspace_class, sel("defaultWorkspace")) ||
        !instance_method(workspace_class, sel("registerPlugin:")) ||
        !instance_method(query_class, sel("initWithExtensionPointIdentifier:")) ||
        !class_method(controller, sel("executeQueries:")) ||
        !class_method(array_class, sel("arrayWithObject:"))) {
        fprintf(stderr, "Required registration/query selectors are unavailable\n");
        return 1;
    }
    void *pool = pool_push();
    unsigned int (^query_visible)(void) = ^{
        checkpoint("constructing ExtensionKit provider query");
        void *point = string_create(NULL, "com.apple.posterkit.provider", 0x08000100);
        void *query = point ? send1(send0(query_class, sel("alloc")),
                                   sel("initWithExtensionPointIdentifier:"), point) : NULL;
        void *queries = query ? send1(array_class, sel("arrayWithObject:"), query) : NULL;
        checkpoint("executing ExtensionKit provider query");
        void *results = queries ? send1(controller, sel("executeQueries:"), queries) : NULL;
        checkpoint("ExtensionKit provider query returned");
        unsigned long count = results ? send_count(results, sel("count")) : 0;
        printf("ExtensionKit currently returns %lu poster providers\n", count);
        unsigned int observed = 0;
        if (!results || count > 1024) observed |= 4;
        else for (unsigned long index = 0; index < count; ++index) {
            void *identity = send_index(results, sel("objectAtIndex:"), index);
            if (!send_bool(identity, sel("respondsToSelector:"), sel("bundleIdentifier"))) {
                fprintf(stderr, "Provider identity does not expose bundleIdentifier\n");
                observed |= 4;
                continue;
            }
            void *identifier = send0(identity, sel("bundleIdentifier"));
            const char *name = identifier ? send_cstring(identifier, sel("UTF8String")) : NULL;
            if (!name) { observed |= 4; continue; }
            printf("Visible provider: %s\n", name);
            if (!strcmp(name, "com.apple.PaperBoard.LegacyPoster")) observed |= 1;
            if (!strcmp(name, "com.apple.WallpaperKit.CollectionsPoster")) observed |= 2;
        }
        if (query) send0(query, sel("release"));
        if (point) release(point);
        return observed;
    };
    unsigned int visible = query_visible();
    int failed = 0;
    if (registering && !(visible & 4)) {
        static const char *paths[] = {
            "/Applications/PaperBoard.app/PlugIns/LegacyPoster.appex",
            "/System/Library/PrivateFrameworks/WallpaperKit.framework/PlugIns/CollectionsPoster.appex",
        };
        checkpoint("opening LaunchServices workspace");
        void *workspace = send0(workspace_class, sel("defaultWorkspace"));
        if (!workspace) failed = 1;
        for (unsigned int i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
            struct stat state;
            if (!workspace || stat(paths[i], &state) || !S_ISDIR(state.st_mode)) {
                fprintf(stderr, "Missing provider/workspace: %s\n", paths[i]);
                failed = 1;
                continue;
            }
            /* registerPlugin: first unregisters existing records. Do not churn
             * an already visible identity on every boot. */
            if (visible & (1u << i)) continue;
            void *url = url_create(NULL, (const unsigned char *)paths[i], (long)strlen(paths[i]), 1);
            fprintf(stderr, "SweetRegisterPosters: registering %s\n", paths[i]);
            checkpoint("waiting for LaunchServices registerPlugin result");
            int accepted = url && send_bool(workspace, sel("registerPlugin:"), url);
            if (url) release(url);
            printf("Registration %s: %s\n", accepted ? "accepted" : "failed", paths[i]);
            if (!accepted) failed = 1;
        }
        visible = query_visible();
    }
    if (visible != 3) {
        fprintf(stderr, "Required LegacyPoster/CollectionsPoster identities are not both visible\n");
        failed = 1;
    }
    if (prewarming && !failed) failed = prewarm_posters();
    checkpoint("releasing registration resources");
    pool_pop(pool);
    alarm(0);
    fprintf(stderr, "SweetRegisterPosters: completed status=%d\n", failed);
    return failed;
}
