#include "audio.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

const float PI_F = 3.14159265358979323846f;
const int SAMPLE_RATE = 22050;
const int MAX_VOICES = 12;

struct Clip {
    std::vector<float> data;
};

struct Voice {
    const Clip *clip = nullptr;
    size_t pos = 0;
};

struct AState {
    SDL_AudioStream *stream = nullptr;
    SDL_Mutex *lock = nullptr;
    Clip clips[SND_COUNT];
    Voice voices[MAX_VOICES];
    bool ready = false;
    unsigned int rng = 0x13579bdfu;
};

AState S;

float Noise() {
    // xorshift, so the synthesis is reproducible and independent of rand()
    S.rng ^= S.rng << 13;
    S.rng ^= S.rng >> 17;
    S.rng ^= S.rng << 5;
    return ((float)(S.rng & 0xFFFFFF) / 8388608.0f) - 1.0f;
}

void Synthesise(SoundId id, Clip &clip) {
    float dur = 0.1f, freq = 140, noiseAmt = 0.7f, decay = 30, lp = 0.35f, vol = 0.8f, sweep = 0;
    bool bell = false;
    switch (id) {
        case SND_HIT:    dur = 0.12f; freq = 150; noiseAmt = 0.75f; decay = 32; lp = 0.35f; vol = 0.80f; break;
        case SND_HEAVY:  dur = 0.22f; freq = 85;  noiseAmt = 0.70f; decay = 17; lp = 0.25f; vol = 0.95f; break;
        case SND_BLOCK:  dur = 0.07f; freq = 820; noiseAmt = 0.50f; decay = 55; lp = 0.90f; vol = 0.40f; break;
        case SND_WHOOSH: dur = 0.14f; freq = 0;   noiseAmt = 1.00f; decay = 0;  lp = 0.12f; vol = 0.30f; break;
        case SND_KO:     dur = 0.80f; freq = 60;  noiseAmt = 0.50f; decay = 5;  lp = 0.15f; vol = 1.00f; sweep = -25; break;
        case SND_BELL:   dur = 0.60f; freq = 880; noiseAmt = 0.00f; decay = 6;  lp = 1.00f; vol = 0.45f; bell = true; break;
        case SND_THUD:   dur = 0.16f; freq = 70;  noiseAmt = 0.40f; decay = 24; lp = 0.12f; vol = 0.70f; break;
        // a bright upward chirp for a successful parry
        case SND_PARRY:  dur = 0.26f; freq = 620; noiseAmt = 0.15f; decay = 14; lp = 0.85f; vol = 0.65f; sweep = 900; break;
        // low swelling rumble when rage arms
        case SND_RAGE:   dur = 0.70f; freq = 110; noiseAmt = 0.35f; decay = 4;  lp = 0.20f; vol = 0.80f; sweep = 60; break;
        default: break;
    }
    int n = (int)(dur * SAMPLE_RATE);
    clip.data.resize(n);
    float low = 0, phase = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = (id == SND_WHOOSH) ? sinf(PI_F * t / dur) : expf(-decay * t);
        if (id == SND_RAGE) env = sinf(PI_F * t / dur);
        float nz = Noise();
        low += (nz - low) * lp;
        phase += 2 * PI_F * (freq + sweep * t) / SAMPLE_RATE;
        float tone = sinf(phase);
        if (bell) tone = 0.6f * tone + 0.4f * sinf(phase * 2.01f);
        float s = (low * noiseAmt + tone * (1 - noiseAmt)) * env * vol;
        clip.data[i] = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
    }
}

void SDLCALL FeedAudio(void *, SDL_AudioStream *stream, int additional, int) {
    if (additional <= 0) return;
    int frames = additional / (int)sizeof(float);
    static std::vector<float> mix;
    mix.assign((size_t)frames, 0.0f);

    SDL_LockMutex(S.lock);
    for (auto &v : S.voices) {
        if (!v.clip) continue;
        const std::vector<float> &d = v.clip->data;
        for (int i = 0; i < frames && v.pos < d.size(); i++, v.pos++) mix[i] += d[v.pos];
        if (v.pos >= d.size()) v.clip = nullptr;
    }
    SDL_UnlockMutex(S.lock);

    // soft clip, so several simultaneous impacts don't tear
    for (auto &s : mix) s = tanhf(s * 0.9f);
    SDL_PutAudioStreamData(stream, mix.data(), frames * (int)sizeof(float));
}

}  // namespace

namespace A {

bool Init() {
    for (int i = 0; i < SND_COUNT; i++) Synthesise((SoundId)i, S.clips[i]);

    S.lock = SDL_CreateMutex();
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = SAMPLE_RATE;
    S.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, FeedAudio, nullptr);
    if (!S.stream) {
        std::printf("[audio] no output device: %s\n", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(S.stream);
    S.ready = true;
    return true;
}

void Shutdown() {
    if (S.stream) SDL_DestroyAudioStream(S.stream);
    S.stream = nullptr;
    if (S.lock) SDL_DestroyMutex(S.lock);
    S.lock = nullptr;
    S.ready = false;
}

bool Ready() { return S.ready; }

void Play(SoundId id) {
    if (!S.ready || id < 0 || id >= SND_COUNT) return;
    SDL_LockMutex(S.lock);
    Voice *slot = nullptr;
    for (auto &v : S.voices) {
        if (!v.clip) { slot = &v; break; }
    }
    // all voices busy: steal the one closest to finishing
    if (!slot) {
        size_t bestRemaining = (size_t)-1;
        for (auto &v : S.voices) {
            size_t remaining = v.clip->data.size() - v.pos;
            if (remaining < bestRemaining) { bestRemaining = remaining; slot = &v; }
        }
    }
    if (slot) {
        slot->clip = &S.clips[id];
        slot->pos = 0;
    }
    SDL_UnlockMutex(S.lock);
}

}  // namespace A
