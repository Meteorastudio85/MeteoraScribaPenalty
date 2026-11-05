#pragma once

#include <juce_core/juce_core.h>

namespace scriba
{

// Soglia di picco usata dalle piattaforme quando alzano il volume (dBTP)
constexpr float peakCeilingDb = -1.0f;

struct Platform
{
    const char*  name;
    float        targetLufs;   // loudness di riferimento (integrato)
    bool         turnsUp;      // alza anche i brani piu' bassi del target?
    bool         limiterOnUp;  // quando alza usa un limiter (Spotify Alto)
    const char*  rule;         // descrizione breve
    juce::uint32 colour;       // colore accento ARGB
};

inline const Platform platforms[] =
{
    { "YouTube",           -14.0f, false, false, "solo abbassamento",                0xffff3b3b },
    { "Spotify (Normale)", -14.0f, true,  false, "alza solo fino a -1 dBTP",         0xff1ed760 },
    { "Spotify (Alto)",    -11.0f, true,  true,  "alza sempre, limiter a -1 dB",     0xff1ed760 },
    { "Spotify (Basso)",   -19.0f, true,  false, "alza solo fino a -1 dBTP",         0xff1ed760 },
    { "Apple Music",       -16.0f, true,  false, "Sound Check, alza fino a -1 dBTP", 0xffff4f81 },
    { "Tidal",             -14.0f, false, false, "solo abbassamento",                0xffe8e8e8 },
    { "Amazon Music",      -14.0f, false, false, "solo abbassamento",                0xff25d1da },
    { "Deezer",            -15.0f, false, false, "solo abbassamento",                0xffa238ff },
};

constexpr int numPlatforms = (int) (sizeof (platforms) / sizeof (platforms[0]));

inline juce::StringArray platformNames()
{
    juce::StringArray names;
    for (const auto& p : platforms)
        names.add (p.name);
    return names;
}

} // namespace scriba
