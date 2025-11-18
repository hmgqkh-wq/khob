#define _GNU_SOURCE
#include "xeno_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
  B-EASY deep collector
  - On load spawns a background thread which:
    * resolves a safe external files directory (prefers Eden app-dir paths)
    * creates eden_wrapper/logs and eden_wrapper/events
    * writes multiple deep probes:
       - /proc/cpuinfo, uname
       - ps snapshot
       - sysfs and candidate GPU files
       - attempts to dlopen libvulkan.so and call loader functions to enumerate physical devices,
         device properties, device extensions and features (if available)
    * writes JSON summary file and textual logs.

  Build: Android NDK (arm64-v8a). Output shared lib name must match meta.json: libvulkan.xeno_wrapper.so
*/

static const char *preferred_paths[] = {
    "/sdcard/eden_wrapper",
    "/storage/emulated/0/eden_wrapper",
    "/storage/emulated/0/Android/data/dev.eden.eden_emulator/files",
    "/data/local/tmp/eden_wrapper",
    NULL
};

static char base_path[1024] = {0};
static FILE *global_log = NULL;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* -------------------- utilities -------------------- */

static void mkdir_p(const char *path) {
    if (!path) return;
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp));
    tmp[sizeof(tmp)-1] = 0;
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0775);
            *p = '/';
        }
    }
    mkdir(tmp, 0775);
}

static const char* resolve_base_path() {
    if (base_path[0]) return base_path;
    for (int i = 0; preferred_paths[i]; ++i) {
        struct stat st;
        if (stat(preferred_paths[i], &st) == 0) {
            strncpy(base_path, preferred_paths[i], sizeof(base_path)-1);
            return base_path;
        }
    }
    /* fallback: try to create first preferred */
    strncpy(base_path, preferred_paths[0], sizeof(base_path)-1);
    mkdir_p(base_path);
    return base_path;
}

static FILE* open_log_file(const char *rel, const char *mode) {
    const char *base = resolve_base_path();
    static char path[2048];
    snprintf(path, sizeof(path), "%s/%s", base, rel);
    char dir[2048];
    strncpy(dir, path, sizeof(dir));
    char *s = strrchr(dir, '/');
    if (s) {
        *s = 0;
        mkdir_p(dir);
    }
    FILE *f = fopen(path, mode);
    return f;
}

static void logf(const char *fmt, ...) {
    pthread_mutex_lock(&log_lock);
    if (!global_log) {
        global_log = open_log_file("logs/eden_log.txt", "a");
    }
    if (!global_log) {
        pthread_mutex_unlock(&log_lock);
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    time_t t = time(NULL);
    char ts[64];
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(global_log, "[%s] ", ts);
    vfprintf(global_log, fmt, ap);
    fprintf(global_log, "\n");
    fflush(global_log);
    va_end(ap);
    pthread_mutex_unlock(&log_lock);
}

/* -------------------- lightweight Vulkan typedefs (avoid needing headers) --------------------
   Define only the fields we need for VkPhysicalDeviceProperties and VkExtensionProperties
   (This matches the standard Vulkan layout for those fields.)
*/

typedef uint32_t VkBool32;
typedef uint32_t VkResult;
typedef void* VkInstance;
typedef void* VkPhysicalDevice;

typedef struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
} VkPhysicalDeviceProperties;

typedef struct VkExtensionProperties {
    char extensionName[256];
    uint32_t specVersion;
} VkExtensionProperties;

/* function pointer types */
typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void (*PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, VkPhysicalDeviceProperties);
typedef VkResult (*PFN_vkEnumerateDeviceExtensionProperties)(VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*);
typedef void (*PFN_vkGetPhysicalDeviceFeatures)(VkPhysicalDevice, void*);

/* -------------------- deep probes -------------------- */

