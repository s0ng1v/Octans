#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>

#include "usb_etw_learn.h"
#include "../engine/frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define ETW_SESSION_NAME L"OctansUsbLearnSession"

/* Microsoft-Windows-USB-USBHUB3 {AC52AD17-CC01-4F85-8DF5-4DCE4333C99B} */
static const GUID kGuidUsbHub3 = {
    0xAC52AD17u, 0xCC01u, 0x4F85u,
    {0x8Du, 0xF5u, 0x4Du, 0xCEu, 0x43u, 0x33u, 0xC9u, 0x9Bu}
};

/* Microsoft-Windows-USB-UCX {36DA592D-E43A-4E28-AF6F-4BC57C5A11E8} */
static const GUID kGuidUsbUcx = {
    0x36DA592Du, 0xE43Au, 0x4E28u,
    {0xAFu, 0x6Fu, 0x4Bu, 0xC5u, 0x7Cu, 0x5Au, 0x11u, 0xE8u}
};

#define MAX_TRACKED_DEVS  16
#define MAX_TRACKED_PIPES 64

typedef struct tracked_usb_dev {
    uint64_t usb_dev_handle;
    uint16_t vid;
    uint16_t pid;
    octans_usb_ep_map_t ep_map;
} tracked_usb_dev_t;

typedef struct tracked_usb_pipe {
    uint64_t pipe_handle;
    uint64_t usb_dev_handle;
    uint8_t ep_addr;
    uint8_t bm_attr;
} tracked_usb_pipe_t;

struct octans_etw_session {
    uint16_t target_vid;
    uint16_t target_pid;
    uint32_t vendor_iface_mask;

    TRACEHANDLE h_session;
    TRACEHANDLE h_trace;
    HANDLE h_thread;
    volatile LONG stopping;

    CRITICAL_SECTION cs;
    tracked_usb_dev_t devs[MAX_TRACKED_DEVS];
    size_t dev_count;
    tracked_usb_pipe_t pipes[MAX_TRACKED_PIPES];
    size_t pipe_count;

    octans_usb_pkt_t *pkts;
    size_t pkt_count;
    size_t pkt_cap;

    volatile LONG tx_count;
    volatile LONG rx_count;
    volatile LONG dev_seen;

    uint64_t last_tx_ts;
    uint64_t last_small_rx_ts;
};

static int ep_slot(uint8_t ep_addr) {
    return (int)(ep_addr & 0x0Fu) + ((ep_addr & 0x80u) ? 16 : 0);
}

int octans_etw_is_elevated(void) {
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return 0;
    }
    TOKEN_ELEVATION elev;
    DWORD sz = sizeof(elev);
    int res = 0;
    if (GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &sz)) {
        res = elev.TokenIsElevated ? 1 : 0;
    }
    CloseHandle(token);
    return res;
}

void octans_usb_parse_config_desc(const uint8_t *cfg,
                                  size_t len,
                                  octans_usb_ep_map_t *out) {
    if (!out) return;
    memset(out->ep_to_iface, -1, sizeof(out->ep_to_iface));
    memset(out->ep_is_boot_hid, 0, sizeof(out->ep_is_boot_hid));
    if (!cfg || len < 4) return;

    int cur_iface = -1;
    uint8_t cur_is_boot = 0;
    size_t pos = 0;

    while (pos + 2 <= len) {
        uint8_t bLen = cfg[pos];
        uint8_t bType = cfg[pos + 1];
        if (bLen < 2 || pos + bLen > len) break;

        if (bType == 4u && bLen >= 9u) {
            /* USB Interface Descriptor */
            cur_iface = (int)cfg[pos + 2];
            uint8_t cls = cfg[pos + 5];
            uint8_t proto = cfg[pos + 7];
            /* HID Boot Keyboard (proto=1) or Boot Mouse (proto=2) */
            cur_is_boot = (cls == 3u && (proto == 1u || proto == 2u)) ? 1u : 0u;
        } else if (bType == 5u && bLen >= 7u && cur_iface >= 0) {
            /* USB Endpoint Descriptor */
            uint8_t ep = cfg[pos + 2];
            int slot = ep_slot(ep);
            if (slot >= 0 && slot < 32) {
                out->ep_to_iface[slot] = (int8_t)cur_iface;
                out->ep_is_boot_hid[slot] = cur_is_boot;
            }
        }
        pos += bLen;
    }
}

static int tdh_read_prop(PEVENT_RECORD rec, const wchar_t *name, void *out, ULONG out_sz) {
    PROPERTY_DATA_DESCRIPTOR pdd;
    memset(&pdd, 0, sizeof(pdd));
    pdd.PropertyName = (ULONGLONG)(uintptr_t)name;
    pdd.ArrayIndex = ULONG_MAX;
    return TdhGetProperty(rec, 0, NULL, 1, &pdd, out_sz, (PBYTE)out) == ERROR_SUCCESS ? 1 : 0;
}

static int tdh_read_struct_prop(PEVENT_RECORD rec,
                                const wchar_t *struct_name,
                                const wchar_t *field_name,
                                void *out,
                                ULONG out_sz) {
    PROPERTY_DATA_DESCRIPTOR pdd[2];
    memset(pdd, 0, sizeof(pdd));
    pdd[0].PropertyName = (ULONGLONG)(uintptr_t)struct_name;
    pdd[0].ArrayIndex = ULONG_MAX;
    pdd[1].PropertyName = (ULONGLONG)(uintptr_t)field_name;
    pdd[1].ArrayIndex = ULONG_MAX;
    return TdhGetProperty(rec, 0, NULL, 2, pdd, out_sz, (PBYTE)out) == ERROR_SUCCESS ? 1 : 0;
}

static tracked_usb_dev_t *find_tracked_dev(octans_etw_session_t *s, uint64_t handle) {
    for (size_t i = 0; i < s->dev_count; ++i) {
        if (s->devs[i].usb_dev_handle == handle) return &s->devs[i];
    }
    return NULL;
}

static tracked_usb_pipe_t *find_tracked_pipe(octans_etw_session_t *s, uint64_t pipe_handle) {
    for (size_t i = 0; i < s->pipe_count; ++i) {
        if (s->pipes[i].pipe_handle == pipe_handle) return &s->pipes[i];
    }
    return NULL;
}

