#include "ui_updater.h"
#include "octans_version.h"
#include <stdio.h>
#include <stdlib.h>

namespace octans_ui {

/* Default dual-source update endpoints (CN fast AtomGit primary + GitHub fallback) */
static constexpr const wchar_t *kUrlManifestCn     = L"https://raw.atomgit.com/s0ng1v/Octans/raw/main/latest.json";
static constexpr const wchar_t *kUrlManifestGlobal = L"https://raw.githubusercontent.com/s0ng1v/Octans/main/latest.json";
static constexpr const wchar_t *kUrlGithubApi      = L"https://api.github.com/repos/s0ng1v/Octans/releases/latest";
static constexpr const wchar_t *kDefaultPageCn     = L"https://atomgit.com/s0ng1v/Octans/releases";
static constexpr const wchar_t *kDefaultPageGlobal = L"https://github.com/s0ng1v/Octans/releases/latest";

/* ============================================================================
 * Minimal WinHTTP & CNG BCrypt Dynamic Bindings (ZERO static DLL imports!)
 * ============================================================================ */

typedef LPVOID HINTERNET;
typedef WORD INTERNET_PORT;

static constexpr DWORD kWinHttpAccessDefaultProxy = 0;
static constexpr DWORD kWinHttpFlagSecure         = 0x00800000;
static constexpr DWORD kWinHttpQueryStatusCode    = 19;
static constexpr DWORD kWinHttpQueryContentLen    = 5;
static constexpr DWORD kWinHttpQueryFlagNumber    = 0x20000000;
static constexpr DWORD kWinHttpOptionRedirect     = 63;
static constexpr DWORD kWinHttpRedirectAlways     = 2;

struct WinHttpUrlComponents {
    DWORD dwStructSize;
    LPWSTR lpszScheme;
    DWORD dwSchemeLength;
    int nScheme;
    LPWSTR lpszHostName;
    DWORD dwHostNameLength;
    INTERNET_PORT nPort;
    LPWSTR lpszUserName;
    DWORD dwUserNameLength;
    LPWSTR lpszPassword;
    DWORD dwPasswordLength;
    LPWSTR lpszUrlPath;
    DWORD dwUrlPathLength;
    LPWSTR lpszExtraInfo;
    DWORD dwExtraInfoLength;
};

struct WinHttpApi {
    HMODULE dll = nullptr;
    HINTERNET (WINAPI *Open)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) = nullptr;
    BOOL (WINAPI *SetTimeouts)(HINTERNET, int, int, int, int) = nullptr;
    BOOL (WINAPI *CrackUrl)(LPCWSTR, DWORD, DWORD, WinHttpUrlComponents *) = nullptr;
    HINTERNET (WINAPI *Connect)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD) = nullptr;
    HINTERNET (WINAPI *OpenRequest)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD) = nullptr;
    BOOL (WINAPI *SetOption)(HINTERNET, DWORD, LPVOID, DWORD) = nullptr;
    BOOL (WINAPI *SendRequest)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR) = nullptr;
    BOOL (WINAPI *ReceiveResponse)(HINTERNET, LPVOID) = nullptr;
    BOOL (WINAPI *QueryHeaders)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD) = nullptr;
    BOOL (WINAPI *ReadData)(HINTERNET, LPVOID, DWORD, LPDWORD) = nullptr;
    BOOL (WINAPI *CloseHandle)(HINTERNET) = nullptr;

    bool Load() {
        dll = LoadLibraryW(L"winhttp.dll");
        if (!dll) return false;
        Open            = reinterpret_cast<decltype(Open)>(GetProcAddress(dll, "WinHttpOpen"));
        SetTimeouts     = reinterpret_cast<decltype(SetTimeouts)>(GetProcAddress(dll, "WinHttpSetTimeouts"));
        CrackUrl        = reinterpret_cast<decltype(CrackUrl)>(GetProcAddress(dll, "WinHttpCrackUrl"));
        Connect         = reinterpret_cast<decltype(Connect)>(GetProcAddress(dll, "WinHttpConnect"));
        OpenRequest     = reinterpret_cast<decltype(OpenRequest)>(GetProcAddress(dll, "WinHttpOpenRequest"));
        SetOption       = reinterpret_cast<decltype(SetOption)>(GetProcAddress(dll, "WinHttpSetOption"));
        SendRequest     = reinterpret_cast<decltype(SendRequest)>(GetProcAddress(dll, "WinHttpSendRequest"));
        ReceiveResponse = reinterpret_cast<decltype(ReceiveResponse)>(GetProcAddress(dll, "WinHttpReceiveResponse"));
        QueryHeaders    = reinterpret_cast<decltype(QueryHeaders)>(GetProcAddress(dll, "WinHttpQueryHeaders"));
        ReadData        = reinterpret_cast<decltype(ReadData)>(GetProcAddress(dll, "WinHttpReadData"));
        CloseHandle     = reinterpret_cast<decltype(CloseHandle)>(GetProcAddress(dll, "WinHttpCloseHandle"));
        return Open && SetTimeouts && CrackUrl && Connect && OpenRequest &&
               SendRequest && ReceiveResponse && QueryHeaders && ReadData && CloseHandle;
    }

    ~WinHttpApi() {
        if (dll) FreeLibrary(dll);
    }
};

