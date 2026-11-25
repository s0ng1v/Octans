#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cfgmgr32.h>
#include <hidsdi.h>

#include "hotplug.h"

#include <stdlib.h>
#include <wctype.h>

static uint64_t fnv1a_update_wstr(uint64_t h, const wchar_t *s) {
    for (; *s; ++s) {
        wchar_t c = towlower(*s);
        h ^= (uint64_t)(c & 0xFFu);
        h *= 1099511628211ULL;
        h ^= (uint64_t)((c >> 8) & 0xFFu);
        h *= 1099511628211ULL;
    }
    h ^= 0xFFu;
    h *= 1099511628211ULL;
    return h;
}

int octans_hotplug_changed(octans_hotplug_snapshot_t *prev) {
    if (!prev) return 1;

    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);

    ULONG buf_chars = 0;
    CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(
        &buf_chars, &hid_guid, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS || buf_chars == 0) {
        return 1;
    }

    wchar_t stack_buf[4096];
    wchar_t *buf = stack_buf;
    wchar_t *heap_buf = NULL;
    if (buf_chars > (ULONG)(sizeof(stack_buf) / sizeof(stack_buf[0]))) {
        heap_buf = (wchar_t *)malloc((size_t)buf_chars * sizeof(wchar_t));
        if (!heap_buf) return 1;
        buf = heap_buf;
    }

    cr = CM_Get_Device_Interface_ListW(
        &hid_guid, NULL, buf, buf_chars, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS) {
        free(heap_buf);
        return 1;
    }

    uint64_t h = 14695981039346656037ULL;
    uint32_t count = 0;
    const wchar_t *p = buf;
    while (*p != L'\0') {
        h = fnv1a_update_wstr(h, p);
        ++count;
        p += wcslen(p) + 1;
    }

    free(heap_buf);

    int changed = (!prev->initialized || prev->hash != h || prev->iface_count != count) ? 1 : 0;
    prev->hash = h;
    prev->iface_count = count;
    prev->initialized = 1;
    return changed;
}
