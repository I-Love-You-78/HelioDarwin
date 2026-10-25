/*
 * Native X676C display metadata for the inspected iOS 20H392 frameworks.
 *
 * SpringBoard publishes UISDisplayContext to UIKit clients. Keep its actual
 * framebuffer configuration and artwork identity; replace the donor phone's
 * exclusion shape and portrait safe area with the native panel contract.
 * Status-bar rendering remains UIKit's ordinary non-Jindo FixedSplit path.
 */
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *Object;
typedef void *Selector;
typedef struct { double x, y; } Point;
typedef struct { double width, height; } Size;
typedef struct { Point origin; Size size; } Rect;
typedef Object (*Getter)(Object, Selector);

static Object (*get_class)(const char *);
static Selector (*selector)(const char *);
static void (*send_message)(void);
static Getter original_shape, original_device_context, original_edges;
static Getter original_context, original_thread_free_context;
static Object corner_radius_key;
static long (*artwork_subtype)(void);
static unsigned int reported_profile, reported_mismatch;
static unsigned int reported_contexts, reported_corner;
static _Thread_local int checking_profile;

static Object
object_get(Object object, const char *name)
{
    return ((Object (*)(Object, Selector))send_message)(object, selector(name));
}

static void
object_release(Object object)
{
    ((void (*)(Object, Selector))send_message)(object, selector("release"));
}

static void
object_set(Object object, const char *name, Object value)
{
    ((void (*)(Object, Selector, Object))send_message)(object, selector(name), value);
}

static double
double_get(Object object, const char *name)
{
    return ((double (*)(Object, Selector))send_message)(object, selector(name));
}

static int
near(double value, double expected)
{
    return isfinite(value) && fabs(value - expected) < 0.000001;
}

static int
verified_image(const char *expected_path, const unsigned char uuid[16])
{
    for (uint32_t index = 0; index < _dyld_image_count(); ++index) {
        const char *name = _dyld_get_image_name(index);
        if (!name || strcmp(name, expected_path))
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
            if (load->cmd == LC_UUID)
                return load->cmdsize >= sizeof(struct uuid_command) &&
                    !memcmp(((const struct uuid_command *)load)->uuid, uuid, 16);
            cursor += load->cmdsize;
            remaining -= load->cmdsize;
        }
    }
    return 0;
}

static int
verified_framework(const char *framework, const unsigned char uuid[16])
{
    char path[256];
    int length = snprintf(path, sizeof(path), "/System/Library/PrivateFrameworks/%s.framework/%s",
                          framework, framework);
    return length >= 0 && (size_t)length < sizeof(path) && verified_image(path, uuid);
}

static int
native_configuration(void)
{
    Object manager = object_get(get_class("FBDisplayManager"), "sharedInstance");
    Object configuration = object_get(manager, "mainConfiguration");
    if (!configuration ||
        !((unsigned char (*)(Object, Selector))send_message)(configuration, selector("isMainRootDisplay")))
        return 0;
    Size pixels = ((Size (*)(Object, Selector))send_message)(configuration, selector("pixelSize"));
    Rect bounds = ((Rect (*)(Object, Selector))send_message)(configuration, selector("bounds"));
    return artwork_subtype() == 2436 && near(pixels.width, 1080) && near(pixels.height, 2400) &&
        near(double_get(configuration, "scale"), 3) && near(bounds.origin.x, 0) &&
        near(bounds.origin.y, 0) && near(bounds.size.width, 360) && near(bounds.size.height, 800);
}

static int
stock_edge_contract(Object old)
{
    Object portrait = object_get(old, "safeAreaInsetsPortrait");
    Object periphery = object_get(old, "peripheryInsets");
    Object allowance = object_get(old, "homeAffordanceOverlayAllowance");
    /* Stock full-screen phone contracts include the iPhone X Display Zoom
     * screen type, whose portrait insets are 38/0/29/0. Other display classes
     * and user overrides retain their original metadata. */
    if (!portrait || !periphery || !allowance)
        return 0;
    double top = double_get(portrait, "topInset");
    double bottom = double_get(portrait, "bottomInset");
    int standard = (near(top, 44) || near(top, 47) || near(top, 50)) && near(bottom, 34);
    int zoomed = near(top, 38) && near(bottom, 29);
    if (!(standard || zoomed) ||
        !near(double_get(portrait, "leftInset"), 0) || !near(double_get(portrait, "rightInset"), 0) ||
        !near(double_get(periphery, "topInset"), 32) || !near(double_get(allowance, "doubleValue"), 21))
        return 0;
    return 1;
}

