// Tiny JSON parser
#include "common.h"
#include "json.h"

struct JParser {
    const char* p; const char* e;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    bool lit(const char* s) { size_t n = strlen(s); if ((size_t)(e - p) >= n && !strncmp(p, s, n)) { p += n; return true; } return false; }
    static void u8(std::string& o, unsigned c) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
    }
    unsigned hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4 && p < e; i++) { char c = *p++; v <<= 4; if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; }
        return v;
    }
    std::string str() {
        std::string o; p++;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++; char c = *p++;
                switch (c) {
                case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
                case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < e && p[0] == '\\' && p[1] == 'u') { p += 2; unsigned lo = hex4(); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                    u8(o, cp); break;
                }
                default: o += c;
                }
            }
            else o += *p++;
        }
        if (p < e) p++;
        return o;
    }
    JV val(int depth = 0) {
        JV v; ws(); if (p >= e || depth > 64) return v;
        if (*p == '{') {
            v.t = JV::Obj; p++; ws(); if (p < e && *p == '}') { p++; return v; }
            while (p < e) {
                ws(); if (p >= e || *p != '"') break;
                std::string k = str(); ws(); if (p < e && *p == ':') p++;
                JV c = val(depth + 1); v.o.emplace_back(k, std::move(c)); ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == '}') p++;
                break;
            }
        }
        else if (*p == '[') {
            v.t = JV::Arr; p++; ws(); if (p < e && *p == ']') { p++; return v; }
            while (p < e) {
                v.a.push_back(val(depth + 1)); ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == ']') p++;
                break;
            }
        }
        else if (*p == '"') { v.t = JV::Str; v.s = str(); }
        else if (lit("true")) { v.t = JV::Bool; v.b = true; }
        else if (lit("false")) { v.t = JV::Bool; }
        else if (lit("null")) {}
        else { char* endp = nullptr; v.n = strtod(p, &endp); if (endp == p) p++; else { p = endp; v.t = JV::Num; } }
        return v;
    }
};
JV parseJson(const std::string& s) { JParser jp; jp.p = s.data(); jp.e = s.data() + s.size(); return jp.val(); }