static void record_packet(octans_etw_session_t *s, const octans_usb_pkt_t *pkt) {
    int is_tx = (pkt->xfer_kind == OCTANS_USB_XFER_CTRL_SET_OUT ||
                 pkt->xfer_kind == OCTANS_USB_XFER_CTRL_SET_FEAT ||
                 pkt->xfer_kind == OCTANS_USB_XFER_INTR_OUT);

    EnterCriticalSection(&s->cs);
    if (s->pkt_count < s->pkt_cap) {
        s->pkts[s->pkt_count++] = *pkt;
    } else if (s->pkt_cap > 0) {
        /* Preserve earlier TX/RX pairs while keeping recent traffic */
        size_t keep = s->pkt_cap / 2u;
        memmove(&s->pkts[keep], &s->pkts[keep + 1u],
                (s->pkt_cap - keep - 1u) * sizeof(octans_usb_pkt_t));
        s->pkts[s->pkt_cap - 1u] = *pkt;
    }
    if (is_tx) {
        s->last_tx_ts = pkt->ts_100ns;
        InterlockedIncrement(&s->tx_count);
    } else {
        InterlockedIncrement(&s->rx_count);
    }
    LeaveCriticalSection(&s->cs);
}

static void handle_usbhub3_event(octans_etw_session_t *s, PEVENT_RECORD rec) {
    USHORT eid = rec->EventHeader.EventDescriptor.Id;
    if (eid != 6u && eid != 43u) return;

    uint16_t vid = 0, pid = 0;
    if (!tdh_read_struct_prop(rec, L"fid_DeviceDescriptor", L"fid_idVendor", &vid, sizeof(vid)) ||
        !tdh_read_struct_prop(rec, L"fid_DeviceDescriptor", L"fid_idProduct", &pid, sizeof(pid))) {
        return;
    }
    if (vid != s->target_vid || pid != s->target_pid) return;

    uint64_t usb_dev = 0;
    if (!tdh_read_prop(rec, L"fid_UsbDevice", &usb_dev, sizeof(usb_dev)) || usb_dev == 0) {
        return;
    }

    octans_usb_ep_map_t ep_map;
    memset(ep_map.ep_to_iface, -1, sizeof(ep_map.ep_to_iface));
    memset(ep_map.ep_is_boot_hid, 0, sizeof(ep_map.ep_is_boot_hid));

    uint16_t cfg_len = 0;
    if (tdh_read_prop(rec, L"fid_ConfigurationDescriptorLength", &cfg_len, sizeof(cfg_len)) &&
        cfg_len > 0 && cfg_len <= 2048u) {
        uint8_t cfg_buf[2048];
        if (tdh_read_prop(rec, L"fid_ConfigurationDescriptor", cfg_buf, cfg_len)) {
            octans_usb_parse_config_desc(cfg_buf, cfg_len, &ep_map);
        }
    }

    EnterCriticalSection(&s->cs);
    tracked_usb_dev_t *d = find_tracked_dev(s, usb_dev);
    if (!d && s->dev_count < MAX_TRACKED_DEVS) {
        d = &s->devs[s->dev_count++];
    }
    if (d) {
        d->usb_dev_handle = usb_dev;
        d->vid = vid;
        d->pid = pid;
        d->ep_map = ep_map;
    }
    InterlockedExchange(&s->dev_seen, 1);
    LeaveCriticalSection(&s->cs);
}