static void probe_system_info(FILE *out) {
    char buf[4096];
    // uname
    FILE *p = popen("uname -a 2>/dev/null", "r");
    if (p) {
        if (fgets(buf, sizeof(buf), p)) fprintf(out, "UNAME: %s\n", buf);
        pclose(p);
    }
    // /proc/cpuinfo
    FILE *c = fopen("/proc/cpuinfo", "r");
    if (c) {
        fprintf(out, "---- /proc/cpuinfo ----\n");
        while (fgets(buf, sizeof(buf), c)) fputs(buf, out);
        fprintf(out, "---- end cpuinfo ----\n");
        fclose(c);
    } else {
        fprintf(out, "cannot open /proc/cpuinfo: %s\n", strerror(errno));
    }
    // PS snapshot
    p = popen("ps -A -o pid,cmd --sort=-pid 2>/dev/null | head -n 200", "r");
    if (p) {
        fprintf(out, "---- ps snapshot ----\n");
        while (fgets(buf, sizeof(buf), p)) fputs(buf, out);
        fprintf(out, "---- end ps ----\n");
        pclose(p);
    }
}

static void probe_sysfs_candidates(FILE *out) {
    const char *candidates[] = {
        "/sys/class/kgsl/kgsl-3d0/gpu_model",
        "/sys/class/kgsl/kgsl-3d0/gpu_version",
        "/sys/class/kgsl/kgsl-3d0/gpu_pwrlevel",
        "/sys/class/drm/card0/device/uevent",
        "/sys/class/drm/card0/device/vendor",
        "/sys/class/devfreq",
        NULL
    };
    char cmd[1024];
    char buf[4096];
    for (int i=0; candidates[i]; ++i) {
        fprintf(out, "---- probe: %s ----\n", candidates[i]);
        snprintf(cmd, sizeof(cmd),
                 "for f in %s 2>/dev/null; do echo 'FILE:' $f; if [ -f \"$f\" ]; then cat \"$f\"; else ls -la \"$f\" 2>/dev/null; fi; echo '---'; done", candidates[i]);
        FILE *p = popen(cmd, "r");
        if (!p) { fprintf(out, "probe popen failed\n"); continue; }
        while (fgets(buf, sizeof(buf), p)) fputs(buf, out);
        pclose(p);
    }
}

/* Attempt to load libvulkan.so and perform Vulkan queries (defensive) */
static void probe_vulkan_loader(FILE *out) {
    logf("Attempting to dlopen libvulkan.so");
    void *h = dlopen("libvulkan.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!h) h = dlopen("libvulkan.so", RTLD_LAZY);
    if (!h) {
        fprintf(out, "dlopen(libvulkan.so) failed: %s\n", dlerror());
        return;
    }
    fprintf(out, "dlopen(libvulkan.so) -> %p\n", h);

    PFN_vkEnumeratePhysicalDevices fp_enum = (PFN_vkEnumeratePhysicalDevices)dlsym(h, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties fp_props = (PFN_vkGetPhysicalDeviceProperties)dlsym(h, "vkGetPhysicalDeviceProperties");
    PFN_vkEnumerateDeviceExtensionProperties fp_exts = (PFN_vkEnumerateDeviceExtensionProperties)dlsym(h, "vkEnumerateDeviceExtensionProperties");
    PFN_vkGetPhysicalDeviceFeatures fp_feats = (PFN_vkGetPhysicalDeviceFeatures)dlsym(h, "vkGetPhysicalDeviceFeatures");

    if (!fp_enum) {
        fprintf(out, "vkEnumeratePhysicalDevices not found\n");
        dlclose(h);
        return;
    }

    uint32_t count = 0;
    VkResult r = fp_enum(NULL, &count, NULL);
    fprintf(out, "vkEnumeratePhysicalDevices -> count=%u result=%d\n", count, (int)r);
    if (count == 0) { dlclose(h); return; }

    VkPhysicalDevice *arr = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice)*count);
    r = fp_enum(NULL, &count, arr);
    fprintf(out, "vkEnumeratePhysicalDevices second call -> result=%d\n", (int)r);

    for (uint32_t i=0;i<count;i++) {
        fprintf(out, "=== PHYSICAL DEVICE %u ===\n", i);
        if (fp_props) {
            VkPhysicalDeviceProperties p;
            memset(&p,0,sizeof(p));
            fp_props(arr[i], &p);
            fprintf(out, "apiVersion=%u driverVersion=%u vendorID=0x%04x deviceID=0x%04x\n", p.apiVersion, p.driverVersion, p.vendorID, p.deviceID);
            fprintf(out, "deviceName=%s\n", p.deviceName);
        } else {
            fprintf(out, "vkGetPhysicalDeviceProperties not available\n");
        }

        // extensions
        if (fp_exts) {
            uint32_t ec = 0;
            fp_exts(arr[i], NULL, &ec, NULL);
            fprintf(out, "device ext count=%u\n", ec);
            if (ec > 0) {
                VkExtensionProperties *exts = malloc(sizeof(VkExtensionProperties)*ec);
                fp_exts(arr[i], NULL, &ec, exts);
                for (uint32_t e=0;e<ec;e++) {
                    fprintf(out, "EXT: %s (spec=%u)\n", exts[e].extensionName, exts[e].specVersion);
                }
                free(exts);
            }
        } else {
            fprintf(out, "vkEnumerateDeviceExtensionProperties not available\n");
        }

        // features
        if (fp_feats) {
            // Void* target since we didn't define VkPhysicalDeviceFeatures fields precisely
            char featsbuf[512];
            memset(featsbuf,0,sizeof(featsbuf));
            fp_feats(arr[i], featsbuf);
            fprintf(out, "vkGetPhysicalDeviceFeatures returned raw blob (first 64 bytes):\n");
            for (int b=0;b<64 && b< (int)sizeof(featsbuf); ++b)
                fprintf(out, "%02x ", (unsigned char)featsbuf[b]);
            fprintf(out, "\n");
        } else {
            fprintf(out, "vkGetPhysicalDeviceFeatures not available\n");
        }
    }
    free(arr);
    dlclose(h);
}

