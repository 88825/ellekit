
// This file is licensed under the BSD-3 Clause License
// Copyright 2022 © Charlotte Belanger

#include "injector.h"

#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>
#include <dirent.h>
#include <dlfcn.h>
#include <os/log.h>
#include <mach-o/dyld.h>

extern void NSLog(CFStringRef, ...);

static int filter_dylib(const struct dirent *entry) {
    char* dot = strrchr(entry->d_name, '.');
    return dot && strcmp(dot + 1, "dylib") == 0;
}

static int isApp(const char* path) {
    char* dot = strrchr(path, '.');
    return dot && memcmp(dot + 1, "app", 4) == 0;
}

#if TARGET_OS_OSX
#define TWEAKS_DIRECTORY "/Library/TweakInject/"
#else
#define TWEAKS_DIRECTORY "/var/jb/usr/lib/TweakInject/"
#define MOBILESAFETY_PATH "/var/jb/usr/lib/ellekit/MobileSafety.dylib"
#define OLDABI_PATH "/var/jb/usr/lib/ellekit/OldABI.dylib"
#endif

char* append_str(const char* str, const char* append_str) {
    size_t str_len = strlen(str);
    size_t append_str_len = strlen(append_str);

    char* new_str = malloc(str_len + append_str_len + 1);  // allocate memory for the new string
    if (new_str == NULL) {
        return NULL;  // allocation failed
    }

    memcpy(new_str, str, str_len); // copy the original string to the new string
    strncpy(new_str + str_len, append_str, append_str_len); // append the new string to the end
    new_str[str_len + append_str_len] = '\0';  // terminate the new string

    return new_str;
}

char *get_last_path_component(const char *path)
{
    char *last_component = strrchr(path, '/');
    if (last_component == NULL) {
        return NULL;
    }
    return last_component + 1;
}

#define MAX_TWEAKMANAGERS 20 // this is very reasonable
char* tweakManagers[MAX_TWEAKMANAGERS];

static bool tweak_needinject(const char* orig_path, bool* isTweakManager) {
    
    CFStringRef plistPath;
    CFURLRef url;
    CFDataRef data;
    CFPropertyListRef plist;
    
    char* path = append_str(orig_path, ".plist");
        
    plistPath = CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);

    if (access(path, F_OK) != F_OK) {
        free(path);
        CFRelease(plistPath);
        return false;
    }
    
    free(path);
    
    url = CFURLCreateWithFileSystemPath(kCFAllocatorSystemDefault, plistPath, kCFURLPOSIXPathStyle, false);
        
    if (url && CFURLCreateDataAndPropertiesFromResource(kCFAllocatorSystemDefault, url, &data, NULL, NULL, NULL)) {
        plist = CFPropertyListCreateWithData(kCFAllocatorSystemDefault, data, kCFPropertyListImmutable, NULL, NULL);
        CFRelease(data);
    } else {
        if (url) {
            CFRelease(url);
        }
        CFRelease(plistPath);
        return false;
    }
    CFRelease(url);
    CFRelease(plistPath);
                            
    CFDictionaryRef filter = CFDictionaryGetValue(plist, CFSTR("Filter"));
    
    CFBooleanRef tweakManager = CFDictionaryGetValue(plist, CFSTR("IsTweakManager"));
    
    if (tweakManager != NULL) {
        *isTweakManager = CFBooleanGetValue(tweakManager);
    }

    CFArrayRef versions = CFDictionaryGetValue(filter, CFSTR("CoreFoundationVersion"));
    if (versions && 
        ((CFArrayGetCount(versions) == 1 && *((double*)CFArrayGetValueAtIndex(versions, 0)) > kCFCoreFoundationVersionNumber) || (CFArrayGetCount(versions) == 2 &&
        (*((double*)CFArrayGetValueAtIndex(versions, 0)) > kCFCoreFoundationVersionNumber || *((double*)CFArrayGetValueAtIndex(versions, 1)) <= kCFCoreFoundationVersionNumber)))) {

        CFRelease(plist);
        return false;
    }

    CFArrayRef bundles = CFDictionaryGetValue(filter, CFSTR("Bundles"));
    
    if (bundles) {
        for (CFIndex i = 0; i < CFArrayGetCount(bundles); i++) {
            CFStringRef id = CFArrayGetValueAtIndex(bundles, i);
            if (id) {
                if (CFBundleGetBundleWithIdentifier(id)) {
                    goto success;
                }

                CFMutableStringRef lowercased = CFStringCreateMutableCopy(NULL, 0, id);
                CFStringLowercase(lowercased, NULL);
                if (CFBundleGetBundleWithIdentifier(lowercased)) {
                    CFRelease(lowercased);
                    goto success;
                }
                CFRelease(lowercased);
            }
        }
    }
    
    CFArrayRef classes = CFDictionaryGetValue(filter, CFSTR("Classes"));
    
    if (classes) {
        for (CFIndex i = 0; i < CFArrayGetCount(classes); i++) {
            CFStringRef id = CFArrayGetValueAtIndex(classes, i);
            const char* str = CFStringGetCStringPtr(id, kCFStringEncodingASCII);
            if (str) {
                if (objc_getClass(str)) {
                    goto success;
                }
            }
            else {
                char* copiedStr = malloc(CFStringGetLength(id)+1);
                CFStringGetCString(id, copiedStr, CFStringGetLength(id)+1, kCFStringEncodingASCII);
                if (objc_getClass(copiedStr)) {
                    free(copiedStr);
                    goto success;
                }
                free(copiedStr);
            }
        }
    }
    
    CFArrayRef executables = CFDictionaryGetValue(filter, CFSTR("Executables"));

    if (executables) {
        
        char executable[1024];
        uint32_t size = 1024;

        if (_NSGetExecutablePath(executable, &size) == 0) {
            
            for (CFIndex i = 0; i < CFArrayGetCount(executables); i++) {
                CFStringRef id = CFArrayGetValueAtIndex(executables, i);
                const char* str = CFStringGetCStringPtr(id, kCFStringEncodingASCII);
                if (str) {
                    if (!strcmp(str, get_last_path_component(executable))) {
                        goto success;
                    }
                }
                else {
                    char* copiedStr = malloc(CFStringGetLength(id)+1);
                    CFStringGetCString(id, copiedStr, CFStringGetLength(id)+1, kCFStringEncodingASCII);
                    if (!strcmp(copiedStr, get_last_path_component(executable))) {
                        free(copiedStr);
                        goto success;
                    }
                    free(copiedStr);
                }
            }
        } else if (CFBundleGetMainBundle()) {
            CFBundleRef bundle = CFBundleGetMainBundle();
            CFURLRef url = CFBundleCopyExecutableURL(bundle);
            if (url) {
                CFStringRef file_name = CFURLCopyLastPathComponent(url);
                if (file_name) {
                    for (CFIndex i = 0; i < CFArrayGetCount(executables); i++) {
                        CFStringRef id = CFArrayGetValueAtIndex(executables, i);
                        if (CFStringCompare(file_name, id, kCFCompareCaseInsensitive) == kCFCompareEqualTo) {
                            CFRelease(file_name);
                            CFRelease(url);
                            goto success;
                        }
                    }
                    CFRelease(file_name);
                }
                CFRelease(url);
            }
        }
    }
    
    CFRelease(plist);
    return false;
    