static Object
native_edge_info(Object old)
{
    if (!stock_edge_contract(old))
        return NULL;
    Object periphery = object_get(old, "peripheryInsets");
    Object allowance = object_get(old, "homeAffordanceOverlayAllowance");
    Object edge = object_get(object_get(get_class("UISApplicationSupportDisplayEdgeInfo"), "alloc"), "init");
    Object wrapper = object_get(get_class("UISApplicationSupportDisplayEdgeInsetsWrapper"), "alloc");
    wrapper = ((Object (*)(Object, Selector, double, double, double, double))send_message)(
        wrapper, selector("initWithTop:left:bottom:right:"), 47, 0, 34, 0);
    if (!edge || !wrapper) {
        object_release(edge);
        object_release(wrapper);
        return NULL;
    }
    object_set(edge, "setSafeAreaInsetsPortrait:", wrapper);
    object_release(wrapper);
    object_set(edge, "setPeripheryInsets:", periphery);
    object_set(edge, "setHomeAffordanceOverlayAllowance:", allowance);
    /* Preserve UIKit's stock nil/fallback semantics in other orientations. */
    object_set(edge, "setSafeAreaInsetsPortraitUpsideDown:", object_get(old, "safeAreaInsetsPortraitUpsideDown"));
    object_set(edge, "setSafeAreaInsetsLandscapeLeft:", object_get(old, "safeAreaInsetsLandscapeLeft"));
    object_set(edge, "setSafeAreaInsetsLandscapeRight:", object_get(old, "safeAreaInsetsLandscapeRight"));
    return object_get(edge, "autorelease");
}

static int
profile_ready(void)
{
    /* Stock screen-type discovery may ask for a device context while
     * initializing its cache. Preserve that first call instead of recursing. */
    if (checking_profile)
        return 0;
    checking_profile = 1;
    if (!native_configuration())
        goto mismatch;
    Object stock_edge = original_edges(get_class("UISApplicationSupportDisplayEdgeInfo"),
                                      selector("sb_thisDeviceDisplayEdgeInfo"));
    if (!stock_edge_contract(stock_edge))
        goto mismatch;
    if (!__atomic_exchange_n(&reported_profile, 1, __ATOMIC_RELAXED))
        fprintf(stderr, "SweetNativeDisplay: native inputs verified: 1080x2400 scale=3 points=360x800 "
                "cutout=(156,0,48,36) portrait-safe=(47,0,34,0) corner=39; renderer readback pending\n");
    checking_profile = 0;
    return 1;
mismatch:
    checking_profile = 0;
    if (!__atomic_exchange_n(&reported_mismatch, 1, __ATOMIC_RELAXED))
        fprintf(stderr, "SweetNativeDisplay: display/edge contract did not match; original context retained\n");
    return 0;
}

static Object
device_shape(Object object, Selector command)
{
    if (!profile_ready())
        return original_shape(object, command);
    Object shape = object_get(get_class("UISDisplaySingleRectShape"), "alloc");
    /* Physical cutout (468,0,144,108) pixels divided by native scale 3. */
    Rect cutout = {{156, 0}, {48, 36}};
    shape = ((Object (*)(Object, Selector, Rect))send_message)(shape, selector("initWithRect:"), cutout);
    return shape ? object_get(shape, "autorelease") : original_shape(object, command);
}

static Object
device_context(Object object, Selector command)
{
    Object context = original_device_context(object, command);
    if (!context || !profile_ready())
        return context;
    Object radius = ((Object (*)(Object, Selector, double))send_message)(
        get_class("NSNumber"), selector("numberWithDouble:"), 39);
    if (!radius)
        return context;
    /* The same producer already publishes the stock corner radius using
     * this key. The physical panel's 117-pixel radius is 39 points. */
    ((void (*)(Object, Selector, Object, Object))send_message)(
        context, selector("setDeviceInfoValue:forKey:"), radius, corner_radius_key);
    double observed = ((double (*)(Object, Selector, Object))send_message)(
        context, selector("deviceInfoFloatValueForKey:"), corner_radius_key);
    if (!__atomic_exchange_n(&reported_corner, 1, __ATOMIC_RELAXED))
        fprintf(stderr, "SweetNativeDisplay: device-context corner readback=%g expected=39 match=%d\n",
                observed, near(observed, 39));
    return context;
}

