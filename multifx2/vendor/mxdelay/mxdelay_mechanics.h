#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace JerzyAudio {

struct MechanicalFrame {
    double pitch = 0.0;
    double gain = 1.0;
    double tone = 1.0;
    double noise = 0.0;
    double impulse = 0.0;
};

class AnalogRng {
    uint32_t state_ = 0x9e3779b9u;
public:
    void seed(uint32_t s) { state_ = s ? s : 0x9e3779b9u; }
    uint32_t nextU32() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    double unit() { return (double(nextU32()) + 0.5) / 4294967296.0; }
    double bipolar() { return unit() * 2.0 - 1.0; }
};

class ArtifactScheduler {
    double sr_ = 44100.0;
    AnalogRng rng_;
    int dropoutRemain_ = 0, dropoutTotal_ = 1;
    int slipRemain_ = 0, slipTotal_ = 1;
    int crinkleRemain_ = 0, crinkleTotal_ = 1;
    int clickRemain_ = 0, clickTotal_ = 1;
    double dropoutDepth_ = 0.0, slipDepth_ = 0.0, crinkleDepth_ = 0.0, clickAmp_ = 0.0;

    bool event(double ratePerSecond) {
        if (ratePerSecond <= 0.0) return false;
        const double p = 1.0 - std::exp(-ratePerSecond / sr_);
        return rng_.unit() < p;
    }
    int samples(double loSeconds, double hiSeconds) {
        const double s = loSeconds + (hiSeconds - loSeconds) * rng_.unit();
        return std::max(1, int(std::lround(s * sr_)));
    }
    static double progress(int remain, int total) {
        return 1.0 - double(std::max(0, remain)) / double(std::max(1, total));
    }
public:
    void prepare(double sampleRate, uint32_t seed) {
        sr_ = std::max(8000.0, sampleRate);
        reset(seed);
    }
    void reset(uint32_t seed) {
        rng_.seed(seed ^ 0xa511e9b3u);
        dropoutRemain_ = slipRemain_ = crinkleRemain_ = clickRemain_ = 0;
        dropoutTotal_ = slipTotal_ = crinkleTotal_ = clickTotal_ = 1;
        dropoutDepth_ = slipDepth_ = crinkleDepth_ = clickAmp_ = 0.0;
    }

    MechanicalFrame tick(double wear, double fault, double staticAmount) {
        wear = std::clamp(wear, 0.0, 1.0);
        fault = std::clamp(fault, 0.0, 1.0);
        staticAmount = std::clamp(staticAmount, 0.0, 1.0);

        if (dropoutRemain_ <= 0 && event(0.003 + 1.9 * wear * wear + 0.35 * fault * fault)) {
            dropoutTotal_ = dropoutRemain_ = samples(0.0015, 0.060 + 0.120 * wear);
            dropoutDepth_ = 0.16 + 0.78 * (0.35 * wear + 0.65 * rng_.unit());
        }
        if (slipRemain_ <= 0 && event(0.002 + 0.42 * fault * fault + 0.16 * wear * wear * wear)) {
            slipTotal_ = slipRemain_ = samples(0.012, 0.090 + 0.180 * fault);
            slipDepth_ = (0.0025 + 0.033 * fault + 0.012 * wear) * (0.45 + 0.75 * rng_.unit());
        }
        if (crinkleRemain_ <= 0 && event(0.005 + 3.6 * wear * wear + 1.2 * fault * fault)) {
            crinkleTotal_ = crinkleRemain_ = samples(0.00035, 0.010 + 0.026 * wear);
            crinkleDepth_ = 0.10 + 0.82 * (0.25 * wear + 0.75 * rng_.unit());
        }
        if (clickRemain_ <= 0 && event(0.008 + 5.0 * staticAmount * staticAmount + 0.8 * wear * wear)) {
            clickTotal_ = clickRemain_ = samples(0.00018, 0.0035 + 0.006 * staticAmount);
            clickAmp_ = (0.001 + 0.050 * staticAmount + 0.018 * wear) * (0.25 + 0.75 * rng_.unit());
        }

        MechanicalFrame f;
        if (dropoutRemain_ > 0) {
            const double x = progress(dropoutRemain_, dropoutTotal_);
            const double env = std::sin(3.14159265358979323846 * std::clamp(x, 0.0, 1.0));
            const double d = dropoutDepth_ * env;
            f.gain *= std::max(0.04, 1.0 - d);
            f.tone *= std::max(0.12, 1.0 - 0.82 * d);
            f.pitch -= 0.0035 * d * (0.4 + wear);
            --dropoutRemain_;
        }
        if (slipRemain_ > 0) {
            const double x = progress(slipRemain_, slipTotal_);
            const double env = std::sin(3.14159265358979323846 * std::clamp(x, 0.0, 1.0));
            f.pitch -= slipDepth_ * env;
            f.gain *= 1.0 - 0.16 * env * fault;
            f.tone *= 1.0 - 0.18 * env * wear;
            --slipRemain_;
        }
        if (crinkleRemain_ > 0) {
            const double x = progress(crinkleRemain_, crinkleTotal_);
            const double env = std::exp(-5.5 * x) * (0.65 + 0.35 * std::abs(rng_.bipolar()));
            const double d = crinkleDepth_ * env;
            f.gain *= std::max(0.06, 1.0 - 0.85 * d);
            f.tone *= std::max(0.10, 1.0 - 0.75 * d);
            f.pitch += 0.0045 * d * rng_.bipolar();
            f.impulse += d * 0.012 * rng_.bipolar();
            --crinkleRemain_;
        }
        if (clickRemain_ > 0) {
            const double x = progress(clickRemain_, clickTotal_);
            const double env = std::exp(-8.0 * x);
            f.impulse += clickAmp_ * env * rng_.bipolar();
            --clickRemain_;
        }
        f.noise = rng_.bipolar() * (0.000015 + 0.00020 * wear + 0.00036 * staticAmount);
        f.pitch = std::clamp(f.pitch, -0.085, 0.085);
        return f;
    }
};

