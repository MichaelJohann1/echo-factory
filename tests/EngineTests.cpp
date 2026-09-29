/*
    Offline checks for DelayEngine: things that are hard to hear reliably or
    dangerous to get wrong by ear (Runaway's bound, Tapestop's exact return).

        cmake --build build --target EngineTests
        ./build/EngineTests_artefacts/Debug/EngineTests

    Exits non-zero if any check fails.
*/

#include <juce_dsp/juce_dsp.h>
#include "DelayEngine.h"
#include <cstdio>
#include <functional>

namespace
{
constexpr float sr = 48000.0f;
constexpr int blockSize = 32;
int failures = 0;

void check (const char* name, bool ok, const char* detail)
{
    std::printf ("%-58s %s  %s\n", name, ok ? "ok  " : "FAIL", detail);
    if (! ok) ++failures;
}

struct Setup
{
    int channels = 2;
    float delayMs = 300.0f, feedback = 0.5f, wear = 0.0f, runawayDrive = 0.5f;
    bool pingPong = false, filters = false;
};

using Events = std::function<void (DelayEngine&, float seconds)>;
using Input  = std::function<float (long sample)>;

/** Runs the engine with mix at 100% and returns the left output. */
std::vector<float> run (const Setup& s, float seconds, const Input& input, const Events& events = {})
{
    DelayEngine e;
    e.prepare (sr, blockSize, s.channels, 5000.0f, 100.0f);
    e.setDelayMs (s.delayMs); e.setDelaySmoothingMs (0); e.setFeedback (s.feedback); e.setMix (1.0f);
    e.setLowCutHz (s.filters ? 200.0f : 0.0f); e.setHighCutHz (s.filters ? 3000.0f : 0.0f);
    e.setFiltersInFeedbackLoop (true); e.setFrozen (false); e.setFreezeFadeMs (20); e.setInputSend (1.0f);
    e.setPingPong (s.pingPong); e.setStereoWidthMs (0); e.setWear (s.wear);
    e.setRunaway (false); e.setRunawayDrive (s.runawayDrive); e.setTapestop (false); e.setTapestopTimeMs (500);
    e.reset();

    std::vector<float> out;
    juce::AudioBuffer<float> buffer (s.channels, blockSize);
    long n = 0;

    for (int b = 0; b < (int) (seconds * sr / blockSize); ++b)
    {
        if (events)
            events (e, (float) n / sr);

        for (int i = 0; i < blockSize; ++i, ++n)
            for (int ch = 0; ch < s.channels; ++ch)
                buffer.setSample (ch, i, input (n));

        e.process (buffer);

        for (int i = 0; i < blockSize; ++i)
            out.push_back (buffer.getSample (0, i));
    }

    return out;
}

Input sine()  { return [] (long n) { return 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) n / sr); }; }
Input dc (float v) { return [v] (long) { return v; }; }

Input noiseBurst()
{
    auto random = std::make_shared<juce::Random> (1);
    return [random] (long n) { return n < (long) (0.2f * sr) ? random->nextFloat() * 2.0f - 1.0f : 0.0f; };
}

Input loudNoise()
{
    auto random = std::make_shared<juce::Random> (2);
    return [random] (long) { return random->nextFloat() * 2.0f - 1.0f; };
}

/** Peak from `from` seconds on; non-finite samples count as huge. */
float peak (const std::vector<float>& v, float from = 0.0f)
{
    float m = 0.0f;
    for (auto i = (size_t) (from * sr); i < v.size(); ++i) m = std::isfinite (v[i]) ? std::max (m, std::abs (v[i])) : 1e9f;
    return m;
}

float rms (const std::vector<float>& v, float from, float to)
{
    double sum = 0.0; int count = 0;
    for (auto i = (size_t) (from * sr); i < (size_t) (to * sr) && i < v.size(); ++i, ++count)
        sum += (double) v[i] * v[i];
    return (float) std::sqrt (sum / std::max (1, count));
}

