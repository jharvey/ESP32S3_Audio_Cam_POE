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
        case 409: return "Conflict";
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
    fw_.tick(ic_.frame());
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
    if (path == "/school") {
        if (req.method != "GET") return text(405, "GET only");
        HttpResponse r;
        r.contentType = "text/html; charset=utf-8";
        r.body = schoolPage();
        return r;
    }
    if (path == "/floorplan.js") {  // floor plan map for /school (FloorPlanJs.cpp)
        if (req.method != "GET") return text(405, "GET only");
        HttpResponse r;
        r.contentType = "application/javascript; charset=utf-8";
        r.body = floorPlanJs();
        return r;
    }
    if (path == "/api/school") {
        HttpResponse r;
        r.contentType = "application/json";
        ic_.consoleSeen();  // an office page is open (console watch)
        r.body = schoolJson();
        return r;
    }
    if (path == "/api/talk") {
        if (req.method != "POST") return text(405, "POST only");
        if (req.xIntercom != "1") return text(403, "missing X-Intercom header");
        if (req.body.size() > 16000) return text(400, "too much audio in one post (max 0.375 s)");
        std::vector<uint8_t> raw;
        if (!base64Decode(req.body, &raw) || raw.size() % 2) return text(400, "body must be base64 of 16-bit PCM");
        std::vector<int16_t> pcm(raw.size() / 2);
        for (size_t i = 0; i < pcm.size(); i++) pcm[i] = int16_t(raw[2 * i] | (raw[2 * i + 1] << 8));
        // ?to=all | room:214 | floor:2 | group:299 (R2 room calls); fixed for the whole press
        TalkTarget tt;
        size_t q = req.path.find("to=");
        if (q != std::string::npos) {
            std::string v = req.path.substr(q + 3);
            v = v.substr(0, v.find('&'));
            for (size_t e; (e = v.find("%3A")) != std::string::npos || (e = v.find("%3a")) != std::string::npos;) v.replace(e, 3, ":");  // browsers send the colon encoded
            size_t c = v.find(':');
            std::string kind = v.substr(0, c);
            unsigned long n = c == std::string::npos ? 0 : std::strtoul(v.c_str() + c + 1, nullptr, 10);
            if (kind == "room") tt.kind = TalkTarget::Room;
            else if (kind == "floor") tt.kind = TalkTarget::Floor;
            else if (kind == "group") tt.kind = TalkTarget::Group;
            else if (kind == "zone") tt.kind = TalkTarget::Zone;
            else if (kind != "all") return text(400, "to= must be all, room:N, floor:N, group:N or zone:N");
            if (tt.kind != TalkTarget::All && (n < 1 || n > 999)) return text(400, "bad room / floor / group number");
            tt.n = uint16_t(n);
        }
        ic_.setWebTarget(tt);
        size_t waiting = ic_.webTalk(pcm.data(), pcm.size());
        return text(200, "ok " + std::to_string(waiting));
    }
    if (path == "/api/replies") {  // ?since=N: reply lines to commands this unit sent
        uint32_t since = 0;
        size_t q = req.path.find("since=");
        if (q != std::string::npos) since = uint32_t(std::strtoul(req.path.c_str() + q + 6, nullptr, 10));
        std::string out = "{\"lines\":[";
        bool first = true;
        uint32_t last = since;
        char u[8];
        for (const auto& l : ic_.replies()) {
            if (l.n <= since) continue;
            formatUnit(l.from, u);
            out += std::string(first ? "" : ",") + "{\"n\":" + std::to_string(l.n) + ",\"from\":\"" + u + "\",\"text\":" + jsonString(l.text) + "}";
            first = false;
            last = l.n;
        }
        HttpResponse r;
        r.contentType = "application/json";
        r.body = out + "],\"next\":" + std::to_string(last) + "}";
        return r;
    }
    if (path == "/api/status") {
        HttpResponse r;
        r.contentType = "application/json";
        r.body = statusJson();
        return r;
    }
    if (path == "/api/log") {  // ?since=N: the events after number N
        uint32_t since = 0;
        size_t q = req.path.find("since=");
        if (q != std::string::npos) since = uint32_t(std::strtoul(req.path.c_str() + q + 6, nullptr, 10));
        const auto& ev = ic_.events();
        if (!ev.empty() && since > ev.back().n) since = 0;  // the unit restarted since the page last asked
        std::string out = "{\"events\":[";
        bool first = true;
        uint32_t last = ev.empty() ? 0 : since;
        for (const auto& e : ev) {
            if (e.n <= since) continue;
            out += std::string(first ? "" : ",") + "{\"n\":" + std::to_string(e.n) + ",\"utc\":" + std::to_string((long long)e.utc) +
                   ",\"up_s\":" + std::to_string(e.frame / 50) + ",\"kind\":" + std::to_string(int(e.kind)) + ",\"room\":" +
                   std::to_string(e.room) + ",\"text\":" + jsonString(e.text) + "}";
            first = false;
            last = e.n;
        }
        HttpResponse r;
        r.contentType = "application/json";
        r.body = out + "],\"next\":" + std::to_string(last) + "}";
        return r;
    }
    if (path == "/api/backup") {
        if (req.method != "GET") return text(405, "GET only");
        return text(200, ic_.backupText());
    }
    if (path == "/api/restore") {
        if (req.method != "POST") return text(405, "POST only");
        if (req.xIntercom != "1") return text(403, "missing X-Intercom header");
        if (req.body.size() > 16000) return text(400, "file too large for a settings backup");
        int done = 0;
        std::string errors;
        size_t a = 0;
        while (a < req.body.size()) {
            size_t e = req.body.find('\n', a);
            if (e == std::string::npos) e = req.body.size();
            std::string line = req.body.substr(a, e - a);
            a = e + 1;
            if (line.find('=') == std::string::npos || line[0] == '#') continue;
            std::string err = ic_.restoreLine(line);
            if (err.empty()) done++;
            else errors += "\n" + err;
        }
        if (!done && errors.empty()) return text(400, "no settings in this file");
        return text(errors.empty() ? 200 : 400, "restored " + std::to_string(done) + " settings" + (errors.empty() ? "" : "; not restored:" + errors));
    }
    if (path == "/api/cam.jpg") {  // the camera is for emergencies only, and every viewing is logged
        if (req.method != "GET") return text(405, "GET only");
        if (!ic_.hasCamera()) return text(404, "this unit has no camera");
        if (!ic_.cameraAllowed()) return text(403, "the camera only works while an emergency is on at this unit");
        ic_.noteCameraView();
        HttpResponse r;
        r.contentType = "image/jpeg";
        r.camera = true;
        return r;
    }
    if (path.compare(0, 9, "/api/site") == 0) return siteRequest(req, path);
    if (path.compare(0, 7, "/api/fw") == 0) return firmwareRequest(req, path);
    if (path == "/api/cmd") {
        if (req.method != "POST") return text(405, "POST only");
        if (req.xIntercom != "1") return text(403, "missing X-Intercom header");
        if (req.body.size() > kMaxCommandBytes) return text(400, "command too long");
        return text(200, ic_.command(req.body, CmdSource::Web));
    }
    return text(404, "not found");
}

