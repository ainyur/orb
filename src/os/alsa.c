#include "../core/log.h"
#include "os.h"

#include <alloca.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdatomic.h>

constexpr int ALSA_FRAMES = 512;

static snd_pcm_t* alsa_pcm;
static bool alsa_running;
static atomic_bool alsa_stop;
static pthread_t alsa_thread;

// Underrun and suspend are recoverable, but anything else will end the thread.
static bool alsa_recover(int err) {
    if (err == -EPIPE) return snd_pcm_prepare(alsa_pcm) == 0;
    if (err != -ESTRPIPE) return false;

    while (!atomic_load(&alsa_stop) && (err = snd_pcm_resume(alsa_pcm)) == -EAGAIN)
        orb_os_sleep(10000000);

    if (atomic_load(&alsa_stop)) return false; // orb_os_close is shutting the thread down

    return err == 0 || snd_pcm_prepare(alsa_pcm) == 0;
}

static void* alsa_run(void* arg) {
    static int16_t buffer[ALSA_FRAMES * ORB_AUDIO_CHANNELS];

    (void)arg;

    while (!atomic_load(&alsa_stop)) {
        const int16_t* at = buffer;
        snd_pcm_uframes_t left = ALSA_FRAMES;

        orb_audio_render(buffer, ALSA_FRAMES);

        while (left > 0) {
            snd_pcm_sframes_t n = snd_pcm_writei(alsa_pcm, at, left);

            if (n < 0) {
                if (alsa_recover((int)n)) continue;
                if (atomic_load(&alsa_stop)) return nullptr; // normal shutdown, not an error

                orb_log("audio: %s; sound stops", snd_strerror((int)n));
                return nullptr;
            }

            at += n * ORB_AUDIO_CHANNELS;
            left -= (snd_pcm_uframes_t)n;
        }
    }

    return nullptr;
}

static bool alsa_configure(void) {
    snd_pcm_hw_params_t* hw;
    unsigned rate = ORB_AUDIO_RATE;
    snd_pcm_uframes_t period = ALSA_FRAMES, buffer = ALSA_FRAMES * 4;

    snd_pcm_hw_params_alloca(&hw);

    return snd_pcm_hw_params_any(alsa_pcm, hw) >= 0 &&
           snd_pcm_hw_params_set_access(alsa_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED) >= 0 &&
           snd_pcm_hw_params_set_format(alsa_pcm, hw, SND_PCM_FORMAT_S16_LE) >= 0 &&
           snd_pcm_hw_params_set_channels(alsa_pcm, hw, ORB_AUDIO_CHANNELS) >= 0 &&
           snd_pcm_hw_params_set_rate_near(alsa_pcm, hw, &rate, nullptr) >= 0 &&
           rate == ORB_AUDIO_RATE &&
           snd_pcm_hw_params_set_period_size_near(alsa_pcm, hw, &period, nullptr) >= 0 &&
           snd_pcm_hw_params_set_buffer_size_near(alsa_pcm, hw, &buffer) >= 0 &&
           snd_pcm_hw_params(alsa_pcm, hw) >= 0;
}

static void alsa_open(void) {
    int err = snd_pcm_open(&alsa_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);

    if (err < 0) {
        orb_log("no audio device: %s", snd_strerror(err));
        alsa_pcm = nullptr;
        return;
    }

    if (!alsa_configure()) {
        orb_log("audio: the default device does not take 48 kHz 16-bit stereo; sound is off");
        snd_pcm_close(alsa_pcm);
        alsa_pcm = nullptr;
        return;
    }

    atomic_store(&alsa_stop, false);
    alsa_running = pthread_create(&alsa_thread, nullptr, alsa_run, nullptr) == 0;

    if (!alsa_running) orb_log("audio: cannot start the audio thread; sound is off");
}

static void alsa_close(void) {
    if (!alsa_pcm) return;

    if (alsa_running) {
        atomic_store(&alsa_stop, true);
        pthread_join(alsa_thread, nullptr);
        alsa_running = false;
    }

    snd_pcm_close(alsa_pcm);
    alsa_pcm = nullptr;
}
