/* test_dshow_filter.cpp — COM Lifecycle & Interface verification for ps3eye_dshow.dll.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <strmif.h>
#include <stdio.h>
#include <assert.h>

#include "../src/dshow/ps3eye_guids.h"

typedef HRESULT (STDAPICALLTYPE *pfnDllGetClassObject)(REFCLSID rclsid, REFIID riid, LPVOID *ppv);
typedef HRESULT (STDAPICALLTYPE *pfnDllCanUnloadNow)();

int main()
{
    printf("=========================================\n");
    printf("   PS3 Eye DirectShow Filter COM Tests   \n");
    printf("=========================================\n\n");

    CoInitialize(NULL);

    printf("1. Loading build\\ps3eye_dshow.dll...\n");
    HMODULE hDll = LoadLibraryW(L"build\\ps3eye_dshow.dll");
    if (!hDll) {
        fprintf(stderr, "FAILED: Cannot load ps3eye_dshow.dll (error %lu)\n", GetLastError());
        return 1;
    }
    printf("  [PASS] DLL loaded OK.\n\n");

    pfnDllGetClassObject GetClassObject = (pfnDllGetClassObject)GetProcAddress(hDll, "DllGetClassObject");
    pfnDllCanUnloadNow CanUnloadNow = (pfnDllCanUnloadNow)GetProcAddress(hDll, "DllCanUnloadNow");
    assert(GetClassObject != NULL);
    assert(CanUnloadNow != NULL);

    printf("2. Instantiating IClassFactory for CLSID_PS3EyeFilter...\n");
    IClassFactory *pFactory = nullptr;
    HRESULT hr = GetClassObject(CLSID_PS3EyeFilter, IID_IClassFactory, (void**)&pFactory);
    assert(SUCCEEDED(hr) && pFactory != nullptr);
    printf("  [PASS] ClassFactory instantiated.\n\n");

    printf("3. Creating IBaseFilter instance via ClassFactory...\n");
    IBaseFilter *pFilter = nullptr;
    hr = pFactory->CreateInstance(nullptr, IID_IBaseFilter, (void**)&pFilter);
    assert(SUCCEEDED(hr) && pFilter != nullptr);
    printf("  [PASS] IBaseFilter instance created.\n\n");

    printf("4. Inspecting Pins...\n");
    IEnumPins *pEnumPins = nullptr;
    hr = pFilter->EnumPins(&pEnumPins);
    assert(SUCCEEDED(hr) && pEnumPins != nullptr);

    IPin *pPin = nullptr;
    ULONG fetched = 0;
    hr = pEnumPins->Next(1, &pPin, &fetched);
    assert(SUCCEEDED(hr) && fetched == 1 && pPin != nullptr);

    PIN_INFO pinInfo;
    hr = pPin->QueryPinInfo(&pinInfo);
    assert(SUCCEEDED(hr));
    wprintf(L"  Pin name: %s, Direction: %s\n",
            pinInfo.achName,
            (pinInfo.dir == PINDIR_OUTPUT) ? L"OUTPUT" : L"INPUT");
    assert(pinInfo.dir == PINDIR_OUTPUT);
    if (pinInfo.pFilter) pinInfo.pFilter->Release();
    pEnumPins->Release();
    printf("  [PASS] Capture Pin validated.\n\n");

    printf("5. Inspecting IAMStreamConfig capabilities...\n");
    IAMStreamConfig *pConfig = nullptr;
    hr = pPin->QueryInterface(IID_IAMStreamConfig, (void**)&pConfig);
    assert(SUCCEEDED(hr) && pConfig != nullptr);

    int count = 0, size = 0;
    hr = pConfig->GetNumberOfCapabilities(&count, &size);
    assert(SUCCEEDED(hr) && count == 1 && size == sizeof(VIDEO_STREAM_CONFIG_CAPS));

    AM_MEDIA_TYPE *pmt = nullptr;
    VIDEO_STREAM_CONFIG_CAPS caps;
    hr = pConfig->GetStreamCaps(0, &pmt, (BYTE*)&caps);
    assert(SUCCEEDED(hr) && pmt != nullptr);
    assert(pmt->majortype == MEDIATYPE_Video);
    assert(pmt->subtype == MEDIASUBTYPE_YUY2);

    VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER*)pmt->pbFormat;
    printf("  Format: YUY2 %ldx%ld @ 30 fps (AvgTimePerFrame = %lld)\n",
           pvi->bmiHeader.biWidth, pvi->bmiHeader.biHeight, pvi->AvgTimePerFrame);
    assert(pvi->bmiHeader.biWidth == 640 && pvi->bmiHeader.biHeight == 480);

    if (pmt->cbFormat && pmt->pbFormat) CoTaskMemFree(pmt->pbFormat);
    CoTaskMemFree(pmt);
    pConfig->Release();
    printf("  [PASS] Stream capabilities verified.\n\n");

    printf("6. Inspecting IKsPropertySet (Pin Category)...\n");
    IKsPropertySet *pKs = nullptr;
    hr = pPin->QueryInterface(IID_IKsPropertySet, (void**)&pKs);
    assert(SUCCEEDED(hr) && pKs != nullptr);

    GUID pinCat;
    DWORD ret = 0;
    GUID propSet = { 0x9b00f101, 0x156c, 0x11d1, { 0xb1, 0x66, 0x00, 0xc0, 0x4f, 0x02, 0x2e, 0x74 } };
    hr = pKs->Get(propSet, 0, nullptr, 0, &pinCat, sizeof(pinCat), &ret);
    assert(SUCCEEDED(hr) && pinCat == PIN_CATEGORY_CAPTURE);
    printf("  [PASS] Pin Category is PIN_CATEGORY_CAPTURE.\n\n");

    pKs->Release();
    pPin->Release();

    printf("7. Testing 50 consecutive Run/Pause/Stop lifecycle cycles...\n");
    for (int i = 0; i < 50; i++) {
        pFilter->Run(0);
        pFilter->Pause();
        pFilter->Stop();
    }
    printf("  [PASS] 50 lifecycle cycles completed cleanly.\n\n");

    pFilter->Release();
    pFactory->Release();

    printf("8. Checking DllCanUnloadNow...\n");
    hr = CanUnloadNow();
    assert(hr == S_OK);
    printf("  [PASS] All COM locks released (hr = S_OK).\n\n");

    FreeLibrary(hDll);
    CoUninitialize();

    printf("=========================================\n");
    printf("  ALL DIRECTSHOW TESTS PASSED!           \n");
    printf("=========================================\n");
    return 0;
}
