#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

int main() {
    HRESULT hrCo = CoInitialize(NULL);
    printf("CoInitialize: 0x%08lx\n", hrCo);

    ICreateDevEnum *pDevEnum = NULL;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                                  IID_ICreateDevEnum, (void**)&pDevEnum);
    if (FAILED(hr)) {
        printf("Failed to create SystemDeviceEnum: 0x%08lx\n", hr);
        return 1;
    }
    IEnumMoniker *pEnum = NULL;
    hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
    if (hr != S_OK || !pEnum) {
        printf("CreateClassEnumerator returned 0x%08lx (no devices or error)\n", hr);
        pDevEnum->Release();
        return 1;
    }

    IMoniker *pMoniker = NULL;
    int idx = 0;
    while (pEnum->Next(1, &pMoniker, NULL) == S_OK) {
        idx++;
        wprintf(L"\n--- Device #%d ---\n", idx);
        IPropertyBag *pPropBag = NULL;
        hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag);
        if (SUCCEEDED(hr)) {
            VARIANT var;
            VariantInit(&var);
            if (SUCCEEDED(pPropBag->Read(L"FriendlyName", &var, 0))) {
                wprintf(L"FriendlyName: %s\n", var.bstrVal);
                VariantClear(&var);
            }
            if (SUCCEEDED(pPropBag->Read(L"CLSID", &var, 0))) {
                wprintf(L"CLSID: %s\n", var.bstrVal);
                VariantClear(&var);
            }
            if (SUCCEEDED(pPropBag->Read(L"DevicePath", &var, 0))) {
                wprintf(L"DevicePath: %s\n", var.bstrVal);
                VariantClear(&var);
            }
            pPropBag->Release();
        }

        // Test OBS-style filter instantiation
        IBaseFilter *pFilter = NULL;
        hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&pFilter);
        if (SUCCEEDED(hr) && pFilter) {
            printf("BindToObject(IBaseFilter): SUCCESS (0x%08lx)\n", hr);
            pFilter->Release();
        } else {
            printf("BindToObject(IBaseFilter): FAILED (0x%08lx)\n", hr);
        }
        pMoniker->Release();
    }

    pEnum->Release();
    pDevEnum->Release();
    CoUninitialize();
    return 0;
}
