// Built-in sounds, made in code (no files needed): the call chime and the timed-event chimes.
#pragma once
#include <cstdint>
#include <string>

namespace ic {

// Lockdown, Evacuate, Shelter: the emergency signals (R3.1), 3 s each, repeated by the core. AllClear ends one.
enum class Sound : uint8_t { Chime = 0, Bell = 1, Lunch = 2, Tone = 3, Lockdown = 4, Evacuate = 5, Shelter = 6, AllClear = 7, Count = 8 };

const char* soundName(Sound s);
bool soundFromName(const std::string& name, Sound* s);
std::string soundList();  // "chime bell lunch tone"
uint32_t soundSamples(Sound s);
// Mixes n samples of sound s, starting at sample pos, into out (with clipping).
// Returns false when the sound has ended (pos past its length).
bool renderSound(Sound s, uint32_t pos, int16_t* out, int n);

}  // namespace ic
