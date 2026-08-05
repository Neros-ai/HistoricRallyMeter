#include "tone_generator.h"
#include <alsa/asoundlib.h>
#include <cmath>
#include <vector>
#include <iostream>

static constexpr unsigned SAMPLE_RATE  = 44100;
static constexpr double   AMPLITUDE    = 0.25;
static constexpr unsigned CHUNK_MS     = 20;
static constexpr unsigned CHUNK_FRAMES = SAMPLE_RATE * CHUNK_MS / 1000;
static constexpr unsigned FADE_MS      = 5;
static constexpr unsigned FADE_FRAMES  = SAMPLE_RATE * FADE_MS / 1000;

// phase is in radians, 0..2*M_PI. Sine is the usual sin(phase); triangle
// is a standard -1..1 triangle built from the same phase so both shapes
// stay phase-continuous under the no-phase-reset frequency handling below.
static double waveSample(double phase, ToneWaveform wave) {
    if (wave == ToneWaveform::Sine) {
        return std::sin(phase);
    }
    double t = phase / (2.0 * M_PI);
    t -= std::floor(t);
    return 4.0 * std::fabs(t - 0.5) - 1.0;
}

ToneGenerator::ToneGenerator() = default;

ToneGenerator::~ToneGenerator() {
    stop();
}

void ToneGenerator::start() {
    if (running_) return;
    running_ = true;
    worker_ = std::thread(&ToneGenerator::threadFunc, this);
}

void ToneGenerator::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void ToneGenerator::setCadence(int tone_ms, int silence_ms, double freq_hz, ToneWaveform wave) {
    std::lock_guard<std::mutex> lk(mu_);
    tone_ms_ = tone_ms;
    silence_ms_ = silence_ms;
    freq_hz_ = freq_hz;
    wave_ = wave;
}

void ToneGenerator::playBeep(double freq_hz, ToneWaveform wave, int duration_ms) {
    beep_freq_hz_ = freq_hz;
    beep_wave_ = wave;
    beep_duration_ms_ = duration_ms;
    beep_requested_ = true;
}

