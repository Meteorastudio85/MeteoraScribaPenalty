#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace scriba
{

//==============================================================================
class Biquad
{
public:
    void set (double nb0, double nb1, double nb2, double na1, double na2) noexcept
    {
        b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
};

//==============================================================================
// Misuratore di loudness secondo ITU-R BS.1770-4 / EBU R128:
//  - K-weighting (shelf + passa-alto) calcolato per qualsiasi sample rate
//  - blocchi da 400 ms con sovrapposizione 75% (passo 100 ms)
//  - integrato con gate assoluto -70 LUFS e gate relativo -10 LU
//  - short-term 3 s, momentary 400 ms
//  - true peak con oversampling 4x
//==============================================================================
class LoudnessMeter
{
public:
    static constexpr float silenceDb   = -100.0f;
    static constexpr int   maxChannels = 8;

    void prepare (double newSampleRate, int numChannels, int maxBlockSize)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        channels   = juce::jlimit (1, maxChannels, numChannels);
        maxBlock   = juce::jmax (1, maxBlockSize);
        hopSize    = juce::jmax (1, (int) std::round (sampleRate * 0.1));

        designKWeighting();

        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channels, 2,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true, false);
        oversampler->initProcessing ((size_t) maxBlock);

        reset();
    }

    void reset() noexcept
    {
        for (auto& f : stage1) f.reset();
        for (auto& f : stage2) f.reset();

        subAccumulator = 0.0;
        samplesInSub   = 0;
        ring.fill (0.0);
        ringPos   = 0;
        subFilled = 0;

        histCount.fill (0);
        histPower.fill (0.0);

        peakLinear = 0.0f;
        if (oversampler != nullptr)
            oversampler->reset();

        integrated.store (silenceDb);
        shortTerm .store (silenceDb);
        momentary .store (silenceDb);
        truePeakDb.store (silenceDb);
        subBlocksMeasured.store (0);
    }

    // Da chiamare dal thread audio
    void process (const juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numCh      = juce::jmin (channels, buffer.getNumChannels());

        if (numSamples <= 0 || numCh <= 0 || oversampler == nullptr)
            return;

        const float* const* in = buffer.getArrayOfReadPointers();

        // --- Loudness ---------------------------------------------------------
        for (int i = 0; i < numSamples; ++i)
        {
            double sum = 0.0;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const double y = stage2[(size_t) ch].process (stage1[(size_t) ch].process ((double) in[ch][i]));
                sum += y * y;
            }

            subAccumulator += sum;

            if (++samplesInSub >= hopSize)
            {
                pushSubBlock (subAccumulator);
                subAccumulator = 0.0;
                samplesInSub   = 0;
            }
        }

        // --- True peak (4x) ---------------------------------------------------
        float peak = peakLinear;

        for (int start = 0; start < numSamples; start += maxBlock)
        {
            const int len = juce::jmin (maxBlock, numSamples - start);

            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax (peak, buffer.getMagnitude (ch, start, len));

            juce::dsp::AudioBlock<const float> block (in, (size_t) numCh, (size_t) start, (size_t) len);
            auto up = oversampler->processSamplesUp (block);

            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                const float* d = up.getChannelPointer (ch);
                for (size_t n = 0; n < up.getNumSamples(); ++n)
                    peak = juce::jmax (peak, std::abs (d[n]));
            }
        }

        peakLinear = peak;
        truePeakDb.store (peak > 0.0f ? juce::Decibels::gainToDecibels (peak, silenceDb) : silenceDb);
    }

    float getIntegrated() const noexcept      { return integrated.load(); }
    float getShortTerm() const noexcept       { return shortTerm.load(); }
    float getMomentary() const noexcept       { return momentary.load(); }
    float getTruePeakDb() const noexcept      { return truePeakDb.load(); }
    float getMeasuredSeconds() const noexcept { return (float) subBlocksMeasured.load() * 0.1f; }