static Object
device_edge_info(Object object, Selector command)
{
    Object original = original_edges(object, command);
    if (!profile_ready())
        return original;
    Object native = native_edge_info(original);
    return native ? native : original;
}

static Object
observe_context(Object context, unsigned int bit)
{
    if (!context || (__atomic_fetch_or(&reported_contexts, bit, __ATOMIC_RELAXED) & bit))
        return context;
    Object config = object_get(context, "displayConfiguration");
    Object shape = object_get(context, "exclusionArea");
    Object portrait = object_get(object_get(context, "displayEdgeInfo"), "safeAreaInsetsPortrait");
    if (!config || !shape || !portrait) {
        fprintf(stderr, "SweetNativeDisplay: display-context readback lacks configuration/shape/portrait edge info\n");
        return context;
    }
    Size pixels = ((Size (*)(Object, Selector))send_message)(config, selector("pixelSize"));
    Rect bounds = ((Rect (*)(Object, Selector))send_message)(config, selector("bounds"));
    Rect cutout = ((Rect (*)(Object, Selector))send_message)(shape, selector("rect"));
    double scale = double_get(config, "scale");
    double top = double_get(portrait, "topInset"), left = double_get(portrait, "leftInset");
    double bottom = double_get(portrait, "bottomInset"), right = double_get(portrait, "rightInset");
    int match = near(pixels.width, 1080) && near(pixels.height, 2400) && near(scale, 3) &&
        near(bounds.origin.x, 0) && near(bounds.origin.y, 0) &&
        near(bounds.size.width, 360) && near(bounds.size.height, 800) &&
        near(cutout.origin.x, 156) && near(cutout.origin.y, 0) &&
        near(cutout.size.width, 48) && near(cutout.size.height, 36) &&
        near(top, 47) && near(left, 0) && near(bottom, 34) && near(right, 0);
    fprintf(stderr, "SweetNativeDisplay: context[%u] readback pixels=%gx%g scale=%g "
            "bounds=(%g,%g,%g,%g) cutout=(%g,%g,%g,%g) safe=(%g,%g,%g,%g) match=%d\n",
            bit, pixels.width, pixels.height, scale, bounds.origin.x, bounds.origin.y,
            bounds.size.width, bounds.size.height, cutout.origin.x, cutout.origin.y,
            cutout.size.width, cutout.size.height, top, left, bottom, right, match);
    return context;
}

static Object
embedded_context(Object object, Selector command)
{
    return observe_context(original_context(object, command), 1);
}

static Object
thread_free_context(Object object, Selector command)
{
    return observe_context(original_thread_free_context(object, command), 2);
}

