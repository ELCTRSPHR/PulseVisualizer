// Spotify Web API client, album art, LRCLIB lyrics
#include "common.h"
#include "util.h"
#include "json.h"
#include "http.h"
#include "settings.h"
#include "color.h"
#include "spotify.h"

void decodeArt(const std::string& data, ArtData& out) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return;
    ComPtr<IWICStream> st; if (FAILED(f->CreateStream(&st))) return;
    if (FAILED(st->InitializeFromMemory((BYTE*)data.data(), (DWORD)data.size()))) return;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec))) return;
    ComPtr<IWICBitmapFrameDecode> fr; if (FAILED(dec->GetFrame(0, &fr))) return;
    ComPtr<IWICFormatConverter> cv; if (FAILED(f->CreateFormatConverter(&cv))) return;
    if (FAILED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return;
    UINT w = 0, h = 0; cv->GetSize(&w, &h); if (!w || !h || w > 2048 || h > 2048) return;
    out.px.resize((size_t)w * h * 4);
    if (FAILED(cv->CopyPixels(nullptr, w * 4, (UINT)out.px.size(), out.px.data()))) { out.px.clear(); return; }
    out.w = (int)w; out.h = (int)h;

    // ---- album palette: k-means in CIELAB (perceptual), then pick 3 distinct, representative colours ----
    struct Lab { float L, a, b; };
    auto toLin = [](float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); };
    auto toSrgb = [](float c) { c = std::max(0.f, c); return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.f / 2.4f) - 0.055f; };
    auto fLab = [](float t) { return t > 0.008856f ? cbrtf(t) : 7.787f * t + 16.f / 116.f; };
    auto rgb2lab = [&](float r, float g, float b) {
        r = toLin(r); g = toLin(g); b = toLin(b);
        float X = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f, Y = 0.2126f * r + 0.7152f * g + 0.0722f * b, Z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
        float fx = fLab(X), fy = fLab(Y), fz = fLab(Z);
        return Lab{ 116.f * fy - 16.f, 500.f * (fx - fy), 200.f * (fy - fz) };
        };
    auto lab2rgb = [&](Lab c, float& r, float& g, float& b) {
        float fy = (c.L + 16.f) / 116.f, fx = fy + c.a / 500.f, fz = fy - c.b / 200.f;
        auto inv = [](float t) { float t3 = t * t * t; return t3 > 0.008856f ? t3 : (t - 16.f / 116.f) / 7.787f; };
        float X = inv(fx) * 0.95047f, Y = inv(fy), Z = inv(fz) * 1.08883f;
        r = toSrgb(3.2406f * X - 1.5372f * Y - 0.4986f * Z);
        g = toSrgb(-0.9689f * X + 1.8758f * Y + 0.0415f * Z);
        b = toSrgb(0.0557f * X - 0.2040f * Y + 1.0570f * Z);
        r = clampv(r, 0.f, 1.f); g = clampv(g, 0.f, 1.f); b = clampv(b, 0.f, 1.f);
        };
    auto dE = [](const Lab& p, const Lab& q) { float x = p.L - q.L, y = p.a - q.a, z = p.b - q.b; return sqrtf(x * x + y * y + z * z); };

    std::vector<Lab> smp;                                                   // ~8-10k evenly spread samples
    UINT step = std::max(1u, (UINT)sqrtf((float)(w * h) / 9000.f));
    for (UINT y = 0; y < h; y += step) for (UINT x = 0; x < w; x += step) {
        const uint8_t* p = &out.px[((size_t)y * w + x) * 4];
        smp.push_back(rgb2lab(p[2] / 255.f, p[1] / 255.f, p[0] / 255.f));
    }
    const int K = 8;
    std::vector<Lab> cen; std::vector<float> cnt(K, 0.f);
    if (!smp.empty()) {
        cen.push_back(smp[smp.size() / 2]);                                 // deterministic k-means++ style seeding: farthest-point
        std::vector<float> md(smp.size(), 1e9f);
        while ((int)cen.size() < K) {
            size_t bi = 0; float bd = -1.f;
            for (size_t i = 0; i < smp.size(); i++) { md[i] = std::min(md[i], dE(smp[i], cen.back())); if (md[i] > bd) { bd = md[i]; bi = i; } }
            if (bd < 1.f) break;
            cen.push_back(smp[bi]);
        }
        std::vector<int> as(smp.size(), 0);
        for (int it = 0; it < 10; it++) {
            std::vector<Lab> acc(cen.size(), Lab{ 0, 0, 0 }); std::fill(cnt.begin(), cnt.end(), 0.f);
            for (size_t i = 0; i < smp.size(); i++) {
                int bk = 0; float bd = 1e9f;
                for (size_t k = 0; k < cen.size(); k++) { float d = dE(smp[i], cen[k]); if (d < bd) { bd = d; bk = (int)k; } }
                as[i] = bk; acc[bk].L += smp[i].L; acc[bk].a += smp[i].a; acc[bk].b += smp[i].b; cnt[bk] += 1.f;
            }
            for (size_t k = 0; k < cen.size(); k++) if (cnt[k] > 0.f) cen[k] = Lab{ acc[k].L / cnt[k], acc[k].a / cnt[k], acc[k].b / cnt[k] };
        }
    }
    // score each cluster: big areas matter most, colourful ones are preferred, near-black / near-white are poor accents
    const float total = std::max(1.f, (float)smp.size());
    std::vector<float> score(cen.size(), 0.f);
    for (size_t k = 0; k < cen.size(); k++) {
        float share = cnt[k] / total; if (share < 0.01f) continue;          // ignore specks
        float chroma = sqrtf(cen[k].a * cen[k].a + cen[k].b * cen[k].b);
        float light = cen[k].L < 12.f ? 0.15f : (cen[k].L > 94.f ? 0.45f : 1.f);
        score[k] = powf(share, 0.55f) * (0.35f + std::min(chroma, 80.f) / 50.f) * light;
    }
    int pick[3] = { -1, -1, -1 };
    for (int s = 0; s < 3; s++) {
        float best = 0.f;
        for (size_t k = 0; k < cen.size(); k++) {
            if (score[k] <= 0.f) continue;
            float minD = 1e9f; for (int j = 0; j < s; j++) minD = std::min(minD, dE(cen[k], cen[pick[j]]));
            if (s > 0 && minD < 16.f) continue;                              // too similar to one we already have
            float sc2 = score[k] * (s == 0 ? 1.f : std::min(1.f, minD / 40.f) + 0.25f);
            if (sc2 > best) { best = sc2; pick[s] = (int)k; }
        }
    }
    // turn a cluster into a colour that reads on a black, additive scene: keep its hue and natural saturation,
    // only lift the lightness so dark covers stay visible (no forced neon)
    auto finish = [&](Lab c, float& r, float& g, float& b) {
        float chroma = sqrtf(c.a * c.a + c.b * c.b);
        c.L = clampv(c.L, 58.f, 92.f);
        if (chroma > 8.f) { float k = std::min(1.25f, 1.f + 12.f / chroma); c.a *= k; c.b *= k; }   // a gentle nudge for muted colours only
        lab2rgb(c, r, g, b);
        float mx = std::max(r, std::max(g, b)); if (mx > 0.01f && mx < 0.85f) { float k = 0.85f / mx; r *= k; g *= k; b *= k; }
        };
    float cols[3][3];
    int have = 0; for (int s = 0; s < 3; s++) if (pick[s] >= 0) have++;
    if (have == 0) { cols[0][0] = cols[0][1] = cols[0][2] = 0.9f; cols[1][0] = 1.f; cols[1][1] = 1.f; cols[1][2] = 1.f; cols[2][0] = 0.75f; cols[2][1] = 0.8f; cols[2][2] = 0.9f; }
    else {
        Lab base = cen[pick[0]];
        for (int s = 0; s < 3; s++) {
            Lab c = pick[s] >= 0 ? cen[pick[s]] : base;
            if (pick[s] < 0) c.L = base.L + (s == 1 ? 18.f : -18.f);        // one-colour cover: lighter / darker shades of it, not invented hues
            finish(c, cols[s][0], cols[s][1], cols[s][2]);
        }
    }
    // slot 1 is the "main" colour (used for washes and flashes): put the dominant pick in the middle of the gradient
    int order[3] = { 1, 0, 2 };
    for (int s = 0; s < 3; s++) for (int c = 0; c < 3; c++) out.col[order[s]][c] = cols[s][c];
}
// state: 0 loading, 1 found, 2 none, 3 instrumental
void parseLrc(const std::string& lrc, std::vector<LyricLine>& out) {      // "[mm:ss.xx] text" lines
    size_t pos = 0;
    while (pos < lrc.size()) {
        size_t e = lrc.find('\n', pos); if (e == std::string::npos) e = lrc.size();
        std::string line = lrc.substr(pos, e - pos); pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<int> times; size_t p = 0;
        while (p < line.size() && line[p] == '[') {
            size_t c = line.find(']', p); if (c == std::string::npos) break;
            std::string tag = line.substr(p + 1, c - p - 1);
            int mm = 0; float ss = 0.f;
            if (!tag.empty() && isdigit((unsigned char)tag[0]) && sscanf_s(tag.c_str(), "%d:%f", &mm, &ss) == 2) times.push_back((int)(mm * 60000 + ss * 1000.f));
            p = c + 1;
        }
        if (times.empty()) continue;
        std::string txt = line.substr(p);
        size_t a = txt.find_first_not_of(" \t"); txt = (a == std::string::npos) ? "" : txt.substr(a);
        while (!txt.empty() && (txt.back() == ' ' || txt.back() == '\t')) txt.pop_back();
        for (int t : times) { LyricLine l; l.ms = t; l.text = txt; out.push_back(l); }
    }
    std::stable_sort(out.begin(), out.end(), [](const LyricLine& a, const LyricLine& b) { return a.ms < b.ms; });
}
void spreadPlain(const std::string& plain, int durMs, std::vector<LyricLine>& out) {   // no timestamps: spread lines across the song
    std::vector<std::string> rows; size_t pos = 0;
    while (pos < plain.size()) {
        size_t e = plain.find('\n', pos); if (e == std::string::npos) e = plain.size();
        std::string line = plain.substr(pos, e - pos); pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) rows.push_back(line);
    }
    if (rows.empty() || durMs <= 0) return;
    double start = durMs * 0.06, span = durMs * 0.88;
    for (size_t i = 0; i < rows.size(); i++) { LyricLine l; l.ms = (int)(start + span * (double)i / (double)rows.size()); l.text = rows[i]; out.push_back(l); }
}
std::string cleanTitle(std::string s) {            // drop " - Remastered", "(feat. X)", "[Live]" suffixes
    const char* cuts[] = { " - ", " (", " [" };
    for (const char* c : cuts) { size_t p = s.find(c); if (p != std::string::npos && p > 0) s = s.substr(0, p); }
    return s;
}
void Spotify::start() { running = true; th = std::thread([this] { run(); }); lth = std::thread([this] { lyricWorker(); }); }
void Spotify::stop() { running = false; abortAuth = true; cv.notify_all(); lcv.notify_all(); if (th.joinable()) th.join(); if (lth.joinable()) lth.join(); }
PlayerState Spotify::state() { std::lock_guard<std::mutex> l(mx); return ps; }
std::string Spotify::message() { std::lock_guard<std::mutex> l(mx); return statusMsg; }
std::string Spotify::takeNotice() { std::lock_guard<std::mutex> l(mx); std::string s; s.swap(notice); return s; }
bool Spotify::getLyrics(LyricsData& out, unsigned& have) {
    std::lock_guard<std::mutex> l(mx);
    if (lyr.gen == have) return false;
    out = lyr; have = lyr.gen; return true;
}
bool Spotify::getArt(ArtData& out, unsigned& have) {
    std::lock_guard<std::mutex> l(mx);
    if (artData.gen == have) return false;
    out = artData; have = artData.gen; return true;
}
void Spotify::setClientId(std::string id) {
    id.erase(std::remove_if(id.begin(), id.end(), [](unsigned char c) { return isspace(c) != 0; }), id.end());
    { std::lock_guard<std::mutex> l(mx); if (id != clientId) { access.clear(); refresh.clear(); } clientId = id; }
    WritePrivateProfileStringW(L"spotify", L"client_id", toW(id).c_str(), iniPath().c_str());
    WritePrivateProfileStringW(L"spotify", L"refresh", L"", iniPath().c_str());
    haveId = !id.empty();
    if (status == ST_NOID && haveId) setStatus(ST_OFF, "");
}
void Spotify::connect() { abortAuth = true; post([this] { doAuth(); }); }
void Spotify::disconnect() {
    abortAuth = true;
    post([this] {
        { std::lock_guard<std::mutex> l(mx); access.clear(); refresh.clear(); ps = PlayerState(); }
        WritePrivateProfileStringW(L"spotify", L"refresh", L"", iniPath().c_str());
        setStatus(haveId ? ST_OFF : ST_NOID, "Disconnected");
        });
}
void Spotify::next() { { std::lock_guard<std::mutex> l(mx); queueAt = -100.0; } post([this] { api("POST", "/v1/me/player/next"); pollSoon(); }); }
void Spotify::prev() { post([this] { api("POST", "/v1/me/player/previous"); pollSoon(); }); }
void Spotify::togglePlay() {
    bool pl; { std::lock_guard<std::mutex> l(mx); pl = ps.playing; ps.playing = !ps.playing; ps.stamp = nowSec(); }
    if (pl) post([this] { api("PUT", "/v1/me/player/pause"); pollSoon(); });
    else playBody("");
}
void Spotify::seek(int ms) { post([this, ms] { api("PUT", "/v1/me/player/seek?position_ms=" + std::to_string(std::max(0, ms))); pollSoon(); }); }
void Spotify::setVolume(int v) {
    v = clampv(v, 0, 100); volPending = v;
    { std::lock_guard<std::mutex> l(mx); ps.volume = v; }
    post([this] { int x = volPending.exchange(-1); if (x >= 0) api("PUT", "/v1/me/player/volume?volume_percent=" + std::to_string(x)); });
}
void Spotify::setShuffle(bool on) {
    { std::lock_guard<std::mutex> l(mx); ps.shuffle = on; }
    post([this, on] { api("PUT", std::string("/v1/me/player/shuffle?state=") + (on ? "true" : "false")); pollSoon(); });
}
void Spotify::setRepeat(const std::string& mode) {
    { std::lock_guard<std::mutex> l(mx); ps.repeat = mode; }
    post([this, mode] { api("PUT", "/v1/me/player/repeat?state=" + mode); pollSoon(); });
}
void Spotify::wantQueue() {
    std::string cur; double now = nowSec();
    { std::lock_guard<std::mutex> l(mx); cur = ps.t.id; if (queueBusy || cur.empty() || (cur == upNextFor && now - queueAt < 10.0)) return; queueBusy = true; }
    post([this, cur] {
        HttpResp r = api("GET", "/v1/me/player/queue");
        std::vector<TrackInfo> v;
        if (r.status == 200) {
            JV j = parseJson(r.body);
            for (auto& it : j["queue"].a) {
                if ((int)v.size() >= 3) break;
                TrackInfo t; t.id = it["id"].str(); t.name = it["name"].str(); t.durMs = (int)it["duration_ms"].num();
                for (auto& a : it["artists"].a) { if (!t.artist.empty()) t.artist += ", "; t.artist += a["name"].str(); }
                if (t.artist.empty()) t.artist = it["show"]["name"].str();            // podcast episodes
                if (!t.name.empty()) v.push_back(t);
            }
        }
        std::lock_guard<std::mutex> l(mx);
        if (r.status == 200) { upNext = std::move(v); upNextFor = cur; }
        queueAt = nowSec(); queueBusy = false;
        });
}
bool Spotify::getQueue(std::vector<TrackInfo>& out, std::string& forTrack) {
    std::lock_guard<std::mutex> l(mx); out = upNext; forTrack = upNextFor; return !upNextFor.empty();
}
void Spotify::skipTo(int k) {
    { std::lock_guard<std::mutex> l(mx); upNext.clear(); upNextFor.clear(); queueAt = -100.0; }
    post([this, k] {
        for (int i = 0; i <= k; i++) { api("POST", "/v1/me/player/next"); if (i < k) Sleep(180); }   // the Web API has no "play queue item", so skip ahead
        pollSoon();
        });
}
void Spotify::post(std::function<void()> f) { { std::lock_guard<std::mutex> l(mx); q.push_back(std::move(f)); } cv.notify_one(); }
void Spotify::setStatus(int s, const std::string& m) { { std::lock_guard<std::mutex> l(mx); statusMsg = m; } status = s; }
void Spotify::setNotice(const std::string& m) { std::lock_guard<std::mutex> l(mx); notice = m; }
void Spotify::pollSoon() { nextPoll = nowSec() + 0.4; }
std::string Spotify::iniGet(const wchar_t* key) { wchar_t b[2048] = {}; GetPrivateProfileStringW(L"spotify", key, L"", b, 2048, iniPath().c_str()); return toU8(b); }
void Spotify::run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    {
        std::string id = iniGet(L"client_id"), rf = dpapiUnprotect(iniGet(L"refresh"));
        { std::lock_guard<std::mutex> l(mx); clientId = id; refresh = rf; }
        haveId = !id.empty(); if (haveId) setStatus(ST_OFF, "");
    }
    if (haveId && !refresh.empty() && refreshToken()) setStatus(ST_ON, "");
    while (running) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> l(mx);
            cv.wait_for(l, std::chrono::milliseconds(100), [&] { return !q.empty() || !running; });
            if (!running) break;
            if (!q.empty()) { job = std::move(q.front()); q.pop_front(); }
        }
        if (job) { job(); continue; }
        if (status == ST_ON && nowSec() >= nextPoll) { poll(); nextPoll = nowSec() + (ps.playing ? 1.0 : 2.0); }
    }
    WSACleanup(); CoUninitialize();
}
bool Spotify::ensureToken() { if (access.empty() || nowSec() > expiresAt - 60.0) return refreshToken(); return true; }
bool Spotify::refreshToken() {
    std::string cid, rf; { std::lock_guard<std::mutex> l(mx); cid = clientId; rf = refresh; }
    if (cid.empty() || rf.empty()) return false;
    HttpResp r = http("POST", "https://accounts.spotify.com/api/token",
        "grant_type=refresh_token&refresh_token=" + urlEnc(rf) + "&client_id=" + urlEnc(cid),
        "Content-Type: application/x-www-form-urlencoded\r\n");
    if (r.status != 200) {
        if (r.status == 400 || r.status == 401) {
            { std::lock_guard<std::mutex> l(mx); access.clear(); refresh.clear(); }
            WritePrivateProfileStringW(L"spotify", L"refresh", L"", iniPath().c_str());
            setStatus(ST_OFF, "Session expired - press S to sign in again");
        }
        return false;
    }
    JV j = parseJson(r.body);
    access = j["access_token"].str(); expiresAt = nowSec() + j["expires_in"].num(3600);
    std::string nr = j["refresh_token"].str();
    if (!nr.empty()) { { std::lock_guard<std::mutex> l(mx); refresh = nr; } WritePrivateProfileStringW(L"spotify", L"refresh", toW(dpapiProtect(nr)).c_str(), iniPath().c_str()); }
    return !access.empty();
}
HttpResp Spotify::api(const char* method, const std::string& path, const std::string& body) {
    if (!ensureToken()) { HttpResp e; e.status = 401; return e; }
    std::string h = "Authorization: Bearer " + access + "\r\n"; if (!body.empty()) h += "Content-Type: application/json\r\n";
    HttpResp r = http(method, "https://api.spotify.com" + path, body, h);
    if (r.status == 401 && refreshToken()) { h = "Authorization: Bearer " + access + "\r\n"; if (!body.empty()) h += "Content-Type: application/json\r\n"; r = http(method, "https://api.spotify.com" + path, body, h); }
    if (r.status == 403 && r.body.find("PREMIUM") != std::string::npos) setNotice("Spotify Premium is required to control playback");
    else if (r.status == 403) setNotice("Spotify refused that request (403) - check the app's user list in the dashboard");
    else if (r.status == 404 && r.body.find("NO_ACTIVE_DEVICE") != std::string::npos) setNotice("No active Spotify device - open Spotify and press play once");
    else if (r.status == 429) { setNotice("Spotify rate limit - slow down a little"); nextPoll = nowSec() + 4.0; }
    return r;
}
void Spotify::playBody(const std::string& body) {
    post([this, body] {
        HttpResp r = api("PUT", "/v1/me/player/play", body);
        if (r.status == 404) {            // no active device: wake the first one we can find
            HttpResp d = api("GET", "/v1/me/player/devices");
            if (d.status == 200) {
                JV j = parseJson(d.body); const JV& arr = j["devices"]; std::string id;
                for (auto& x : arr.a) if (x["is_active"].boolean()) { id = x["id"].str(); break; }
                if (id.empty() && !arr.a.empty()) id = arr.at(0)["id"].str();
                if (!id.empty()) api("PUT", "/v1/me/player/play?device_id=" + urlEnc(id), body);
            }
        }
        pollSoon();
        });
}
void Spotify::poll() {
    double tReq = nowSec();
    HttpResp r = api("GET", "/v1/me/player?additional_types=track");
    if (r.status == 204 || r.status == 202) { std::lock_guard<std::mutex> l(mx); ps.device = false; ps.playing = false; return; }
    if (r.status != 200) return;
    JV j = parseJson(r.body); PlayerState n;
    n.device = true; n.playing = j["is_playing"].boolean(); n.progMs = (int)j["progress_ms"].num(); n.stamp = (tReq + nowSec()) * 0.5;   // the position was sampled mid-request, not when the reply landed
    n.shuffle = j["shuffle_state"].boolean(); n.repeat = j["repeat_state"].str("off");
    n.volume = (int)j["device"]["volume_percent"].num(ps.volume);
    const JV& it = j["item"];
    n.t.id = it["id"].str(); n.t.name = it["name"].str(); n.t.durMs = (int)it["duration_ms"].num();
    for (auto& a : it["artists"].a) { if (!n.t.artist.empty()) n.t.artist += ", "; n.t.artist += a["name"].str(); }
    n.t.album = it["album"]["name"].str();
    int bestW = 100000; std::string url;
    for (auto& im : it["album"]["images"].a) { int w = (int)im["width"].num(0); if (w >= 250 && w < bestW) { bestW = w; url = im["url"].str(); } }
    if (url.empty()) url = it["album"]["images"].at(0)["url"].str();
    { std::lock_guard<std::mutex> l(mx); ps = n; }
    if (!url.empty() && url != artUrlDone) { artUrlDone = url; fetchArt(url); }
    if (!n.t.id.empty() && n.t.id != lyrIdReq) {          // new track: look up lyrics
        lyrIdReq = n.t.id;
        { std::lock_guard<std::mutex> l(mx); LyricsData ld; ld.id = n.t.id; ld.gen = lyr.gen + 1; lyr = std::move(ld); }
        { std::lock_guard<std::mutex> l(lmx); lpend = n.t; lhas = true; }
        lcv.notify_one();
    }
}
void Spotify::fetchArt(const std::string& url) {
    HttpResp r = http("GET", url); if (r.status != 200 || r.body.empty()) return;
    ArtData ad; decodeArt(r.body, ad); if (ad.px.empty()) return;
    std::lock_guard<std::mutex> l(mx); ad.gen = artData.gen + 1; artData = std::move(ad);
}
LyricsData Spotify::lookupLyrics(const TrackInfo& t, bool& netFail) {      // LRCLIB: several increasingly loose lookups
    std::string artist = t.artist; size_t cm = artist.find(", "); if (cm != std::string::npos) artist = artist.substr(0, cm);
    int dur = t.durMs / 1000; std::string clean = cleanTitle(t.name);
    JV j; bool got = false;
    auto note = [&](const HttpResp& r) { if (r.status == 0 || r.status == 429 || r.status >= 500) netFail = true; };
    auto tryGet = [&](const std::string& title, bool album, bool withDur) -> bool {
        std::string url = "https://lrclib.net/api/get?track_name=" + urlEnc(title) + "&artist_name=" + urlEnc(artist);
        if (album && !t.album.empty()) url += "&album_name=" + urlEnc(t.album);
        if (withDur && dur > 0) url += "&duration=" + std::to_string(dur);
        HttpResp r = http("GET", url); note(r);
        if (r.status != 200) return false;
        JV x = parseJson(r.body); if (x.t != JV::Obj) return false;
        double d = x["duration"].num(0);
        if (!withDur && dur > 0 && d > 0 && fabs(d - dur) > 5.0) return false;     // loose match must still be the same recording
        j = x; return true;
        };
    got = tryGet(t.name, true, true) || tryGet(t.name, false, true) || (clean != t.name && tryGet(clean, false, true)) || tryGet(clean, false, false);
    auto runSearch = [&](const std::string& qs) -> bool {
        HttpResp r = http("GET", "https://lrclib.net/api/search?" + qs); note(r);
        if (r.status != 200) return false;
        JV arr = parseJson(r.body); const JV* best = nullptr; double bs = 1e9;
        for (auto& it : arr.a) {
            bool hasS = !it["syncedLyrics"].str().empty(), hasP = !it["plainLyrics"].str().empty();
            if (!hasS && !hasP && !it["instrumental"].boolean()) continue;
            double dd = fabs(it["duration"].num(0) - dur);
            if (dur > 0 && it["duration"].num(0) > 0 && dd > 6.0) continue;
            double sc = dd + (hasS ? 0.0 : 1000.0);
            if (sc < bs) { bs = sc; best = &it; }
        }
        if (best) { j = *best; return true; }
        return false;
        };
    if (!got) got = runSearch("track_name=" + urlEnc(clean) + "&artist_name=" + urlEnc(artist));
    if (!got) got = runSearch("q=" + urlEnc(clean + " " + artist));
    if (!got && dur > 0) got = runSearch("q=" + urlEnc(clean));
    LyricsData out; out.id = t.id;
    if (got) {
        std::string synced = j["syncedLyrics"].str(), plain = j["plainLyrics"].str();
        if (!synced.empty()) { parseLrc(synced, out.lines); if (!out.lines.empty()) out.state = 1; }
        if (out.state != 1 && !plain.empty() && t.durMs > 0) { spreadPlain(plain, t.durMs, out.lines); if (!out.lines.empty()) out.state = 1; }
        if (out.state != 1 && j["instrumental"].boolean()) out.state = 3;
    }
    if (out.state != 1 && out.state != 3) out.state = 2;
    if (out.state == 1) netFail = false;
    return out;
}
void Spotify::lyricWorker() {                                    // own thread: slow lookups never stall playback polling or button presses
    while (running) {
        TrackInfo t;
        { std::unique_lock<std::mutex> l(lmx); lcv.wait(l, [&] { return lhas || !running; }); if (!running) break; t = lpend; lhas = false; }
        for (int attempt = 0; attempt < 4 && running; attempt++) {
            bool netFail = false;
            LyricsData out = lookupLyrics(t, netFail);
            {
                std::lock_guard<std::mutex> l(mx);
                if (lyr.id != t.id) break;                                   // song changed meanwhile
                if (!(netFail && out.state == 2 && attempt < 3)) { out.gen = lyr.gen + 1; lyr = std::move(out); break; }   // network hiccup: retry instead of "not found"
            }
            for (int i = 0; i < (attempt == 0 ? 20 : 50) && running; i++) { Sleep(100); std::lock_guard<std::mutex> l(lmx); if (lhas) break; }
            { std::lock_guard<std::mutex> l(lmx); if (lhas) break; }
        }
    }
}
void Spotify::doAuth() {
    std::string cid; { std::lock_guard<std::mutex> l(mx); cid = clientId; }
    if (cid.empty()) { setStatus(ST_NOID, ""); return; }
    abortAuth = false;
    std::string verifier = randStr(96), challenge = b64url(sha256(verifier)), state = randStr(16);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_port = htons(8888); inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (ls == INVALID_SOCKET || bind(ls, (sockaddr*)&a, sizeof(a)) != 0 || listen(ls, 1) != 0) {
        if (ls != INVALID_SOCKET) closesocket(ls);
        setStatus(ST_ERR, "Port 8888 is busy - close whatever is using it"); return;
    }
    setStatus(ST_WAIT, "Approve access in your browser...");
    std::string url = "https://accounts.spotify.com/authorize?response_type=code&client_id=" + urlEnc(cid) + "&redirect_uri=" + urlEnc(REDIRECT) +
        "&scope=" + urlEnc(SCOPES) + "&code_challenge_method=S256&code_challenge=" + challenge + "&state=" + state;
    ShellExecuteW(nullptr, L"open", toW(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    std::string code, err; double t0 = nowSec();
    while (!abortAuth && running && nowSec() - t0 < 180.0) {
        fd_set fs; FD_ZERO(&fs); FD_SET(ls, &fs); timeval tv = { 0, 200000 };
        if (select(0, &fs, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET c = accept(ls, nullptr, nullptr); if (c == INVALID_SOCKET) continue;
        char buf[4096]; int n = recv(c, buf, sizeof(buf) - 1, 0);
        std::string req = n > 0 ? std::string(buf, n) : "";
        size_t e1 = req.find(' '), e2 = e1 == std::string::npos ? e1 : req.find(' ', e1 + 1);
        std::string target = (e1 != std::string::npos && e2 != std::string::npos) ? req.substr(e1 + 1, e2 - e1 - 1) : "";
        if (target.rfind("/callback", 0) != 0) { closesocket(c); continue; }
        code = queryParam(target, "code"); err = queryParam(target, "error");
        if (queryParam(target, "state") != state) { code.clear(); if (err.empty()) err = "state mismatch"; }
        std::string html = "<html><body style='background:#0b0b12;color:#fff;font-family:Segoe UI,sans-serif;text-align:center;padding-top:20vh'>"
            "<h1>" + std::string(code.empty() ? "Sign-in failed" : "Pulse Visualizer is connected") + "</h1><p>You can close this tab.</p></body></html>";
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " + std::to_string(html.size()) + "\r\n\r\n" + html;
        send(c, resp.c_str(), (int)resp.size(), 0); closesocket(c); break;
    }
    closesocket(ls);
    if (code.empty()) { if (!abortAuth) setStatus(ST_ERR, err.empty() ? "Sign-in cancelled or timed out" : "Spotify: " + err); return; }

    HttpResp r = http("POST", "https://accounts.spotify.com/api/token",
        "grant_type=authorization_code&code=" + urlEnc(code) + "&redirect_uri=" + urlEnc(REDIRECT) + "&client_id=" + urlEnc(cid) + "&code_verifier=" + verifier,
        "Content-Type: application/x-www-form-urlencoded\r\n");
    JV j = parseJson(r.body);
    if (r.status != 200 || j["access_token"].str().empty()) { setStatus(ST_ERR, "Token exchange failed: " + j["error_description"].str("check Client ID / redirect URI")); return; }
    access = j["access_token"].str(); expiresAt = nowSec() + j["expires_in"].num(3600);
    std::string nr = j["refresh_token"].str();
    { std::lock_guard<std::mutex> l(mx); refresh = nr; }
    WritePrivateProfileStringW(L"spotify", L"refresh", toW(dpapiProtect(nr)).c_str(), iniPath().c_str());
    setStatus(ST_ON, ""); nextPoll = 0;
}
Spotify gSp;