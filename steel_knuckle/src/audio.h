// =============================================================================
//  audio.h - procedurally generated sound effects, mixed onto one SDL3 stream.
//
//  No asset files: every sound is synthesised at startup from a noise/tone
//  envelope, exactly as the raylib build did. SDL plays one stream, so the
//  voices are mixed here.
// =============================================================================
#pragma once

enum SoundId {
    SND_HIT = 0,
    SND_HEAVY,
    SND_BLOCK,
    SND_WHOOSH,
    SND_KO,
    SND_BELL,
    SND_THUD,
    SND_PARRY,
    SND_RAGE,
    SND_COUNT
};

namespace A {

bool Init();
void Shutdown();
void Play(SoundId id);
bool Ready();

}  // namespace A