static void handle_ucx_event(octans_etw_session_t *s, PEVENT_RECORD rec) {
    USHORT eid = rec->EventHeader.EventDescriptor.Id;
    if (eid != 6u && eid != 22u && eid != 25u && eid != 28u && eid != 29u) return;

    uint64_t usb_dev = 0;
    if (!tdh_read_prop(rec, L"fid_UsbDevice", &usb_dev, sizeof(usb_dev)) || usb_dev == 0) {
        return;
    }

    EnterCriticalSection(&s->cs);
    tracked_usb_dev_t dev_copy;
    tracked_usb_dev_t *d = find_tracked_dev(s, usb_dev);
    int matched = (d != NULL);
    if (matched) dev_copy = *d;
    LeaveCriticalSection(&s->cs);

    if (!matched) return;

    if (eid == 6u) {
        /* UCX Endpoint Information */
        uint64_t pipe_h = 0;
        uint8_t ep_addr = 0, bm_attr = 0;
        if (!tdh_read_prop(rec, L"fid_PipeHandle", &pipe_h, sizeof(pipe_h)) || pipe_h == 0) return;
        tdh_read_struct_prop(rec, L"fid_UCX_Endpoint_Descriptor", L"fid_bEndpointAddress", &ep_addr, sizeof(ep_addr));
        tdh_read_struct_prop(rec, L"fid_UCX_Endpoint_Descriptor", L"fid_bmAttributes", &bm_attr, sizeof(bm_attr));

        EnterCriticalSection(&s->cs);
        tracked_usb_pipe_t *p = find_tracked_pipe(s, pipe_h);
        if (!p && s->pipe_count < MAX_TRACKED_PIPES) {
            p = &s->pipes[s->pipe_count++];
        }
        if (p) {
            p->pipe_handle = pipe_h;
            p->usb_dev_handle = usb_dev;
            p->ep_addr = ep_addr;
            p->bm_attr = bm_attr;
        }
        LeaveCriticalSection(&s->cs);
        return;
    }

    uint32_t nt_status = 1u;
    if (!tdh_read_prop(rec, L"fid_IRP_NtStatus", &nt_status, sizeof(nt_status)) || nt_status != 0u) {
        return;
    }

    uint16_t data_len = 0;
    if (!tdh_read_prop(rec, L"fid_URB_TransferDataLength", &data_len, sizeof(data_len)) ||
        data_len == 0u || data_len > 1024u) {
        return;
    }

    uint8_t tmp[1024];
    if (!tdh_read_prop(rec, L"fid_URB_TransferData", tmp, data_len)) {
        return;
    }

    octans_usb_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.ts_100ns = (uint64_t)rec->EventHeader.TimeStamp.QuadPart;
    pkt.vid = dev_copy.vid;
    pkt.pid = dev_copy.pid;
    pkt.iface_num = -1;
    pkt.len = (data_len <= OCTANS_ETW_MAX_PKT_DATA) ? data_len : (uint16_t)OCTANS_ETW_MAX_PKT_DATA;
    memcpy(pkt.data, tmp, pkt.len);

    if (eid == 22u || eid == 25u) {
        /* Control Transfer (HID SET_REPORT / GET_REPORT) */
        const wchar_t *sname = (eid == 25u)
            ? L"fid_UCX_URB_CONTROL_TRANSFER_EX"
            : L"fid_UCX_URB_CONTROL_TRANSFER";
        uint8_t bmReq = 0, bReq = 0;
        uint16_t wVal = 0, wIdx = 0;
        if (!tdh_read_struct_prop(rec, sname, L"fid_URB_Setup_bmRequestType", &bmReq, sizeof(bmReq)) ||
            !tdh_read_struct_prop(rec, sname, L"fid_URB_Setup_bRequest", &bReq, sizeof(bReq))) {
            return;
        }
        tdh_read_struct_prop(rec, sname, L"fid_URB_Setup_wValue", &wVal, sizeof(wVal));
        tdh_read_struct_prop(rec, sname, L"fid_URB_Setup_wIndex", &wIdx, sizeof(wIdx));

        uint8_t rtype = (uint8_t)((wVal >> 8) & 0xFFu);
        uint8_t rid = (uint8_t)(wVal & 0xFFu);
        pkt.report_id = rid;
        pkt.iface_num = (int16_t)(wIdx & 0xFFu);

        if (bmReq == 0x21u && bReq == 0x09u) {
            /* Class Interface SET_REPORT */
            if (rtype == 0x03u) pkt.xfer_kind = OCTANS_USB_XFER_CTRL_SET_FEAT;
            else if (rtype == 0x02u) pkt.xfer_kind = OCTANS_USB_XFER_CTRL_SET_OUT;
            else return;
        } else if (bmReq == 0xA1u && bReq == 0x01u) {
            /* Class Interface GET_REPORT */
            if (rtype == 0x03u) pkt.xfer_kind = OCTANS_USB_XFER_CTRL_GET_FEAT;
            else if (rtype == 0x01u) pkt.xfer_kind = OCTANS_USB_XFER_CTRL_GET_IN;
            else return;
        } else {
            return;
        }

        record_packet(s, &pkt);
        return;
    }

    /* Event 28 / 29: Bulk or Interrupt Transfer */
    uint64_t pipe_h = 0;
    uint32_t xfer_flags = 0;
    tdh_read_prop(rec, L"fid_PipeHandle", &pipe_h, sizeof(pipe_h));
    tdh_read_struct_prop(rec, L"fid_UCX_URB_BULK_OR_INTERRUPT_TRANSFER",
                         L"fid_URB_TransferFlags", &xfer_flags, sizeof(xfer_flags));

    uint8_t ep_addr = 0;
    uint8_t is_boot = 0;
    int16_t iface_num = -1;

    EnterCriticalSection(&s->cs);
    tracked_usb_pipe_t *p = find_tracked_pipe(s, pipe_h);
    if (p) {
        ep_addr = p->ep_addr;
        int slot = ep_slot(ep_addr);
        if (slot >= 0 && slot < 32) {
            iface_num = dev_copy.ep_map.ep_to_iface[slot];
            is_boot = dev_copy.ep_map.ep_is_boot_hid[slot];
        }
    }
    LeaveCriticalSection(&s->cs);

    int is_in = (xfer_flags & 0x01u) ? 1 : ((ep_addr & 0x80u) ? 1 : 0);
    pkt.ep_addr = ep_addr;
    pkt.iface_num = iface_num;
    pkt.xfer_kind = is_in ? OCTANS_USB_XFER_INTR_IN : OCTANS_USB_XFER_INTR_OUT;

    /* Drop pure boot mouse/keyboard Interrupt IN floods (e.g., 1000Hz/8000Hz gaming mice) */
    if (is_in && is_boot && iface_num >= 0 && iface_num < 32) {
        if ((s->vendor_iface_mask & (1u << (unsigned)iface_num)) == 0u) {
            return;
        }
    }

    /* Secondary rate-limiter for unknown-interface short mouse/key packets when no TX occurred */
    if (is_in && iface_num < 0 && pkt.len <= 12u) {
        uint64_t now_ts = pkt.ts_100ns;
        int near_tx = (s->last_tx_ts > 0 && now_ts >= s->last_tx_ts &&
                       (now_ts - s->last_tx_ts) < 8000000ULL); /* 800 ms */
        if (!near_tx && s->last_small_rx_ts > 0 &&
            now_ts >= s->last_small_rx_ts &&
            (now_ts - s->last_small_rx_ts) < 2000000ULL) { /* 200 ms */
            return;
        }
        s->last_small_rx_ts = now_ts;
    }

    record_packet(s, &pkt);
}

static VOID WINAPI etw_event_cb(PEVENT_RECORD rec) {
    if (!rec || !rec->UserContext) return;
    octans_etw_session_t *s = (octans_etw_session_t *)rec->UserContext;
    if (InterlockedCompareExchange(&s->stopping, 0, 0)) return;

    if (IsEqualGUID(&rec->EventHeader.ProviderId, &kGuidUsbHub3)) {
        handle_usbhub3_event(s, rec);
    } else if (IsEqualGUID(&rec->EventHeader.ProviderId, &kGuidUsbUcx)) {
        handle_ucx_event(s, rec);
    }
}

static DWORD WINAPI etw_consumer_thread(LPVOID param) {
    octans_etw_session_t *s = (octans_etw_session_t *)param;
    if (!s || s->h_trace == INVALID_PROCESSTRACE_HANDLE) return 0;
    ProcessTrace(&s->h_trace, 1, NULL, NULL);
    return 0;
}

static void stop_named_session(void) {
    size_t prop_sz = sizeof(EVENT_TRACE_PROPERTIES) + 256u * sizeof(wchar_t);
    EVENT_TRACE_PROPERTIES *props = (EVENT_TRACE_PROPERTIES *)calloc(1, prop_sz);
    if (!props) return;
    props->Wnode.BufferSize = (ULONG)prop_sz;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    ControlTraceW(0, ETW_SESSION_NAME, props, EVENT_TRACE_CONTROL_STOP);
    free(props);
}