/* Dynamic CNG BCrypt SHA-256 (bcrypt.dll, built into Windows 7/10/11) */
typedef LONG NTSTATUS_T;
typedef PVOID BCRYPT_ALG_HANDLE_T;
typedef PVOID BCRYPT_HASH_HANDLE_T;

static bool ComputeFileSha256Hex(const wchar_t *path, char out_hex[65]) {
    if (!path || !out_hex) return false;
    out_hex[0] = '\0';

    HMODULE bcdll = LoadLibraryW(L"bcrypt.dll");
    if (!bcdll) return false;

    auto pOpenAlg  = reinterpret_cast<NTSTATUS_T(WINAPI *)(BCRYPT_ALG_HANDLE_T *, LPCWSTR, LPCWSTR, ULONG)>(GetProcAddress(bcdll, "BCryptOpenAlgorithmProvider"));
    auto pCloseAlg = reinterpret_cast<NTSTATUS_T(WINAPI *)(BCRYPT_ALG_HANDLE_T, ULONG)>(GetProcAddress(bcdll, "BCryptCloseAlgorithmProvider"));
    auto pGetProp  = reinterpret_cast<NTSTATUS_T(WINAPI *)(PVOID, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG)>(GetProcAddress(bcdll, "BCryptGetProperty"));
    auto pCreateH  = reinterpret_cast<NTSTATUS_T(WINAPI *)(BCRYPT_ALG_HANDLE_T, BCRYPT_HASH_HANDLE_T *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG)>(GetProcAddress(bcdll, "BCryptCreateHash"));
    auto pHashData = reinterpret_cast<NTSTATUS_T(WINAPI *)(BCRYPT_HASH_HANDLE_T, PUCHAR, ULONG, ULONG)>(GetProcAddress(bcdll, "BCryptHashData"));
    auto pFinishH  = reinterpret_cast<NTSTATUS_T(WINAPI *)(BCRYPT_HASH_HANDLE_T, PUCHAR, ULONG, ULONG)>(GetProcAddress(bcdll, "BCryptFinishHash"));
    auto pDestroyH = reinterpret_cast<NTSTATUS_T(WINAPI *)(BCRYPT_HASH_HANDLE_T)>(GetProcAddress(bcdll, "BCryptDestroyHash"));

    if (!pOpenAlg || !pCloseAlg || !pGetProp || !pCreateH || !pHashData || !pFinishH || !pDestroyH) {
        FreeLibrary(bcdll);
        return false;
    }

    HANDLE fh = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        FreeLibrary(bcdll);
        return false;
    }

    bool ok = false;
    BCRYPT_ALG_HANDLE_T alg = nullptr;
    BCRYPT_HASH_HANDLE_T hash = nullptr;
    BYTE *obj_buf = nullptr;

    if (pOpenAlg(&alg, L"SHA256", nullptr, 0) >= 0) {
        DWORD obj_len = 0, cb_res = 0;
        if (pGetProp(alg, L"ObjectLength", reinterpret_cast<PUCHAR>(&obj_len), sizeof(obj_len), &cb_res, 0) >= 0 && obj_len > 0) {
            obj_buf = static_cast<BYTE *>(malloc(obj_len));
            if (obj_buf && pCreateH(alg, &hash, obj_buf, obj_len, nullptr, 0, 0) >= 0) {
                BYTE io[16384];
                DWORD rd = 0;
                bool read_ok = true;
                while (ReadFile(fh, io, sizeof(io), &rd, nullptr)) {
                    if (rd == 0) break;
                    if (pHashData(hash, io, rd, 0) < 0) {
                        read_ok = false;
                        break;
                    }
                }
                BYTE digest[32] = {};
                if (read_ok && pFinishH(hash, digest, sizeof(digest), 0) >= 0) {
                    static const char kHex[] = "0123456789abcdef";
                    for (int i = 0; i < 32; ++i) {
                        out_hex[i * 2]     = kHex[(digest[i] >> 4) & 0x0F];
                        out_hex[i * 2 + 1] = kHex[digest[i] & 0x0F];
                    }
                    out_hex[64] = '\0';
                    ok = true;
                }
            }
        }
    }

    if (hash) pDestroyH(hash);
    if (obj_buf) free(obj_buf);
    if (alg) pCloseAlg(alg, 0);
    CloseHandle(fh);
    FreeLibrary(bcdll);
    return ok;
}

