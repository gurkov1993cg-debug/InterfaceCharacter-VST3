#include "core/MeasurementEngine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numeric>

namespace rt60 {
namespace {

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr std::array<float, kBandCount> kCenters{{
    25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200,
    250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
    2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000
}};

std::size_t nextPow2(std::size_t value)
{
    std::size_t n = 1;
    while (n < value)
        n <<= 1u;
    return n;
}

void fft(std::vector<std::complex<double>>& a, bool inverse)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1u;
        for (; j & bit; bit >>= 1u)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }

    for (std::size_t len = 2; len <= n; len <<= 1u) {
        const double angle = 2.0 * kPi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        const double invN = 1.0 / static_cast<double>(n);
        for (auto& v : a)
            v *= invN;
    }
}

struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    float process(float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
};

Biquad makeLowPass(double sampleRate, double frequency, double q)
{
    const double omega = 2.0 * kPi * frequency / sampleRate;
    const double alpha = std::sin(omega) / (2.0 * q);
    const double c = std::cos(omega);
    const double a0 = 1.0 + alpha;
    Biquad f;
    f.b0 = ((1.0 - c) * 0.5) / a0;
    f.b1 = (1.0 - c) / a0;
    f.b2 = f.b0;
    f.a1 = (-2.0 * c) / a0;
    f.a2 = (1.0 - alpha) / a0;
    return f;
}

Biquad makeHighPass(double sampleRate, double frequency, double q)
{
    const double omega = 2.0 * kPi * frequency / sampleRate;
    const double alpha = std::sin(omega) / (2.0 * q);
    const double c = std::cos(omega);
    const double a0 = 1.0 + alpha;
    Biquad f;
    f.b0 = ((1.0 + c) * 0.5) / a0;
    f.b1 = -(1.0 + c) / a0;
    f.b2 = f.b0;
    f.a1 = (-2.0 * c) / a0;
    f.a2 = (1.0 - alpha) / a0;
    return f;
}

std::vector<float> filterThirdOctave(const std::vector<float>& input, double sampleRate, float center)
{
    const double edge = std::pow(2.0, 1.0 / 6.0);
    const double low = static_cast<double>(center) / edge;
    const double high = static_cast<double>(center) * edge;
    if (low < 5.0 || high > sampleRate * 0.47)
        return {};

    // Cascaded 2nd-order HP + LP. Not a standards-certified IEC filter,
    // but it gives a stable 1/3-octave analysis band for room-decay estimation.
    auto hp1 = makeHighPass(sampleRate, low, 0.7071067811865476);
    auto hp2 = makeHighPass(sampleRate, low, 0.7071067811865476);
    auto lp1 = makeLowPass(sampleRate, high, 0.7071067811865476);
    auto lp2 = makeLowPass(sampleRate, high, 0.7071067811865476);

    std::vector<float> out(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        float v = input[i];
        v = hp1.process(v);
        v = hp2.process(v);
        v = lp1.process(v);
        v = lp2.process(v);
        out[i] = v;
    }
    return out;
}

struct FitResult {
    float rt60Ms = 0.0f;
    float r2 = 0.0f;
    bool valid = false;
};

