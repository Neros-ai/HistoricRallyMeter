#ifndef TONE_GENERATOR_H
#define TONE_GENERATOR_H

#include <atomic>
#include <deque>
#include <thread>
#include <mutex>

enum class ToneWaveform { Sine, Triangle };

class ToneGenerator {
public:
    ToneGenerator();
    ~ToneGenerator();

    void start();
    void stop();

    // Set beep cadence, pitch, and waveform shape.
    // tone_ms/silence_ms: on/off durations. Both 0 = silent.
    // freq_hz: tone frequency in Hz (e.g. 523.25 for C5).
    // wave: Sine (default) or Triangle.
    void setCadence(int tone_ms, int silence_ms, double freq_hz = 0.0,
                     ToneWaveform wave = ToneWaveform::Sine);

    // Play a short one-shot beep. The defaults (1200 Hz sine, 50ms, 0.20) are
    // the original button-click feedback beep, unchanged: every global button
    // click routes through here, so a louder or longer default would raise
    // and lengthen the click too. Beep Assist (ui_copilot.cpp) passes its own
    // frequency, waveform, duration and amplitude so Navigation and Timing
    // sound distinct (RB-SEG-04) without touching the click.
    //
    // Requests are queued rather than latched, so two beeps raised close
    // together (a Navigation double-beep overlapping a Timing beep) both
    // play, each with its own parameters. A non-positive duration or
    // frequency is ignored.
    void playBeep(double freq_hz = 1200.0, ToneWaveform wave = ToneWaveform::Sine,
                  int duration_ms = 50, double amplitude = 0.20);

private:
    void threadFunc();

    std::thread worker_;
    std::atomic<bool> running_{false};
    // One queued one-shot beep. Parameters travel WITH the request instead of
    // alongside it: independent atomics let a second playBeep() call
    // overwrite the first's frequency, waveform and duration before the
    // worker had read them, rendering one beep with the other's voice.
    struct BeepRequest {
        double freq_hz;
        ToneWaveform wave;
        unsigned frames;
        double amplitude;
    };
    // Bounds the queue so a stuck audio device cannot grow it without limit.
    static constexpr size_t MAX_QUEUED_BEEPS = 8;
    std::mutex beep_mu_;
    std::deque<BeepRequest> beep_queue_;

    std::mutex mu_;
    int tone_ms_ = 0;
    int silence_ms_ = 0;
    double freq_hz_ = 0.0;
    ToneWaveform wave_ = ToneWaveform::Sine;
};

#endif // TONE_GENERATOR_H
