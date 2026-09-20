#include "tone_cadence.h"

#include <cstdio>

ToneCadence decideToneCadence(bool tone_enabled, bool simple_tone_mode,
                              const ArrowToneResult& arrow,
                              SimpleToneState& simple_state,
                              double seconds_ahead_behind,
                              double stage_distance_m, bool past_stage_end) {
    ToneCadence cadence;
    if (!tone_enabled) return cadence;

    if (simple_tone_mode) {
        SimpleToneResult simple = updateSimpleTone(
            simple_state, seconds_ahead_behind, stage_distance_m, past_stage_end);
        if (simple.active) {
            cadence.tone_ms = SIMPLE_TONE_SUSTAIN_MS;
            cadence.silence_ms = 0;
            cadence.freq_hz = simple.freq_hz;
            cadence.triangle = simple.triangle_wave;
        }
        return cadence;
    }

    if (arrow.tone_active) {
        cadence.tone_ms = arrow.tone_ms;
        cadence.silence_ms = arrow.silence_ms;
        cadence.freq_hz = arrow.freq_hz;
    }
    return cadence;
}

std::string toneCadenceJson(const ToneCadence& cadence) {
    const bool on = cadence.sounding();
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"tone_ms\":%d,\"silence_ms\":%d,\"freq_hz\":%.2f,\"wave\":\"%s\"}",
             on ? cadence.tone_ms : 0,
             on ? cadence.silence_ms : 0,
             on ? cadence.freq_hz : 0.0,
             (on && cadence.triangle) ? "triangle" : "sine");
    return buf;
}

std::string beepEventJson(const BeepEvent& beep) {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"seq\":%lu,\"freq_hz\":%.2f,\"ms\":%d,\"amp\":%.2f,"
             "\"wave\":\"%s\",\"twice\":%s,\"gap_ms\":%d}",
             beep.seq, beep.freq_hz, beep.duration_ms, beep.amplitude,
             beep.triangle ? "triangle" : "sine",
             beep.twice ? "true" : "false", beep.gap_ms);
    return buf;
}