octans_etw_session_t *octans_etw_start(uint16_t target_vid,
                                       uint16_t target_pid,
                                       uint32_t vendor_iface_mask,
                                       DWORD *out_err) {
    if (out_err) *out_err = ERROR_SUCCESS;
    if (target_vid == 0u) {
        if (out_err) *out_err = ERROR_INVALID_PARAMETER;
        return NULL;
    }

    stop_named_session();

    octans_etw_session_t *s = (octans_etw_session_t *)calloc(1, sizeof(*s));
    if (!s) {
        if (out_err) *out_err = ERROR_OUTOFMEMORY;
        return NULL;
    }
    s->target_vid = target_vid;
    s->target_pid = target_pid;
    s->vendor_iface_mask = vendor_iface_mask;
    s->h_trace = INVALID_PROCESSTRACE_HANDLE;
    s->pkt_cap = OCTANS_ETW_MAX_PKTS;
    s->pkts = (octans_usb_pkt_t *)calloc(s->pkt_cap, sizeof(octans_usb_pkt_t));
    if (!s->pkts) {
        free(s);
        if (out_err) *out_err = ERROR_OUTOFMEMORY;
        return NULL;
    }
    InitializeCriticalSection(&s->cs);

    size_t prop_sz = sizeof(EVENT_TRACE_PROPERTIES) + 256u * sizeof(wchar_t);
    EVENT_TRACE_PROPERTIES *props = (EVENT_TRACE_PROPERTIES *)calloc(1, prop_sz);
    if (!props) {
        DeleteCriticalSection(&s->cs);
        free(s->pkts);
        free(s);
        if (out_err) *out_err = ERROR_OUTOFMEMORY;
        return NULL;
    }

    props->Wnode.BufferSize = (ULONG)prop_sz;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1u; /* QPC */
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->BufferSize = 128u;
    props->MinimumBuffers = 8u;
    props->MaximumBuffers = 64u;
    props->FlushTimer = 1u;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    ULONG rc = StartTraceW(&s->h_session, ETW_SESSION_NAME, props);
    free(props);
    if (rc != ERROR_SUCCESS) {
        if (out_err) *out_err = rc;
        DeleteCriticalSection(&s->cs);
        free(s->pkts);
        free(s);
        return NULL;
    }

    EVENT_TRACE_LOGFILEW log;
    memset(&log, 0, sizeof(log));
    log.LoggerName = (LPWSTR)ETW_SESSION_NAME;
    log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    log.EventRecordCallback = etw_event_cb;
    log.Context = s;

    s->h_trace = OpenTraceW(&log);
    if (s->h_trace == INVALID_PROCESSTRACE_HANDLE) {
        DWORD err = GetLastError();
        if (out_err) *out_err = err;
        stop_named_session();
        DeleteCriticalSection(&s->cs);
        free(s->pkts);
        free(s);
        return NULL;
    }

    s->h_thread = CreateThread(NULL, 0, etw_consumer_thread, s, 0, NULL);
    if (!s->h_thread) {
        DWORD err = GetLastError();
        if (out_err) *out_err = err;
        CloseTrace(s->h_trace);
        stop_named_session();
        DeleteCriticalSection(&s->cs);
        free(s->pkts);
        free(s);
        return NULL;
    }

    /* Enable USBHUB3 first & trigger rundown so fid_UsbDevice -> VID:PID populates first. */
    EnableTraceEx2(s->h_session, &kGuidUsbHub3, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                   TRACE_LEVEL_INFORMATION, 0x8001ULL, 0ULL, 0u, NULL);
    EnableTraceEx2(s->h_session, &kGuidUsbHub3, EVENT_CONTROL_CODE_CAPTURE_STATE,
                   TRACE_LEVEL_INFORMATION, 0x8001ULL, 0ULL, 0u, NULL);
    Sleep(80);

    /* Enable UCX with Default (0x1) | FullDataBusTrace (0x100) | Rundown (0x8000). */
    EnableTraceEx2(s->h_session, &kGuidUsbUcx, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                   TRACE_LEVEL_INFORMATION, 0x8101ULL, 0ULL, 0u, NULL);
    EnableTraceEx2(s->h_session, &kGuidUsbUcx, EVENT_CONTROL_CODE_CAPTURE_STATE,
                   TRACE_LEVEL_INFORMATION, 0x8101ULL, 0ULL, 0u, NULL);

    return s;
}

void octans_etw_get_stats(const octans_etw_session_t *s,
                          uint32_t *tx_count,
                          uint32_t *rx_count,
                          int *dev_seen) {
    if (!s) {
        if (tx_count) *tx_count = 0;
        if (rx_count) *rx_count = 0;
        if (dev_seen) *dev_seen = 0;
        return;
    }
    if (tx_count) *tx_count = (uint32_t)InterlockedCompareExchange((LONG *)&s->tx_count, 0, 0);
    if (rx_count) *rx_count = (uint32_t)InterlockedCompareExchange((LONG *)&s->rx_count, 0, 0);
    if (dev_seen) *dev_seen = (int)InterlockedCompareExchange((LONG *)&s->dev_seen, 0, 0);
}

size_t octans_etw_stop_and_copy(octans_etw_session_t *s,
                                octans_usb_pkt_t *out,
                                size_t max_pkts) {
    if (!s) return 0;

    InterlockedExchange(&s->stopping, 1);

    EnableTraceEx2(s->h_session, &kGuidUsbUcx, EVENT_CONTROL_CODE_DISABLE_PROVIDER,
                   0, 0ULL, 0ULL, 0u, NULL);
    EnableTraceEx2(s->h_session, &kGuidUsbHub3, EVENT_CONTROL_CODE_DISABLE_PROVIDER,
                   0, 0ULL, 0ULL, 0u, NULL);
    stop_named_session();

    if (s->h_trace != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(s->h_trace);
        s->h_trace = INVALID_PROCESSTRACE_HANDLE;
    }
    if (s->h_thread) {
        WaitForSingleObject(s->h_thread, 2500);
        CloseHandle(s->h_thread);
        s->h_thread = NULL;
    }

    size_t copied = 0;
    if (out && max_pkts > 0 && s->pkts && s->pkt_count > 0) {
        copied = (s->pkt_count < max_pkts) ? s->pkt_count : max_pkts;
        memcpy(out, s->pkts, copied * sizeof(octans_usb_pkt_t));
    }

    DeleteCriticalSection(&s->cs);
    free(s->pkts);
    free(s);
    return copied;
}

/* ============================================================================
 * Protocol Inference Engine & Live Replay Verification
 * ============================================================================ */

static int is_tx_xfer(uint8_t k) {
    return k == OCTANS_USB_XFER_CTRL_SET_OUT ||
           k == OCTANS_USB_XFER_CTRL_SET_FEAT ||
           k == OCTANS_USB_XFER_INTR_OUT;
}

static int is_rx_xfer(uint8_t k) {
    return k == OCTANS_USB_XFER_CTRL_GET_FEAT ||
           k == OCTANS_USB_XFER_CTRL_GET_IN ||
           k == OCTANS_USB_XFER_INTR_IN;
}

