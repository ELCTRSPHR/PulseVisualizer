// Tiny JSON parser
#pragma once
#include "common.h"

// ============================================================================
//  TINY JSON PARSER
// ============================================================================
struct JV {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    double n = 0; bool b = false; std::string s;
    std::vector<JV> a; std::vector<std::pair<std::string, JV>> o;
    static const JV& nul() { static JV v; return v; }
    const JV& operator[](const char* k) const { for (auto& p : o) if (p.first == k) return p.second; return nul(); }
    const JV& at(size_t i) const { return i < a.size() ? a[i] : nul(); }
    std::string str(const std::string& d = "") const { return t == Str ? s : d; }
    double num(double d = 0) const { return t == Num ? n : d; }
    bool boolean(bool d = false) const { return t == Bool ? b : d; }
};
JV parseJson(const std::string& s);