private:
    static constexpr int ringSize = 30;   // 30 x 100 ms = 3 s (short-term)
    static constexpr int numBins  = 800;  // istogramma da -70 a +10 LUFS, passo 0.1 dB

    static float toLufs (double meanPower) noexcept
    {
        return meanPower > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (meanPower)) : silenceDb;
    }

    void designKWeighting()
    {
        const double pi = juce::MathConstants<double>::pi;

        // Stage 1: high shelf (effetto della testa)
        {
            const double f0 = 1681.974450955533;
            const double G  = 3.999843853973347;
            const double Q  = 0.7071752369554196;
            const double K  = std::tan (pi * f0 / sampleRate);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;

            const double b0 = (Vh + Vb * K / Q + K * K) / a0;
            const double b1 = 2.0 * (K * K - Vh) / a0;
            const double b2 = (Vh - Vb * K / Q + K * K) / a0;
            const double a1 = 2.0 * (K * K - 1.0) / a0;
            const double a2 = (1.0 - K / Q + K * K) / a0;

            for (auto& f : stage1)
                f.set (b0, b1, b2, a1, a2);
        }

        // Stage 2: passa-alto (RLB)
        {
            const double f0 = 38.13547087602444;
            const double Q  = 0.5003270373238773;
            const double K  = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;

            const double a1 = 2.0 * (K * K - 1.0) / a0;
            const double a2 = (1.0 - K / Q + K * K) / a0;

            for (auto& f : stage2)
                f.set (1.0, -2.0, 1.0, a1, a2);
        }
    }

    double sumLast (int n) const noexcept
    {
        double s = 0.0;
        for (int j = 0; j < n; ++j)
            s += ring[(size_t) ((ringPos - 1 - j + ringSize) % ringSize)];
        return s;
    }

    void pushSubBlock (double power) noexcept
    {
        ring[(size_t) ringPos] = power;
        ringPos   = (ringPos + 1) % ringSize;
        subFilled = juce::jmin (subFilled + 1, ringSize);
        subBlocksMeasured.store (subBlocksMeasured.load() + 1);

        if (subFilled < 4)
            return;

        // Momentary: ultimo blocco da 400 ms
        const double pm = sumLast (4) / (4.0 * (double) hopSize);
        const float  lm = toLufs (pm);
        momentary.store (lm);

        // Short-term: ultimi 3 s (o quanto disponibile)
        const int nST = subFilled;
        shortTerm.store (toLufs (sumLast (nST) / ((double) nST * (double) hopSize)));

        // Integrato: blocchi da 400 ms sopra il gate assoluto
        if (lm > -70.0f)
        {
            const int idx = juce::jlimit (0, numBins - 1, (int) ((lm + 70.0f) * 10.0f));
            ++histCount[(size_t) idx];
            histPower[(size_t) idx] += pm;
            updateIntegrated();
        }
    }

    void updateIntegrated() noexcept
    {
        double   sumP  = 0.0;
        uint64_t count = 0;

        for (int b = 0; b < numBins; ++b)
        {
            sumP  += histPower[(size_t) b];
            count += histCount[(size_t) b];
        }

        if (count == 0)
            return;

        const double relGate = -0.691 + 10.0 * std::log10 (sumP / (double) count) - 10.0;
        const int    start   = juce::jlimit (0, numBins, (int) std::ceil ((relGate + 70.0) * 10.0));

        double   gatedP     = 0.0;
        uint64_t gatedCount = 0;

        for (int b = start; b < numBins; ++b)
        {
            gatedP     += histPower[(size_t) b];
            gatedCount += histCount[(size_t) b];
        }

        if (gatedCount > 0)
            integrated.store (toLufs (gatedP / (double) gatedCount));
    }

    double sampleRate = 44100.0;
    int    channels   = 2;
    int    maxBlock   = 512;
    int    hopSize    = 4410;

    std::array<Biquad, maxChannels> stage1, stage2;

    double subAccumulator = 0.0;
    int    samplesInSub   = 0;

    std::array<double, ringSize> ring {};
    int ringPos   = 0;
    int subFilled = 0;

    std::array<uint64_t, numBins> histCount {};
    std::array<double,   numBins> histPower {};

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    float peakLinear = 0.0f;

    std::atomic<float> integrated { silenceDb };
    std::atomic<float> shortTerm  { silenceDb };
    std::atomic<float> momentary  { silenceDb };
    std::atomic<float> truePeakDb { silenceDb };
    std::atomic<int>   subBlocksMeasured { 0 };
};

} // namespace scriba
