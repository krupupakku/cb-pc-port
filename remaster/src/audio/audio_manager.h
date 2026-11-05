#pragma once
// =============================================================================
// Crash Bash: Remaster+ — SPU & Audio Streaming Manager
// Backend audio PC a bassa latenza per Windows (WaveOut)
// =============================================================================

#include <cstdint>

namespace cb::audio {

void Initialize();
void Shutdown();

// Invia un blocco di campioni audio stereo a 44100 Hz alle casse/cuffie
void SubmitSamples(const int16_t* stereo_pcm, int num_samples);

} // namespace cb::audio
