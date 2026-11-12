#include "WinAudio.h"

#include <cstring>

#include "Config.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>

namespace {
WAVEFORMATEX format() {
    WAVEFORMATEX f = {};
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 1;
    f.nSamplesPerSec = ic::kSampleRate;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 2;
    f.nAvgBytesPerSec = ic::kSampleRate * 2;
    return f;
}
constexpr DWORD kBytes = ic::kFrameSamples * 2;
}  // namespace

bool WinAudioIn::open() {
    WAVEFORMATEX f = format();
    HWAVEIN h;
    if (waveInOpen(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return false;
    h_ = h;
    for (int i = 0; i < kBufs; i++) {
        data_[i] = new int16_t[ic::kFrameSamples];
        WAVEHDR* w = new WAVEHDR();
        w->lpData = reinterpret_cast<LPSTR>(data_[i]);
        w->dwBufferLength = kBytes;
        waveInPrepareHeader(h, w, sizeof(WAVEHDR));
        waveInAddBuffer(h, w, sizeof(WAVEHDR));
        hdr_[i] = w;
    }
    next_ = 0;
    waveInStart(h);
    return true;
}

void WinAudioIn::close() {
    if (!h_) return;
    HWAVEIN h = static_cast<HWAVEIN>(h_);
    waveInReset(h);
    for (int i = 0; i < kBufs; i++) {
        waveInUnprepareHeader(h, static_cast<WAVEHDR*>(hdr_[i]), sizeof(WAVEHDR));
        delete static_cast<WAVEHDR*>(hdr_[i]);
        delete[] data_[i];
    }
    waveInClose(h);
    h_ = nullptr;
}

bool WinAudioIn::read(int16_t* frame) {
    if (!h_) return false;
    WAVEHDR* w = static_cast<WAVEHDR*>(hdr_[next_]);
    if (!(w->dwFlags & WHDR_DONE)) return false;
    std::memcpy(frame, data_[next_], kBytes);
    w->dwFlags &= ~WHDR_DONE;
    waveInAddBuffer(static_cast<HWAVEIN>(h_), w, sizeof(WAVEHDR));
    next_ = (next_ + 1) % kBufs;
    return true;
}

bool WinAudioOut::open() {
    WAVEFORMATEX f = format();
    HWAVEOUT h;
    if (waveOutOpen(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return false;
    h_ = h;
    for (int i = 0; i < kBufs; i++) {
        data_[i] = new int16_t[ic::kFrameSamples]();
        WAVEHDR* w = new WAVEHDR();
        w->lpData = reinterpret_cast<LPSTR>(data_[i]);
        w->dwBufferLength = kBytes;
        w->dwFlags = WHDR_DONE;  // free
        hdr_[i] = w;
    }
    next_ = 0;
    int16_t silence[ic::kFrameSamples] = {};
    for (int i = 0; i < 3; i++) write(silence);  // 60 ms cushion against timer jitter
    return true;
}

void WinAudioOut::close() {
    if (!h_) return;
    HWAVEOUT h = static_cast<HWAVEOUT>(h_);
    waveOutReset(h);
    for (int i = 0; i < kBufs; i++) {
        waveOutUnprepareHeader(h, static_cast<WAVEHDR*>(hdr_[i]), sizeof(WAVEHDR));
        delete static_cast<WAVEHDR*>(hdr_[i]);
        delete[] data_[i];
    }
    waveOutClose(h);
    h_ = nullptr;
}

void WinAudioOut::write(const int16_t* frame) {
    if (!h_) return;
    HWAVEOUT h = static_cast<HWAVEOUT>(h_);
    WAVEHDR* w = static_cast<WAVEHDR*>(hdr_[next_]);
    if (!(w->dwFlags & WHDR_DONE)) return;  // device backed up: drop
    if (w->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(h, w, sizeof(WAVEHDR));
    std::memcpy(data_[next_], frame, kBytes);
    w->dwFlags = 0;
    waveOutPrepareHeader(h, w, sizeof(WAVEHDR));
    waveOutWrite(h, w, sizeof(WAVEHDR));
    next_ = (next_ + 1) % kBufs;
}

#else  // not Windows: audio devices unavailable, the synthetic talker still works
bool WinAudioIn::open() { return false; }
void WinAudioIn::close() {}
bool WinAudioIn::read(int16_t*) { return false; }
bool WinAudioOut::open() { return false; }
void WinAudioOut::close() {}
void WinAudioOut::write(const int16_t*) {}
#endif