namespace {
// "?size=123&sha=ab" -> the value of one name, or "".
std::string queryValue(const std::string& path, const char* name) {
    size_t q = path.find('?');
    while (q != std::string::npos && q + 1 < path.size()) {
        size_t end = path.find('&', q + 1);
        std::string kv = path.substr(q + 1, end == std::string::npos ? std::string::npos : end - q - 1);
        size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.compare(0, eq, name) == 0) return kv.substr(eq + 1);
        q = end;
    }
    return "";
}
// Digits only, at most 9 of them (no overflow, no signs, no spaces).
bool parseSize(const std::string& s, size_t* out) {
    if (s.empty() || s.size() > 9) return false;
    size_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + size_t(c - '0');
    }
    *out = v;
    return true;
}
}  // namespace

// The office page's plan, dots, areas and emergency steps, kept on the unit (and copied between the office units).
//   GET  /api/site                      the saved text, or 404
//   POST /api/site/put?off=N&total=T    a piece of at most 4096 bytes, in order; the last piece saves it
HttpResponse WebUi::siteRequest(const HttpRequest& req, const std::string& path) {
    if (path == "/api/site") {
        if (req.method != "GET") return text(405, "GET only");
        HttpResponse r;
        r.contentType = "application/json";
        r.body = ic_.siteText();
        if (r.body.empty()) return text(404, "no plan saved on this unit");
        return r;
    }
    if (path == "/api/site/put") {
        if (req.method != "POST") return text(405, "POST only");
        if (req.xIntercom != "1") return text(403, "missing X-Intercom header");
        size_t off = 0, total = 0;
        if (!parseSize(queryValue(req.path, "off"), &off) || !parseSize(queryValue(req.path, "total"), &total)) return text(400, "off=N and total=T are needed");
        if (total == 0 || total > 200000 || req.body.empty() || req.body.size() > 4096) return text(400, "the plan is too large for the unit (200 kB at most)");
        if (off == 0) siteUp_.clear();
        if (off != siteUp_.size() || off + req.body.size() > total) return text(409, "out of order: start again at off=0");
        siteUp_ += req.body;
        if (siteUp_.size() < total) return text(200, "more");
        std::string err = ic_.sitePut(siteUp_);
        siteUp_.clear();
        siteUp_.shrink_to_fit();
        if (!err.empty()) return text(507, err);
        return text(200, "saved " + std::to_string(ic_.siteRev()) + " " + std::to_string(ic_.siteHash()));
    }
    return text(404, "not found");
}

