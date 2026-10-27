/* dll_main.cpp — COM server entry points and registration for ps3eye_dshow.dll.
 */
#include "ps3eye_filter.h"
#include <shlwapi.h>

static HINSTANCE g_hInst = NULL;

// Helper: Set registry string value
static bool SetRegString(HKEY hRoot, const wchar_t *subKey, const wchar_t *valName, const wchar_t *valData)
{
    HKEY hKey;
    if (RegCreateKeyExW(hRoot, subKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return false;
    RegSetValueExW(hKey, valName, 0, REG_SZ, (const BYTE*)valData, (DWORD)(wcslen(valData) + 1) * sizeof(WCHAR));
    RegCloseKey(hKey);
    return true;
}

// ---------------------------------------------------------------------------
// Class Factory
// ---------------------------------------------------------------------------

class PS3EyeClassFactory : public IClassFactory
{
    std::atomic<long> m_cRef{1};
public:
    PS3EyeClassFactory() { InterlockedIncrement(&g_cLock); }
    ~PS3EyeClassFactory() { InterlockedDecrement(&g_cLock); }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_cRef; }
    STDMETHODIMP_(ULONG) Release() override {
        long c = --m_cRef;
        if (c == 0) delete this;
        return c;
    }
    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv) override {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        if (!ppv) return E_POINTER;
        PS3EyeFilter *pFilter = new PS3EyeFilter();
        if (!pFilter) return E_OUTOFMEMORY;
        HRESULT hr = pFilter->QueryInterface(riid, ppv);
        pFilter->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) InterlockedIncrement(&g_cLock);
        else InterlockedDecrement(&g_cLock);
        return S_OK;
    }
};

// ---------------------------------------------------------------------------
// DLL Entry Point
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInst = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// COM Exports
// ---------------------------------------------------------------------------

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid == CLSID_PS3EyeFilter) {
        PS3EyeClassFactory *pFactory = new PS3EyeClassFactory();
        if (!pFactory) return E_OUTOFMEMORY;
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow()
{
    return (g_cLock == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    WCHAR szPath[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, szPath, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());

    const wchar_t *szClsid = L"{E85B697D-4F12-4C67-B029-7928C3D82490}";
    const wchar_t *szCategory = L"{860BB310-5D01-11d0-BD3B-00A0C911CE86}";
    const wchar_t *szName = L"PS3 Eye Camera";

    // 1. Register InprocServer32 under HKCR\CLSID
    WCHAR szKey[512];
    swprintf_s(szKey, L"CLSID\\%s", szClsid);
    SetRegString(HKEY_CLASSES_ROOT, szKey, NULL, szName);

    swprintf_s(szKey, L"CLSID\\%s\\InprocServer32", szClsid);
    SetRegString(HKEY_CLASSES_ROOT, szKey, NULL, szPath);
    SetRegString(HKEY_CLASSES_ROOT, szKey, L"ThreadingModel", L"Both");

    // 2. Register under DirectShow Video Input Device Category
    swprintf_s(szKey, L"CLSID\\%s\\Instance\\%s", szCategory, szClsid);
    SetRegString(HKEY_CLASSES_ROOT, szKey, L"FriendlyName", szName);
    SetRegString(HKEY_CLASSES_ROOT, szKey, L"CLSID", szClsid);

    // 3. Register via IFilterMapper2 for native DirectShow enumerator
    HRESULT hrCo = CoInitialize(NULL);
    IFilterMapper2 *pMapper = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IFilterMapper2, (void**)&pMapper);
    if (SUCCEEDED(hr) && pMapper) {
        REGPINTYPES regTypes = { &MEDIATYPE_Video, &MEDIASUBTYPE_YUY2 };
        REGFILTERPINS2 regPin = {
            REG_PINFLAG_B_OUTPUT,
            1,
            1,
            &regTypes,
            0,
            NULL,
            &PIN_CATEGORY_CAPTURE
        };
        REGFILTER2 regFilter = {
            2,
            MERIT_DO_NOT_USE,
            1,
            (const REGFILTERPINS*)&regPin
        };
        pMapper->RegisterFilter(CLSID_PS3EyeFilter, szName, NULL, &CLSID_VideoInputDeviceCategory, szName, &regFilter);
        pMapper->Release();
    }
    if (SUCCEEDED(hrCo)) CoUninitialize();

    return S_OK;
}

STDAPI DllUnregisterServer()
{
    const wchar_t *szClsid = L"{E85B697D-4F12-4C67-B029-7928C3D82490}";
    const wchar_t *szCategory = L"{860BB310-5D01-11d0-BD3B-00A0C911CE86}";
    const wchar_t *szName = L"PS3 Eye Camera";

    // 1. Unregister via IFilterMapper2
    HRESULT hrCo = CoInitialize(NULL);
    IFilterMapper2 *pMapper = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
                                   IID_IFilterMapper2, (void**)&pMapper)) && pMapper) {
        pMapper->UnregisterFilter(&CLSID_VideoInputDeviceCategory, szName, CLSID_PS3EyeFilter);
        pMapper->Release();
    }
    if (SUCCEEDED(hrCo)) CoUninitialize();

    // 2. Remove DirectShow Video Input Device Category instance
    WCHAR szKey[512];
    swprintf_s(szKey, L"CLSID\\%s\\Instance\\%s", szCategory, szClsid);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, szKey);

    // 3. Remove InprocServer32 and CLSID
    swprintf_s(szKey, L"CLSID\\%s", szClsid);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, szKey);

    return S_OK;
}