FitResult fitDecay(const std::vector<float>& decayDb, double sampleRate,
                   float upperDb, float lowerDb)
{
    std::size_t begin = decayDb.size();
    std::size_t end = decayDb.size();
    for (std::size_t i = 0; i < decayDb.size(); ++i) {
        if (begin == decayDb.size() && decayDb[i] <= upperDb)
            begin = i;
        if (begin != decayDb.size() && decayDb[i] <= lowerDb) {
            end = i;
            break;
        }
    }
    if (begin == decayDb.size() || end == decayDb.size() || end <= begin + 16)
        return {};

    const std::size_t count = end - begin + 1;
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (std::size_t i = begin; i <= end; ++i) {
        const double x = static_cast<double>(i) / sampleRate;
        const double y = decayDb[i];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    const double n = static_cast<double>(count);
    const double denom = n * sxx - sx * sx;
    if (std::abs(denom) < 1.0e-20)
        return {};
    const double slope = (n * sxy - sx * sy) / denom;
    const double intercept = (sy - slope * sx) / n;
    if (slope >= -1.0)
        return {};

    double ssRes = 0.0, ssTot = 0.0;
    const double mean = sy / n;
    for (std::size_t i = begin; i <= end; ++i) {
        const double x = static_cast<double>(i) / sampleRate;
        const double y = decayDb[i];
        const double estimate = intercept + slope * x;
        ssRes += (y - estimate) * (y - estimate);
        ssTot += (y - mean) * (y - mean);
    }

    FitResult result;
    result.rt60Ms = static_cast<float>((-60.0 / slope) * 1000.0);
    result.r2 = ssTot > 1.0e-12 ? static_cast<float>(1.0 - ssRes / ssTot) : 0.0f;
    result.valid = std::isfinite(result.rt60Ms) && result.rt60Ms > 20.0f && result.rt60Ms < 10000.0f;
    return result;
}

BandDecayMetrics analyseBand(const std::vector<float>& ir, double sampleRate, float center)
{
    BandDecayMetrics m;
    m.frequencyHz = center;
    const auto filtered = filterThirdOctave(ir, sampleRate, center);
    if (filtered.empty())
        return m;

    // Estimate the stationary late noise floor from the final 12.5% of the
    // filtered IR. Subtract it from the reverse integration so broadband
    // microphone/self-noise does not masquerade as a very long room tail.
    const std::size_t noiseBegin = filtered.size() * 7 / 8;
    double noisePower = 0.0;
    std::size_t noiseCount = 0;
    for (std::size_t i = noiseBegin; i < filtered.size(); ++i) {
        const double v = filtered[i];
        noisePower += v * v;
        ++noiseCount;
    }
    noisePower = noiseCount > 0 ? noisePower / static_cast<double>(noiseCount) : 0.0;

    double peakPower = 0.0;
    for (float v : filtered)
        peakPower = std::max(peakPower, static_cast<double>(v) * v);
    m.noiseFloorDb = static_cast<float>(10.0 * std::log10(std::max(noisePower, 1.0e-20)));
    m.dynamicRangeDb = static_cast<float>(10.0 * std::log10(std::max(peakPower, 1.0e-20) / std::max(noisePower, 1.0e-20)));

    std::vector<double> integrated(filtered.size());
    double sum = 0.0;
    for (std::size_t n = filtered.size(); n-- > 0;) {
        const double v = filtered[n];
        const double cleaned = std::max(0.0, v * v - noisePower);
        sum += cleaned;
        integrated[n] = sum;
    }
    if (integrated.empty() || integrated[0] <= 1.0e-20)
        return m;

    const double reference = integrated[0];
    std::vector<float> decayDb(integrated.size());
    for (std::size_t i = 0; i < integrated.size(); ++i) {
        const double ratio = std::max(integrated[i] / reference, 1.0e-14);
        decayDb[i] = static_cast<float>(10.0 * std::log10(ratio));
    }

    const auto edt = fitDecay(decayDb, sampleRate, 0.0f, -10.0f);
    const auto t20 = fitDecay(decayDb, sampleRate, -5.0f, -25.0f);
    const auto t30 = fitDecay(decayDb, sampleRate, -5.0f, -35.0f);
    m.edtMs = edt.rt60Ms;
    m.t20Ms = t20.rt60Ms;
    m.t30Ms = t30.rt60Ms;

    if (t30.valid && t30.r2 >= 0.90f) {
        m.rt60Ms = t30.rt60Ms;
        m.fitQuality = t30.r2;
        m.valid = true;
    } else if (t20.valid && t20.r2 >= 0.88f) {
        m.rt60Ms = t20.rt60Ms;
        m.fitQuality = t20.r2;
        m.valid = true;
    } else if (edt.valid && edt.r2 >= 0.85f) {
        m.rt60Ms = edt.rt60Ms;
        m.fitQuality = edt.r2;
        m.valid = true;
    }

    // Confidence deliberately combines linearity of the decay fit with usable
    // dynamic range. A clean but non-linear tail or a perfect line sitting in
    // the noise floor should not drive an aggressive correction.
    const float rangeConfidence = std::clamp((m.dynamicRangeDb - 22.0f) / 28.0f, 0.0f, 1.0f);
    m.confidence = m.valid ? std::clamp(m.fitQuality * rangeConfidence, 0.0f, 1.0f) : 0.0f;
    if (m.confidence < 0.20f)
        m.valid = false;
    return m;
}

std::vector<float> trimAroundPeak(const std::vector<float>& input, double sampleRate)
{
    if (input.empty())
        return {};
    const auto peakIt = std::max_element(input.begin(), input.end(), [](float a, float b) {
        return std::abs(a) < std::abs(b);
    });
    const std::size_t peak = static_cast<std::size_t>(std::distance(input.begin(), peakIt));
    const std::size_t pre = static_cast<std::size_t>(0.010 * sampleRate);
    const std::size_t start = peak > pre ? peak - pre : 0;
    const std::size_t maxLength = static_cast<std::size_t>(4.0 * sampleRate);
    const std::size_t end = std::min(input.size(), start + maxLength);
    return std::vector<float>(input.begin() + static_cast<std::ptrdiff_t>(start),
                              input.begin() + static_cast<std::ptrdiff_t>(end));
}

} // namespace

