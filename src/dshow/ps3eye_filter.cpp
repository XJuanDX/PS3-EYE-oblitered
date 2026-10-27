/* ps3eye_filter.cpp — DirectShow Video Capture Source Filter implementation.
 */
#include "ps3eye_filter.h"
#include <cstdio>

// AMPROPSETID_Pin GUID: {9B00F101-156C-11d1-B166-00C04F022E74}
static const GUID PS3EYE_AMPROPSETID_Pin = 
    { 0x9b00f101, 0x156c, 0x11d1, { 0xb1, 0x66, 0x00, 0xc0, 0x4f, 0x02, 0x2e, 0x74 } };

// Global DLL lock count
LONG g_cLock = 0;

// Helper: Free an AM_MEDIA_TYPE structure
void FreeMediaType(AM_MEDIA_TYPE *pmt)
{
    if (!pmt) return;
    if (pmt->cbFormat != 0 && pmt->pbFormat != nullptr) {
        CoTaskMemFree(pmt->pbFormat);
        pmt->pbFormat = nullptr;
        pmt->cbFormat = 0;
    }
    if (pmt->pUnk) {
        pmt->pUnk->Release();
        pmt->pUnk = nullptr;
    }
}

// Helper: Fill an AM_MEDIA_TYPE with 640x480 YUY2 @ 30fps
HRESULT CreateMediaType(AM_MEDIA_TYPE *pmt)
{
    if (!pmt) return E_POINTER;
    ZeroMemory(pmt, sizeof(AM_MEDIA_TYPE));

    VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)CoTaskMemAlloc(sizeof(VIDEOINFOHEADER));
    if (!pvi) return E_OUTOFMEMORY;
    ZeroMemory(pvi, sizeof(VIDEOINFOHEADER));

    pvi->rcSource.left   = 0;
    pvi->rcSource.top    = 0;
    pvi->rcSource.right  = 640;
    pvi->rcSource.bottom = 480;
    pvi->rcTarget = pvi->rcSource;

    pvi->dwBitRate = 640 * 480 * 16 * 30;
    pvi->AvgTimePerFrame = 333333; // 30 fps (100ns units)

    BITMAPINFOHEADER &bmi = pvi->bmiHeader;
    bmi.biSize = sizeof(BITMAPINFOHEADER);
    bmi.biWidth = 640;
    bmi.biHeight = 480;
    bmi.biPlanes = 1;
    bmi.biBitCount = 16;
    bmi.biCompression = MAKEFOURCC('Y', 'U', 'Y', '2');
    bmi.biSizeImage = 640 * 480 * 2;

    pmt->majortype = MEDIATYPE_Video;
    pmt->subtype = MEDIASUBTYPE_YUY2;
    pmt->bFixedSizeSamples = TRUE;
    pmt->bTemporalCompression = FALSE;
    pmt->lSampleSize = bmi.biSizeImage;
    pmt->formattype = FORMAT_VideoInfo;
    pmt->pUnk = nullptr;
    pmt->cbFormat = sizeof(VIDEOINFOHEADER);
    pmt->pbFormat = (BYTE *)pvi;

    return S_OK;
}