/* full feature dump called from init and xeno_force_dump */
static void run_deep_dump(FILE *out) {
    if (!out) return;
    probe_system_info(out);
    probe_sysfs_candidates(out);
    probe_vulkan_loader(out);
    /* create a JSON summary file for the final wrapper design */
    char jsonpath[1024];
    const char *base = resolve_base_path();
    snprintf(jsonpath, sizeof(jsonpath), "%s/events/xeno_feature_summary.json", base);
    FILE *j = fopen(jsonpath, "w");
    if (j) {
        fprintf(j, "{\n  \"collected_at\": %ld,\n  \"pid\": %d,\n  \"note\": \"B-easy deep dump collected\"\n}\n", (long)time(NULL), getpid());
        fclose(j);
    }
}

/* background starter thread */
static void *starter_thread(void *arg) {
    (void)arg;
    const char *base = resolve_base_path();
    mkdir_p(base);
    mkdir_p((char[]){0}); // harmless noop to satisfy some compilers if needed
    /* ensure events and logs */
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s/logs", base); mkdir_p(tmp);
    snprintf(tmp, sizeof(tmp), "%s/events", base); mkdir_p(tmp);

    FILE *out = open_log_file("logs/init_probe.txt", "a");
    if (!out) return NULL;
    logf("starter_thread: beginning deep B-easy probe");
    run_deep_dump(out);
    fprintf(out, "probe finished\n");
    fclose(out);
    return NULL;
}

/* exported entrypoint that Eden may call */
void xeno_init(void) {
    /* ensure one-time startup */
    pthread_t t;
    pthread_create(&t, NULL, starter_thread, NULL);
    pthread_detach(t);
}

/* destructor flushes log */
__attribute__((destructor))
static void xeno_fini(void) {
    if (global_log) {
        logf("xeno_fini: shutting down");
        fclose(global_log);
        global_log = NULL;
    }
}

/* externally callable force dump */
void xeno_force_dump(void) {
    FILE *out = open_log_file("logs/forced_dump.txt", "a");
    if (!out) return;
    run_deep_dump(out);
    fclose(out);
}

void xeno_flush(void) {
    if (global_log) fflush(global_log);
}
