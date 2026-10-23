#include "TapeEngine.h"
#include <algorithm>
#include <numbers>
namespace jerzy
{
namespace
{
constexpr double pi = std::numbers::pi;
double db(double x)
{
    return std::pow(10., x / 20.);
}
double clip(double x, double l, double h)
{
    return std::clamp(x, l, h);
}
} // namespace
void TapeEngine::prepare(double sampleRate)
{
    sr = clip(sampleRate, 8000, 192000);
    for (auto &t : tape)
        t.assign(static_cast<std::size_t>(std::ceil(sr * 2)) + 8, 0.f);
    smooth = target;
    reset();
}
void TapeEngine::reset() noexcept
{
    for (auto &t : tape)
        std::fill(t.begin(), t.end(), 0.f);
    channels = {};
    loop = {};
    previousRecorded = {};
    position = 0;
    velocity = .25 / longTime();
    motorAcceleration = 0;
    wowPhase = flutterPhase = humPhase = drift = driftTarget = envelope = elapsedAge = 0;
    driftCounter = 0;
    rng = static_cast<std::uint32_t>(target[seed]);
    outputPeak = loopPeak = 0;
    smooth = target;
    active = target[enabled];
    dryGain = target[wetOnly] ? 0 : 1;
    shortGain = target[shortOn];
    longGain = target[longOn];
    freezeGain = target[freeze];
}
void TapeEngine::set(const Values &v, double tempo) noexcept
{
    for (std::size_t i = 0; i < count; ++i)
        target[i] = std::clamp(safe(v[i]), descriptors[i].min, descriptors[i].max);
    bpm = std::isfinite(tempo) ? clip(tempo, 20, 400) : 120;
}
double TapeEngine::longTime() const noexcept
{
    if (target[mode] < .5)
        return clip(double(target[timeMs]) * .001, .215, 1.0);
    constexpr double beats[] = {.25, 1. / 3., .5, 1, 1.5, 2, 3, 4};
    return target[sync] > .5 ? clip(60. / bpm * beats[int(target[division])], .04, 4.)
                             : double(target[timeMs]) * .001;
}
double TapeEngine::random() noexcept
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return double(rng) / 4294967295. * 2. - 1.;
}
double TapeEngine::read(int c, double offset) const noexcept
{
    const auto &t = tape[std::size_t(c)];
    const int n = int(t.size());
    double p = position - offset;
    p -= std::floor(p / n) * n;
    const int i = int(p);
    const double f = p - i;
    auto at = [&](int x)
    {
        if (x < 0)
            x += n;
        if (x >= n)
            x -= n;
        return double(t[std::size_t(x)]);
    };
    const double a = at(i - 1), b = at(i), cc = at(i + 1), d = at(i + 2);
    return b + .5 * f * (cc - a + f * (2 * a - 5 * b + 4 * cc - d + f * (3 * (b - cc) + d - a)));
}
double TapeEngine::curve(double x) noexcept
{
    x = clip(x, -1.5, 1.5);
    return x - x * x * x * (4. / 27.);
}
double TapeEngine::primitive(double x) noexcept
{
    const double a = std::abs(x);
    return a > 1.5 ? a - .5625 : .5 * x * x - x * x * x * x / 27.;
}
double TapeEngine::adaa(double x, double p) noexcept
{
    return std::abs(x - p) > 1e-6 ? (primitive(x) - primitive(p)) / (x - p) : curve((x + p) * .5);
}
double TapeEngine::record(int c, double input, double wear) noexcept
{
    auto &q = channels[std::size_t(c)];
    const double cutoff = clip(15500 - 9500 * wear - 2500 * std::abs(smooth[bias] * .01 - .5), 2200, 18000);
    const double spatialCutoff = std::min(cutoff, sr * std::max(.03, velocity) * .42);
    const double a = 1 - std::exp(-2 * pi * spatialCutoff / sr);
    q.pre += a * (input - q.pre);
    const double drive = db(smooth[saturation]) * (1. + .45 * wear);
    const double x = q.pre * drive;
    // Two substeps, first-order antiderivative anti-aliasing and magnetic memory.
    double y = 0;
    for (int k = 1; k <= 2; ++k)
    {
        const double u = q.previous + (x - q.previous) * (k * .5);
        q.mag += .08 * (curve(u + q.mag * (.12 + .18 * wear)) - q.mag);
        const double biased = u + q.mag * .22 + (smooth[bias] * .01 - .5) * .14;
        y += adaa(biased, q.post);
        q.post = biased;
    }
    q.previous = x;
    // Normalize the maximum small-signal loop gain, including magnetic memory.
    const double magneticGain = 1. + .22 / (1. - (.12 + .18 * wear));
    y *= .5 / (drive * magneticGain);
    const double hp = y - q.dcIn + std::exp(-2 * pi * 28 / sr) * q.dcOut;
    q.dcIn = y;
    q.dcOut = hp;
    return clip(hp, -2., 2.);
}
void TapeEngine::process(float *l, float *r, int samples) noexcept
{
    if (tape[0].empty())
        return;
    const double smoothing = 1 - std::exp(-1. / (.02 * sr));
    const bool original = target[mode] < .5, experimental = target[mode] > 1.5;
    const double targetSpeed = (experimental && target[stop] > .5) ? 0. : .25 / longTime();
    outputPeak = loopPeak = 0;
    for (int i = 0; i < samples; ++i)
    {
        for (std::size_t p = 0; p < count; ++p)
            if (!descriptors[p].discrete || p == cassette)
                smooth[p] += float(smoothing * (target[p] - smooth[p]));
        const double tapeType = smooth[cassette];
        const double wear = clip(smooth[age] * .01 + elapsedAge + .18 * tapeType, 0, 1);
        const double health = smooth[motor] * .01;
        // Damped second-order motor; all heads share exactly the same tape coordinate.
        const double acceleration = (targetSpeed - velocity) * 9. - motorAcceleration * 6.;
        motorAcceleration += acceleration / sr;
        velocity = clip(velocity + motorAcceleration / sr, 0, 7);
        if (--driftCounter <= 0)
        {
            driftTarget = random();
            driftCounter = int(sr * .12);
        }
        drift += (driftTarget - drift) * (.8 / sr);
        wowPhase += 2 * pi * .63 / sr;
        flutterPhase += 2 * pi * (9.1 + .5 * drift) / sr;
        humPhase += 2 * pi * 50 / sr;
        if (wowPhase > 2 * pi)
            wowPhase -= 2 * pi;
        if (flutterPhase > 2 * pi)
            flutterPhase -= 2 * pi;
        if (humPhase > 2 * pi)
            humPhase -= 2 * pi;
        const double strength = experimental ? 6. : 1.;
        const double modulation = 1. +
                                  strength * (.008 * smooth[wow] * .01 * std::sin(wowPhase) +
                                              .002 * smooth[flutter] * .01 * std::sin(flutterPhase)) +
                                  (1 - health) * .01 * drift + wear * .001 * drift;
        const double step = clip(velocity * modulation, 0, 7.);
        const double circumference = double(tape[0].size());
        auto contactLoss = [&](double offset)
        {
            double p = position - offset;
            p -= std::floor(p / circumference) * circumference;
            const double phase = p / circumference;
            const double defectPhase = std::fmod(phase * 7. + double(target[seed]) * .00001, 1.);
            const double defect = std::pow(std::max(0., std::cos(2 * pi * defectPhase)), 32.);
            const double spliceShape = std::exp(-std::pow(std::min(phase, 1. - phase) / .002, 2.));
            return clip(1. - (smooth[dropout] * .01 + .22 * tapeType) * defect -
                            .65 * smooth[splice] * .01 * spliceShape,
                        .04, 1.);
        };
        const double inputL = safe(l[i]), inputR = r ? safe(r[i]) : inputL;
        const double detector = std::max(std::abs(inputL), std::abs(inputR));
        const double envTime = (detector > envelope ? smooth[duckAttack] : smooth[duckRelease]) * .001;
        envelope += (detector - envelope) * (1 - std::exp(-1. / (sr * envTime)));
        active += smoothing * (target[enabled] - active);
        const bool wet = !original && target[wetOnly] > .5;
        dryGain += smoothing * ((wet && target[enabled] > .5 ? 0. : 1.) - dryGain);
        shortGain += smoothing * (target[shortOn] - shortGain);
        longGain += smoothing * (target[longOn] - longGain);
        const bool locked = experimental && (target[freeze] > .5 || target[sos] > .5);
        freezeGain += smoothing * ((locked ? 1. : 0.) - freezeGain);
        const double ratio = original ? .5 : smooth[shortRatio] * .01;
        const double ampShort = shortGain * (original ? .75 : smooth[shortLevel] * .01),
                     ampLong = longGain * (original ? .75 : smooth[longLevel] * .01);
        std::array<std::array<double, 2>, 2> heads{};
        const double readCoefficient =
            1 - std::exp(-2 * pi *
                         clip(14000 - 8500 * wear - 3500 * (1 - smooth[head] * .01), 1800, sr * .45) / sr);
        const std::array<double, 2> headLoss{contactLoss(sr * .25 * ratio), contactLoss(sr * .25)};
        for (int c = 0; c < 2; ++c)
            for (int h = 0; h < 2; ++h)
            {
                const double v = read(c, sr * .25 * (h == 0 ? ratio : 1.));
                auto &lp = channels[std::size_t(c)].readLP[std::size_t(h)];
                lp += readCoefficient * (v - lp);
                heads[std::size_t(c)][std::size_t(h)] = lp * headLoss[std::size_t(h)];
            }
        double feedbackGain = .97 * std::pow(smooth[feedback] * .01, .62);
        if (experimental)
            feedbackGain += .16 * std::pow(smooth[feedback] * .01, 10.) + (target[dub] > .5 ? .18 : 0.);
        const double norm = std::max(1., ampShort + ampLong);
        const double mute = (target[enabled] > .5 || target[tails] > .5) ? 1. : active;
        const double noise = smooth[hiss] > -99 ? db(smooth[hiss]) * random() : 0.;
        const double humNoise = smooth[hum] > -99 ? db(smooth[hum]) * std::sin(humPhase) : 0.;
        std::array<double, 2> recorded{};
        for (int c = 0; c < 2; ++c)
        {
            const double in = original ? .5 * (inputL + inputR) : (c == 0 ? inputL : inputR);
            loop[std::size_t(c)] =
                (heads[std::size_t(c)][0] * ampShort + heads[std::size_t(c)][1] * ampLong) / norm;
            loopPeak = std::max(loopPeak, float(std::abs(loop[std::size_t(c)] * feedbackGain)));
            const double overdub = (experimental && target[freeze] > .5) ? 0. : in * active;
            recorded[std::size_t(c)] = record(
                c, clip(overdub, -16., 16.) + loop[std::size_t(c)] * feedbackGain * mute + noise + humNoise,
                wear);
        }
        // Spatial tape: write each crossed tape cell, preserving speed/pitch coupling.
        const double next = position + step;
        const auto first = static_cast<long long>(std::floor(position)) + 1;
        const auto last = static_cast<long long>(std::floor(next));
        for (auto cell = first; cell <= last; ++cell)
        {
            const auto index = std::size_t(cell % static_cast<long long>(tape[0].size()));
            for (int c = 0; c < 2; ++c)
            {
                auto &dest = tape[std::size_t(c)][index];
                const double overdub = experimental && target[sos] > .5 && target[freeze] < .5
                                           ? (c == 0 ? inputL : inputR) * active * .25
                                           : 0.;
                const double held = target[freeze] > .5 ? double(dest) : curve(double(dest) + overdub);
                const double fraction = step > 0 ? (double(cell) - position) / step : 0.;
                const double written =
                    previousRecorded[std::size_t(c)] +
                    fraction * (recorded[std::size_t(c)] - previousRecorded[std::size_t(c)]);
                dest = float(written * (1. - freezeGain) + held * freezeGain);
            }
        }
        previousRecorded = recorded;
        position = next;
        if (position >= circumference)
            position -= circumference;
        double wl = 0, wr = 0;
        if (original)
        {
            wl = wr = .5 * ((heads[0][0] + heads[1][0]) * ampShort + (heads[0][1] + heads[1][1]) * ampLong);
        }
        else
            for (int h = 0; h < 2; ++h)
            {
                const double pan = smooth[h == 0 ? shortPan : longPan] * .01;
                const double a = h == 0 ? ampShort : ampLong;
                wl += heads[0][std::size_t(h)] * a * std::sqrt(1. - std::max(0., pan));
                wr += heads[1][std::size_t(h)] * a * std::sqrt(1. + std::min(0., pan));
            }
        if (!original)
        {
            const double mid = .5 * (wl + wr), side = .5 * (wl - wr) * smooth[width] * .01;
            wl = mid + side;
            wr = mid - side;
        }
        const double duck = original
                                ? 1.
                                : 1. / (1. + smooth[duckAmount] * .09 *
                                                 std::max(0., envelope / db(smooth[duckThreshold]) - 1.));
        const double motion = clip(velocity / .02, 0, 1);
        const double gain = smooth[level] * .01 * duck * motion * (target[tails] > .5 ? 1. : active);
        const double outL = inputL * dryGain + wl * gain, outR = inputR * dryGain + wr * gain;
        l[i] = float(std::isfinite(outL) ? (r ? outL : .5 * (outL + outR)) : 0.);
        if (r)
            r[i] = float(std::isfinite(outR) ? outR : 0.);
        outputPeak = std::max(outputPeak, float(std::max(std::abs(outL), std::abs(outR))));
        if (target[autoAge] > .5)
            elapsedAge = std::min(.5, elapsedAge + 1. / (sr * 36000.));
    }
}
} // namespace jerzy
