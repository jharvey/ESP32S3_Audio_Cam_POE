// Audio tuning parameters. Auto-tune (Calibrator) fills them in from measurements in the
// real room; any of them can be read or overridden by hand from the command console
// (USB serial or network), and they are saved in flash.
#pragma once
#include <string>

namespace ic {

struct TuneParams {
    bool aec = true;          // echo canceller on
    int taps = 2048;          // echo canceller filter length, samples (16 per ms)
    float delayMs = 0;        // speaker -> mic delay beyond one frame (DMA, converters, distance)
    float mu = 0.15f;         // adaptation step, 0.01..1 (lower = slower, steadier in reverb)
    float dtDb = 9;           // double-talk threshold: error this far above normal = someone talking here
    bool nlp = true;          // residual echo suppression on
    float nlpDb = -12;        // residual echo suppression depth
    bool duck = false;        // half duplex: speaker down while TALK held (gym)
    float duckDb = -20;
    float volumeDb = 0;       // speaker volume
    float micGainDb = 0;      // gain on the sent (echo-cancelled) audio
    float calLevelDb = -20;   // auto-tune noise burst level, dBFS
    bool opus = true;         // send Opus (falls back to PCM if Opus isn't built in)
    int bitrateKbps = 24;     // Opus bitrate
    float coupleDuckDb = -6;  // play talkers this unit hears through a wall / door this much quieter (coupling test)

    // Text form "key=value key=value ...", used for flash storage and the console.
    std::string toString() const;
    void fromString(const std::string& s);  // unknown keys ignored, values clamped
    bool set(const std::string& key, const std::string& value);  // false = unknown key / bad value
    bool get(const std::string& key, std::string* value) const;
    static const char* help();  // one line per key with range
    // JSON array for the web interface: [{"key","kind","min","max","desc","value"}, ...]
    std::string toJson() const;
};

}  // namespace ic