HRESULT CopyMediaType(AM_MEDIA_TYPE *pmtTarget, const AM_MEDIA_TYPE *pmtSource)
{
    if (!pmtTarget || !pmtSource) return E_POINTER;
    *pmtTarget = *pmtSource;
    if (pmtSource->cbFormat && pmtSource->pbFormat) {
        pmtTarget->pbFormat = (BYTE *)CoTaskMemAlloc(pmtSource->cbFormat);
        if (!pmtTarget->pbFormat) return E_OUTOFMEMORY;
        memcpy(pmtTarget->pbFormat, pmtSource->pbFormat, pmtSource->cbFormat);
    }
    if (pmtTarget->pUnk) {
        pmtTarget->pUnk->AddRef();
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// Enumerators
// ---------------------------------------------------------------------------

class CEnumPins : public IEnumPins
{
    std::atomic<long> m_cRef{1};
    IPin             *m_pPin;
    ULONG             m_iIndex{0};
public:
    CEnumPins(IPin *pPin, ULONG idx = 0) : m_pPin(pPin), m_iIndex(idx) {
        if (m_pPin) m_pPin->AddRef();
    }
    ~CEnumPins() {
        if (m_pPin) m_pPin->Release();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumPins) {
            *ppv = static_cast<IEnumPins*>(this);
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
    STDMETHODIMP Next(ULONG cPins, IPin **ppPins, ULONG *pcFetched) override {
        if (!ppPins) return E_POINTER;
        if (cPins > 1 && !pcFetched) return E_INVALIDARG;
        ULONG fetched = 0;
        if (m_iIndex == 0 && cPins > 0) {
            ppPins[0] = m_pPin;
            m_pPin->AddRef();
            m_iIndex++;
            fetched = 1;
        }
        if (pcFetched) *pcFetched = fetched;
        return (fetched == cPins) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG cPins) override {
        m_iIndex += cPins;
        return (m_iIndex <= 1) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override {
        m_iIndex = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumPins **ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new CEnumPins(m_pPin, m_iIndex);
        return S_OK;
    }
};

class CEnumMediaTypes : public IEnumMediaTypes
{
    std::atomic<long> m_cRef{1};
    ULONG             m_iIndex{0};
public:
    CEnumMediaTypes(ULONG idx = 0) : m_iIndex(idx) {}
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumMediaTypes) {
            *ppv = static_cast<IEnumMediaTypes*>(this);
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
    STDMETHODIMP Next(ULONG cMediaTypes, AM_MEDIA_TYPE **ppMediaTypes, ULONG *pcFetched) override {
        if (!ppMediaTypes) return E_POINTER;
        if (cMediaTypes > 1 && !pcFetched) return E_INVALIDARG;
        ULONG fetched = 0;
        if (m_iIndex == 0 && cMediaTypes > 0) {
            AM_MEDIA_TYPE *pmt = (AM_MEDIA_TYPE*)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
            if (pmt && SUCCEEDED(CreateMediaType(pmt))) {
                ppMediaTypes[0] = pmt;
                m_iIndex++;
                fetched = 1;
            }
        }
        if (pcFetched) *pcFetched = fetched;
        return (fetched == cMediaTypes) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Skip(ULONG cMediaTypes) override {
        m_iIndex += cMediaTypes;
        return (m_iIndex <= 1) ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override {
        m_iIndex = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumMediaTypes **ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new CEnumMediaTypes(m_iIndex);
        return S_OK;
    }
};

// ---------------------------------------------------------------------------
// PS3EyePin Implementation
// ---------------------------------------------------------------------------

PS3EyePin::PS3EyePin(PS3EyeFilter *pFilter)
    : m_cRef(1)
    , m_pFilter(pFilter)
    , m_pConnectedPin(nullptr)
    , m_pMemInputPin(nullptr)
    , m_pAllocator(nullptr)
    , m_bConnected(false)
    , m_bActive(false)
    , m_hThread(nullptr)
    , m_hStopEvent(nullptr)
{
    InitializeCriticalSection(&m_csPin);
    ZeroMemory(&m_mtCurrent, sizeof(m_mtCurrent));
    CreateMediaType(&m_mtCurrent);
    m_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
}

PS3EyePin::~PS3EyePin()
{
    Disconnect();
    FreeMediaType(&m_mtCurrent);
    if (m_hStopEvent) {
        CloseHandle(m_hStopEvent);
        m_hStopEvent = nullptr;
    }
    DeleteCriticalSection(&m_csPin);
}

STDMETHODIMP PS3EyePin::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPin) {
        *ppv = static_cast<IPin*>(this);
    } else if (riid == IID_IAMStreamConfig) {
        *ppv = static_cast<IAMStreamConfig*>(this);
    } else if (riid == IID_IKsPropertySet) {
        *ppv = static_cast<IKsPropertySet*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) PS3EyePin::AddRef()
{
    return ++m_cRef;
}

STDMETHODIMP_(ULONG) PS3EyePin::Release()
{
    long c = --m_cRef;
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP PS3EyePin::Connect(IPin *pReceivePin, const AM_MEDIA_TYPE *pmt)
{
    if (!pReceivePin) return E_POINTER;
    EnterCriticalSection(&m_csPin);
    if (m_bConnected) {
        LeaveCriticalSection(&m_csPin);
        return VFW_E_ALREADY_CONNECTED;
    }

    AM_MEDIA_TYPE mt;
    ZeroMemory(&mt, sizeof(mt));
    if (pmt) {
        if (FAILED(QueryAccept(pmt))) {
            LeaveCriticalSection(&m_csPin);
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
        CopyMediaType(&mt, pmt);
    } else {
        CreateMediaType(&mt);
    }

    HRESULT hr = pReceivePin->ReceiveConnection(this, &mt);
    if (SUCCEEDED(hr)) {
        m_pConnectedPin = pReceivePin;
        m_pConnectedPin->AddRef();
        FreeMediaType(&m_mtCurrent);
        CopyMediaType(&m_mtCurrent, &mt);
        m_bConnected = true;

        pReceivePin->QueryInterface(IID_IMemInputPin, (void**)&m_pMemInputPin);
    }
    FreeMediaType(&mt);
    LeaveCriticalSection(&m_csPin);
    return hr;
}

STDMETHODIMP PS3EyePin::ReceiveConnection(IPin*, const AM_MEDIA_TYPE*)
{
    return E_UNEXPECTED; // Output pin does not receive connection
}

STDMETHODIMP PS3EyePin::Disconnect()
{
    EnterCriticalSection(&m_csPin);
    if (m_bActive) {
        LeaveCriticalSection(&m_csPin);
        return VFW_E_NOT_STOPPED;
    }
    if (m_pMemInputPin) {
        m_pMemInputPin->Release();
        m_pMemInputPin = nullptr;
    }
    if (m_pAllocator) {
        m_pAllocator->Release();
        m_pAllocator = nullptr;
    }
    if (m_pConnectedPin) {
        m_pConnectedPin->Release();
        m_pConnectedPin = nullptr;
    }
    m_bConnected = false;
    LeaveCriticalSection(&m_csPin);
    return S_OK;
}

STDMETHODIMP PS3EyePin::ConnectedTo(IPin **ppPin)
{
    if (!ppPin) return E_POINTER;
    EnterCriticalSection(&m_csPin);
    if (!m_bConnected || !m_pConnectedPin) {
        LeaveCriticalSection(&m_csPin);
        return VFW_E_NOT_CONNECTED;
    }
    *ppPin = m_pConnectedPin;
    (*ppPin)->AddRef();
    LeaveCriticalSection(&m_csPin);
    return S_OK;
}

STDMETHODIMP PS3EyePin::ConnectionMediaType(AM_MEDIA_TYPE *pmt)
{
    if (!pmt) return E_POINTER;
    EnterCriticalSection(&m_csPin);
    if (!m_bConnected) {
        LeaveCriticalSection(&m_csPin);
        return VFW_E_NOT_CONNECTED;
    }
    HRESULT hr = CopyMediaType(pmt, &m_mtCurrent);
    LeaveCriticalSection(&m_csPin);
    return hr;
}

STDMETHODIMP PS3EyePin::QueryPinInfo(PIN_INFO *pInfo)
{
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = m_pFilter;
    if (m_pFilter) m_pFilter->AddRef();
    pInfo->dir = PINDIR_OUTPUT;
    wcscpy_s(pInfo->achName, L"Capture");
    return S_OK;
}

STDMETHODIMP PS3EyePin::QueryDirection(PIN_DIRECTION *pPinDir)
{
    if (!pPinDir) return E_POINTER;
    *pPinDir = PINDIR_OUTPUT;
    return S_OK;
}

STDMETHODIMP PS3EyePin::QueryId(LPWSTR *Id)
{
    if (!Id) return E_POINTER;
    size_t len = sizeof(L"Capture");
    *Id = (LPWSTR)CoTaskMemAlloc(len);
    if (!*Id) return E_OUTOFMEMORY;
    wcscpy_s(*Id, len / sizeof(WCHAR), L"Capture");
    return S_OK;
}

STDMETHODIMP PS3EyePin::QueryAccept(const AM_MEDIA_TYPE *pmt)
{
    if (!pmt) return E_POINTER;
    if (pmt->majortype != MEDIATYPE_Video) return VFW_E_INVALIDMEDIATYPE;
    if (pmt->subtype != MEDIASUBTYPE_YUY2) return VFW_E_INVALIDMEDIATYPE;
    if (pmt->formattype != FORMAT_VideoInfo || !pmt->pbFormat) return VFW_E_INVALIDMEDIATYPE;

    VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER*)pmt->pbFormat;
    if (pvi->bmiHeader.biWidth != 640 || abs(pvi->bmiHeader.biHeight) != 480) {
        return VFW_E_INVALIDMEDIATYPE;
    }
    return S_OK;
}

STDMETHODIMP PS3EyePin::EnumMediaTypes(IEnumMediaTypes **ppEnum)
{
    if (!ppEnum) return E_POINTER;
    *ppEnum = new CEnumMediaTypes();
    return S_OK;
}

STDMETHODIMP PS3EyePin::QueryInternalConnections(IPin**, ULONG *nPin)
{
    if (!nPin) return E_POINTER;
    *nPin = 0;
    return S_OK;
}

STDMETHODIMP PS3EyePin::EndOfStream() { return S_OK; }
STDMETHODIMP PS3EyePin::BeginFlush() { return S_OK; }
STDMETHODIMP PS3EyePin::EndFlush() { return S_OK; }
STDMETHODIMP PS3EyePin::NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) { return S_OK; }

// IAMStreamConfig
STDMETHODIMP PS3EyePin::SetFormat(AM_MEDIA_TYPE *pmt)
{
    if (!pmt) return E_POINTER;
    if (FAILED(QueryAccept(pmt))) return VFW_E_INVALIDMEDIATYPE;
    EnterCriticalSection(&m_csPin);
    if (m_bActive) {
        LeaveCriticalSection(&m_csPin);
        return VFW_E_NOT_STOPPED;
    }
    FreeMediaType(&m_mtCurrent);
    CopyMediaType(&m_mtCurrent, pmt);
    LeaveCriticalSection(&m_csPin);
    return S_OK;
}

STDMETHODIMP PS3EyePin::GetFormat(AM_MEDIA_TYPE **ppmt)
{
    if (!ppmt) return E_POINTER;
    *ppmt = (AM_MEDIA_TYPE*)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
    if (!*ppmt) return E_OUTOFMEMORY;
    EnterCriticalSection(&m_csPin);
    HRESULT hr = CopyMediaType(*ppmt, &m_mtCurrent);
    LeaveCriticalSection(&m_csPin);
    return hr;
}

STDMETHODIMP PS3EyePin::GetNumberOfCapabilities(int *piCount, int *piSize)
{
    if (!piCount || !piSize) return E_POINTER;
    *piCount = 1;
    *piSize  = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

STDMETHODIMP PS3EyePin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE **ppmt, BYTE *pSCC)
{
    if (!ppmt || !pSCC) return E_POINTER;
    if (iIndex != 0) return S_FALSE;

    *ppmt = (AM_MEDIA_TYPE*)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
    if (!*ppmt) return E_OUTOFMEMORY;
    CreateMediaType(*ppmt);

    VIDEO_STREAM_CONFIG_CAPS *pCaps = (VIDEO_STREAM_CONFIG_CAPS*)pSCC;
    ZeroMemory(pCaps, sizeof(VIDEO_STREAM_CONFIG_CAPS));
    pCaps->guid = FORMAT_VideoInfo;
    pCaps->VideoStandard = AnalogVideo_None;
    pCaps->InputSize.cx = 640; pCaps->InputSize.cy = 480;
    pCaps->MinCroppingSize = pCaps->InputSize;
    pCaps->MaxCroppingSize = pCaps->InputSize;
    pCaps->CropGranularityX = 1; pCaps->CropGranularityY = 1;
    pCaps->CropAlignX = 1; pCaps->CropAlignY = 1;
    pCaps->MinOutputSize = pCaps->InputSize;
    pCaps->MaxOutputSize = pCaps->InputSize;
    pCaps->OutputGranularityX = 1; pCaps->OutputGranularityY = 1;
    pCaps->MinFrameInterval = 333333;
    pCaps->MaxFrameInterval = 333333;
    pCaps->MinBitsPerSecond = 640 * 480 * 16 * 30;
    pCaps->MaxBitsPerSecond = pCaps->MinBitsPerSecond;

    return S_OK;
}

// IKsPropertySet
STDMETHODIMP PS3EyePin::Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID, DWORD, LPVOID pPropData, DWORD cbPropData, DWORD *pcbReturned)
{
    if (guidPropSet == PS3EYE_AMPROPSETID_Pin && dwPropID == 0) {
        if (pcbReturned) *pcbReturned = sizeof(GUID);
        if (!pPropData) return S_OK;
        if (cbPropData < sizeof(GUID)) return E_UNEXPECTED;
        *(GUID*)pPropData = PIN_CATEGORY_CAPTURE;
        return S_OK;
    }
    return E_PROP_SET_UNSUPPORTED;
}

STDMETHODIMP PS3EyePin::Set(REFGUID, DWORD, LPVOID, DWORD, LPVOID, DWORD)
{
    return E_NOTIMPL;
}

STDMETHODIMP PS3EyePin::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD *pTypeSupport)
{
    if (guidPropSet == PS3EYE_AMPROPSETID_Pin && dwPropID == 0) {
        if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
        return S_OK;
    }
    return E_PROP_SET_UNSUPPORTED;
}

// Streaming Control
HRESULT PS3EyePin::Active()
{
    EnterCriticalSection(&m_csPin);
    if (!m_bConnected || !m_pMemInputPin) {
        LeaveCriticalSection(&m_csPin);
        return S_OK;
    }

    if (!m_pAllocator) {
        HRESULT hr = m_pMemInputPin->GetAllocator(&m_pAllocator);
        if (FAILED(hr) || !m_pAllocator) {
            hr = CoCreateInstance(CLSID_MemoryAllocator, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IMemAllocator, (void**)&m_pAllocator);
            if (FAILED(hr)) {
                LeaveCriticalSection(&m_csPin);
                return hr;
            }
        }
        ALLOCATOR_PROPERTIES prop = { 3, 614400, 1, 0 };
        ALLOCATOR_PROPERTIES actual;
        m_pAllocator->SetProperties(&prop, &actual);
        m_pMemInputPin->NotifyAllocator(m_pAllocator, FALSE);
    }

    m_pAllocator->Commit();

    ResetEvent(m_hStopEvent);
    m_bActive = true;
    m_hThread = CreateThread(NULL, 0, ThreadProc, this, 0, NULL);
    LeaveCriticalSection(&m_csPin);
    return S_OK;
}

HRESULT PS3EyePin::Inactive()
{
    EnterCriticalSection(&m_csPin);
    m_bActive = false;
    SetEvent(m_hStopEvent);

    if (m_pAllocator) {
        m_pAllocator->Decommit();
    }

    if (m_hThread) {
        WaitForSingleObject(m_hThread, 2000);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }

    LeaveCriticalSection(&m_csPin);
    return S_OK;
}

DWORD WINAPI PS3EyePin::ThreadProc(LPVOID pParam)
{
    PS3EyePin *pThis = (PS3EyePin*)pParam;
    pThis->DoBufferProcessingLoop();
    return 0;
}

void PS3EyePin::DoBufferProcessingLoop()
{
    REFERENCE_TIME rtFrameDuration = 333333; // 30 fps
    uint64_t frameIndex = 0;
    capture_session *session = m_pFilter->GetSession();

    while (m_bActive) {
        if (WaitForSingleObject(m_hStopEvent, 0) == WAIT_OBJECT_0) {
            break;
        }

        IMediaSample *pSample = nullptr;
        HRESULT hr = m_pAllocator->GetBuffer(&pSample, nullptr, nullptr, 0);
        if (FAILED(hr)) {
            if (hr == VFW_E_NOT_COMMITTED) break;
            Sleep(5);
            continue;
        }

        BYTE *pDst = nullptr;
        pSample->GetPointer(&pDst);
        long cbSize = pSample->GetSize();

        uint64_t qpc = 0;
        if (capture_session_get_frame(session, pDst, cbSize, &qpc, 100)) {
            REFERENCE_TIME rtStart = frameIndex * rtFrameDuration;
            REFERENCE_TIME rtEnd   = rtStart + rtFrameDuration;
            pSample->SetTime(&rtStart, &rtEnd);
            pSample->SetSyncPoint(TRUE);
            pSample->SetActualDataLength(614400);

            hr = m_pMemInputPin->Receive(pSample);
            pSample->Release();
            frameIndex++;

            if (hr != S_OK) {
                if (hr == S_FALSE) Sleep(10);
            }
        } else {
            pSample->Release();
            if (capture_session_is_device_lost(session)) {
                m_pFilter->NotifyEvent(EC_DEVICE_LOST, 0, 0);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// PS3EyeFilter Implementation
// ---------------------------------------------------------------------------

PS3EyeFilter::PS3EyeFilter()
    : m_cRef(1)
    , m_state(State_Stopped)
    , m_pClock(nullptr)
    , m_pGraph(nullptr)
    , m_pEventSink(nullptr)
    , m_pPin(nullptr)
    , m_bSessionOpened(false)
{
    InitializeCriticalSection(&m_csFilter);
    wcscpy_s(m_wszName, L"PS3 Eye Camera");
    m_pPin = new PS3EyePin(this);

    char err[256];
    if (capture_session_open(&m_session, PS3EYE_MODE_VGA_640x480, 30, err, sizeof(err))) {
        m_bSessionOpened = true;
    }
    InterlockedIncrement(&g_cLock);
}

PS3EyeFilter::~PS3EyeFilter()
{
    Stop();
    if (m_bSessionOpened) {
        capture_session_close(&m_session);
        m_bSessionOpened = false;
    }
    if (m_pPin) {
        m_pPin->Release();
        m_pPin = nullptr;
    }
    if (m_pClock) {
        m_pClock->Release();
        m_pClock = nullptr;
    }
    if (m_pEventSink) {
        m_pEventSink->Release();
        m_pEventSink = nullptr;
    }
    DeleteCriticalSection(&m_csFilter);
    InterlockedDecrement(&g_cLock);
}

STDMETHODIMP PS3EyeFilter::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPersist || riid == IID_IMediaFilter || riid == IID_IBaseFilter) {
        *ppv = static_cast<IBaseFilter*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) PS3EyeFilter::AddRef()
{
    return ++m_cRef;
}

STDMETHODIMP_(ULONG) PS3EyeFilter::Release()
{
    long c = --m_cRef;
    if (c == 0) delete this;
    return c;
}

STDMETHODIMP PS3EyeFilter::GetClassID(CLSID *pClassID)
{
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_PS3EyeFilter;
    return S_OK;
}

STDMETHODIMP PS3EyeFilter::Stop()
{
    EnterCriticalSection(&m_csFilter);
    if (m_state != State_Stopped) {
        if (m_pPin) m_pPin->Inactive();
        if (m_bSessionOpened) capture_session_stop(&m_session);
        m_state = State_Stopped;
    }
    LeaveCriticalSection(&m_csFilter);
    return S_OK;
}

STDMETHODIMP PS3EyeFilter::Pause()
{
    EnterCriticalSection(&m_csFilter);
    if (m_state == State_Stopped) {
        char err[256];
        if (m_bSessionOpened) {
            capture_session_start(&m_session, err, sizeof(err));
        }
        if (m_pPin) m_pPin->Active();
    }
    m_state = State_Paused;
    LeaveCriticalSection(&m_csFilter);
    return S_OK;
}

STDMETHODIMP PS3EyeFilter::Run(REFERENCE_TIME)
{
    EnterCriticalSection(&m_csFilter);
    if (m_state == State_Stopped) {
        Pause();
    }
    m_state = State_Running;
    LeaveCriticalSection(&m_csFilter);
    return S_OK;
}

STDMETHODIMP PS3EyeFilter::GetState(DWORD, FILTER_STATE *State)
{
    if (!State) return E_POINTER;
    EnterCriticalSection(&m_csFilter);
    *State = m_state;
    LeaveCriticalSection(&m_csFilter);
    return S_OK;
}

STDMETHODIMP PS3EyeFilter::SetSyncSource(IReferenceClock *pClock)
{
    EnterCriticalSection(&m_csFilter);
    if (m_pClock) {
        m_pClock->Release();
        m_pClock = nullptr;
    }
    m_pClock = pClock;
    if (m_pClock) m_pClock->AddRef();
    LeaveCriticalSection(&m_csFilter);
    return S_OK;
}

STDMETHODIMP PS3EyeFilter::GetSyncSource(IReferenceClock **pClock)
{
    if (!pClock) return E_POINTER;
    EnterCriticalSection(&m_csFilter);
    *pClock = m_pClock;
    if (*pClock) (*pClock)->AddRef();
    LeaveCriticalSection(&m_csFilter);
    return S_OK;
}

STDMETHODIMP PS3EyeFilter::EnumPins(IEnumPins **ppEnum)
{
    if (!ppEnum) return E_POINTER;
    *ppEnum = new CEnumPins(m_pPin);
    return S_OK;
}

STDMETHODIMP PS3EyeFilter::FindPin(LPCWSTR Id, IPin **ppPin)
{
    if (!ppPin) return E_POINTER;
    if (Id && wcscmp(Id, L"Capture") == 0) {
        *ppPin = m_pPin;
        m_pPin->AddRef();
        return S_OK;
    }
    *ppPin = nullptr;
    return VFW_E_NOT_FOUND;
}

STDMETHODIMP PS3EyeFilter::QueryFilterInfo(FILTER_INFO *pInfo)
{
    if (!pInfo) return E_POINTER;
    wcscpy_s(pInfo->achName, m_wszName);
    pInfo->pGraph = m_pGraph;
    if (pInfo->pGraph) pInfo->pGraph->AddRef();
    return S_OK;
}

STDMETHODIMP PS3EyeFilter::JoinFilterGraph(IFilterGraph *pGraph, LPCWSTR pName)
{
    EnterCriticalSection(&m_csFilter);
    m_pGraph = pGraph;
    if (m_pEventSink) {
        m_pEventSink->Release();
        m_pEventSink = nullptr;
    }
    if (m_pGraph) {
        m_pGraph->QueryInterface(IID_IMediaEventSink, (void**)&m_pEventSink);
    }
    if (pName) {
        wcscpy_s(m_wszName, pName);
    }
    LeaveCriticalSection(&m_csFilter);
    return S_OK;
}

STDMETHODIMP PS3EyeFilter::QueryVendorInfo(LPWSTR *pVendorInfo)
{
    if (!pVendorInfo) return E_POINTER;
    size_t len = sizeof(L"Sony / Nam Tai");
    *pVendorInfo = (LPWSTR)CoTaskMemAlloc(len);
    if (!*pVendorInfo) return E_OUTOFMEMORY;
    wcscpy_s(*pVendorInfo, len / sizeof(WCHAR), L"Sony / Nam Tai");
    return S_OK;
}

void PS3EyeFilter::NotifyEvent(long EventCode, LONG_PTR EventParam1, LONG_PTR EventParam2)
{
    EnterCriticalSection(&m_csFilter);
    if (m_pEventSink) {
        m_pEventSink->Notify(EventCode, EventParam1, EventParam2);
    }
    LeaveCriticalSection(&m_csFilter);
}