void ToneGenerator::threadFunc() {
    snd_pcm_t* pcm = nullptr;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::cerr << "ToneGenerator: cannot open audio: " << snd_strerror(err) << std::endl;
        return;
    }

    snd_pcm_set_params(pcm,
        SND_PCM_FORMAT_S16_LE,
        SND_PCM_ACCESS_RW_INTERLEAVED,
        1,
        SAMPLE_RATE,
        1,
        50000);

    std::vector<int16_t> chunk_buf(CHUNK_FRAMES);
    std::vector<int16_t> silence_buf(CHUNK_FRAMES, 0);

    double phase = 0.0;
    double active_freq = 0.0;
    ToneWaveform active_wave = ToneWaveform::Sine;
    int cycle_ms = 0;
    bool in_tone = true;
    // Envelope: 0.0 = silent, 1.0 = full volume; ramps smoothly between states
    double envelope = 0.0;

    static constexpr double BEEP_AMP = 0.45;

    // Silent bracket written before and after every one-shot beep. Splicing
    // the beep straight into the stream (no gap) can hit the ahead/behind
    // cadence tone mid-cycle at non-zero amplitude, producing an audible
    // click at the boundary; padding with true silence on both sides always
    // splices at zero, so it's click-free regardless of what's playing. It
    // also gives the beep a perceptual "onset/offset" flag that separates
    // it from a concurrent cadence tone -- most noticeable when the two are
    // close in pitch, e.g. Timing's 3200 Hz beep against simple-tone-mode's
    // near-3500 Hz "behind" ramp approaching -3s.
    static constexpr unsigned BEEP_SILENCE_FRAMES = SAMPLE_RATE * 50 / 1000;  // 50ms
    const std::vector<int16_t> beep_silence_buf(BEEP_SILENCE_FRAMES, 0);

    while (running_) {
        // Handle one-shot beep request
        if (beep_requested_.exchange(false)) {
            const double beep_freq = beep_freq_hz_;
            const ToneWaveform beep_wave = beep_wave_;
            const unsigned beep_frames = SAMPLE_RATE * static_cast<unsigned>(beep_duration_ms_) / 1000;
            std::vector<int16_t> beep_buf(beep_frames);
            double bp = 0.0;
            const double bp_inc = 2.0 * M_PI * beep_freq / SAMPLE_RATE;
            for (unsigned i = 0; i < beep_frames; i++) {
                double env = 1.0;
                if (i < FADE_FRAMES) env = static_cast<double>(i) / FADE_FRAMES;
                if (i > beep_frames - FADE_FRAMES) env = static_cast<double>(beep_frames - i) / FADE_FRAMES;
                beep_buf[i] = static_cast<int16_t>(BEEP_AMP * 32767.0 * env * waveSample(bp, beep_wave));
                bp += bp_inc;
            }
            snd_pcm_writei(pcm, beep_silence_buf.data(), BEEP_SILENCE_FRAMES);
            snd_pcm_writei(pcm, beep_buf.data(), beep_frames);
            snd_pcm_writei(pcm, beep_silence_buf.data(), BEEP_SILENCE_FRAMES);
        }

        int cur_tone, cur_silence;
        double cur_freq;
        ToneWaveform cur_wave;
        {
            std::lock_guard<std::mutex> lk(mu_);
            cur_tone = tone_ms_;
            cur_silence = silence_ms_;
            cur_freq = freq_hz_;
            cur_wave = wave_;
        }

        if (cur_tone <= 0 || cur_freq <= 0.0) {
            // Fade out if we were playing, then stay silent
            if (envelope > 0.0) {
                const double phase_inc = 2.0 * M_PI * active_freq / SAMPLE_RATE;
                const double env_dec = 1.0 / FADE_FRAMES;
                for (unsigned i = 0; i < CHUNK_FRAMES; i++) {
                    envelope -= env_dec;
                    if (envelope < 0.0) envelope = 0.0;
                    chunk_buf[i] = static_cast<int16_t>(AMPLITUDE * 32767.0 * envelope * waveSample(phase, active_wave));
                    phase += phase_inc;
                    if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
                }
                snd_pcm_writei(pcm, chunk_buf.data(), CHUNK_FRAMES);
            } else {
                snd_pcm_writei(pcm, silence_buf.data(), CHUNK_FRAMES);
            }
            cycle_ms = 0;
            in_tone = true;
            continue;
        }

        // Never reset phase on a frequency change -- only the phase
        // increment (rate of advance) needs to change going forward.
        // Snapping phase to 0 here used to make sin(phase) jump straight
        // from wherever the waveform was to sin(0)=0 at full envelope,
        // an audible click/thud on every retune -- most noticeable with
        // the simple-tone-mode continuous pitch ramp, which retunes far
        // more often than the old fixed two-frequency scheme did.
        active_freq = cur_freq;
        // A waveform-shape change (sine<->triangle) can still jump the
        // sample value at a given phase even without a frequency change.
        // In practice this only happens crossing simple-tone-mode's
        // behind/ahead boundary, which always passes through the silent
        // +/-0.2s quiet band first, so envelope is at/near 0 when the
        // shape actually switches -- no audible click in the common case.
        active_wave = cur_wave;

        int threshold = in_tone ? cur_tone : cur_silence;
        if (cycle_ms >= threshold) {
            in_tone = !in_tone;
            cycle_ms = 0;
        }

        // Generate chunk with envelope ramping for smooth transitions
        const double phase_inc = 2.0 * M_PI * active_freq / SAMPLE_RATE;
        const double env_rate = 1.0 / FADE_FRAMES;
        double target_env = in_tone ? 1.0 : 0.0;

        for (unsigned i = 0; i < CHUNK_FRAMES; i++) {
            if (envelope < target_env) {
                envelope += env_rate;
                if (envelope > 1.0) envelope = 1.0;
            } else if (envelope > target_env) {
                envelope -= env_rate;
                if (envelope < 0.0) envelope = 0.0;
            }
            chunk_buf[i] = static_cast<int16_t>(AMPLITUDE * 32767.0 * envelope * waveSample(phase, active_wave));
            phase += phase_inc;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
        }

        snd_pcm_sframes_t frames = snd_pcm_writei(pcm, chunk_buf.data(), CHUNK_FRAMES);
        if (frames < 0) snd_pcm_recover(pcm, static_cast<int>(frames), 1);

        cycle_ms += CHUNK_MS;
    }

    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
}