static int is_all_zero(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (b[i] != 0u) return 0;
    }
    return 1;
}

/* Convert raw USB wire payload into Windows HID report buffer (with Report ID at byte 0).
 * Sets *out_shift = 1 if a leading 0x00 Report ID was prepended, else 0. */
static size_t normalize_wire_to_win_hid(const uint8_t *wire,
                                        size_t wire_len,
                                        uint8_t ctrl_report_id,
                                        size_t win_report_len,
                                        int force_prepend_zero,
                                        uint8_t *out,
                                        size_t out_cap,
                                        int *out_shift) {
    if (!wire || wire_len == 0 || !out || out_cap == 0) return 0;
    int prepend = 0;
    uint8_t lead = 0x00u;

    if (force_prepend_zero) {
        prepend = 1;
        lead = 0x00u;
    } else if (ctrl_report_id != 0u) {
        if (wire[0] != ctrl_report_id) {
            prepend = 1;
            lead = ctrl_report_id;
        }
    } else if (win_report_len > 0u) {
        if (wire_len + 1u == win_report_len) {
            prepend = 1;
            lead = 0x00u;
        } else if (wire_len == win_report_len) {
            prepend = 0;
        } else if (wire_len < win_report_len && wire[0] != 0x00u) {
            prepend = 0;
        }
    }

    size_t pos = 0;
    if (prepend && pos < out_cap) {
        out[pos++] = lead;
    }
    size_t cp = wire_len;
    if (pos + cp > out_cap) cp = out_cap - pos;
    memcpy(out + pos, wire, cp);
    pos += cp;

    if (out_shift) *out_shift = prepend;
    return pos;
}

static size_t trim_trailing_zeros(const uint8_t *buf, size_t len) {
    size_t n = len;
    while (n > 2u && buf[n - 1u] == 0u) --n;
    return n;
}

static int candidate_exists(const octans_learn_candidate_t *arr,
                            size_t count,
                            const octans_user_rule_t *r) {
    for (size_t i = 0; i < count; ++i) {
        const octans_user_rule_t *e = &arr[i].rule;
        if (e->usage_page == r->usage_page &&
            e->usage == r->usage &&
            e->interface_num == r->interface_num &&
            e->tx == r->tx &&
            e->batt_offset == r->batt_offset &&
            e->batt_mode == r->batt_mode &&
            e->req_hex_len == r->req_hex_len &&
            (e->req_hex_len == 0 || memcmp(e->req, r->req, e->req_hex_len) == 0) &&
            e->resp_head_len == r->resp_head_len &&
            (e->resp_head_len == 0 || memcmp(e->resp_head, r->resp_head, e->resp_head_len) == 0)) {
            return 1;
        }
    }
    return 0;
}

static void insert_candidate_sorted(octans_learn_candidate_t *arr,
                                    size_t *count,
                                    size_t max_out,
                                    const octans_learn_candidate_t *cand) {
    if (!arr || !count || max_out == 0 || !cand) return;
    if (candidate_exists(arr, *count, &cand->rule)) return;

    size_t n = *count;
    size_t pos = n;
    while (pos > 0 && arr[pos - 1u].score < cand->score) {
        --pos;
    }
    if (pos >= max_out) return;

    size_t new_n = (n < max_out) ? (n + 1u) : max_out;
    for (size_t k = new_n - 1u; k > pos; --k) {
        arr[k] = arr[k - 1u];
    }
    arr[pos] = *cand;
    *count = new_n;
}

static void build_resp_head_and_mask(const octans_usb_pkt_t *pkts,
                                     size_t pkt_count,
                                     const octans_usb_pkt_t *tx_pkt,
                                     const octans_usb_pkt_t *rx_pkt,
                                     const uint8_t *win_rx,
                                     size_t win_rx_len,
                                     int rx_shift,
                                     size_t batt_off,
                                     uint8_t *head_out,
                                     uint8_t *mask_out,
                                     size_t *head_len_out) {
    size_t hlen = batt_off;
    if (hlen > 4u) hlen = 4u;
    if (hlen == 0u && win_rx_len >= 1u) hlen = 1u;
    if (hlen > win_rx_len) hlen = win_rx_len;

    memcpy(head_out, win_rx, hlen);
    memset(mask_out, 0xFFu, hlen);

    /* If the same TX produced multiple RX packets, mask out any varying header byte (e.g., seq counter). */
    if (tx_pkt) {
        for (size_t i = 0; i < pkt_count; ++i) {
            const octans_usb_pkt_t *p = &pkts[i];
            if (p == rx_pkt) continue;
            if (p->xfer_kind != rx_pkt->xfer_kind || p->len != rx_pkt->len) continue;
            if (p->iface_num != rx_pkt->iface_num) continue;

            /* Find nearest preceding TX for p */
            const octans_usb_pkt_t *ptx = NULL;
            for (size_t j = i; j > 0; --j) {
                const octans_usb_pkt_t *q = &pkts[j - 1u];
                if (p->ts_100ns >= q->ts_100ns && (p->ts_100ns - q->ts_100ns) > 8000000ULL) break;
                if (is_tx_xfer(q->xfer_kind)) {
                    ptx = q;
                    break;
                }
            }
            if (!ptx || ptx->xfer_kind != tx_pkt->xfer_kind || ptx->len != tx_pkt->len ||
                memcmp(ptx->data, tx_pkt->data, tx_pkt->len) != 0) {
                continue;
            }
            for (size_t b = 0; b < hlen; ++b) {
                if (b == 0 && rx_shift) continue; /* Report ID 0x00 is constant */
                size_t wire_idx = rx_shift ? (b - 1u) : b;
                if (wire_idx < p->len && p->data[wire_idx] != head_out[b]) {
                    mask_out[b] = 0x00u;
                }
            }
        }
    }

    *head_len_out = hlen;
}

