#include "WebUi.h"

#include <cstdio>

#include "Auth.h"
#include "Config.h"

namespace ic {

namespace {
constexpr int kMaxFailures = 5;
constexpr uint32_t kLockFrames = 30 * 1000 / kFrameMs;  // 30 s
constexpr size_t kMaxCommandBytes = 200;

const char* linkName(LinkType l) {
    switch (l) {
        case LinkType::Ethernet: return "ethernet";
        case LinkType::WiFi: return "wifi";
        default: return "none";
    }
}

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c == '\n') out += "\\n";
        else if (c < 0x20) {
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", c);
            out += b;
        } else out += char(c);
    }
    return out + "\"";
}

HttpResponse text(int status, const std::string& body) {
    HttpResponse r;
    r.status = status;
    r.body = body;
    return r;
}
}  // namespace

const char* HttpResponse::reason() const {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 429: return "Too Many Requests";
        case 503: return "Service Unavailable";
        default: return "Error";
    }
}

bool WebUi::authorized(const std::string& header) {
    if (header.compare(0, 6, "Basic ") != 0) return false;
    // Fast path: same header as the last good login (the page polls every second, and the full
    // check is deliberately slow).
    std::string h = sha256Hex(header);
    std::string current = auth::stored(ic_.store());
    if (!goodHeader_.empty() && h == goodHeader_ && goodFor_ == current) return true;
    std::string userPass;
    if (!base64Decode(header.substr(6), &userPass)) return false;
    size_t colon = userPass.find(':');
    std::string pass = colon == std::string::npos ? userPass : userPass.substr(colon + 1);
    if (!auth::checkPassword(ic_.store(), pass)) return false;
    goodHeader_ = h;
    goodFor_ = current;
    return true;
}

HttpResponse WebUi::handle(const HttpRequest& req) {
    if (!auth::hasPassword(ic_.store())) {
        goodHeader_.clear();
        return text(403, "Web interface is off. Set a password on the USB console: webpass <password>");
    }
    if (ic_.frame() < lockedUntil_) return text(429, "Too many wrong passwords. Try again in 30 seconds.");
    if (!authorized(req.authorization)) {
        if (!req.authorization.empty() && ++failures_ >= kMaxFailures) {
            failures_ = 0;
            lockedUntil_ = ic_.frame() + kLockFrames;
        }
        HttpResponse r = text(401, "Password needed.");
        r.askLogin = true;
        char u[8];
        formatUnit(ic_.unit(), u);
        r.realm = std::string("Intercom ") + u;
        return r;
    }
    failures_ = 0;

    std::string path = req.path.substr(0, req.path.find('?'));
    if (path == "/" || path == "/index.html") {
        if (req.method != "GET") return text(405, "GET only");
        HttpResponse r;
        r.contentType = "text/html; charset=utf-8";
        r.body = webPage();
        return r;
    }
    if (path == "/api/status") {
        HttpResponse r;
        r.contentType = "application/json";
        r.body = statusJson();
        return r;
    }
    if (path == "/api/cmd") {
        if (req.method != "POST") return text(405, "POST only");
        if (req.xIntercom != "1") return text(403, "missing X-Intercom header");
        if (req.body.size() > kMaxCommandBytes) return text(400, "command too long");
        return text(200, ic_.command(req.body, CmdSource::Web));
    }
    return text(404, "not found");
}

std::string WebUi::statusJson() const {
    char u[8], ip[16], buf[512];
    formatUnit(ic_.unit(), u);
    formatIp(ic_.unit(), ip);
    PowerState pw = ic_.power();
    const AecStats& a = const_cast<Intercom&>(ic_).aec().stats();
    AudioStats au = ic_.audioStats();
    std::string out = "{";
    snprintf(buf, sizeof(buf),
             "\"unit\":\"%s\",\"ip\":\"%s\",\"version\":\"%s\",\"uptime_s\":%u,\"link\":\"%s\",\"power\":\"%s\","
             "\"has_battery\":%s,\"battery\":%u,\"talking\":%s,\"tone\":%s,\"calibrating\":%s,\"cal_progress\":%d,\"in_setup\":%s,",
             u, ip, kFirmwareVersion, unsigned(ic_.frame() / (1000 / kFrameMs)), linkName(ic_.link()),
             pw.source == PowerSource::Battery ? "battery" : "poe", pw.hasBattery ? "true" : "false", unsigned(pw.batteryPct),
             ic_.transmitting() ? "true" : "false", ic_.toneActive() ? "true" : "false", ic_.calibrating() ? "true" : "false",
             ic_.calProgress(), ic_.inSetup() ? "true" : "false");
    out += buf;
    snprintf(buf, sizeof(buf),
             "\"aec\":{\"on\":%s,\"name\":\"%s\",\"erle_db\":%.1f,\"far\":%s,\"double_talk\":%s,\"converged\":%s,\"nlp_gain\":%.2f},"
             "\"levels\":{\"mic\":%u,\"sent\":%u,\"spk\":%u},\"talkers\":%d,"
             "\"audio\":{\"played\":%u,\"concealed\":%u,\"late\":%u},",
             ic_.params().aec ? "true" : "false", const_cast<Intercom&>(ic_).aec().name(), double(a.erleDb),
             a.farActive ? "true" : "false", a.doubleTalk ? "true" : "false", a.converged ? "true" : "false", double(a.nlpGain),
             ic_.micLevel(), ic_.sentLevel(), ic_.speakerLevel(), ic_.talkersPlaying(), unsigned(au.framesPlayed),
             unsigned(au.concealed), unsigned(au.lateDrops));
    out += buf;
    out += "\"params\":" + ic_.params().toJson() + ",";
    const CalResult* cal = ic_.lastCal();
    out += "\"cal\":" + (cal ? jsonString(cal->summary()) : std::string("null")) + ",";
    out += "\"peers\":[";
    bool first = true;
    for (const Peer& p : ic_.peers()) {
        char pu[8];
        formatUnit(p.status.from, pu);
        snprintf(buf, sizeof(buf), "%s{\"unit\":\"%s\",\"link\":\"%s\",\"power\":\"%s\",\"battery\":%u,\"talking\":%s,\"uptime_s\":%u,\"age_s\":%.1f}",
                 first ? "" : ",", pu, linkName(p.status.link), p.status.power == PowerSource::Battery ? "battery" : "poe",
                 unsigned(p.status.battery), (p.status.flags & kStatusTalking) ? "true" : "false", unsigned(p.status.uptimeS),
                 double(ic_.frame() - p.lastFrame) * kFrameMs / 1000.0);
        out += buf;
        first = false;
    }
    return out + "]}";
}

}  // namespace ic
