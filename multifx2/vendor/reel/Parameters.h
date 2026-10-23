#pragma once
#include <array>
#include <cmath>
#include <cstddef>
namespace jerzy
{
enum Param : std::size_t
{
    mode,
    cassette,
    enabled,
    level,
    feedback,
    timeMs,
    shortOn,
    longOn,
    wow,
    flutter,
    age,
    motor,
    head,
    bias,
    saturation,
    splice,
    dropout,
    sync,
    division,
    shortPan,
    longPan,
    width,
    duckAmount,
    duckThreshold,
    duckAttack,
    duckRelease,
    wetOnly,
    stop,
    dub,
    sos,
    freeze,
    tails,
    hiss,
    hum,
    shortLevel,
    longLevel,
    shortRatio,
    autoAge,
    seed,
    count
};
struct Descriptor
{
    const char *id;
    const char *name;
    const char *unit;
    float min, max, initial, skew;
    bool discrete;
};
inline constexpr std::array<Descriptor, count> descriptors{
    {{"mode", "Mode", "", 0, 2, 0, 1, true},
     {"cassette", "Cassette", "", 0, 2, 0, 1, true},
     {"enabled", "On", "", 0, 1, 1, 1, true},
     {"level", "Delay level", "%", 0, 100, 35, 1, false},
     {"feedback", "Feedback", "%", 0, 100, 38, 1, false},
     {"time", "Long head time", "ms", 40, 4000, 420, .4f, false},
     {"short", "Short head", "", 0, 1, 1, 1, true},
     {"long", "Long head", "", 0, 1, 1, 1, true},
     {"wow", "Wow", "%", 0, 100, 12, 1, false},
     {"flutter", "Flutter", "%", 0, 100, 8, 1, false},
     {"age", "Tape age", "%", 0, 100, 15, 1, false},
     {"motor", "Motor health", "%", 0, 100, 90, 1, false},
     {"head", "Head condition", "%", 0, 100, 90, 1, false},
     {"bias", "Bias", "%", 0, 100, 50, 1, false},
     {"saturation", "Saturation", "dB", 0, 24, 4, 1, false},
     {"splice", "Splice level", "%", 0, 100, 0, 1, false},
     {"dropout", "Dropout amount", "%", 0, 100, 0, 1, false},
     {"sync", "Tempo sync", "", 0, 1, 0, 1, true},
     {"division", "Note division", "", 0, 7, 3, 1, true},
     {"shortPan", "Short pan", "%", -100, 100, -30, 1, false},
     {"longPan", "Long pan", "%", -100, 100, 30, 1, false},
     {"width", "Stereo width", "%", 0, 150, 100, 1, false},
     {"duckAmount", "Ducking amount", "%", 0, 100, 0, 1, false},
     {"duckThreshold", "Ducking threshold", "dB", -60, 0, -24, 1, false},
     {"duckAttack", "Ducking attack", "ms", 1, 200, 10, .5f, false},
     {"duckRelease", "Ducking release", "ms", 10, 2000, 300, .5f, false},
     {"wetOnly", "Wet only", "", 0, 1, 0, 1, true},
     {"stop", "Tape stop", "", 0, 1, 0, 1, true},
     {"dub", "Dub push", "", 0, 1, 0, 1, true},
     {"sos", "Sound on sound", "", 0, 1, 0, 1, true},
     {"freeze", "Freeze", "", 0, 1, 0, 1, true},
     {"tails", "Bypass tails", "", 0, 1, 1, 1, true},
     {"hiss", "Tape hiss", "dB", -100, -35, -100, 1, false},
     {"hum", "Mains hum", "dB", -100, -35, -100, 1, false},
     {"shortLevel", "Short level", "%", 0, 100, 75, 1, false},
     {"longLevel", "Long level", "%", 0, 100, 75, 1, false},
     {"shortRatio", "Short time ratio", "%", 10, 95, 50, 1, false},
     {"autoAge", "Automatic ageing", "", 0, 1, 0, 1, true},
     {"seed", "Tape seed", "", 1, 65535, 1971, 1, true}}};
using Values = std::array<float, count>;
inline Values defaults()
{
    Values a{};
    for (std::size_t i = 0; i < count; ++i)
        a[i] = descriptors[i].initial;
    return a;
}
inline float safe(float x) noexcept
{
    return std::isfinite(x) ? x : 0.f;
}
} // namespace jerzy