success:
    CFRelease(plist);
    return true;
}

int
alphasort2 (const struct dirent **a, const struct dirent **b)
{
  return -strcoll ((*a)->d_name, (*b)->d_name);
}

static void tweaks_iterate() {
    struct dirent **files;
    int n;

    n = scandir(TWEAKS_DIRECTORY, &files, filter_dylib, alphasort2);
    if (n == -1) {
        perror("scandir");
        return;
    }

    while (n--) {
        
        if (*(files[n]->d_name)) {
            char* full_path = append_str(TWEAKS_DIRECTORY, files[n]->d_name);
            char* plist = strndup(full_path, strlen(full_path) - 6);
            
            bool isTweakManager = false;
            bool ret = tweak_needinject(plist, &isTweakManager);
                        
            if (ret) {
                if (isTweakManager) {
                    int i;
                    for (i = 0; i < MAX_TWEAKMANAGERS && tweakManagers[i] != NULL; i++); // find the end of the array
                    
                    if (i == MAX_TWEAKMANAGERS) return; // array is full
                    tweakManagers[i] = malloc(strlen(full_path) + 1); // allocate memory for the new string
                    strcpy(tweakManagers[i], full_path); // copy the string into the new memory location
                } else {
                    int i;
                    for (i = 0; i < MAX_TWEAKMANAGERS && tweakManagers[i] != NULL; i++) {
                        dlopen(tweakManagers[i], RTLD_LAZY);
                    }
                    
                    #if !TARGET_OS_OSX
                    if (!access(OLDABI_PATH, F_OK)) {
                        dlopen(OLDABI_PATH, RTLD_LAZY);
                    }
                    #endif

                    dlopen(full_path, RTLD_LAZY);

                    dlerror();
                }
            }
            
            free(full_path);
            free(plist);
            free(files[n]);
        }
    }
    
    free(files);
}

__attribute__((constructor))
static void injection_init() {
    
#if !TARGET_OS_OSX
    if (CFBundleGetMainBundle() && CFBundleGetIdentifier(CFBundleGetMainBundle())) {
        if (CFEqual(CFBundleGetIdentifier(CFBundleGetMainBundle()), CFSTR("com.apple.springboard"))) {
            dlopen(MOBILESAFETY_PATH, RTLD_NOW);
        }
    }
    
    if (!access("/var/mobile/.eksafemode", F_OK)) {
        return;
    }
#endif
    
    const char* extension = getenv("SANDBOX_EXTENSION");
    if (extension) {
        sandbox_extension_consume(extension);
    }
    tweaks_iterate();
}