class MechanicalTransport {
    static constexpr double pi_ = 3.14159265358979323846;
    double sr_ = 44100.0;
    AnalogRng rng_;
    ArtifactScheduler artifacts_;
    double wowPhase_ = 0.0, eccentricPhase_ = 0.0, flutterPhase_ = 0.0, flutterPhase2_ = 0.0;
    double motor_ = 0.0, motorTarget_ = 0.0, flutterNoise_ = 0.0;
    int targetCountdown_ = 0;
    uint32_t seed_ = 0x12345678u;

    void chooseMotorTarget(double mechanics, double wear) {
        motorTarget_ = rng_.bipolar() * (0.00025 + 0.0035 * mechanics + 0.0045 * wear);
        const double seconds = 0.18 + rng_.unit() * (1.4 + 2.0 * (1.0 - mechanics));
        targetCountdown_ = std::max(1, int(std::lround(seconds * sr_)));
    }
public:
    void prepare(double sampleRate, uint32_t seed) {
        sr_ = std::max(8000.0, sampleRate);
        seed_ = seed ? seed : 0x12345678u;
        artifacts_.prepare(sr_, seed_ ^ 0x6d2b79f5u);
        reset();
    }
    void reset() {
        rng_.seed(seed_);
        artifacts_.reset(seed_ ^ 0x6d2b79f5u);
        wowPhase_ = eccentricPhase_ = flutterPhase_ = flutterPhase2_ = 0.0;
        motor_ = motorTarget_ = flutterNoise_ = 0.0;
        targetCountdown_ = 0;
    }

    MechanicalFrame tick(double wowAmount, double flutterAmount, double wear, double fault, double staticAmount = 0.0) {
        wowAmount = std::clamp(wowAmount, 0.0, 1.0);
        flutterAmount = std::clamp(flutterAmount, 0.0, 1.0);
        wear = std::clamp(wear, 0.0, 1.0);
        fault = std::clamp(fault, 0.0, 1.0);

        if (--targetCountdown_ <= 0) chooseMotorTarget(std::max(wowAmount, fault), wear);
        const double motorTau = 0.22 + 1.8 * (1.0 - fault);
        const double motorA = 1.0 - std::exp(-1.0 / (motorTau * sr_));
        motor_ += (motorTarget_ - motor_) * motorA;

        const double wowRate = 0.18 + 1.15 * wowAmount + 0.12 * wear;
        const double eccRate = 0.31 + 0.42 * wowAmount;
        const double flutterRate = 4.2 + 9.0 * flutterAmount + 2.5 * wear;
        wowPhase_ += 2.0 * pi_ * wowRate / sr_;
        eccentricPhase_ += 2.0 * pi_ * eccRate / sr_;
        flutterPhase_ += 2.0 * pi_ * flutterRate / sr_;
        flutterPhase2_ += 2.0 * pi_ * (flutterRate * 1.73 + 0.91) / sr_;
        if (wowPhase_ > 2.0 * pi_) wowPhase_ -= 2.0 * pi_;
        if (eccentricPhase_ > 2.0 * pi_) eccentricPhase_ -= 2.0 * pi_;
        if (flutterPhase_ > 2.0 * pi_) flutterPhase_ -= 2.0 * pi_;
        if (flutterPhase2_ > 2.0 * pi_) flutterPhase2_ -= 2.0 * pi_;

        const double flutterA = 1.0 - std::exp(-2.0 * pi_ * (7.0 + 20.0 * flutterAmount) / sr_);
        flutterNoise_ += (rng_.bipolar() - flutterNoise_) * flutterA;

        const double wow = (0.00020 + 0.0075 * wowAmount + 0.0028 * wear) *
                           (0.72 * std::sin(wowPhase_) + 0.28 * std::sin(eccentricPhase_));
        const double flutter = (0.00008 + 0.0045 * flutterAmount + 0.0018 * wear) *
                               (0.48 * std::sin(flutterPhase_) + 0.25 * std::sin(flutterPhase2_) + 0.38 * flutterNoise_);

        MechanicalFrame f = artifacts_.tick(wear, fault, staticAmount);
        f.pitch = std::clamp(f.pitch + motor_ + wow + flutter, -0.095, 0.095);
        return f;
    }
};

} // namespace JerzyAudio