/* ============================================================================
 * Semver & Lightweight JSON Parsing Helpers
 * ============================================================================ */

static void ParseSemverTriple(const char *s, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    if (!s) return;
    while (*s == ' ' || *s == '\t' || *s == 'v' || *s == 'V') ++s;
    for (int i = 0; i < 3 && *s; ++i) {
        int val = 0;
        while (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            ++s;
        }
        out[i] = val;
        if (*s == '.') ++s;
        else break;
    }
}

int CompareSemver(const char *a, const char *b) {
    int va[3], vb[3];
    ParseSemverTriple(a, va);
    ParseSemverTriple(b, vb);
    for (int i = 0; i < 3; ++i) {
        if (va[i] != vb[i]) return (va[i] > vb[i]) ? 1 : -1;
    }
    return 0;
}

static bool JsonExtractString(const char *json, const char *key, char *out, size_t cap) {
    if (!json || !key || !out || cap < 2) return false;
    out[0] = '\0';

    char pat[64];
    _snprintf(pat, sizeof(pat) - 1, "\"%s\"", key);
    pat[sizeof(pat) - 1] = '\0';

    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != ':') return false;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != '\"') return false;
    ++p;

    size_t pos = 0;
    while (*p && *p != '\"' && pos + 1 < cap) {
        if (*p == '\\' && p[1]) {
            ++p;
            if (*p == 'n') out[pos++] = '\n';
            else if (*p == 'r') { /* skip */ }
            else if (*p == 't') out[pos++] = ' ';
            else out[pos++] = *p;
            ++p;
        } else {
            out[pos++] = *p++;
        }
    }
    out[pos] = '\0';
    return pos > 0;
}

static void Utf8ToWide(const char *u8, wchar_t *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = L'\0';
    if (!u8 || !u8[0]) return;
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, out, static_cast<int>(cap));
    out[cap - 1] = L'\0';
}

/* ============================================================================
 * WinHTTP GET (Text or Streaming File Download)
 * ============================================================================ */