static void emit_matches_for_pair(const octans_usb_pkt_t *pkts,
                                  size_t pkt_count,
                                  const octans_usb_pkt_t *tx_pkt,
                                  const octans_usb_pkt_t *rx_pkt,
                                  const octans_hid_info_t *iface,
                                  uint16_t vid,
                                  uint16_t pid,
                                  octans_device_kind_t kind,
                                  const wchar_t *disp_name,
                                  int expected_pct,
                                  int iface_exact,
                                  octans_learn_candidate_t *out,
                                  size_t *out_count,
                                  size_t max_out) {
    octans_tx_kind_t tx_mode = TX_LISTEN;
    size_t win_tx_len = 0;
    size_t win_rx_expect = 0;

    if (rx_pkt->xfer_kind == OCTANS_USB_XFER_CTRL_GET_FEAT) {
        tx_mode = TX_FEATURE;
        win_tx_len = iface->feature_len;
        win_rx_expect = iface->feature_len;
        if (iface->feature_len == 0) return;
    } else if (tx_pkt) {
        if (tx_pkt->xfer_kind == OCTANS_USB_XFER_CTRL_SET_FEAT) {
            tx_mode = TX_FEATURE;
            win_tx_len = iface->feature_len;
            win_rx_expect = (rx_pkt->xfer_kind == OCTANS_USB_XFER_CTRL_GET_FEAT)
                ? iface->feature_len : iface->input_len;
            if (iface->feature_len == 0) return;
        } else if (tx_pkt->xfer_kind == OCTANS_USB_XFER_CTRL_SET_OUT) {
            tx_mode = TX_OUTPUT_REPORT;
            win_tx_len = iface->output_len;
            win_rx_expect = iface->input_len;
            if (iface->output_len == 0 || iface->input_len == 0) return;
        } else if (tx_pkt->xfer_kind == OCTANS_USB_XFER_INTR_OUT) {
            tx_mode = TX_OUTPUT_INTR;
            win_tx_len = iface->output_len;
            win_rx_expect = iface->input_len;
            if (iface->output_len == 0 || iface->input_len == 0) return;
        }
    } else {
        tx_mode = TX_LISTEN;
        win_rx_expect = iface->input_len;
        if (iface->input_len == 0) return;
    }

    /* Check wire length plausibility against this HID interface */
    if (win_rx_expect > 0u) {
        if (rx_pkt->len > win_rx_expect) return;
    }
    if (tx_pkt && win_tx_len > 0u) {
        if (tx_pkt->len > win_tx_len) return;
    }

    uint8_t win_rx[OCTANS_ETW_MAX_PKT_DATA + 2];
    int rx_shift = 0;
    size_t win_rx_len = normalize_wire_to_win_hid(
        rx_pkt->data, rx_pkt->len, rx_pkt->report_id, win_rx_expect, 0,
        win_rx, sizeof(win_rx), &rx_shift);
    if (win_rx_len < 2u || is_all_zero(win_rx, win_rx_len)) return;

    /* For short TX packets where Report-ID-0 presence is ambiguous, try both framings */
    int tx_variants = 1;
    if (tx_pkt && tx_pkt->report_id == 0u && win_tx_len > 0u &&
        tx_pkt->len + 1u < win_tx_len && tx_pkt->data[0] != 0x00u) {
        tx_variants = 2;
    }

    for (int tv = 0; tv < tx_variants; ++tv) {
        uint8_t win_tx[OCTANS_ETW_MAX_PKT_DATA + 2];
        size_t win_tx_used = 0;
        if (tx_pkt) {
            int tx_shift = 0;
            win_tx_used = normalize_wire_to_win_hid(
                tx_pkt->data, tx_pkt->len, tx_pkt->report_id, win_tx_len,
                (tv == 1), win_tx, sizeof(win_tx), &tx_shift);
            if (win_tx_used == 0 || is_all_zero(win_tx, win_tx_used)) continue;
            win_tx_used = trim_trailing_zeros(win_tx, win_tx_used);
        }

        /* Scan reply bytes (skip byte 0 which is Windows Report ID) */
        for (size_t off = 1u; off < win_rx_len; ++off) {
            uint8_t b = win_rx[off];
            uint8_t modes[4];
            int mode_scores[4];
            size_t mcount = 0;

            if ((int)b == expected_pct) {
                modes[mcount] = RULE_BATT_PERCENT;
                mode_scores[mcount] = 100;
                ++mcount;
            }
            if (expected_pct % 10 == 0 && (int)b == expected_pct / 10 && b >= 1u && b <= 10u) {
                modes[mcount] = RULE_BATT_STEP10;
                mode_scores[mcount] = 65;
                ++mcount;
            }
            if (expected_pct % 5 == 0 && (int)b == expected_pct / 5 && b >= 1u && b <= 20u) {
                modes[mcount] = RULE_BATT_STEP5;
                mode_scores[mcount] = 75;
                ++mcount;
            }
            if (off + 1u < win_rx_len) {
                uint16_t mv_be = (uint16_t)(((uint16_t)win_rx[off] << 8) | win_rx[off + 1u]);
                if (mv_be >= 3100u && mv_be <= 4350u) {
                    int p_be = octans_mv_to_percent(mv_be, CURVE_SOLAAR_13PT);
                    if (abs(p_be - expected_pct) <= 4) {
                        modes[mcount] = RULE_BATT_MV_BE;
                        mode_scores[mcount] = 70;
                        ++mcount;
                    }
                }
                uint16_t mv_le = (uint16_t)(((uint16_t)win_rx[off + 1u] << 8) | win_rx[off]);
                if (mv_le >= 3100u && mv_le <= 4350u && mcount < 4u) {
                    int p_le = octans_mv_to_percent(mv_le, CURVE_SOLAAR_13PT);
                    if (abs(p_le - expected_pct) <= 4) {
                        modes[mcount] = RULE_BATT_MV_LE;
                        mode_scores[mcount] = 70;
                        ++mcount;
                    }
                }
            }

            for (size_t m = 0; m < mcount; ++m) {
                octans_learn_candidate_t cand;
                memset(&cand, 0, sizeof(cand));
                octans_user_rule_t *r = &cand.rule;

                snprintf(r->section, sizeof(r->section), "Learned_%04X_%04X",
                         (unsigned)vid, (unsigned)pid);
                if (disp_name && disp_name[0]) {
                    wcsncpy(r->name, disp_name, 63);
                    r->name[63] = L'\0';
                } else if (iface->product[0]) {
                    wcsncpy(r->name, iface->product, 63);
                    r->name[63] = L'\0';
                }
                r->vid = vid;
                r->pid = pid;
                r->usage_page = iface->usage_page;
                r->usage = iface->usage;
                r->interface_num = (int16_t)iface->interface_num;
                r->kind = kind;
                r->conn = iface->is_bluetooth ? OCTANS_CONN_BLUETOOTH : OCTANS_CONN_DONGLE_24G;
                r->tx = tx_mode;
                r->batt_offset = (int)off;
                r->batt_mode = modes[m];
                r->chg_offset = -1;
                r->chg_mask = 0x01u;
                r->chg_eq = -1;

                if (tx_pkt) {
                    size_t cplen = (win_tx_used <= sizeof(r->req)) ? win_tx_used : sizeof(r->req);
                    memcpy(r->req, win_tx, cplen);
                    r->req_hex_len = cplen;
                    r->req_len = win_tx_len;
                }

                build_resp_head_and_mask(pkts, pkt_count, tx_pkt, rx_pkt,
                                         win_rx, win_rx_len, rx_shift, off,
                                         r->resp_head, r->resp_mask, &r->resp_head_len);
                r->resp_mask_len = r->resp_head_len;

                int score = mode_scores[m];
                if (tx_pkt) score += 30; /* Request-response strongly preferred over passive guess */
                if (iface_exact) score += 25;
                if (iface->usage_page >= 0xFF00u) score += 20;
                if (win_rx_expect > 0u &&
                    (rx_pkt->len == win_rx_expect || rx_pkt->len + 1u == win_rx_expect)) {
                    score += 15;
                }
                if (off >= 2u && off <= 16u) score += 10; /* Battery byte is typically in bytes 2..16 */
                if (tv == 1) score -= 5;
                /* Slight penalty if batt byte equals packet length (common false positive like 64 == 0x40) */
                if ((size_t)b == win_rx_len || (size_t)b == rx_pkt->len) score -= 18;
                /* Slight penalty if batt byte collides with a byte inside the TX command header */
                if (tx_pkt && off < win_tx_used && win_tx[off] == b) score -= 12;

                cand.score = score;
                insert_candidate_sorted(out, out_count, max_out, &cand);
            }
        }
    }
}

