#include "../core/log.h"
#include "os.h"

#include <alloca.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdatomic.h>

constexpr int ALSA_FRAMES = 512;

// The main thread owns the thread and the stop flag; the audio thread owns the
// PCM and reopens it on its own when the device goes.
static snd_pcm_t* alsa_pcm;
static bool alsa_running;
static atomic_bool alsa_stop;
static pthread_t alsa_thread;

// alsa-lib would print its own diagnostics on every failed open.
static void alsa_quiet(const char*, int, const char*, int, const char*, ...) {
}

static void alsa_release(void) {
    if (alsa_pcm) snd_pcm_close(alsa_pcm);

    alsa_pcm = nullptr;
}

// Underrun and suspend are recoverable, but anything else will end the thread.
static bool alsa_recover(int err) {
    if (err == -EPIPE) return snd_pcm_prepare(alsa_pcm) == 0;
    if (err != -ESTRPIPE) return false;

    while (!atomic_load(&alsa_stop) && (err = snd_pcm_resume(alsa_pcm)) == -EAGAIN)
        orb_os_sleep(10000000);

    if (atomic_load(&alsa_stop)) return false; // orb_os_close is shutting the thread down

    return err == 0 || snd_pcm_prepare(alsa_pcm) == 0;
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

// Opens and configures the default device; on failure nothing is left held.
static int alsa_start(void) {
    int err = snd_pcm_open(&alsa_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);

    if (err < 0) {
        alsa_pcm = nullptr;
        return err;
    }

    if (alsa_configure()) return 0;

    alsa_release();
    return -EINVAL;
}

// One period: render, then write it through underrun and suspend recovery.
// Returns the error that ends the device, which is also where a stop during a
// suspend lands; the caller checks the flag.
static int alsa_fill(int16_t* buffer) {
    const int16_t* at = buffer;
    snd_pcm_uframes_t left = ALSA_FRAMES;

    orb_audio_render(buffer, ALSA_FRAMES);

    while (left > 0) {
        snd_pcm_sframes_t n = snd_pcm_writei(alsa_pcm, at, left);

        if (n < 0) {
            if (alsa_recover((int)n)) continue;

            return (int)n;
        }

        at += n * ORB_AUDIO_CHANNELS;
        left -= (snd_pcm_uframes_t)n;
    }

    return 0;
}

// No device: keep the mixer's clock running and try the device again once a
// second, the first time a full second after the loss.
static int alsa_outage(void) {
    uint64_t start = orb_os_ticks(), rendered = 0;
    int err = -ENODEV;

    for (int tick = 1; !atomic_load(&alsa_stop); tick++) {
        orb_os_sleep(ORB_AUDIO_TICK_NS);
        orb_audio_idle(orb_os_ticks() - start, &rendered);

        if (tick % ORB_AUDIO_RETRY_TICKS || atomic_load(&alsa_stop)) continue;
        if ((err = alsa_start()) == 0) return 0;
    }

    return err;
}

static void* alsa_run(void* arg) {
    static int16_t buffer[ALSA_FRAMES * ORB_AUDIO_CHANNELS];

    (void)arg;

    snd_lib_error_set_handler(alsa_quiet);

    int err = alsa_start();

    if (err < 0)
        orb_log(
            "no audio device: %s (orb plays 48 kHz 16-bit stereo on \"default\"); retrying",
            snd_strerror(err)
        );

    while (!atomic_load(&alsa_stop)) {
        if (err < 0) {
            err = alsa_outage();

            if (err == 0) orb_log("audio: device open");

            continue;
        }

        err = alsa_fill(buffer);

        if (err < 0 && !atomic_load(&alsa_stop)) {
            orb_log("audio: %s; device lost, retrying", snd_strerror(err));
            alsa_release();
        }
    }

    alsa_release();
    return nullptr;
}

static void alsa_open(void) {
    atomic_store(&alsa_stop, false);
    alsa_running = pthread_create(&alsa_thread, nullptr, alsa_run, nullptr) == 0;

    if (!alsa_running) orb_log("audio: cannot start the audio thread; sound is off");
}

static void alsa_close(void) {
    if (!alsa_running) return;

    atomic_store(&alsa_stop, true);
    pthread_join(alsa_thread, nullptr);
    alsa_running = false;
}