static bool HttpGetToBuffer(WinHttpApi &api, const wchar_t *url, char *out_buf, size_t max_bytes, size_t *out_len) {
    if (!url || !url[0] || !out_buf || max_bytes < 2) return false;
    out_buf[0] = '\0';
    if (out_len) *out_len = 0;

    wchar_t host[256] = {};
    wchar_t path[512] = {};
    WinHttpUrlComponents uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 511;
    if (!api.CrackUrl(url, 0, 0, &uc)) return false;

    bool is_https = (uc.nScheme == 2);
    HINTERNET hSes = api.Open(L"Octans/" OCTANS_VERSION_WSTR, kWinHttpAccessDefaultProxy, nullptr, nullptr, 0);
    if (!hSes) return false;
    api.SetTimeouts(hSes, 3500, 3500, 4000, 5000);

    bool ok = false;
    HINTERNET hCon = api.Connect(hSes, host, uc.nPort, 0);
    if (hCon) {
        HINTERNET hReq = api.OpenRequest(hCon, L"GET", path[0] ? path : L"/", nullptr, nullptr, nullptr, is_https ? kWinHttpFlagSecure : 0);
        if (hReq) {
            if (api.SetOption) {
                DWORD redir = kWinHttpRedirectAlways;
                api.SetOption(hReq, kWinHttpOptionRedirect, &redir, sizeof(redir));
            }
            const wchar_t *hdrs = L"Accept: application/json\r\nCache-Control: no-cache\r\n";
            if (api.SendRequest(hReq, hdrs, static_cast<DWORD>(wcslen(hdrs)), nullptr, 0, 0, 0) &&
                api.ReceiveResponse(hReq, nullptr)) {
                DWORD status = 0, slen = sizeof(status);
                api.QueryHeaders(hReq, kWinHttpQueryStatusCode | kWinHttpQueryFlagNumber, nullptr, &status, &slen, nullptr);
                if (status >= 200 && status < 300) {
                    size_t total = 0;
                    DWORD avail = 0;
                    while (total + 1 < max_bytes) {
                        DWORD want = static_cast<DWORD>((max_bytes - 1 - total > 4096) ? 4096 : (max_bytes - 1 - total));
                        if (!api.ReadData(hReq, out_buf + total, want, &avail) || avail == 0) break;
                        total += avail;
                    }
                    out_buf[total] = '\0';
                    if (out_len) *out_len = total;
                    ok = (total > 2);
                }
            }
            api.CloseHandle(hReq);
        }
        api.CloseHandle(hCon);
    }
    api.CloseHandle(hSes);
    return ok;
}

static bool HttpDownloadToFile(
    WinHttpApi &api,
    const wchar_t *url,
    const wchar_t *dest_path,
    UpdateProgressFn cb,
    void *user_ctx
) {
    if (!url || !url[0] || !dest_path || !dest_path[0]) return false;

    wchar_t host[256] = {};
    wchar_t path[512] = {};
    WinHttpUrlComponents uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 511;
    if (!api.CrackUrl(url, 0, 0, &uc)) return false;

    bool is_https = (uc.nScheme == 2);
    HINTERNET hSes = api.Open(L"Octans/" OCTANS_VERSION_WSTR, kWinHttpAccessDefaultProxy, nullptr, nullptr, 0);
    if (!hSes) return false;
    api.SetTimeouts(hSes, 5000, 6000, 15000, 20000);

    bool ok = false;
    HINTERNET hCon = api.Connect(hSes, host, uc.nPort, 0);
    if (hCon) {
        HINTERNET hReq = api.OpenRequest(hCon, L"GET", path[0] ? path : L"/", nullptr, nullptr, nullptr, is_https ? kWinHttpFlagSecure : 0);
        if (hReq) {
            if (api.SetOption) {
                DWORD redir = kWinHttpRedirectAlways;
                api.SetOption(hReq, kWinHttpOptionRedirect, &redir, sizeof(redir));
            }
            if (api.SendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) &&
                api.ReceiveResponse(hReq, nullptr)) {
                DWORD status = 0, slen = sizeof(status);
                api.QueryHeaders(hReq, kWinHttpQueryStatusCode | kWinHttpQueryFlagNumber, nullptr, &status, &slen, nullptr);
                DWORD content_len = 0, clen_sz = sizeof(content_len);
                api.QueryHeaders(hReq, kWinHttpQueryContentLen | kWinHttpQueryFlagNumber, nullptr, &content_len, &clen_sz, nullptr);

                if (status >= 200 && status < 300) {
                    HANDLE fh = CreateFileW(dest_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (fh != INVALID_HANDLE_VALUE) {
                        BYTE buf[16384];
                        DWORD got = 0;
                        uint64_t total = 0;
                        BYTE first2[2] = { 0, 0 };
                        bool write_ok = true;
                        int last_pct = -1;

                        while (api.ReadData(hReq, buf, sizeof(buf), &got) && got > 0) {
                            if (total == 0 && got >= 2) {
                                first2[0] = buf[0];
                                first2[1] = buf[1];
                            }
                            DWORD wr = 0;
                            if (!WriteFile(fh, buf, got, &wr, nullptr) || wr != got) {
                                write_ok = false;
                                break;
                            }
                            total += got;
                            if (cb) {
                                int pct = (content_len > 0)
                                    ? static_cast<int>((total * 95ULL) / content_len)
                                    : static_cast<int>((total / 32768ULL) % 90ULL);
                                if (pct > 95) pct = 95;
                                if (pct != last_pct) {
                                    last_pct = pct;
                                    cb(pct, false, user_ctx);
                                }
                            }
                        }
                        CloseHandle(fh);
                        /* Sanity: Windows PE installer must start with "MZ" and be >= 100 KB */
                        if (write_ok && total >= 102400ULL && first2[0] == 'M' && first2[1] == 'Z') {
                            ok = true;
                        } else {
                            DeleteFileW(dest_path);
                        }
                    }
                }
            }
            api.CloseHandle(hReq);
        }
        api.CloseHandle(hCon);
    }
    api.CloseHandle(hSes);
    return ok;
}

