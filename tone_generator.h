#ifndef TONE_GENERATOR_H
#define TONE_GENERATOR_H

#include <atomic>
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

    // Play a short one-shot beep. Defaults (1200 Hz sine, 50ms) match the
    // original button-click feedback beep unchanged; Beep Assist
    // (ui_copilot.cpp) passes its own explicit frequency/waveform/duration
    // so Navigation and Timing modes sound distinct (RB-SEG-04) without
    // affecting the button-click beep's length.
    void playBeep(double freq_hz = 1200.0, ToneWaveform wave = ToneWaveform::Sine,
                  int duration_ms = 50);

private:
    void threadFunc();

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> beep_requested_{false};
    std::atomic<double> beep_freq_hz_{1200.0};
    std::atomic<ToneWaveform> beep_wave_{ToneWaveform::Sine};
    std::atomic<int> beep_duration_ms_{50};

    std::mutex mu_;
    int tone_ms_ = 0;
    int silence_ms_ = 0;
    double freq_hz_ = 0.0;
    ToneWaveform wave_ = ToneWaveform::Sine;
};

#endif // TONE_GENERATOR_H
