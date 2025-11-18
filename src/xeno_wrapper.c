#include <vulkan/vulkan.h>
#include <android/log.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG_TAG "XENO_B_EASY"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static FILE* g_log_file = NULL;
static const char* LOG_PATH = "/storage/emulated/0/eden_wrapper/xeno_deep.log";

static void open_log()
{
    if (!g_log_file)
    {
        g_log_file = fopen(LOG_PATH, "a");
        if (!g_log_file)
            LOGE("Failed to open log file at %s", LOG_PATH);
        else
            LOGI("Logging to %s", LOG_PATH);
    }
}

static void log_line(const char* msg)
{
    if (!g_log_file) open_log();
    if (!g_log_file) return;

    fprintf(g_log_file, "%s\n", msg);
    fflush(g_log_file);
}

static PFN_vkCreateInstance real_vkCreateInstance = NULL;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
        const VkInstanceCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkInstance* pInstance)
{
    open_log();

    log_line("=== vkCreateInstance intercepted ===");

    if (pCreateInfo && pCreateInfo->enabledExtensionCount > 0)
    {
        log_line("Extensions requested:");
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
        {
            log_line(pCreateInfo->ppEnabledExtensionNames[i]);
        }
    }

    if (!real_vkCreateInstance)
    {
        real_vkCreateInstance =
            (PFN_vkCreateInstance)dlsym(RTLD_NEXT, "vkCreateInstance");
    }

    return real_vkCreateInstance(pCreateInfo, pAllocator, pInstance);
}

__attribute__((constructor))
static void xeno_init()
{
    open_log();
    log_line("=== XENO B-EASY WRAPPER INIT ===");
    LOGI("Xeno wrapper initialized");
}