/* ============================================================================
 * Public Update API
 * ============================================================================ */

bool CheckForUpdatesSync(UpdateInfo *out) {
    if (!out) return false;
    *out = {};
    wcsncpy(out->page_cn, kDefaultPageCn, 255);
    wcsncpy(out->page_global, kDefaultPageGlobal, 255);

    WinHttpApi api;
    if (!api.Load()) {
        wcscpy(out->error_msg, L"\u7cfb\u7edf\u7f51\u7edc\u7ec4\u4ef6\u4e0d\u53ef\u7528");
        return false;
    }

    char *body = static_cast<char *>(malloc(32768));
    if (!body) return false;

    bool fetched = false;
    bool is_gh_api = false;

    /* 1. Primary: AtomGit CN raw latest.json (fast in China, no login required) */
    if (HttpGetToBuffer(api, kUrlManifestCn, body, 32768, nullptr) && strstr(body, "\"version\"")) {
        fetched = true;
        out->used_cn_mirror = true;
    }
    /* 2. Secondary: GitHub raw latest.json */
    if (!fetched && HttpGetToBuffer(api, kUrlManifestGlobal, body, 32768, nullptr) && strstr(body, "\"version\"")) {
        fetched = true;
        out->used_cn_mirror = false;
    }
    /* 3. Tertiary: GitHub Releases REST API */
    if (!fetched && HttpGetToBuffer(api, kUrlGithubApi, body, 32768, nullptr) && strstr(body, "\"tag_name\"")) {
        fetched = true;
        is_gh_api = true;
        out->used_cn_mirror = false;
    }

    if (!fetched) {
        free(body);
        wcscpy(out->error_msg, L"\u65e0\u6cd5\u8fde\u63a5\u66f4\u65b0\u670d\u52a1\u5668\uff0c\u8bf7\u68c0\u67e5\u7f51\u7edc\u6216\u624b\u52a8\u4e0b\u8f7d");
        return false;
    }

    char ver[32] = {};
    char notes[512] = {};
    char sha[65] = {};
    char dl_cn[256] = {};
    char dl_gl[256] = {};
    char pg_cn[256] = {};
    char pg_gl[256] = {};

    if (!is_gh_api) {
        JsonExtractString(body, "version", ver, sizeof(ver));
        JsonExtractString(body, "notes", notes, sizeof(notes));
        JsonExtractString(body, "sha256", sha, sizeof(sha));
        JsonExtractString(body, "download_cn", dl_cn, sizeof(dl_cn));
        JsonExtractString(body, "download_global", dl_gl, sizeof(dl_gl));
        JsonExtractString(body, "page_cn", pg_cn, sizeof(pg_cn));
        JsonExtractString(body, "page_global", pg_gl, sizeof(pg_gl));
    } else {
        JsonExtractString(body, "tag_name", ver, sizeof(ver));
        JsonExtractString(body, "body", notes, sizeof(notes));
        JsonExtractString(body, "html_url", pg_gl, sizeof(pg_gl));
        /* Find first .exe browser_download_url */
        const char *scan = body;
        while ((scan = strstr(scan, "\"browser_download_url\"")) != nullptr) {
            char cand[256] = {};
            if (JsonExtractString(scan, "browser_download_url", cand, sizeof(cand))) {
                if (strstr(cand, ".exe") != nullptr) {
                    strncpy(dl_gl, cand, sizeof(dl_gl) - 1);
                    break;
                }
            }
            scan += 22;
        }
    }

    free(body);

    if (!ver[0]) {
        wcscpy(out->error_msg, L"\u66f4\u65b0\u6e05\u5355\u89e3\u6790\u5931\u8d25\uff0c\u8bf7\u524d\u5f80\u53d1\u5e03\u9875\u67e5\u770b");
        return false;
    }

    const char *clean_v = ver;
    while (*clean_v == 'v' || *clean_v == 'V') ++clean_v;
    strncpy(out->latest_version, clean_v, sizeof(out->latest_version) - 1);
    Utf8ToWide(notes, out->release_notes, 512);
    strncpy(out->sha256, sha, sizeof(out->sha256) - 1);
    if (dl_cn[0]) Utf8ToWide(dl_cn, out->download_cn, 256);
    if (dl_gl[0]) Utf8ToWide(dl_gl, out->download_global, 256);
    if (pg_cn[0]) Utf8ToWide(pg_cn, out->page_cn, 256);
    if (pg_gl[0]) Utf8ToWide(pg_gl, out->page_global, 256);

    out->has_update = (CompareSemver(out->latest_version, OCTANS_VERSION_STR) > 0);
    return true;
}

