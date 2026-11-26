#ifndef OCTANS_UI_UPDATER_H
#define OCTANS_UI_UPDATER_H

#include "ui_internal.h"

namespace octans_ui {

struct UpdateInfo {
    char latest_version[32] = {};
    wchar_t release_notes[512] = {};
    char sha256[65] = {};
    wchar_t download_cn[256] = {};
    wchar_t download_global[256] = {};
    wchar_t page_cn[256] = {};
    wchar_t page_global[256] = {};
    bool has_update = false;
    bool used_cn_mirror = false;
    wchar_t error_msg[128] = {};
};

/* Compare two semantic version strings (e.g. "0.5.0" vs "v0.5.1").
 * Returns >0 if a > b, 0 if equal, <0 if a < b. */
int CompareSemver(const char *a, const char *b);

/* Blocking check for updates (run on worker thread; dynamically loads winhttp.dll on demand).
 * Returns true if check completed (even if already up-to-date), false on network error. */
bool CheckForUpdatesSync(UpdateInfo *out);

/* Return current UTC-based system time in seconds (for 24h update check throttling). */
uint64_t GetCurrentUnixTimeSec();

typedef void (*UpdateProgressFn)(int percent, bool verifying_sha, void *user_ctx);

/* Blocking download + SHA256 verification (bcrypt.dll dynamically loaded on demand).
 * Tries AtomGit CN mirror first, falls back to GitHub. Returns true when verified .exe is ready. */
bool DownloadAndVerifyInstallerSync(
    const UpdateInfo &info,
    UpdateProgressFn cb,
    void *user_ctx,
    wchar_t *out_exe_path,
    size_t out_cap,
    wchar_t *err_msg,
    size_t err_cap
);

/* Launch verified Inno Setup installer silently to upgrade & relaunch Octans. */
bool LaunchSilentInstaller(const wchar_t *exe_path);

} /* namespace octans_ui */

#endif /* OCTANS_UI_UPDATER_H */
