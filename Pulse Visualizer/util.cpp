// String / encoding / crypto helpers
#include "common.h"
#include "util.h"

float frand() { static std::mt19937 g(1337); return std::uniform_real_distribution<float>(0.f, 1.f)(g); }
double nowSec() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart;
}
// ============================================================================
//  STRING / ENCODING UTILITIES
// ============================================================================
std::wstring toW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w;
}
std::string toU8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
std::string urlEnc(const std::string& s) {
    static const char* hex = "0123456789ABCDEF"; std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
std::string urlDec(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) { o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
        else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}
std::string queryParam(const std::string& target, const std::string& key) {
    size_t q = target.find('?'); if (q == std::string::npos) return "";
    std::string qs = "&" + target.substr(q + 1), k = "&" + key + "=";
    size_t p = qs.find(k); if (p == std::string::npos) return "";
    p += k.size(); size_t e = qs.find('&', p);
    return urlDec(qs.substr(p, e == std::string::npos ? std::string::npos : e - p));
}
std::string b64url(const std::vector<uint8_t>& d) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string o; size_t i = 0;
    for (; i + 2 < d.size(); i += 3) {
        uint32_t v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
    }
    if (i + 1 == d.size()) { uint32_t v = d[i] << 16; o += t[v >> 18]; o += t[(v >> 12) & 63]; }
    else if (i + 2 == d.size()) { uint32_t v = (d[i] << 16) | (d[i + 1] << 8); o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; }
    return o;
}
std::vector<uint8_t> sha256(const std::string& s) {
    std::vector<uint8_t> out(32, 0);
    BCRYPT_ALG_HANDLE h = nullptr; BCRYPT_HASH_HANDLE hh = nullptr;
    if (BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(h, &hh, nullptr, 0, nullptr, 0, 0) == 0) {
            BCryptHashData(hh, (PUCHAR)s.data(), (ULONG)s.size(), 0);
            BCryptFinishHash(hh, out.data(), 32, 0); BCryptDestroyHash(hh);
        }
        BCryptCloseAlgorithmProvider(h, 0);
    }
    return out;
}
std::string randStr(int n) {
    static const char* cs = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::vector<uint8_t> b(n); BCryptGenRandom(nullptr, b.data(), (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::string s; for (int i = 0; i < n; i++) s += cs[b[i] % 66]; return s;
}
std::string dpapiProtect(const std::string& in) {          // refresh token is stored encrypted for this Windows user
    if (in.empty()) return "";
    DATA_BLOB i; i.cbData = (DWORD)in.size(); i.pbData = (BYTE*)in.data(); DATA_BLOB o = {};
    if (!CryptProtectData(&i, L"rgbpulse", nullptr, nullptr, nullptr, 0, &o)) return "";
    std::string hex; for (DWORD k = 0; k < o.cbData; k++) { char b[4]; sprintf_s(b, "%02x", o.pbData[k]); hex += b; }
    LocalFree(o.pbData); return hex;
}
std::string dpapiUnprotect(const std::string& hex) {
    std::vector<BYTE> raw; for (size_t k = 0; k + 1 < hex.size(); k += 2) raw.push_back((BYTE)strtol(hex.substr(k, 2).c_str(), nullptr, 16));
    if (raw.empty()) return "";
    DATA_BLOB i; i.cbData = (DWORD)raw.size(); i.pbData = raw.data(); DATA_BLOB o = {};
    if (!CryptUnprotectData(&i, nullptr, nullptr, nullptr, nullptr, 0, &o)) return "";
    std::string s((char*)o.pbData, o.cbData); LocalFree(o.pbData); return s;
}