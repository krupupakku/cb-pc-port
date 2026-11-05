#include "audio/audio_manager.h"
#include <cstdio>
#include <vector>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

namespace cb::audio {

#ifdef _WIN32
static constexpr int SAMPLE_RATE  = 44100;
static constexpr int CHANNELS     = 2;
static constexpr int BUFFER_COUNT = 4;
static constexpr int BUFFER_SIZE  = 1024; // campioni stereo per blocco (~23ms)

static HWAVEOUT   g_hWaveOut = nullptr;
static WAVEHDR    g_waveHeaders[BUFFER_COUNT] = {};
static std::vector<int16_t> g_buffers[BUFFER_COUNT];
static int        g_currentBuf = 0;
static bool       g_initialized = false;

// Buffer circolare temporaneo per accumulare i campioni inviati da SubmitSamples
static std::vector<int16_t> g_ringBuffer;
#endif

void Initialize() {
#ifdef _WIN32
    WAVEFORMATEX wfx{};
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = CHANNELS;
    wfx.nSamplesPerSec  = SAMPLE_RATE;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize          = 0;

    MMRESULT res = waveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        std::printf("[Audio] Impossibile aprire periferica WaveOut (errore %u)\n", res);
        return;
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        g_buffers[i].resize(BUFFER_SIZE * CHANNELS, 0);
        std::memset(&g_waveHeaders[i], 0, sizeof(WAVEHDR));
        g_waveHeaders[i].lpData         = reinterpret_cast<LPSTR>(g_buffers[i].data());
        g_waveHeaders[i].dwBufferLength = BUFFER_SIZE * CHANNELS * sizeof(int16_t);
        waveOutPrepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
        g_waveHeaders[i].dwFlags |= WHDR_DONE; // Pronto per essere riempito
    }

    g_currentBuf = 0;
    g_ringBuffer.reserve(BUFFER_SIZE * CHANNELS * 8);
    g_initialized = true;

    std::printf("[Audio] Output audio WaveOut inizializzato a 44.1 kHz stereo.\n");
#else
    std::printf("[Audio] Piattaforma non-Windows: audio dummy.\n");
#endif
}

void Shutdown() {
#ifdef _WIN32
    if (!g_initialized || !g_hWaveOut) return;
    g_initialized = false;

    waveOutReset(g_hWaveOut);
    for (int i = 0; i < BUFFER_COUNT; i++) {
        waveOutUnprepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
    }
    waveOutClose(g_hWaveOut);
    g_hWaveOut = nullptr;
    std::printf("[Audio] Output audio chiuso con successo.\n");
#endif
}

void SubmitSamples(const int16_t* stereo_pcm, int num_samples) {
#ifdef _WIN32
    if (!g_initialized || !g_hWaveOut || !stereo_pcm || num_samples <= 0) return;

    // Accumula campioni nel buffer circolare
    size_t prev_size = g_ringBuffer.size();
    g_ringBuffer.resize(prev_size + num_samples * CHANNELS);
    std::memcpy(g_ringBuffer.data() + prev_size, stereo_pcm, num_samples * CHANNELS * sizeof(int16_t));

    // Se abbiamo almeno un blocco completo BUFFER_SIZE, invialo al prossimo header disponibile
    while (g_ringBuffer.size() >= BUFFER_SIZE * CHANNELS) {
        auto& hdr = g_waveHeaders[g_currentBuf];
        if (hdr.dwFlags & WHDR_DONE) {
            std::memcpy(g_buffers[g_currentBuf].data(), g_ringBuffer.data(), BUFFER_SIZE * CHANNELS * sizeof(int16_t));
            g_ringBuffer.erase(g_ringBuffer.begin(), g_ringBuffer.begin() + BUFFER_SIZE * CHANNELS);

            hdr.dwFlags &= ~WHDR_DONE;
            waveOutWrite(g_hWaveOut, &hdr, sizeof(WAVEHDR));
            g_currentBuf = (g_currentBuf + 1) % BUFFER_COUNT;
        } else {
            // Buffer ancora occupato, evita accumulo eccessivo (massimo 4 blocchi)
            if (g_ringBuffer.size() > BUFFER_SIZE * CHANNELS * 4) {
                g_ringBuffer.erase(g_ringBuffer.begin(), g_ringBuffer.begin() + BUFFER_SIZE * CHANNELS);
            }
            break;
        }
    }
#else
    (void)stereo_pcm; (void)num_samples;
#endif
}

} // namespace cb::audio