float maxDiff (const std::vector<float>& a, const std::vector<float>& b, float from)
{
    float m = 0.0f;
    for (auto i = (size_t) (from * sr); i < a.size(); ++i) m = std::max (m, std::abs (a[i] - b[i]));
    return m;
}

float maxStep (const std::vector<float>& v)
{
    float m = 0.0f;
    for (size_t i = 1; i < v.size(); ++i) m = std::isfinite (v[i]) ? std::max (m, std::abs (v[i] - v[i - 1])) : 1e9f;
    return m;
}

void testCleanPath()
{
    // Wear 0 is the clean delay: an impulse comes back at exactly 1.0, then 0.5.
    Setup s; s.channels = 1; s.delayMs = 100.0f;
    const auto out = run (s, 0.5f, [] (long n) { return n == 0 ? 1.0f : 0.0f; });
    const auto first = out[4800], second = out[9600];
    char d[96]; std::snprintf (d, sizeof d, "echoes %.6f %.6f", first, second);
    check ("wear 0: clean impulse echoes", std::abs (first - 1.0f) < 1e-6f && std::abs (second - 0.5f) < 1e-6f, d);
}

void testRunaway()
{
    // Runaway is pressed like a key at 0.5 s, so it ramps in. Peaks are measured
    // from 1 s: after the ramp, and after echoes recorded before the press (which
    // are the clean delay's own peaks) have played out.
    char d[96];
    const auto runaway = [] (DelayEngine& e, float t) { e.setRunaway (t >= 0.5f); };

    for (int channels : { 1, 2 })
        for (bool pingPong : { false, true })
        {
            if (channels == 1 && pingPong) continue;

            for (float drive : { 0.0f, 0.5f, 1.0f })
            {
                Setup s; s.channels = channels; s.pingPong = pingPong; s.feedback = 0.35f; s.runawayDrive = drive;
                const auto out = run (s, 20.0f, noiseBurst(), runaway);
                char name[96];
                std::snprintf (name, sizeof name, "runaway ch%d pp%d drive %.1f: bounded, sustains", channels, (int) pingPong, drive);
                std::snprintf (d, sizeof d, "peak %.3f  last-second rms %.4f", peak (out, 1.0f), rms (out, 19.0f, 20.0f));
                check (name, peak (out, 1.0f) < 1.0f && rms (out, 19.0f, 20.0f) > 0.01f, d);
            }
        }

    Setup s; s.runawayDrive = 1.0f;
    auto out = run (s, 20.0f, dc (0.5f), runaway);
    std::snprintf (d, sizeof d, "peak %.3f", peak (out, 1.0f));
    check ("runaway with DC 0.5 input: bounded", peak (out, 1.0f) < 1.0f, d);

    for (float delayMs : { 1.0f, 300.0f })
    {
        s.delayMs = delayMs;
        out = run (s, 10.0f, loudNoise(), runaway);
        std::snprintf (d, sizeof d, "peak after pressed %.3f", peak (out, 1.0f));
        check (delayMs < 2.0f ? "runaway, loud input, 1 ms delay: bounded" : "runaway, loud input, 300 ms delay: bounded",
               peak (out, 1.0f) < 1.0f, d);
    }

    // Pressing it never overshoots what the clean delay was already doing.
    s.delayMs = 1.0f;
    const auto pressed = run (s, 1.0f, loudNoise(), runaway);
    const auto clean = run (s, 1.0f, loudNoise());
    const auto peakIn = [] (const std::vector<float>& v)
    {
        return peak (std::vector<float> (v.begin() + (long) (0.5f * sr), v.end()));
    };
    std::snprintf (d, sizeof d, "peak while ramping %.3f, clean delay %.3f", peakIn (pressed), peakIn (clean));
    check ("runaway press, 1 ms delay: no overshoot", peakIn (pressed) <= peakIn (clean), d);
}

