// String / encoding / crypto helpers
#pragma once
#include "common.h"

float frand();
double nowSec();
std::wstring toW(const std::string& s);
std::string toU8(const std::wstring& w);
std::string urlEnc(const std::string& s);
std::string urlDec(const std::string& s);
std::string queryParam(const std::string& target, const std::string& key);
std::string b64url(const std::vector<uint8_t>& d);
std::vector<uint8_t> sha256(const std::string& s);
std::string randStr(int n);
std::string dpapiProtect(const std::string& in);
std::string dpapiUnprotect(const std::string& hex);