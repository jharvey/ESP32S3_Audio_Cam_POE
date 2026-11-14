#include "Tuning.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace ic {

namespace {
enum class Kind { Bool, Int, Float };
struct Field {
    const char* key;
    Kind kind;
    float lo, hi;
    const char* desc;
};
const Field kFields[] = {
    {"aec", Kind::Bool, 0, 1, "echo canceller on (0/1)"},
    {"taps", Kind::Int, 128, 4096, "echo canceller length, samples (16 per ms)"},
    {"delay_ms", Kind::Float, 0, 300, "speaker->mic delay beyond one frame, ms"},
    {"mu", Kind::Float, 0.01f, 1.0f, "adaptation step (lower = steadier in reverb)"},
    {"dt_db", Kind::Float, 3, 20, "double-talk threshold, dB"},
    {"nlp", Kind::Bool, 0, 1, "residual echo suppression on (0/1)"},
    {"nlp_db", Kind::Float, -40, 0, "residual echo suppression depth, dB"},
    {"duck", Kind::Bool, 0, 1, "speaker down while TALK held (0/1)"},
    {"duck_db", Kind::Float, -40, 0, "ducking depth, dB"},
    {"volume_db", Kind::Float, -40, 12, "speaker volume, dB"},
    {"mic_gain_db", Kind::Float, -20, 30, "sent audio gain, dB"},
    {"cal_level_db", Kind::Float, -40, -6, "auto-tune noise burst level, dBFS"},
};

float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
}  // namespace

static void* fieldPtr(TuneParams& p, int i) {
    void* ptrs[] = {&p.aec, &p.taps, &p.delayMs, &p.mu, &p.dtDb, &p.nlp, &p.nlpDb, &p.duck, &p.duckDb, &p.volumeDb, &p.micGainDb, &p.calLevelDb};
    return ptrs[i];
}

bool TuneParams::set(const std::string& key, const std::string& value) {
    for (int i = 0; i < int(sizeof(kFields) / sizeof(kFields[0])); i++) {
        const Field& f = kFields[i];
        if (key != f.key) continue;
        char* end = nullptr;
        float v = std::strtof(value.c_str(), &end);
        if (end == value.c_str()) return false;
        v = clampf(v, f.lo, f.hi);
        void* ptr = fieldPtr(*this, i);
        if (f.kind == Kind::Bool) *static_cast<bool*>(ptr) = v >= 0.5f;
        else if (f.kind == Kind::Int) *static_cast<int*>(ptr) = int(v + 0.5f);
        else *static_cast<float*>(ptr) = v;
        return true;
    }
    return false;
}

bool TuneParams::get(const std::string& key, std::string* value) const {
    TuneParams& self = const_cast<TuneParams&>(*this);
    for (int i = 0; i < int(sizeof(kFields) / sizeof(kFields[0])); i++) {
        const Field& f = kFields[i];
        if (key != f.key) continue;
        char buf[32];
        void* ptr = fieldPtr(self, i);
        if (f.kind == Kind::Bool) snprintf(buf, sizeof(buf), "%d", *static_cast<bool*>(ptr) ? 1 : 0);
        else if (f.kind == Kind::Int) snprintf(buf, sizeof(buf), "%d", *static_cast<int*>(ptr));
        else snprintf(buf, sizeof(buf), "%g", double(*static_cast<float*>(ptr)));
        *value = buf;
        return true;
    }
    return false;
}

std::string TuneParams::toString() const {
    std::string out, v;
    for (const Field& f : kFields) {
        get(f.key, &v);
        if (!out.empty()) out += ' ';
        out += std::string(f.key) + "=" + v;
    }
    return out;
}

void TuneParams::fromString(const std::string& s) {
    std::istringstream in(s);
    std::string tok;
    while (in >> tok) {
        size_t eq = tok.find('=');
        if (eq != std::string::npos) set(tok.substr(0, eq), tok.substr(eq + 1));
    }
}

const char* TuneParams::help() {
    static std::string h;
    if (h.empty()) {
        char line[128];
        for (const Field& f : kFields) {
            snprintf(line, sizeof(line), "  %-13s %g..%g  %s\n", f.key, double(f.lo), double(f.hi), f.desc);
            h += line;
        }
    }
    return h.c_str();
}

std::string TuneParams::toJson() const {
    static const char* kinds[] = {"bool", "int", "float"};
    std::string out = "[";
    char buf[320];
    for (int i = 0; i < int(sizeof(kFields) / sizeof(kFields[0])); i++) {
        const Field& f = kFields[i];
        std::string v;
        get(f.key, &v);
        snprintf(buf, sizeof(buf), "%s{\"key\":\"%s\",\"kind\":\"%s\",\"min\":%g,\"max\":%g,\"desc\":\"%s\",\"value\":%s}", i ? "," : "",
                 f.key, kinds[int(f.kind)], double(f.lo), double(f.hi), f.desc, v.c_str());
        out += buf;
    }
    return out + "]";
}

}  // namespace ic