bool DownloadAndVerifyInstallerSync(
    const UpdateInfo &info,
    UpdateProgressFn cb,
    void *user_ctx,
    wchar_t *out_exe_path,
    size_t out_cap,
    wchar_t *err_msg,
    size_t err_cap
) {
    if (!out_exe_path || out_cap < 32) return false;
    out_exe_path[0] = L'\0';
    if (err_msg && err_cap > 0) err_msg[0] = L'\0';

    WinHttpApi api;
    if (!api.Load()) {
        if (err_msg) wcsncpy(err_msg, L"\u7cfb\u7edf\u7f51\u7edc\u7ec4\u4ef6\u4e0d\u53ef\u7528", err_cap - 1);
        return false;
    }

    wchar_t tmp_dir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tmp_dir) == 0) {
        if (err_msg) wcsncpy(err_msg, L"\u65e0\u6cd5\u8bbf\u95ee\u4e34\u65f6\u76ee\u5f55", err_cap - 1);
        return false;
    }

    wchar_t ver_w[32] = {};
    Utf8ToWide(info.latest_version[0] ? info.latest_version : "latest", ver_w, 32);
    _snwprintf(out_exe_path, out_cap - 1, L"%lsOctans-Setup-v%ls.exe", tmp_dir, ver_w);
    out_exe_path[out_cap - 1] = L'\0';

    /* Try CN AtomGit mirror first, then fallback to GitHub */
    bool dl_ok = false;
    if (info.download_cn[0]) {
        dl_ok = HttpDownloadToFile(api, info.download_cn, out_exe_path, cb, user_ctx);
    }
    if (!dl_ok && info.download_global[0]) {
        if (cb) cb(0, false, user_ctx);
        dl_ok = HttpDownloadToFile(api, info.download_global, out_exe_path, cb, user_ctx);
    }

    if (!dl_ok) {
        if (err_msg) wcsncpy(err_msg, L"\u81ea\u52a8\u4e0b\u8f7d\u5931\u8d25\uff0c\u8bf7\u70b9\u51fb\u4e0b\u65b9\u6309\u94ae\u524d\u5f80\u53d1\u5e03\u9875\u624b\u52a8\u4e0b\u8f7d", err_cap - 1);
        return false;
    }

    /* Verify SHA-256 integrity if provided in manifest */
    if (cb) cb(97, true, user_ctx);
    if (strlen(info.sha256) == 64) {
        char actual[65] = {};
        if (!ComputeFileSha256Hex(out_exe_path, actual) || _stricmp(actual, info.sha256) != 0) {
            DeleteFileW(out_exe_path);
            out_exe_path[0] = L'\0';
            if (err_msg) wcsncpy(err_msg, L"\u5b89\u88c5\u5305 SHA256 \u6821\u9a8c\u5931\u8d25\uff0c\u5df2\u4e2d\u6b62\u5b89\u88c5\uff0c\u8bf7\u624b\u52a8\u4e0b\u8f7d", err_cap - 1);
            return false;
        }
    }

    if (cb) cb(100, false, user_ctx);
    return true;
}

bool LaunchSilentInstaller(const wchar_t *exe_path) {
    if (!exe_path || !exe_path[0]) return false;
    HINSTANCE res = ShellExecuteW(
        nullptr,
        L"open",
        exe_path,
        L"/SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS",
        nullptr,
        SW_SHOWNORMAL
    );
    return reinterpret_cast<intptr_t>(res) > 32;
}

} /* namespace octans_ui */