HttpResponse WebUi::firmwareRequest(const HttpRequest& req, const std::string& path) {
    static const char* kStates[] = {"idle", "receiving", "done", "failed"};
    if (path == "/api/fw") {
        HttpResponse r;
        r.contentType = "application/json";
        r.body = std::string("{\"available\":") + (fw_.available() ? "true" : "false") + ",\"allowed\":" +
                 (ic_.netUpdateAllowed() ? "true" : "false") + ",\"pending\":" +
                 (ic_.firmware() && ic_.firmware()->pending() ? "true" : "false") + ",\"version\":\"" + kFirmwareVersion + "\",\"state\":\"" +
                 kStates[int(fw_.state())] + "\",\"size\":" + std::to_string(fw_.size()) + ",\"received\":" +
                 std::to_string(fw_.received()) + ",\"max_chunk\":" + std::to_string(kFwMaxChunk) + ",\"sha\":\"" + fw_.sha() +
                 "\",\"error\":" + jsonString(fw_.error()) + "}";
        return r;
    }
    if (req.method != "POST") return text(405, "POST only");
    if (req.xIntercom != "1") return text(403, "missing X-Intercom header");
    if (!fw_.available()) return text(404, "firmware updates over the network are not available on this unit");
    if (!ic_.netUpdateAllowed()) return text(403, "firmware updates are off. Switch them on at the USB console: netupdate on");
    if (path == "/api/fw/begin") {
        size_t size = 0;
        if (!parseSize(queryValue(req.path, "size"), &size)) return text(400, "size=N (bytes) is needed");
        PowerState pw = ic_.power();
        if (pw.source == PowerSource::Battery && pw.batteryPct < 30) return text(409, "on battery below 30 %: connect PoE first");
        std::string err = fw_.begin(size, queryValue(req.path, "sha"), ic_.frame());
        if (!err.empty()) return text(400, err);
        return text(200, "ok");
    }
    if (path == "/api/fw/data") {
        // Calls come first: a flash write stalls the audio for a moment, so wait while anything plays or is sent.
        if (ic_.transmitting() || ic_.talkersPlaying() || ic_.soundPlaying() || ic_.calibrating() || ic_.couplingTest())
            return text(503, "busy: audio is playing, send this piece again");
        size_t off = 0;
        if (!parseSize(queryValue(req.path, "off"), &off)) return text(400, "off=N is needed");
        if (req.body.size() > kFwMaxChunk * 4 / 3 + 8) return text(400, "piece too large (8192 bytes at most)");
        std::vector<uint8_t> raw;
        if (!base64Decode(req.body, &raw) || raw.empty()) return text(400, "body must be base64");
        std::string err = fw_.data(off, raw.data(), raw.size(), ic_.frame());
        if (err.empty()) return text(200, "ok " + std::to_string(fw_.received()));
        return text(err.compare(0, 15, "expected offset") == 0 ? 409 : 400, err);
    }
    if (path == "/api/fw/end") {
        std::string err = fw_.end();
        if (!err.empty()) return text(400, err);
        std::string r = ic_.command("reboot", CmdSource::Web);
        return text(200, "ok sha256 " + fw_.sha() + "; " + r);
    }
    if (path == "/api/fw/abort") {
        fw_.abort("stopped by the sender");
        return text(200, "ok");
    }
    return text(404, "not found");
}

