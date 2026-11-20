// WinHTTP client
#pragma once
#include "common.h"

// ============================================================================
//  HTTP (WinHTTP)
// ============================================================================
struct HttpResp { int status = 0; std::string body; };
HttpResp http(const char* method, const std::string& url, const std::string& body = "", const std::string& headers = "");