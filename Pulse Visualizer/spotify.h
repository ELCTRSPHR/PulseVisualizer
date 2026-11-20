// Spotify Web API client, album art, LRCLIB lyrics
#pragma once
#include "common.h"
#include "http.h"

// ============================================================================
//  SPOTIFY  (Web API: PKCE sign-in, playback state, controls, up-next queue)
// ============================================================================
struct TrackInfo { std::string id, name, artist, album; int durMs = 0; };
struct PlayerState {
    bool device = false, playing = false, shuffle = false;
    std::string repeat = "off"; int volume = 50, progMs = 0; double stamp = 0; TrackInfo t;
};
struct ArtData { std::vector<uint8_t> px; int w = 0, h = 0; float col[3][3] = {}; unsigned gen = 0; };
void decodeArt(const std::string& data, ArtData& out);
struct LyricLine { int ms = 0; std::string text; };
struct LyricsData { std::string id; int state = 0; std::vector<LyricLine> lines; unsigned gen = 0; };
void parseLrc(const std::string& lrc, std::vector<LyricLine>& out);
void spreadPlain(const std::string& plain, int durMs, std::vector<LyricLine>& out);
std::string cleanTitle(std::string s);
class Spotify {
public:
    enum { ST_NOID, ST_OFF, ST_WAIT, ST_ON, ST_ERR };
    std::atomic<int> status{ ST_NOID };
    std::atomic<bool> haveId{ false };
    void start();
    void stop();
    PlayerState state();
    std::string message();
    std::string takeNotice();
    bool getLyrics(LyricsData& out, unsigned& have);
    bool getArt(ArtData& out, unsigned& have);
    void setClientId(std::string id);
    void connect();
    void disconnect();
    void next();
    void prev();
    void togglePlay();
    void seek(int ms);
    void setVolume(int v);
    void setShuffle(bool on);
    void setRepeat(const std::string& mode);
    void wantQueue();                                   // fetch "up next" (rate-limited; call freely while the card is open)
    bool getQueue(std::vector<TrackInfo>& out, std::string& forTrack);
    void skipTo(int k);                                 // jump to the k-th upcoming track (0 = next)
private:
    std::vector<TrackInfo> upNext; std::string upNextFor; double queueAt = -100.0; bool queueBusy = false;
    std::thread th;
    std::mutex mx;
    std::condition_variable cv;
    std::deque<std::function<void()>> q;
    std::atomic<bool> running{ false }, abortAuth{ false };
    std::atomic<int> volPending{ -1 };
    std::string clientId, access, refresh, statusMsg, notice, artUrlDone;
    double expiresAt = 0, nextPoll = 0;
    PlayerState ps;
    ArtData artData;
    LyricsData lyr;
    std::string lyrIdReq;
    std::thread lth;
    std::mutex lmx;
    std::condition_variable lcv;
    TrackInfo lpend;
    bool lhas = false;
    static constexpr const char* REDIRECT = "http://127.0.0.1:8888/callback";
    static constexpr const char* SCOPES = "user-read-playback-state user-modify-playback-state user-read-currently-playing";
    void post(std::function<void()> f);
    void setStatus(int s, const std::string& m);
    void setNotice(const std::string& m);
    void pollSoon();
    static std::string iniGet(const wchar_t* key);
    void run();
    bool ensureToken();
    bool refreshToken();
    HttpResp api(const char* method, const std::string& path, const std::string& body = "");
    void playBody(const std::string& body);
    void poll();
    void fetchArt(const std::string& url);
    LyricsData lookupLyrics(const TrackInfo& t, bool& netFail);
    void lyricWorker();
    void doAuth();
};

extern Spotify gSp;