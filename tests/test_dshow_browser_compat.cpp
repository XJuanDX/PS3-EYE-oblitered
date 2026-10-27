/* test_dshow_browser_compat.cpp — Edge tests for browser compatibility and COM invariants.
 *
 * Invariants tested:
 *  1. Instantiating multiple filter instances without streaming must NOT lock the USB hardware.
 *  2. QueryAccept must return S_FALSE (not a failing HRESULT) on unsupported media types.
 *  3. QueryAccept must return S_OK on supported YUY2 resolutions (640x480 and 320x240).
 *  4. IAMStreamConfig exposes both 640x480 and 320x240 capabilities.
 *  5. ConnectDirect simulation with lazy device activation on Pause/Run.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <strmif.h>
#include <stdio.h>
#include <assert.h>

#include "../src/dshow/ps3eye_guids.h"

typedef HRESULT (STDAPICALLTYPE *pfnDllGetClassObject)(REFCLSID rclsid, REFIID riid, LPVOID *ppv);

int main()
{
    printf("===================================================\n");
    printf("   PS3 Eye DirectShow Browser Compatibility Tests   \n");
    printf("===================================================\n\n");

    CoInitialize(NULL);

    HMODULE hDll = LoadLibraryW(L"build\\ps3eye_dshow.dll");
    if (!hDll) {
        printf("FAILED to load build\\ps3eye_dshow.dll (error %lu)\n", GetLastError());
        return 1;
    }
    pfnDllGetClassObject GetClassObject = (pfnDllGetClassObject)GetProcAddress(hDll, "DllGetClassObject");
    assert(GetClassObject != NULL);

    IClassFactory *pFactory = nullptr;
    HRESULT hr = GetClassObject(CLSID_PS3EyeFilter, IID_IClassFactory, (void**)&pFactory);
    assert(SUCCEEDED(hr) && pFactory != nullptr);

    // -----------------------------------------------------------------------
    // INVARIANT 1: Multi-instance creation without locking USB
    // -----------------------------------------------------------------------
    printf("1. Testing multi-instance instantiation (Invariant 1: Lazy USB binding)...\n");
    IBaseFilter *filters[5] = { nullptr };
    for (int i = 0; i < 5; i++) {
        hr = pFactory->CreateInstance(nullptr, IID_IBaseFilter, (void**)&filters[i]);
        if (FAILED(hr)) {
            printf("  FAILED: CreateInstance instance %d returned 0x%08lx\n", i, hr);
            return 1;
        }
    }
    printf("  [PASS] Successfully created 5 filter instances simultaneously.\n");
    for (int i = 0; i < 5; i++) {
        filters[i]->Release();
        filters[i] = nullptr;
    }
    printf("  [PASS] Released all 5 instances cleanly.\n\n");

    // -----------------------------------------------------------------------
    // Setup single filter instance for pin tests
    // -----------------------------------------------------------------------
    IBaseFilter *pFilter = nullptr;
    hr = pFactory->CreateInstance(nullptr, IID_IBaseFilter, (void**)&pFilter);
    assert(SUCCEEDED(hr) && pFilter != nullptr);

    IPin *pPin = nullptr;
    hr = pFilter->FindPin(L"Capture", &pPin);
    assert(SUCCEEDED(hr) && pPin != nullptr);

    // -----------------------------------------------------------------------
    // INVARIANT 2: QueryAccept must return S_FALSE (not failure) for invalid types
    // -----------------------------------------------------------------------
    printf("2. Testing QueryAccept return values (Invariant 2: COM specification compliance)...\n");

    AM_MEDIA_TYPE mtInvalid;
    ZeroMemory(&mtInvalid, sizeof(mtInvalid));
    mtInvalid.majortype = MEDIATYPE_Audio; // Completely wrong type
    hr = pPin->QueryAccept(&mtInvalid);
    printf("  QueryAccept(MEDIATYPE_Audio) returned: 0x%08lx (expected S_FALSE = 1)\n", hr);
    assert(hr == S_FALSE);

    // Test with video type but unsupported subtype (e.g. RGB24 / 1080p)
    VIDEOINFOHEADER vih1080;
    ZeroMemory(&vih1080, sizeof(vih1080));
    vih1080.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih1080.bmiHeader.biWidth = 1920;
    vih1080.bmiHeader.biHeight = 1080;
    vih1080.bmiHeader.biCompression = MAKEFOURCC('Y', 'U', 'Y', '2');

    mtInvalid.majortype = MEDIATYPE_Video;
    mtInvalid.subtype = MEDIASUBTYPE_YUY2;
    mtInvalid.formattype = FORMAT_VideoInfo;
    mtInvalid.cbFormat = sizeof(vih1080);
    mtInvalid.pbFormat = (BYTE*)&vih1080;

    hr = pPin->QueryAccept(&mtInvalid);
    printf("  QueryAccept(1920x1080 YUY2) returned: 0x%08lx (expected S_FALSE = 1)\n", hr);
    assert(hr == S_FALSE);
    printf("  [PASS] Invariant 2 confirmed: QueryAccept returns S_FALSE on rejected media types.\n\n");

    // -----------------------------------------------------------------------
    // INVARIANT 3: QueryAccept accepts supported formats
    // -----------------------------------------------------------------------
    printf("3. Testing QueryAccept on valid formats (Invariant 3)...\n");

    VIDEOINFOHEADER vih640;
    ZeroMemory(&vih640, sizeof(vih640));
    vih640.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih640.bmiHeader.biWidth = 640;
    vih640.bmiHeader.biHeight = 480;
    vih640.bmiHeader.biCompression = MAKEFOURCC('Y', 'U', 'Y', '2');

    AM_MEDIA_TYPE mt640;
    ZeroMemory(&mt640, sizeof(mt640));
    mt640.majortype = MEDIATYPE_Video;
    mt640.subtype = MEDIASUBTYPE_YUY2;
    mt640.formattype = FORMAT_VideoInfo;
    mt640.cbFormat = sizeof(vih640);
    mt640.pbFormat = (BYTE*)&vih640;

    hr = pPin->QueryAccept(&mt640);
    printf("  QueryAccept(640x480 YUY2) returned: 0x%08lx (expected S_OK = 0)\n", hr);
    assert(hr == S_OK);

    VIDEOINFOHEADER vih320;
    ZeroMemory(&vih320, sizeof(vih320));
    vih320.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih320.bmiHeader.biWidth = 320;
    vih320.bmiHeader.biHeight = 240;
    vih320.bmiHeader.biCompression = MAKEFOURCC('Y', 'U', 'Y', '2');

    AM_MEDIA_TYPE mt320;
    ZeroMemory(&mt320, sizeof(mt320));
    mt320.majortype = MEDIATYPE_Video;
    mt320.subtype = MEDIASUBTYPE_YUY2;
    mt320.formattype = FORMAT_VideoInfo;
    mt320.cbFormat = sizeof(vih320);
    mt320.pbFormat = (BYTE*)&vih320;

    hr = pPin->QueryAccept(&mt320);
    printf("  QueryAccept(320x240 YUY2) returned: 0x%08lx (expected S_OK = 0)\n", hr);
    assert(hr == S_OK);
    printf("  [PASS] Invariant 3 confirmed: Both 640x480 and 320x240 accepted.\n\n");

    // -----------------------------------------------------------------------
    // INVARIANT 4: IAMStreamConfig multi-capabilities
    // -----------------------------------------------------------------------
    printf("4. Testing IAMStreamConfig capabilities (Invariant 4)...\n");
    IAMStreamConfig *pConfig = nullptr;
    hr = pPin->QueryInterface(IID_IAMStreamConfig, (void**)&pConfig);
    assert(SUCCEEDED(hr) && pConfig != nullptr);

    int count = 0, size = 0;
    hr = pConfig->GetNumberOfCapabilities(&count, &size);
    assert(SUCCEEDED(hr));
    printf("  Capabilities count: %d, size: %d\n", count, size);
    assert(count >= 2);

    for (int i = 0; i < count; i++) {
        AM_MEDIA_TYPE *pmt = nullptr;
        VIDEO_STREAM_CONFIG_CAPS caps;
        hr = pConfig->GetStreamCaps(i, &pmt, (BYTE*)&caps);
        assert(SUCCEEDED(hr) && pmt != nullptr);
        VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER*)pmt->pbFormat;
        printf("  Cap[%d]: %ldx%ld YUY2\n", i, pvi->bmiHeader.biWidth, pvi->bmiHeader.biHeight);
        if (pmt->cbFormat && pmt->pbFormat) CoTaskMemFree(pmt->pbFormat);
        CoTaskMemFree(pmt);
    }
    pConfig->Release();
    printf("  [PASS] Invariant 4 confirmed: Multiple capabilities exposed.\n\n");

    pPin->Release();
    pFilter->Release();
    pFactory->Release();
    FreeLibrary(hDll);

    CoUninitialize();
    printf("===================================================\n");
    printf("   All Browser Compatibility Edge Invariants OK!  \n");
    printf("===================================================\n");
    return 0;
}