void testWear()
{
    char d[96];

    for (bool pingPong : { false, true })
    {
        Setup s; s.feedback = 0.95f; s.wear = 1.0f; s.pingPong = pingPong;
        const auto out = run (s, 30.0f, noiseBurst());
        std::snprintf (d, sizeof d, "peak %.3f  last-second rms %.4f", peak (out), rms (out, 29.0f, 30.0f));
        check (pingPong ? "wear 1, feedback 95%, ping-pong: bounded, decays" : "wear 1, feedback 95%: bounded, decays",
               peak (out) < 1.2f && rms (out, 29.0f, 30.0f) < 0.05f, d);
    }
}

void testTapestop()
{
    char d[128];

    for (bool filters : { false, true })
    {
        Setup s; s.filters = filters;
        const auto ref = run (s, 6.0f, sine());
        const auto full = run (s, 6.0f, sine(), [] (DelayEngine& e, float t) { e.setTapestop (t >= 1.0f && t < 3.0f); });
        const auto early = run (s, 6.0f, sine(), [] (DelayEngine& e, float t) { e.setTapestop (t >= 1.0f && t < 1.2f); });
        const auto stepLimit = maxStep (ref) * 1.05f;
        const auto tag = filters ? ", filters on" : "";
        char name[96];

        std::snprintf (name, sizeof name, "tapestop full stop%s: silent while stopped", tag);
        std::snprintf (d, sizeof d, "stopped rms %.5f (reference %.3f)", rms (full, 1.6f, 3.0f), rms (ref, 1.6f, 3.0f));
        check (name, rms (full, 1.6f, 3.0f) < 1e-4f, d);

        std::snprintf (name, sizeof name, "tapestop full stop%s: exactly back, no clicks", tag);
        std::snprintf (d, sizeof d, "diff after 4 s %.2e, max step %.4f (reference %.4f)", maxDiff (full, ref, 4.0f), maxStep (full), maxStep (ref));
        check (name, maxDiff (full, ref, 4.0f) < 1e-6f && maxStep (full) <= stepLimit, d);

        std::snprintf (name, sizeof name, "tapestop early release%s: exactly back, no clicks", tag);
        std::snprintf (d, sizeof d, "diff after 3 s %.2e, max step %.4f", maxDiff (early, ref, 3.0f), maxStep (early));
        check (name, maxDiff (early, ref, 3.0f) < 1e-6f && maxStep (early) <= stepLimit, d);
    }

    {
        Setup s;
        const auto frozenRef  = run (s, 7.0f, sine(), [] (DelayEngine& e, float t) { e.setFrozen (t >= 1.0f); });
        const auto frozenStop = run (s, 7.0f, sine(), [] (DelayEngine& e, float t) { e.setFrozen (t >= 1.0f); e.setTapestop (t >= 2.0f && t < 3.5f); });
        std::snprintf (d, sizeof d, "stopped rms %.5f, diff after 4.5 s %.2e", rms (frozenStop, 2.6f, 3.5f), maxDiff (frozenStop, frozenRef, 4.5f));
        check ("tapestop while frozen: same loop comes back", rms (frozenStop, 2.6f, 3.5f) < 1e-4f
               && maxDiff (frozenStop, frozenRef, 4.5f) < 1e-6f && maxStep (frozenStop) <= maxStep (frozenRef) * 1.05f, d);
    }

    {
        Setup s; s.delayMs = 5000.0f; s.feedback = 0.9f;
        const auto ref = run (s, 20.0f, sine());
        const auto out = run (s, 20.0f, sine(), [] (DelayEngine& e, float t)
        {
            e.setTapestopTimeMs (2000);
            e.setTapestop ((t >= 6.0f && t < 12.0f) || (t >= 12.5f && t < 13.0f));
        });
        std::snprintf (d, sizeof d, "max step %.4f (reference %.4f)", maxStep (out), maxStep (ref));
        check ("tapestop 5 s delay, 2 s stop, re-press: stable", maxStep (out) <= maxStep (ref) * 1.05f, d);
    }
}
}

int main()
{
    testCleanPath();
    testRunaway();
    testWear();
    testTapestop();

    std::printf ("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
