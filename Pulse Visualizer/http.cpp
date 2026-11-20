// WinHTTP client
#include "common.h"
#include "util.h"
#include "http.h"

HttpResp http(const char* method, const std::string& url, const std::string& body, const std::string& headers) {
    HttpResp r;
    static HINTERNET ses = [] {
        HINTERNET s = WinHttpOpen(L"PulseVisualizer/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (s) WinHttpSetTimeouts(s, 5000, 5000, 15000, 20000);
        return s; }();
        if (!ses) return r;
        std::wstring wurl = toW(url);
        wchar_t host[256] = {}, path[2048] = {}, extra[2048] = {};
        URL_COMPONENTS uc = {}; uc.dwStructSize = sizeof(uc);
        uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
        uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return r;
        HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0); if (!con) return r;
        std::wstring full = std::wstring(path) + extra;
        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET rq = WinHttpOpenRequest(con, toW(method).c_str(), full.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (rq) {
            std::string h = headers;
            if (body.empty() && strcmp(method, "GET") != 0) h += "Content-Length: 0\r\n";
            std::wstring wh = toW(h);
            BOOL ok = WinHttpSendRequest(rq, wh.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wh.c_str(), wh.empty() ? 0 : (DWORD)-1L,
                body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)
                && WinHttpReceiveResponse(rq, nullptr);
            if (ok) {
                DWORD code = 0, sz = sizeof(code);
                WinHttpQueryHeaders(rq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
                r.status = (int)code;
                for (;;) {
                    DWORD n = 0; if (!WinHttpQueryDataAvailable(rq, &n) || n == 0) break;
                    size_t o = r.body.size(); r.body.resize(o + n); DWORD rd = 0;
                    if (!WinHttpReadData(rq, &r.body[o], n, &rd)) { r.body.resize(o); break; }
                    r.body.resize(o + rd);
                }
            }
            WinHttpCloseHandle(rq);
        }
        WinHttpCloseHandle(con);
        return r;
}