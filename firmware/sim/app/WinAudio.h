// PC microphone and speakers through the Windows waveIn / waveOut API (16 kHz mono,
// 20 ms buffers). No extra Qt modules needed.
#pragma once
#include <cstdint>

class WinAudioIn {
public:
    ~WinAudioIn() { close(); }
    bool open();
    void close();
    bool isOpen() const { return h_ != nullptr; }
    bool read(int16_t* frame);  // one 320-sample frame if captured, else false

private:
    static constexpr int kBufs = 8;
    void* h_ = nullptr;
    void* hdr_[kBufs] = {};
    int16_t* data_[kBufs] = {};
    int next_ = 0;
};

class WinAudioOut {
public:
    ~WinAudioOut() { close(); }
    bool open();
    void close();
    bool isOpen() const { return h_ != nullptr; }
    void write(const int16_t* frame);  // drops the frame if the device is backed up

private:
    static constexpr int kBufs = 10;
    void* h_ = nullptr;
    void* hdr_[kBufs] = {};
    int16_t* data_[kBufs] = {};
    int next_ = 0;
};
