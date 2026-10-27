/* test_dshow_render.cpp — Builds a real DirectShow graph and runs the camera.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <stdio.h>

#include "../src/dshow/ps3eye_guids.h"

int main()
{
    printf("Initializing COM...\n");
    CoInitialize(NULL);

    IGraphBuilder *pGraph = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IGraphBuilder, (void**)&pGraph);
    if (FAILED(hr)) {
        printf("Failed to create FilterGraph: 0x%08lx\n", hr);
        return 1;
    }

    printf("Creating PS3EyeFilter instance...\n");
    IBaseFilter *pCamera = nullptr;
    hr = CoCreateInstance(CLSID_PS3EyeFilter, NULL, CLSCTX_INPROC_SERVER,
                          IID_IBaseFilter, (void**)&pCamera);
    if (FAILED(hr)) {
        printf("Failed to create PS3EyeFilter: 0x%08lx\n", hr);
        pGraph->Release();
        return 1;
    }

    printf("Adding PS3EyeFilter to graph...\n");
    hr = pGraph->AddFilter(pCamera, L"PS3 Eye Camera");
    if (FAILED(hr)) {
        printf("Failed AddFilter: 0x%08lx\n", hr);
        pCamera->Release();
        pGraph->Release();
        return 1;
    }

    printf("Finding Capture Pin...\n");
    IPin *pOutPin = nullptr;
    hr = pCamera->FindPin(L"Capture", &pOutPin);
    if (FAILED(hr) || !pOutPin) {
        printf("Failed FindPin: 0x%08lx\n", hr);
        pCamera->Release();
        pGraph->Release();
        return 1;
    }

    printf("Rendering pin (RenderStream / Render)...\n");
    hr = pGraph->Render(pOutPin);
    printf("pGraph->Render returned: 0x%08lx\n", hr);

    if (SUCCEEDED(hr)) {
        IMediaControl *pControl = nullptr;
        pGraph->QueryInterface(IID_IMediaControl, (void**)&pControl);
        if (pControl) {
            printf("Starting graph (Run)...\n");
            hr = pControl->Run();
            printf("pControl->Run returned: 0x%08lx\n", hr);

            printf("Running for 2 seconds...\n");
            Sleep(2000);

            printf("Stopping graph (Stop)...\n");
            pControl->Stop();
            pControl->Release();
        }
    } else {
        printf("ERROR: DirectShow could not connect/render the output pin!\n");
        printf("Error code 0x%08lx\n", hr);
    }

    pOutPin->Release();
    pCamera->Release();
    pGraph->Release();
    CoUninitialize();
    return 0;
}
