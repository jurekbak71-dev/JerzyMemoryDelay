#pragma once
#include "Parameters.h"
#include <cstdint>
#include <vector>
namespace jerzy
{
class TapeEngine
{
  public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void set(const Values &values, double bpm = 120.0) noexcept;
    void process(float *left, float *right, int samples) noexcept;
    float speed() const noexcept
    {
        return float(velocity);
    }
    float peak() const noexcept
    {
        return outputPeak;
    }
    float feedbackPeak() const noexcept
    {
        return loopPeak;
    }
    double longTime() const noexcept;
    std::size_t memoryBytes() const noexcept
    {
        return (tape[0].capacity() + tape[1].capacity()) * sizeof(float);
    }

  private:
    struct Channel
    {
        double pre = 0, post = 0, dcIn = 0, dcOut = 0, previous = 0, mag = 0;
        std::array<double, 2> readLP{};
    };
    std::array<std::vector<float>, 2> tape;
    std::array<Channel, 2> channels{};
    Values target = defaults(), smooth = defaults();
    double sr = 48000, bpm = 120, position = 0, velocity = 1, motorAcceleration = 0, wowPhase = 0,
           flutterPhase = 0, humPhase = 0;
    double drift = 0, driftTarget = 0, envelope = 0, elapsedAge = 0, active = 1, dryGain = 1, shortGain = 1,
           longGain = 1, freezeGain = 0;
    std::array<double, 2> loop{}, previousRecorded{};
    std::uint32_t rng = 1971;
    int driftCounter = 0;
    float outputPeak = 0, loopPeak = 0;
    double read(int channel, double offset) const noexcept;
    double record(int channel, double input, double wear) noexcept;
    double random() noexcept;
    static double curve(double x) noexcept;
    static double primitive(double x) noexcept;
    static double adaa(double x, double previous) noexcept;
};
} // namespace jerzy
