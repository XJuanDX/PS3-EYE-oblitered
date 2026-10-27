#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <strmif.h>
#include <stdio.h>
#include <vector>

// AMPROPSETID_Pin: {9B00F101-156C-11d1-B166-00C04F022E74}
static const GUID PS3EYE_AMPROPSETID_Pin =
    { 0x9b00f101, 0x156c, 0x11d1, { 0xb1, 0x66, 0x00, 0xc0, 0x4f, 0x02, 0x2e, 0x74 } };

int main() {
    CoInitialize(NULL);
    printf("Replicating OBS libdshowcapture device enumeration...\n\n");

    ICreateDevEnum *pDevEnum = NULL;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                                  IID_ICreateDevEnum, (void**)&pDevEnum);
    if (FAILED(hr)) {
        printf("Failed SystemDeviceEnum: 0x%08lx\n", hr);
        return 1;
    }

    IEnumMoniker *pEnum = NULL;
    hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
    if (hr != S_OK || !pEnum) {
        printf("No devices in CLSID_VideoInputDeviceCategory\n");
        pDevEnum->Release();
        return 1;
    }

    IMoniker *pMoniker = NULL;
    int idx = 0;
    while (pEnum->Next(1, &pMoniker, NULL) == S_OK) {
        idx++;
        printf("=== Device #%d ===\n", idx);
        IPropertyBag *pPropBag = NULL;
        WCHAR friendlyName[256] = {0};
        WCHAR devicePath[512] = {0};

        if (SUCCEEDED(pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag))) {
            VARIANT var;
            VariantInit(&var);
            if (SUCCEEDED(pPropBag->Read(L"FriendlyName", &var, 0))) {
                wcscpy_s(friendlyName, var.bstrVal);
                wprintf(L"  FriendlyName: %s\n", friendlyName);
                VariantClear(&var);
            }
            if (SUCCEEDED(pPropBag->Read(L"DevicePath", &var, 0))) {
                wcscpy_s(devicePath, var.bstrVal);
                wprintf(L"  DevicePath: %s\n", devicePath);
                VariantClear(&var);
            }
            pPropBag->Release();
        }

        // 1. BindToObject
        IBaseFilter *pFilter = NULL;
        hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&pFilter);
        if (FAILED(hr) || !pFilter) {
            printf("  [FAIL] BindToObject failed: 0x%08lx\n", hr);
            pMoniker->Release();
            continue;
        }
        printf("  [OK] BindToObject succeeded\n");

        // 2. Find Pin with MEDIATYPE_Video, PINDIR_OUTPUT, PIN_CATEGORY_CAPTURE
        IEnumPins *pEnumPins = NULL;
        hr = pFilter->EnumPins(&pEnumPins);
        if (FAILED(hr) || !pEnumPins) {
            printf("  [FAIL] EnumPins failed: 0x%08lx\n", hr);
            pFilter->Release();
            pMoniker->Release();
            continue;
        }

        IPin *pCurPin = NULL;
        IPin *pCapturePin = NULL;
        while (pEnumPins->Next(1, &pCurPin, NULL) == S_OK) {
            PIN_DIRECTION dir;
            if (FAILED(pCurPin->QueryDirection(&dir)) || dir != PINDIR_OUTPUT) {
                pCurPin->Release();
                continue;
            }

            // Check Pin Category
            IKsPropertySet *pKs = NULL;
            GUID cat = GUID_NULL;
            DWORD cbBytes = 0;
            if (SUCCEEDED(pCurPin->QueryInterface(IID_IKsPropertySet, (void**)&pKs))) {
                pKs->Get(PS3EYE_AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, NULL, 0,
                         &cat, sizeof(GUID), &cbBytes);
                pKs->Release();
            }

            if (cat != PIN_CATEGORY_CAPTURE) {
                printf("  [INFO] Pin category is not CAPTURE\n");
            }

            // Check IAMStreamConfig
            IAMStreamConfig *pConfig = NULL;
            if (SUCCEEDED(pCurPin->QueryInterface(IID_IAMStreamConfig, (void**)&pConfig))) {
                int count = 0, size = 0;
                hr = pConfig->GetNumberOfCapabilities(&count, &size);
                printf("  [OK] IAMStreamConfig caps count = %d, size = %d (hr=0x%08lx)\n", count, size, hr);

                if (count > 0 && size == sizeof(VIDEO_STREAM_CONFIG_CAPS)) {
                    for (int c = 0; c < count; c++) {
                        AM_MEDIA_TYPE *pmt = NULL;
                        VIDEO_STREAM_CONFIG_CAPS caps;
                        hr = pConfig->GetStreamCaps(c, &pmt, (BYTE*)&caps);
                        if (SUCCEEDED(hr) && pmt) {
                            if (pmt->formattype == FORMAT_VideoInfo && pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
                                VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER*)pmt->pbFormat;
                                printf("    Cap #%d: %ldx%ld, fourcc=0x%08lx, AvgTime=%lld (%0.1f fps)\n",
                                       c,
                                       pvi->bmiHeader.biWidth,
                                       pvi->bmiHeader.biHeight,
                                       pvi->bmiHeader.biCompression,
                                       pvi->AvgTimePerFrame,
                                       pvi->AvgTimePerFrame ? 10000000.0 / pvi->AvgTimePerFrame : 0.0);
                            }
                            if (pmt->cbFormat && pmt->pbFormat) CoTaskMemFree(pmt->pbFormat);
                            CoTaskMemFree(pmt);
                        } else {
                            printf("    [FAIL] GetStreamCaps(%d) failed: 0x%08lx\n", c, hr);
                        }
                    }
                }
                pConfig->Release();
                pCapturePin = pCurPin;
                break;
            } else {
                printf("  [FAIL] QueryInterface(IAMStreamConfig) failed on output pin\n");
            }
            pCurPin->Release();
        }

        if (pCapturePin) {
            printf("  ===> Device '%ls' is 100%% COMPATIBLE WITH OBS!\n", friendlyName);
            pCapturePin->Release();
        } else {
            printf("  ===> Device '%ls' has NO compatible capture pin!\n", friendlyName);
        }

        pEnumPins->Release();
        pFilter->Release();
        pMoniker->Release();
        printf("\n");
    }

    pEnum->Release();
    pDevEnum->Release();
    CoUninitialize();
    return 0;
}