MeasurementEngine::MeasurementEngine(MeasurementConfig config)
    : config_(config)
{
}

void MeasurementEngine::setConfig(const MeasurementConfig& config) noexcept
{
    config_ = config;
}

std::vector<float> MeasurementEngine::makeSweepOnly() const
{
    const double sr = std::max(8000.0, config_.sampleRate);
    const float start = std::max(10.0f, config_.startHz);
    const float end = std::min(config_.endHz, static_cast<float>(sr * 0.45));
    const double duration = std::max(0.5f, config_.sweepSeconds);
    const std::size_t count = static_cast<std::size_t>(std::llround(duration * sr));
    std::vector<float> sweep(count);

    const double w1 = 2.0 * kPi * start;
    const double ratio = static_cast<double>(end) / start;
    const double logRatio = std::log(ratio);
    const double scale = duration / logRatio;
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double phase = w1 * scale * (std::exp(t / scale) - 1.0);
        // Short raised-cosine fade avoids clicks without altering the main sweep.
        const double fadeSeconds = 0.020;
        double fade = 1.0;
        if (t < fadeSeconds)
            fade *= 0.5 - 0.5 * std::cos(kPi * t / fadeSeconds);
        if (duration - t < fadeSeconds)
            fade *= 0.5 - 0.5 * std::cos(kPi * (duration - t) / fadeSeconds);
        sweep[i] = static_cast<float>(config_.level * fade * std::sin(phase));
    }
    return sweep;
}

std::vector<float> MeasurementEngine::makeExcitation() const
{
    const double sr = std::max(8000.0, config_.sampleRate);
    const std::size_t pre = static_cast<std::size_t>(std::llround(config_.preSilenceSeconds * sr));
    const std::size_t tail = static_cast<std::size_t>(std::llround(config_.tailSeconds * sr));
    const auto sweep = makeSweepOnly();
    std::vector<float> out(pre + sweep.size() + tail, 0.0f);
    std::copy(sweep.begin(), sweep.end(), out.begin() + static_cast<std::ptrdiff_t>(pre));
    return out;
}

std::vector<float> MeasurementEngine::deconvolve(const std::vector<float>& captured,
                                                 const std::vector<float>& excitation) const
{
    if (captured.empty() || excitation.empty())
        return {};
    const std::size_t n = nextPow2(captured.size() + excitation.size());
    std::vector<std::complex<double>> y(n), x(n);
    for (std::size_t i = 0; i < captured.size(); ++i)
        y[i] = captured[i];
    for (std::size_t i = 0; i < excitation.size(); ++i)
        x[i] = excitation[i];
    fft(y, false);
    fft(x, false);

    double maxPower = 0.0;
    for (const auto& v : x)
        maxPower = std::max(maxPower, std::norm(v));
    const double regularization = std::max(1.0e-18, maxPower * 1.0e-10);
    for (std::size_t i = 0; i < n; ++i)
        y[i] = y[i] * std::conj(x[i]) / (std::norm(x[i]) + regularization);
    fft(y, true);

    std::vector<float> ir(n);
    for (std::size_t i = 0; i < n; ++i)
        ir[i] = static_cast<float>(y[i].real());
    return trimAroundPeak(ir, config_.sampleRate);
}

MeasurementResult MeasurementEngine::analyseCapture(const std::vector<float>& captured) const
{
    MeasurementResult result;
    result.sampleRate = config_.sampleRate;
    if (captured.empty())
        return result;

    float peak = 0.0f;
    for (float v : captured)
        peak = std::max(peak, std::abs(v));
    result.peakDbFs = 20.0f * std::log10(std::max(peak, 1.0e-9f));
    result.clipped = peak >= 0.999f;

    result.impulseResponse = deconvolve(captured, makeExcitation());
    auto analysed = analyseImpulseResponse(result.impulseResponse);
    analysed.peakDbFs = result.peakDbFs;
    analysed.clipped = result.clipped;
    return analysed;
}

MeasurementResult MeasurementEngine::analyseImpulseResponse(const std::vector<float>& impulseResponse) const
{
    MeasurementResult result;
    result.sampleRate = config_.sampleRate;
    result.impulseResponse = impulseResponse;
    std::size_t validCount = 0;
    for (std::size_t i = 0; i < kBandCount; ++i) {
        result.bands[i] = analyseBand(impulseResponse, config_.sampleRate, kCenters[i]);
        if (result.bands[i].valid)
            ++validCount;
    }
    result.valid = validCount >= 8;
    return result;
}

BandDecayMetrics MeasurementEngine::analyseBandAtFrequency(const std::vector<float>& impulseResponse, float frequencyHz) const
{
    return analyseBand(impulseResponse, config_.sampleRate, frequencyHz);
}


} // namespace rt60
