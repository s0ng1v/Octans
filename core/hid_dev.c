#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>

#include "hid_dev.h"

#include <string.h>

int octans_hid_open(const wchar_t *path, int want_write, int want_read, octans_hid_dev_t *out) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->h_write = INVALID_HANDLE_VALUE;
    out->h_read = INVALID_HANDLE_VALUE;

    DWORD access = want_write ? (GENERIC_READ | GENERIC_WRITE) : 0;
    HANDLE hw = CreateFileW(path,
                            access,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                            OPEN_EXISTING,
                            0,
                            NULL);
    if (hw != INVALID_HANDLE_VALUE) {
        out->can_write = want_write ? 1 : 0;
    } else if (want_write) {
        /* Fallback to zero-access handle for Feature/caps reads on restricted nodes. */
        hw = CreateFileW(path,
                         0,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
        out->can_write = 0;
    }

    if (hw == INVALID_HANDLE_VALUE) {
        out->last_error = (int)GetLastError();
        return -1;
    }
    out->h_write = hw;

    PHIDP_PREPARSED_DATA preparsed = NULL;
    if (HidD_GetPreparsedData(hw, &preparsed) && preparsed != NULL) {
        HIDP_CAPS caps;
        memset(&caps, 0, sizeof(caps));
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            out->input_len = caps.InputReportByteLength;
            out->output_len = caps.OutputReportByteLength;
            out->feature_len = caps.FeatureReportByteLength;
        }
        out->preparsed = preparsed;
    }

    if (want_read && out->input_len > 0) {
        HANDLE hr = CreateFileW(path,
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED,
                                NULL);
        if (hr != INVALID_HANDLE_VALUE) {
            HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
            if (ev != NULL) {
                out->h_read = hr;
                out->h_event = ev;
                out->can_read = 1;
            } else {
                CloseHandle(hr);
            }
        }
    }

    return 0;
}

void octans_hid_close(octans_hid_dev_t *dev) {
    if (!dev) return;

    if (dev->h_read != NULL && dev->h_read != INVALID_HANDLE_VALUE) {
        CancelIoEx(dev->h_read, NULL);
        CloseHandle(dev->h_read);
        dev->h_read = INVALID_HANDLE_VALUE;
    }
    if (dev->h_event != NULL) {
        CloseHandle(dev->h_event);
        dev->h_event = NULL;
    }
    if (dev->preparsed != NULL) {
        HidD_FreePreparsedData((PHIDP_PREPARSED_DATA)dev->preparsed);
        dev->preparsed = NULL;
    }
    if (dev->h_write != NULL && dev->h_write != INVALID_HANDLE_VALUE) {
        CloseHandle(dev->h_write);
        dev->h_write = INVALID_HANDLE_VALUE;
    }
    dev->can_write = 0;
    dev->can_read = 0;
}

int octans_hid_get_feature(octans_hid_dev_t *dev, uint8_t report_id, uint8_t *buf, size_t len) {
    if (!dev || dev->h_write == INVALID_HANDLE_VALUE || !buf || len == 0) return -1;
    memset(buf, 0, len);
    buf[0] = report_id;
    if (!HidD_GetFeature(dev->h_write, buf, (ULONG)len)) {
        dev->last_error = (int)GetLastError();
        return -1;
    }
    return (int)len;
}

int octans_hid_set_feature(octans_hid_dev_t *dev, const uint8_t *buf, size_t len) {
    /* Windows permits HidD_SetFeature IOCTL on dwDesiredAccess=0 handles (needed for Razer mice). */
    if (!dev || dev->h_write == INVALID_HANDLE_VALUE || !buf || len == 0) {
        return -1;
    }
    uint8_t tmp[1024];
    if (len > sizeof(tmp)) return -1;
    memcpy(tmp, buf, len);
    if (!HidD_SetFeature(dev->h_write, tmp, (ULONG)len)) {
        dev->last_error = (int)GetLastError();
        return -1;
    }
    return 0;
}

int octans_hid_write_output(octans_hid_dev_t *dev, const uint8_t *buf, size_t len) {
    if (!dev || !dev->can_write || dev->h_write == INVALID_HANDLE_VALUE || !buf || len == 0) {
        return -1;
    }
    uint8_t tmp[1024];
    if (len > sizeof(tmp)) return -1;
    memcpy(tmp, buf, len);

    if (HidD_SetOutputReport(dev->h_write, tmp, (ULONG)len)) {
        return 0;
    }

    DWORD written = 0;
    if (WriteFile(dev->h_write, tmp, (DWORD)len, &written, NULL)) {
        return 0;
    }
    dev->last_error = (int)GetLastError();
    return -1;
}

int octans_hid_write_output_intr(octans_hid_dev_t *dev, const uint8_t *buf, size_t len) {
    if (!dev || !dev->can_write || dev->h_write == INVALID_HANDLE_VALUE || !buf || len == 0) {
        return -1;
    }
    uint8_t tmp[1024];
    if (len > sizeof(tmp)) return -1;
    memcpy(tmp, buf, len);

    DWORD written = 0;
    if (WriteFile(dev->h_write, tmp, (DWORD)len, &written, NULL)) {
        return 0;
    }
    if (HidD_SetOutputReport(dev->h_write, tmp, (ULONG)len)) {
        return 0;
    }
    dev->last_error = (int)GetLastError();
    return -1;
}

int octans_hid_read_input(octans_hid_dev_t *dev, uint8_t *buf, size_t cap, uint32_t timeout_ms) {
    if (!dev || !dev->can_read || dev->h_read == INVALID_HANDLE_VALUE || !dev->h_event || !buf || cap == 0) {
        return -1;
    }

    size_t read_len = dev->input_len > 0 ? dev->input_len : cap;
    if (read_len > cap) read_len = cap;

    ResetEvent(dev->h_event);
    OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = dev->h_event;

    DWORD bytes_read = 0;
    if (ReadFile(dev->h_read, buf, (DWORD)read_len, &bytes_read, &ov)) {
        return (int)bytes_read;
    }

    DWORD err = GetLastError();
    if (err != ERROR_IO_PENDING) {
        dev->last_error = (int)err;
        return -1;
    }

    DWORD wait_rc = WaitForSingleObject(dev->h_event, timeout_ms > 0 ? timeout_ms : 1);
    if (wait_rc == WAIT_OBJECT_0) {
        if (GetOverlappedResult(dev->h_read, &ov, &bytes_read, FALSE)) {
            return (int)bytes_read;
        }
        dev->last_error = (int)GetLastError();
        return -1;
    }

    /* Cancel pending I/O and wait until kernel drops references to stack OVERLAPPED/buf. */
    CancelIoEx(dev->h_read, &ov);
    WaitForSingleObject(dev->h_event, 200);
    if (GetOverlappedResult(dev->h_read, &ov, &bytes_read, TRUE) && bytes_read > 0) {
        return (int)bytes_read;
    }
    return 0;
}

void octans_hid_drain(octans_hid_dev_t *dev, int max_packets) {
    if (!dev || !dev->can_read || dev->input_len == 0) return;
    if (max_packets <= 0 || max_packets > 64) max_packets = 64;

    uint8_t discard[1024];
    for (int i = 0; i < max_packets; ++i) {
        int n = octans_hid_read_input(dev, discard, sizeof(discard), 8);
        if (n <= 0) break;
    }
}