bool base64Decode(const std::string& in, std::vector<uint8_t>* out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out->clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = val(c);
        if (v < 0) return false;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back(uint8_t(acc >> bits));
        }
    }
    return true;
}

std::string WebUi::schoolJson() {
    const uint32_t now = ic_.frame();
    char buf[400], u[8], ip[16];
    auto one = [&](const StatusPacket& st, bool online, double ageS) {
        formatUnit(st.from, u);
        formatIp(st.from, ip);
        snprintf(buf, sizeof(buf),
                 "{\"unit\":\"%s\",\"room\":%u,\"ip\":\"%s\",\"group\":%u,\"link\":\"%s\",\"power\":\"%s\",\"battery\":%u,"
                 "\"flags\":%u,\"fault\":%u,\"online\":%s,\"age_s\":%.1f,\"fw\":\"%s\",\"emergency\":%u,\"flags2\":%u,\"sync\":%d}",
                 u, unsigned(st.from.room), ip, unsigned(st.group ? st.group : st.from.room), linkName(st.link),
                 st.power == PowerSource::Battery ? "battery" : "poe", unsigned(st.battery), unsigned(st.flags),
                 unsigned(st.fault), online ? "true" : "false", ageS, versionText(st.fwVer).c_str(), unsigned(st.emergency), unsigned(st.flags2),
                 !st.syncHash ? -1 : st.syncHash == ic_.syncHash() ? 1 : 0);
        std::string o(buf);
        if (st.radarKind) {  // "radar":[kind, x1,y1, x2,y2, ...] in mm
            o.pop_back();
            o += ",\"radar\":[" + std::to_string(st.radarKind);
            for (int k = 0; k < int(st.radarCount) && k < 3; k++) o += "," + std::to_string(st.radarX[k]) + "," + std::to_string(st.radarY[k]);
            o += "],\"ppl\":[" + std::to_string(st.radarPeople) + "," + std::to_string(st.radarQuietMin) + "]}";  // people in the room, minutes empty
        }
        return o;
    };
    // this unit too
    StatusPacket me;
    me.from = ic_.unit();
    me.link = ic_.link();
    PowerState pw = ic_.power();
    me.power = pw.source;
    me.battery = pw.hasBattery ? pw.batteryPct : 0;
    me.flags = ic_.statusFlags();
    me.fwVer = packVersion(kFirmwareVersion);
    me.emergency = uint8_t(ic_.emergency());
    me.flags2 = ic_.statusFlags2();
    me.syncRev = ic_.syncRev();
    me.syncHash = ic_.syncHash();
    me.siteRev = ic_.siteRev();
    me.siteHash = ic_.siteHash();
    {
        const RadarState r = ic_.radar();
        me.radarKind = r.kind;
        me.radarCount = r.count;
        me.radarPeople = ic_.people();
        me.radarQuietMin = ic_.radarQuietMin();
        for (int k = 0; k < 3; k++) {
            me.radarX[k] = r.x[k];
            me.radarY[k] = r.y[k];
        }
    }
    // R3.2: feature switches, the office and who acts as it, zones, and the rooms waiting for the office
    std::string officeList;
    for (uint16_t r : ic_.offices()) officeList += (officeList.empty() ? "" : ",") + std::to_string(r);
    std::string pinBad;
    for (uint16_t r : ic_.officeMismatch()) pinBad += (pinBad.empty() ? "" : ",") + std::to_string(r);
    std::string extra = std::string("\"site\":{\"ok\":") + (ic_.siteAvailable() ? "true" : "false") + ",\"rev\":" + std::to_string(ic_.siteRev()) +
                        ",\"hash\":" + std::to_string(ic_.siteHash()) + ",\"size\":" + std::to_string(ic_.siteSize()) + "},\"pinbad\":[" + pinBad + "],\"features\":" + std::to_string(ic_.features()) + ",\"conwatch\":" + std::to_string(ic_.consoleWatchMin()) + ",\"office\":" + std::to_string(ic_.officeRoom()) +
                        ",\"standin\":" + std::to_string(ic_.officeStandIn()) + ",\"acting\":" + std::to_string(ic_.actingOffice()) + ",\"offices\":[" + officeList + "],\"zones\":[";
    bool firstX = true;
    for (const auto& z : ic_.zones()) {
        extra += std::string(firstX ? "" : ",") + "{\"n\":" + std::to_string(z.n) + ",\"name\":" + jsonString(z.name) + ",\"rooms\":" + jsonString(z.rooms) + "}";
        firstX = false;
    }
    extra += "],\"queue\":[";
    firstX = true;
    if (ic_.feature(Intercom::kFeatQueue))
        for (const auto& w : ic_.callQueue()) {
            extra += std::string(firstX ? "" : ",") + "{\"room\":" + std::to_string(w.room) + ",\"urgent\":" + (w.urgent ? "true" : "false") +
                     ",\"silent\":" + (w.silent ? "true" : "false") + ",\"wait_s\":" + std::to_string((now - w.since) / 50) + "}";
            firstX = false;
        }
    extra += "],";
    me.fault = ic_.faultCode();
    me.group = ic_.group();
    formatUnit(me.from, u);
    std::string out = std::string("{\"self\":\"") + u + "\",\"fw\":\"" + kFirmwareVersion + "\",\"tests_locked\":" + (ic_.testsLocked() ? "true" : "false") +
                      "," + extra + "\"units\":[" + one(me, true, 0);
    for (const Peer& s : ic_.known()) {
        double age = double(now - s.lastFrame) * kFrameMs / 1000.0;
        out += "," + one(s.status, age < 5.0, age);
    }
    return out + "]}";
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
    static const char* kEm[] = {"", "lockdown", "evacuate", "shelter"};
    snprintf(buf, sizeof(buf), "\"emergency\":\"%s\",\"audio_fault\":%s,\"has_camera\":%s,\"camera_allowed\":%s,", kEm[int(ic_.emergency())],
             ic_.audioFault() ? "true" : "false", ic_.hasCamera() ? "true" : "false", ic_.cameraAllowed() ? "true" : "false");
    out += buf;
    out += "\"params\":" + ic_.params().toJson() + ",";
    const CalResult* cal = ic_.lastCal();
    out += "\"cal\":" + (cal ? jsonString(cal->summary()) : std::string("null")) + ",";
    out += "\"peers\":[";
    bool first = true;
    for (const Peer& p : ic_.peers()) {
        char pu[8];
        formatUnit(p.status.from, pu);
        snprintf(buf, sizeof(buf), "%s{\"unit\":\"%s\",\"link\":\"%s\",\"power\":\"%s\",\"battery\":%u,\"talking\":%s,\"muted\":%s,\"talk_held\":%s,\"fault\":%u,\"uptime_s\":%u,\"age_s\":%.1f}",
                 first ? "" : ",", pu, linkName(p.status.link), p.status.power == PowerSource::Battery ? "battery" : "poe",
                 unsigned(p.status.battery), (p.status.flags & kStatusTalking) ? "true" : "false",
                 (p.status.flags & kStatusMuted) ? "true" : "false", (p.status.flags & kStatusTalkHeld) ? "true" : "false",
                 unsigned(p.status.fault), unsigned(p.status.uptimeS),
                 double(ic_.frame() - p.lastFrame) * kFrameMs / 1000.0);
        out += buf;
        first = false;
    }
    return out + "]}";
}

}  // namespace ic
