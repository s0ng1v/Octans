#ifndef OCTANS_USB_ETW_LEARN_H
#define OCTANS_USB_ETW_LEARN_H

#include <stddef.h>
#include <stdint.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../core/hid_enum.h"
#include "../engine/user_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OCTANS_ETW_MAX_PKT_DATA  96u
#define OCTANS_ETW_MAX_PKTS      4096u
#define OCTANS_ETW_MAX_CANDS     48u

typedef enum octans_usb_xfer_kind {
    OCTANS_USB_XFER_NONE = 0,
    OCTANS_USB_XFER_CTRL_SET_OUT  = 1, /* HID SET_REPORT Output  (TX) */
    OCTANS_USB_XFER_CTRL_SET_FEAT = 2, /* HID SET_REPORT Feature (TX) */
    OCTANS_USB_XFER_CTRL_GET_FEAT = 3, /* HID GET_REPORT Feature (RX) */
    OCTANS_USB_XFER_CTRL_GET_IN   = 4, /* HID GET_REPORT Input   (RX) */
    OCTANS_USB_XFER_INTR_OUT      = 5, /* HID Interrupt OUT      (TX) */
    OCTANS_USB_XFER_INTR_IN       = 6  /* HID Interrupt IN       (RX) */
} octans_usb_xfer_kind_t;

typedef struct octans_usb_pkt {
    uint64_t ts_100ns;
    uint16_t vid;
    uint16_t pid;
    int16_t iface_num; /**< -1 if unknown, else USB bInterfaceNumber (MI_xx) */
    uint8_t xfer_kind; /**< octans_usb_xfer_kind_t */
    uint8_t report_id; /**< Control wValue low byte, or 0 */
    uint8_t ep_addr;   /**< 0 for Control, else bEndpointAddress */
    uint16_t len;      /**< Payload byte length stored in data[] */
    uint8_t data[OCTANS_ETW_MAX_PKT_DATA];
} octans_usb_pkt_t;

typedef struct octans_usb_ep_map {
    /* Slot index: (ep_addr & 0x0F) + ((ep_addr & 0x80) ? 16 : 0) */
    int8_t ep_to_iface[32];
    uint8_t ep_is_boot_hid[32];
} octans_usb_ep_map_t;

typedef struct octans_learn_candidate {
    octans_user_rule_t rule;
    int score;
    int verified;
    int live_pct;
    uint8_t live_power_state;
    uint8_t live_raw[OCTANS_ETW_MAX_PKT_DATA];
    size_t live_raw_len;
} octans_learn_candidate_t;

typedef struct octans_etw_session octans_etw_session_t;

/** True if current process token is elevated (Administrator). */
int octans_etw_is_elevated(void);

/**
 * Parse a raw USB Configuration Descriptor into an endpoint -> interface map
 * and boot mouse/keyboard classification flags.
 */
void octans_usb_parse_config_desc(const uint8_t *cfg,
                                  size_t len,
                                  octans_usb_ep_map_t *out);

/**
 * Start a real-time Windows native USB ETW capture session (`Microsoft-Windows-USB-USBHUB3`
 * + `Microsoft-Windows-USB-UCX`) filtered strictly to `target_vid:target_pid`.
 * `vendor_iface_mask` is a bitmask of USB `interface_num` (0..31) having Vendor HID (`UP >= 0xFF00`).
 * Requires Administrator privileges; on failure returns NULL and sets `*out_err`.
 */
octans_etw_session_t *octans_etw_start(uint16_t target_vid,
                                       uint16_t target_pid,
                                       uint32_t vendor_iface_mask,
                                       DWORD *out_err);

/** Read live capture statistics while the ETW session is running. */
void octans_etw_get_stats(const octans_etw_session_t *s,
                          uint32_t *tx_count,
                          uint32_t *rx_count,
                          int *dev_seen);

/** Stop ETW session cleanly, copy captured packets into `out[0..max_pkts-1]`, and free `s`. */
size_t octans_etw_stop_and_copy(octans_etw_session_t *s,
                                octans_usb_pkt_t *out,
                                size_t max_pkts);

/**
 * Pure protocol inference: given captured USB HID packets for `(vid, pid)` and
 * the battery percentage `expected_pct` (1..100) shown by the official driver,
 * synthesize ranked `octans_learn_candidate_t` rules aligned to Windows HID report framing.
 */
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
                                   size_t max_out);

/**
 * Live independent verification: replays inferred candidate requests directly against
 * the connected 2.4G HID interfaces (without the official driver) and marks candidates
 * whose real hardware response reproduces `expected_pct`.
 * Returns number of verified candidates; sets `*out_best_idx` and `*out_ambiguous_count`.
 */
size_t octans_etw_verify_candidates(octans_learn_candidate_t *cands,
                                    size_t cand_count,
                                    const octans_hid_info_t *ifaces,
                                    size_t iface_count,
                                    int expected_pct,
                                    int *out_best_idx,
                                    int *out_ambiguous_count);

/**
 * Optional second-sample disambiguation & charging-bit discovery.
 * Re-queries surviving verified candidates with a new observed `new_pct` (1..100)
 * and `is_charging_now` (0/1). Updates `chg_offset`/`chg_eq` when a clean state flip is observed.
 * Returns number of remaining matching candidates, updating `*out_best_idx`.
 */
int octans_etw_refine_with_second_sample(octans_learn_candidate_t *cands,
                                         size_t cand_count,
                                         const octans_hid_info_t *ifaces,
                                         size_t iface_count,
                                         int new_pct,
                                         int is_charging_now,
                                         int *out_best_idx);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_USB_ETW_LEARN_H */
