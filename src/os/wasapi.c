#include "../core/log.h"
#include "os.h"

#define COBJMACROS
#include <initguid.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <stdatomic.h>

constexpr REFERENCE_TIME WASAPI_BUFFER = 200000; // 20 ms in 100 ns units
constexpr DWORD WASAPI_WAIT_MS = 100;  // a dead device may never signal, so the wait polls
constexpr int WASAPI_STALL_WAITS = 10; // full-buffer timeouts before the device counts as dead
constexpr DWORD WASAPI_TICK_MS = (DWORD)(ORB_AUDIO_TICK_NS / 1000000);
constexpr DWORD WASAPI_JOIN_MS = 2000; // close gives up on a thread wedged inside a COM call

// The main thread owns the event, the thread handle, and the stop flag; the
// audio thread owns the client and reopens it on its own when the device goes.
static HANDLE wasapi_event, wasapi_thread;
static IAudioClient* wasapi_client;
static IAudioRenderClient* wasapi_render;
static UINT32 wasapi_buffer_frames;
static atomic_bool wasapi_stop;

static void wasapi_release(void) {
    if (wasapi_render) IAudioRenderClient_Release(wasapi_render);

    if (wasapi_client) {
        IAudioClient_Stop(wasapi_client);
        IAudioClient_Release(wasapi_client);
    }

    wasapi_render = nullptr;
    wasapi_client = nullptr;
}

// Opens the default render endpoint in shared mode; on failure nothing is left held.
static HRESULT wasapi_start(void) {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    WAVEFORMATEX format = {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = ORB_AUDIO_CHANNELS,
        .nSamplesPerSec = ORB_AUDIO_RATE,
        .nAvgBytesPerSec = ORB_AUDIO_RATE * ORB_AUDIO_CHANNELS * 2,
        .nBlockAlign = ORB_AUDIO_CHANNELS * 2,
        .wBitsPerSample = 16,
    };
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    HRESULT hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, &IID_IMMDeviceEnumerator,
        (void**)&enumerator
    );

    if (SUCCEEDED(hr))
        hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
    if (SUCCEEDED(hr))
        hr = IMMDevice_Activate(
            device, &IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&wasapi_client
        );
    if (SUCCEEDED(hr))
        hr = IAudioClient_Initialize(
            wasapi_client, AUDCLNT_SHAREMODE_SHARED, flags, WASAPI_BUFFER, 0, &format, nullptr
        );
    if (SUCCEEDED(hr)) hr = IAudioClient_SetEventHandle(wasapi_client, wasapi_event);
    if (SUCCEEDED(hr)) hr = IAudioClient_GetBufferSize(wasapi_client, &wasapi_buffer_frames);
    if (SUCCEEDED(hr))
        hr =
            IAudioClient_GetService(wasapi_client, &IID_IAudioRenderClient, (void**)&wasapi_render);
    if (SUCCEEDED(hr)) hr = IAudioClient_Start(wasapi_client);

    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    if (FAILED(hr)) wasapi_release();

    return hr;
}

// One buffer: wait for the engine, then fill what the padding leaves free. A
// sleeping HDMI sink stops draining without invalidating the client, so the
// buffer stays full and the event never fires; a run of timed-out waits like
// that counts as the device being gone.
static HRESULT wasapi_fill(void) {
    static int stalls;
    DWORD wait = WaitForSingleObject(wasapi_event, WASAPI_WAIT_MS);

    if (wait == WAIT_FAILED) return HRESULT_FROM_WIN32(GetLastError());
    if (atomic_load(&wasapi_stop)) return S_OK;

    UINT32 padding;
    HRESULT hr = IAudioClient_GetCurrentPadding(wasapi_client, &padding);

    if (FAILED(hr)) return hr;

    UINT32 frames = wasapi_buffer_frames - padding;

    if (frames == 0) {
        if (wait != WAIT_TIMEOUT || ++stalls < WASAPI_STALL_WAITS) return S_OK;

        stalls = 0;
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }

    stalls = 0;

    BYTE* data;

    hr = IAudioRenderClient_GetBuffer(wasapi_render, frames, &data);

    if (FAILED(hr)) return hr;

    orb_audio_render((int16_t*)data, (int)frames);
    return IAudioRenderClient_ReleaseBuffer(wasapi_render, frames, 0);
}

// No device: keep the mixer's clock running and try the device again once a
// second. The clock is the tick clock, since Sleep rounds to the scheduler tick
// and a reopen attempt blocks; the wait is on the event so close wakes it. The
// first retry waits a full second so an endpoint that opens but fails at once
// cannot spin the thread through open and release.
static HRESULT wasapi_outage(void) {
    uint64_t start = orb_os_ticks(), rendered = 0;
    HRESULT hr = E_FAIL;

    while (!atomic_load(&wasapi_stop)) {
        WaitForSingleObject(wasapi_event, WASAPI_TICK_MS);

        if (orb_audio_idle(orb_os_ticks() - start, &rendered) && SUCCEEDED(hr = wasapi_start()))
            return hr;
    }

    return hr;
}

static DWORD WINAPI wasapi_run(void* arg) {
    (void)arg;

    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = SUCCEEDED(com) ? wasapi_start() : com; // without COM every open fails, but
                                                        // the outage loop still keeps time

    if (FAILED(hr)) orb_log("no audio device: WASAPI error 0x%08lx; retrying", (unsigned long)hr);

    while (!atomic_load(&wasapi_stop)) {
        if (FAILED(hr)) {
            hr = wasapi_outage();

            if (SUCCEEDED(hr)) orb_log("audio: device open");

            continue;
        }

        hr = wasapi_fill();

        if (FAILED(hr)) {
            orb_log("audio: WASAPI error 0x%08lx; device lost, retrying", (unsigned long)hr);
            wasapi_release();
        }
    }

    wasapi_release();

    if (SUCCEEDED(com)) CoUninitialize();

    return 0;
}

static void wasapi_open(void) {
    wasapi_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    atomic_store(&wasapi_stop, false);
    wasapi_thread =
        wasapi_event ? CreateThread(nullptr, 0, wasapi_run, nullptr, 0, nullptr) : nullptr;

    if (wasapi_thread) return;

    orb_log("audio: cannot start the audio thread; sound is off");

    if (wasapi_event) CloseHandle(wasapi_event);

    wasapi_event = nullptr;
}

static void wasapi_close(void) {
    if (!wasapi_thread) return;

    atomic_store(&wasapi_stop, true);
    SetEvent(wasapi_event);

    if (WaitForSingleObject(wasapi_thread, WASAPI_JOIN_MS) != WAIT_OBJECT_0) {
        orb_log("audio: the audio thread is stuck in the device; exit reaps it");
        return; // close runs only at exit, and the thread may still touch the handles
    }

    CloseHandle(wasapi_thread);
    CloseHandle(wasapi_event);
    wasapi_thread = nullptr;
    wasapi_event = nullptr;
}
