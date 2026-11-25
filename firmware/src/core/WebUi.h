// Web interface for one unit: a status and settings page, served by the unit itself.
//
// Transport-free: the board (Arduino WebServer) or the simulator (QTcpServer) parses HTTP
// and hands over an HttpRequest; handle() returns the response. Call it on the same thread
// as Intercom::processFrame (it runs console commands and reads live state).
//
//   GET  /             the page (HTML + JS, built in)
//   GET  /api/status   live state as JSON (polled by the page once a second)
//   GET  /school       whole-school status: every unit this one hears, as a grid of rooms
//   GET  /api/school   that list as JSON (offline units stay listed, marked offline)
//   GET  /floorplan.js the floor plan map on /school (the DXF is loaded and kept in the browser only)
//   POST /api/talk     web console talk: body = base64 of 16 kHz mono 16-bit PCM (about 100 ms per post);
//                      this unit transmits it as an office all-call. Needs X-Intercom: 1.
//   POST /api/cmd      body = one console command, reply = its text. Needs header X-Intercom: 1
//                      (a browser on another site can't add it, which blocks cross-site requests).
//   Firmware update (R3.0, FwUpdate.h). Off until "netupdate on" is typed on the USB console. All POSTs
//   need X-Intercom: 1.
//   GET  /api/fw               state as JSON (available, allowed, state, size, received, error)
//   POST /api/fw/begin?size=N[&sha=HEX]   start an image of N bytes (SHA-256 optional)
//   POST /api/fw/data?off=N    body = base64 of the next piece (up to 8192 bytes). 409 "expected offset M":
//                              send from M. 503: a call or sound is playing, send the piece again shortly.
//   POST /api/fw/end           check the image, switch to it and restart
//   POST /api/fw/abort         drop an unfinished transfer
//
// Security: HTTP Basic login with the password set over USB ("webpass"). No password = the
// web interface is off. 5 wrong passwords lock it for 30 s. Firmware uploads are a separate switch
// ("netupdate on", USB only) on top of the password.
#pragma once
#include <string>
#include <vector>

#include "FwUpdate.h"
#include "Intercom.h"

namespace ic {

struct HttpRequest {
    std::string method;         // "GET", "POST"
    std::string path;           // "/api/status" (query string ignored)
    std::string authorization;  // the Authorization header, or empty
    std::string xIntercom;      // the X-Intercom header, or empty
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;
    bool askLogin = false;  // add "WWW-Authenticate: Basic realm=..." (status 401)
    const char* reason() const;
    std::string realm;      // login prompt text
};

class WebUi {
public:
    explicit WebUi(Intercom& ic) : ic_(ic), fw_(ic.firmware()) {}
    const FwUpdate& firmware() const { return fw_; }
    HttpResponse handle(const HttpRequest& req);
    std::string statusJson() const;
    std::string schoolJson();

private:
    bool authorized(const std::string& header);
    HttpResponse firmwareRequest(const HttpRequest& req, const std::string& path);

    Intercom& ic_;
    FwUpdate fw_;
    std::string goodHeader_;  // hash of the last accepted Authorization header (skips the slow check)
    std::string goodFor_;     // ...valid only while the stored password is unchanged
    int failures_ = 0;
    uint32_t lockedUntil_ = 0;
};

const char* webPage();     // WebPage.cpp
const char* schoolPage();  // SchoolPage.cpp
const char* floorPlanJs();  // FloorPlanJs.cpp: the floor plan map on /school (DXF loaded in the browser)
bool base64Decode(const std::string& in, std::vector<uint8_t>* out);

}  // namespace ic