size_t octans_etw_infer_candidates(const octans_usb_pkt_t *pkts,
                                   size_t pkt_count,
                                   uint16_t vid,
                                   uint16_t pid,
                                   octans_device_kind_t kind,
                                   const wchar_t *disp_name,
                                   const octans_hid_info_t *ifaces,
                                   size_t iface_count,
                                   int expected_pct,
                                   octans_learn_candidate_t *out,
                                   size_t max_out) {
    if (!pkts || pkt_count == 0 || !ifaces || iface_count == 0 || !out || max_out == 0) return 0;
    if (expected_pct < 1 || expected_pct > 100) return 0;

    size_t out_count = 0;

    /* Walk packets in reverse (most recent driver interaction first) */
    for (size_t idx = pkt_count; idx > 0; --idx) {
        const octans_usb_pkt_t *rx = &pkts[idx - 1u];
        if (!is_rx_xfer(rx->xfer_kind) || rx->len < 2u) continue;
        if (rx->vid != vid || rx->pid != pid) continue;

        /* Find nearest preceding TX within 800 ms (prefer same interface) */
        const octans_usb_pkt_t *tx_same = NULL;
        const octans_usb_pkt_t *tx_any = NULL;
        for (size_t j = idx - 1u; j > 0; --j) {
            const octans_usb_pkt_t *q = &pkts[j - 1u];
            if (rx->ts_100ns >= q->ts_100ns && (rx->ts_100ns - q->ts_100ns) > 8000000ULL) {
                break;
            }
            if (!is_tx_xfer(q->xfer_kind)) continue;
            if (q->vid != vid || q->pid != pid) continue;
            if (!tx_any) tx_any = q;
            if (rx->iface_num >= 0 && q->iface_num == rx->iface_num) {
                tx_same = q;
                break;
            }
        }
        const octans_usb_pkt_t *tx = tx_same ? tx_same : tx_any;

        /* Match against vendor HID interfaces belonging to this VID:PID */
        for (size_t k = 0; k < iface_count; ++k) {
            const octans_hid_info_t *it = &ifaces[k];
            if (it->vid != vid || it->pid != pid || it->is_system_exclusive) continue;
            if (it->usage_page < 0xFF00u) continue;

            int iface_exact = 0;
            if (rx->iface_num >= 0 && it->interface_num >= 0) {
                if (it->interface_num != rx->iface_num) continue;
                iface_exact = 1;
            } else if (tx && tx->iface_num >= 0 && it->interface_num >= 0) {
                if (it->interface_num != tx->iface_num) continue;
                iface_exact = 1;
            }

            emit_matches_for_pair(pkts, pkt_count, tx, rx, it, vid, pid, kind, disp_name,
                                  expected_pct, iface_exact, out, &out_count, max_out);

            /* For GET_REPORT Feature, also allow pure GET_FEATURE without preceding SET */
            if (rx->xfer_kind == OCTANS_USB_XFER_CTRL_GET_FEAT && tx != NULL) {
                emit_matches_for_pair(pkts, pkt_count, NULL, rx, it, vid, pid, kind, disp_name,
                                      expected_pct, iface_exact, out, &out_count, max_out);
            }
        }
    }

    return out_count;
}

typedef struct replay_cache_entry {
    const octans_hid_info_t *iface;
    octans_tx_kind_t tx;
    uint8_t req[96];
    size_t req_hex_len;
    size_t req_len;
    uint8_t resp_head0;
    int queried;
    int got_reply;
    uint8_t raw[OCTANS_ETW_MAX_PKT_DATA];
    size_t raw_len;
} replay_cache_entry_t;

static int pct_matches_expected(int live_pct, int expected_pct, uint8_t batt_mode) {
    if (live_pct < 1 || live_pct > 100) return 0;
    int tol = (batt_mode == RULE_BATT_STEP10 || batt_mode == RULE_BATT_MV_BE || batt_mode == RULE_BATT_MV_LE)
        ? 5 : 2;
    return abs(live_pct - expected_pct) <= tol;
}