__attribute__((constructor)) static void
install_native_display(void)
{
    const char *enabled = getenv("SWEET_NATIVE_DISPLAY");
    if (!enabled || strcmp(enabled, "1") || strcmp(getprogname(), "SpringBoard"))
        return;
    static const struct { const char *name; unsigned char uuid[16]; } frameworks[] = {
        {"FrontBoard", {0x0a,0xbe,0x70,0xd4,0x6c,0xc0,0x37,0xf9,0xaf,0x9e,0xca,0x2f,0xd4,0x24,0x42,0xde}},
        {"FrontBoardServices", {0x9d,0xf9,0x0f,0x7d,0x2d,0x79,0x31,0x6c,0x92,0xe4,0x13,0xea,0xd6,0xcf,0x21,0x4d}},
        {"SpringBoardFoundation", {0x79,0x49,0x27,0x9b,0xdb,0xc1,0x39,0x49,0xb8,0xef,0x90,0xe7,0xef,0x34,0xdd,0x46}},
        {"UIKitCore", {0x12,0x42,0x97,0x8a,0x2c,0x2c,0x37,0x81,0x8d,0x6c,0x97,0x77,0xed,0xce,0x28,0x04}},
        {"UIKitServices", {0xb9,0xad,0xed,0x5a,0x37,0xc0,0x30,0xd4,0xac,0x8a,0x28,0xdd,0xc2,0xf5,0x65,0x4c}},
        {"SpringBoardHome", {0xee,0x83,0x26,0x7f,0x13,0x2c,0x35,0xc6,0xaa,0x2d,0xae,0xa8,0xee,0xe0,0x35,0xa3}},
        {"SpringBoard", {0x22,0x03,0x4a,0xba,0x9a,0x9a,0x3b,0x94,0x93,0xa5,0xa0,0xde,0x02,0xbe,0xfd,0x2e}},
    };
    for (size_t i = 0; i < sizeof(frameworks) / sizeof(frameworks[0]); ++i) {
        if (!verified_framework(frameworks[i].name, frameworks[i].uuid))
            goto unsupported;
    }
    static const unsigned char flags_uuid[16] = {
        0x3a,0x16,0x0c,0x9e,0x34,0x88,0x3f,0xfd,0xa1,0x94,0xe2,0xa1,0x66,0xfe,0x11,0x69};
    if (!verified_image("/usr/lib/system/libsystem_featureflags.dylib", flags_uuid))
        goto unsupported;
    get_class = dlsym(RTLD_DEFAULT, "objc_getClass");
    selector = dlsym(RTLD_DEFAULT, "sel_registerName");
    send_message = dlsym(RTLD_DEFAULT, "objc_msgSend");
    Object (*get_method)(Object, Selector) = dlsym(RTLD_DEFAULT, "class_getClassMethod");
    Object (*get_instance_method)(Object, Selector) = dlsym(RTLD_DEFAULT, "class_getInstanceMethod");
    unsigned int (*arguments)(Object) = dlsym(RTLD_DEFAULT, "method_getNumberOfArguments");
    void (*return_type)(Object, char *, size_t) = dlsym(RTLD_DEFAULT, "method_getReturnType");
    void (*argument_type)(Object, unsigned int, char *, size_t) = dlsym(RTLD_DEFAULT, "method_getArgumentType");
    Getter (*implementation)(Object) = dlsym(RTLD_DEFAULT, "method_getImplementation");
    Getter (*replace)(Object, Getter) = dlsym(RTLD_DEFAULT, "method_setImplementation");
    unsigned char (*feature_enabled)(const char *, const char *) = dlsym(RTLD_DEFAULT, "_os_feature_enabled_impl");
    if (!get_class || !selector || !send_message || !get_method || !get_instance_method ||
        !arguments || !return_type || !argument_type || !implementation || !replace || !feature_enabled)
        goto unsupported;
    if (feature_enabled("SpringBoard", "Jindo")) {
        fprintf(stderr, "SweetNativeDisplay: Jindo policy remains enabled; native metadata adapter not installed\n");
        return;
    }
    artwork_subtype = dlsym(RTLD_DEFAULT, "SBFEffectiveArtworkSubtype");
    Object *key_pointer = dlsym(RTLD_DEFAULT, "UISDeviceContextDeviceCornerRadiusKey");
    if (!artwork_subtype || !key_pointer || !*key_pointer)
        goto unsupported;
    corner_radius_key = *key_pointer;
    static const struct {
        const char *class_name, *name, *result;
        unsigned int count, class_method;
    } required[] = {
        {"FBDisplayManager", "sharedInstance", "@", 2, 1},
        {"FBDisplayManager", "mainConfiguration", "@", 2, 0},
        {"FBSDisplayConfiguration", "isMainRootDisplay", "B", 2, 0},
        {"FBSDisplayConfiguration", "pixelSize", "{CGSize=dd}", 2, 0},
        {"FBSDisplayConfiguration", "bounds", "{CGRect={CGPoint=dd}{CGSize=dd}}", 2, 0},
        {"FBSDisplayConfiguration", "scale", "d", 2, 0},
        {"UISDisplaySingleRectShape", "initWithRect:", "@", 3, 0},
        {"UISDisplaySingleRectShape", "rect", "{CGRect={CGPoint=dd}{CGSize=dd}}", 2, 0},
        {"UISApplicationSupportDisplayEdgeInsetsWrapper", "initWithTop:left:bottom:right:", "@", 6, 0},
        {"UISApplicationSupportDisplayEdgeInsetsWrapper", "topInset", "d", 2, 0},
        {"UISApplicationSupportDisplayEdgeInsetsWrapper", "leftInset", "d", 2, 0},
        {"UISApplicationSupportDisplayEdgeInsetsWrapper", "bottomInset", "d", 2, 0},
        {"UISApplicationSupportDisplayEdgeInsetsWrapper", "rightInset", "d", 2, 0},
        {"UISMutableDeviceContext", "setDeviceInfoValue:forKey:", "v", 4, 0},
        {"UISDeviceContext", "deviceInfoFloatValueForKey:", "d", 3, 0},
        {"UISDisplayContext", "displayConfiguration", "@", 2, 0},
        {"UISDisplayContext", "displayEdgeInfo", "@", 2, 0},
        {"UISDisplayContext", "exclusionArea", "@", 2, 0},
        {"NSNumber", "numberWithDouble:", "@", 3, 1},
    };
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        Object klass = get_class(required[i].class_name);
        if (!klass)
            goto unsupported;
        Object method = (required[i].class_method ? get_method : get_instance_method)(klass, selector(required[i].name));
        char type[128] = {0};
        if (!method || arguments(method) != required[i].count)
            goto unsupported;
        return_type(method, type, sizeof(type));
        if (strcmp(type, required[i].result) && !(type[0] == 'c' && !strcmp(required[i].result, "B")))
            goto unsupported;
        if (!strcmp(required[i].name, "initWithRect:")) {
            argument_type(method, 2, type, sizeof(type));
            if (strcmp(type, "{CGRect={CGPoint=dd}{CGSize=dd}}"))
                goto unsupported;
        } else if (!strcmp(required[i].name, "initWithTop:left:bottom:right:") ||
                   !strcmp(required[i].name, "numberWithDouble:")) {
            for (unsigned int argument = 2; argument < required[i].count; ++argument) {
                argument_type(method, argument, type, sizeof(type));
                if (strcmp(type, "d"))
                    goto unsupported;
            }
        }
    }
    static const char *edge_properties[] = {"SafeAreaInsetsPortrait", "SafeAreaInsetsPortraitUpsideDown",
        "SafeAreaInsetsLandscapeLeft", "SafeAreaInsetsLandscapeRight", "PeripheryInsets", "HomeAffordanceOverlayAllowance"};
    for (size_t i = 0; i < sizeof(edge_properties) / sizeof(edge_properties[0]); ++i) {
        char name[96], type[16] = {0};
        snprintf(name, sizeof(name), "set%s:", edge_properties[i]);
        Object method = get_instance_method(get_class("UISApplicationSupportDisplayEdgeInfo"), selector(name));
        if (!method || arguments(method) != 3)
            goto unsupported;
        return_type(method, type, sizeof(type));
        if (strcmp(type, "v"))
            goto unsupported;
        snprintf(name, sizeof(name), "%c%s", edge_properties[i][0] + ('a' - 'A'), edge_properties[i] + 1);
        method = get_instance_method(get_class("UISApplicationSupportDisplayEdgeInfo"), selector(name));
        if (!method || arguments(method) != 2)
            goto unsupported;
        return_type(method, type, sizeof(type));
        if (strcmp(type, "@"))
            goto unsupported;
    }
    Object methods[] = {
        get_method(get_class("UISDisplaySingleRectShape"), selector("sb_thisDeviceDisplayShape")),
        get_method(get_class("UISDeviceContext"), selector("sb_defaultContext")),
        get_method(get_class("UISApplicationSupportDisplayEdgeInfo"), selector("sb_thisDeviceDisplayEdgeInfo")),
        get_method(get_class("UISDisplayContext"), selector("sb_embeddedDisplayDefaultContext")),
        get_method(get_class("UISDisplayContext"), selector("sb_mainThreadFree_embeddedDisplayDefaultContext")),
    };
    Getter originals[5], replacements[] = {device_shape, device_context, device_edge_info,
        embedded_context, thread_free_context};
    for (size_t i = 0; i < 5; ++i) {
        char type[16] = {0};
        if (!methods[i] || arguments(methods[i]) != 2)
            goto unsupported;
        return_type(methods[i], type, sizeof(type));
        originals[i] = implementation(methods[i]);
        if (strcmp(type, "@") || !originals[i] || originals[i] == replacements[i])
            goto unsupported;
    }
    original_shape = originals[0];
    original_device_context = originals[1];
    original_edges = originals[2];
    original_context = originals[3];
    original_thread_free_context = originals[4];
    for (size_t i = 0; i < 5; ++i) {
        Getter previous = replace(methods[i], replacements[i]);
        if (previous != originals[i]) {
            replace(methods[i], previous);
            while (i > 0) {
                --i;
                if (implementation(methods[i]) == replacements[i])
                    replace(methods[i], originals[i]);
            }
            goto unsupported;
        }
    }
    fprintf(stderr, "SweetNativeDisplay: native metadata adapter installed; first context validation pending\n");
    return;
unsupported:
    fprintf(stderr, "SweetNativeDisplay: unverified framework/runtime contract; adapter not installed\n");
}
