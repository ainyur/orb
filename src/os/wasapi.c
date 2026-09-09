#include "../core/log.h"
#include "os.h"

#define COBJMACROS
#include <initguid.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <stdatomic.h>

constexpr REFERENCE_TIME WASAPI_BUFFER = 200000; // 20 ms in 100 ns units

static HANDLE wasapi_event, wasapi_thread;
static IAudioClient* wasapi_client;
static IAudioRenderClient* wasapi_render;
static UINT32 wasapi_buffer_frames;
static atomic_bool wasapi_stop;
static bool wasapi_com;

static DWORD WINAPI wasapi_run(void* arg) {
    (void)arg;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    HRESULT hr = S_OK;

    for (;;) {
        if (WaitForSingleObject(wasapi_event, INFINITE) != WAIT_OBJECT_0) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            break;
        }

        if (atomic_load(&wasapi_stop)) break;

        UINT32 padding;

        hr = IAudioClient_GetCurrentPadding(wasapi_client, &padding);
        if (FAILED(hr)) break;

        UINT32 frames = wasapi_buffer_frames - padding;

        if (frames == 0) continue;

        BYTE* data;

        hr = IAudioRenderClient_GetBuffer(wasapi_render, frames, &data);
        if (FAILED(hr)) break;

        orb_audio_render((int16_t*)data, (int)frames);
        IAudioRenderClient_ReleaseBuffer(wasapi_render, frames, 0);
    }

    if (!atomic_load(&wasapi_stop))
        orb_log("audio: WASAPI error 0x%08lx; sound stops", (unsigned long)hr);

    IAudioClient_Stop(wasapi_client);
    CoUninitialize();
    return 0;
}

static void wasapi_release(void) {
    if (wasapi_render) IAudioRenderClient_Release(wasapi_render);
    if (wasapi_client) IAudioClient_Release(wasapi_client);
    if (wasapi_event) CloseHandle(wasapi_event);

    wasapi_render = nullptr;
    wasapi_client = nullptr;
    wasapi_event = nullptr;
}

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

    if (SUCCEEDED(hr)) {
        wasapi_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        hr = wasapi_event ? IAudioClient_SetEventHandle(wasapi_client, wasapi_event) : E_FAIL;
    }

    if (SUCCEEDED(hr)) hr = IAudioClient_GetBufferSize(wasapi_client, &wasapi_buffer_frames);
    if (SUCCEEDED(hr))
        hr =
            IAudioClient_GetService(wasapi_client, &IID_IAudioRenderClient, (void**)&wasapi_render);
    if (SUCCEEDED(hr)) hr = IAudioClient_Start(wasapi_client);

    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);

    return hr;
}

static void wasapi_open(void) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        orb_log("no audio device: COM did not initialize");
        return;
    }

    wasapi_com = true; // S_FALSE still counts as success and needs a matching uninitialize

    HRESULT hr = wasapi_start();

    if (FAILED(hr)) {
        orb_log("no audio device: WASAPI error 0x%08lx", (unsigned long)hr);
        wasapi_release();
        return;
    }

    atomic_store(&wasapi_stop, false);
    wasapi_thread = CreateThread(nullptr, 0, wasapi_run, nullptr, 0, nullptr);

    if (!wasapi_thread) {
        orb_log("audio: cannot start the audio thread; sound is off");
        IAudioClient_Stop(wasapi_client);
        wasapi_release();
    }
}

static void wasapi_close(void) {
    if (wasapi_thread) {
        atomic_store(&wasapi_stop, true);
        SetEvent(wasapi_event);
        WaitForSingleObject(wasapi_thread, INFINITE);
        CloseHandle(wasapi_thread);
        wasapi_thread = nullptr;
    }

    wasapi_release();

    if (wasapi_com) {
        CoUninitialize();
        wasapi_com = false;
    }
}