size_t octans_etw_verify_candidates(octans_learn_candidate_t *cands,
                                    size_t cand_count,
                                    const octans_hid_info_t *ifaces,
                                    size_t iface_count,
                                    int expected_pct,
                                    int *out_best_idx,
                                    int *out_ambiguous_count) {
    if (out_best_idx) *out_best_idx = -1;
    if (out_ambiguous_count) *out_ambiguous_count = 0;
    if (!cands || cand_count == 0 || !ifaces || iface_count == 0) return 0;

    replay_cache_entry_t rcache[24];
    size_t rcache_count = 0;
    size_t verified_count = 0;
    int best_idx = -1;
    int best_score = -100000;

    for (size_t i = 0; i < cand_count; ++i) {
        octans_learn_candidate_t *c = &cands[i];
        c->verified = 0;
        c->live_pct = -1;
        c->live_raw_len = 0;

        /* Find matching connected HID interface */
        const octans_hid_info_t *iface = NULL;
        for (size_t k = 0; k < iface_count; ++k) {
            if (octans_user_rule_matches_iface(&c->rule, &ifaces[k])) {
                iface = &ifaces[k];
                break;
            }
        }
        if (!iface) continue;

        uint8_t r0 = (c->rule.resp_head_len > 0) ? c->rule.resp_head[0] : 0u;

        /* Reuse live hardware reply if we already sent this exact (iface, tx, req) */
        replay_cache_entry_t *ent = NULL;
        for (size_t r = 0; r < rcache_count; ++r) {
            if (rcache[r].iface == iface &&
                rcache[r].tx == c->rule.tx &&
                rcache[r].req_hex_len == c->rule.req_hex_len &&
                rcache[r].req_len == c->rule.req_len &&
                rcache[r].resp_head0 == r0 &&
                (c->rule.req_hex_len == 0 || memcmp(rcache[r].req, c->rule.req, c->rule.req_hex_len) == 0)) {
                ent = &rcache[r];
                break;
            }
        }

        if (!ent) {
            if (rcache_count >= 24u) continue; /* Bound hardware I/O latency */
            ent = &rcache[rcache_count++];
            memset(ent, 0, sizeof(*ent));
            ent->iface = iface;
            ent->tx = c->rule.tx;
            ent->req_hex_len = c->rule.req_hex_len;
            ent->req_len = c->rule.req_len;
            ent->resp_head0 = r0;
            if (c->rule.req_hex_len > 0) {
                memcpy(ent->req, c->rule.req, c->rule.req_hex_len);
            }
            ent->queried = 1;

            octans_proto_reading_t tmp_read;
            size_t raw_len = 0;
            int qok = octans_user_rule_query_iface(
                &c->rule, iface, 320u, &tmp_read,
                ent->raw, sizeof(ent->raw), &raw_len);
            ent->raw_len = raw_len;
            ent->got_reply = (qok || raw_len > 0) ? 1 : 0;
            if (qok && tmp_read.valid && pct_matches_expected(tmp_read.battery_percent, expected_pct, c->rule.batt_mode)) {
                c->verified = 1;
                c->live_pct = tmp_read.battery_percent;
                c->live_power_state = (uint8_t)tmp_read.power_state;
                c->live_raw_len = raw_len;
                if (raw_len > 0) memcpy(c->live_raw, ent->raw, raw_len);
            }
        } else if (ent->got_reply && ent->raw_len > 0) {
            octans_proto_reading_t parsed;
            memset(&parsed, 0, sizeof(parsed));
            if (octans_user_rule_parse_reply(&c->rule, ent->raw, ent->raw_len, &parsed) &&
                parsed.valid && pct_matches_expected(parsed.battery_percent, expected_pct, c->rule.batt_mode)) {
                c->verified = 1;
                c->live_pct = parsed.battery_percent;
                c->live_power_state = (uint8_t)parsed.power_state;
                c->live_raw_len = ent->raw_len;
                memcpy(c->live_raw, ent->raw, ent->raw_len);
            }
        }

        if (c->verified) {
            ++verified_count;
            int adj_score = c->score + ((c->live_pct == expected_pct) ? 20 : 0);
            if (adj_score > best_score) {
                best_score = adj_score;
                best_idx = (int)i;
            }
        }
    }

    if (out_best_idx) *out_best_idx = best_idx;
    if (out_ambiguous_count) {
        /* Count distinct batt_offset positions that verified on the best TX/iface */
        int amb = 0;
        if (best_idx >= 0) {
            const octans_learn_candidate_t *best = &cands[best_idx];
            for (size_t i = 0; i < cand_count; ++i) {
                if (!cands[i].verified) continue;
                if (cands[i].rule.batt_offset != best->rule.batt_offset ||
                    cands[i].rule.batt_mode != best->rule.batt_mode) {
                    ++amb;
                }
            }
        }
        *out_ambiguous_count = amb;
    }
    return verified_count;
}

int octans_etw_refine_with_second_sample(octans_learn_candidate_t *cands,
                                         size_t cand_count,
                                         const octans_hid_info_t *ifaces,
                                         size_t iface_count,
                                         int new_pct,
                                         int is_charging_now,
                                         int *out_best_idx) {
    if (out_best_idx) *out_best_idx = -1;
    if (!cands || cand_count == 0 || !ifaces || iface_count == 0) return 0;

    int remaining = 0;
    int best_idx = -1;
    int best_score = -100000;

    for (size_t i = 0; i < cand_count; ++i) {
        octans_learn_candidate_t *c = &cands[i];
        if (!c->verified) continue;

        const octans_hid_info_t *iface = NULL;
        for (size_t k = 0; k < iface_count; ++k) {
            if (octans_user_rule_matches_iface(&c->rule, &ifaces[k])) {
                iface = &ifaces[k];
                break;
            }
        }
        if (!iface) {
            c->verified = 0;
            continue;
        }

        uint8_t new_raw[OCTANS_ETW_MAX_PKT_DATA];
        size_t new_raw_len = 0;
        octans_proto_reading_t r;
        if (!octans_user_rule_query_iface(&c->rule, iface, 320u, &r, new_raw, sizeof(new_raw), &new_raw_len) ||
            !r.valid || !pct_matches_expected(r.battery_percent, new_pct, c->rule.batt_mode)) {
            c->verified = 0;
            continue;
        }

        /* Auto-discover charging flag byte if charger was plugged in and one byte flipped 0 -> 1/2 */
        if (is_charging_now && c->live_raw_len > 0 && new_raw_len == c->live_raw_len) {
            for (size_t b = 1u; b < new_raw_len; ++b) {
                if ((int)b == c->rule.batt_offset) continue;
                if (c->live_raw[b] == 0u && (new_raw[b] == 1u || new_raw[b] == 2u)) {
                    c->rule.chg_offset = (int)b;
                    c->rule.chg_eq = (int)new_raw[b];
                    break;
                }
            }
        }

        c->live_pct = r.battery_percent;
        c->live_power_state = is_charging_now ? OCTANS_PWR_CHARGING : (uint8_t)r.power_state;
        if (new_raw_len > 0) {
            memcpy(c->live_raw, new_raw, new_raw_len);
            c->live_raw_len = new_raw_len;
        }
        ++remaining;
        if (c->score > best_score) {
            best_score = c->score;
            best_idx = (int)i;
        }
    }

    if (out_best_idx) *out_best_idx = best_idx;
    return remaining;
}
