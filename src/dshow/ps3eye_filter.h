/* ps3eye_filter.h — DirectShow filter and pin class definitions.
 */
#ifndef PS3EYE_FILTER_H
#define PS3EYE_FILTER_H

#include "ps3eye_dshow.h"

class PS3EyeFilter;

class PS3EyePin : public IPin, public IAMStreamConfig, public IKsPropertySet
{
public:
    PS3EyePin(PS3EyeFilter *pFilter);
    virtual ~PS3EyePin();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPin
    STDMETHODIMP Connect(IPin *pReceivePin, const AM_MEDIA_TYPE *pmt) override;
    STDMETHODIMP ReceiveConnection(IPin *pConnector, const AM_MEDIA_TYPE *pmt) override;
    STDMETHODIMP Disconnect() override;
    STDMETHODIMP ConnectedTo(IPin **pPin) override;
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE *pmt) override;
    STDMETHODIMP QueryPinInfo(PIN_INFO *pInfo) override;
    STDMETHODIMP QueryDirection(PIN_DIRECTION *pPinDir) override;
    STDMETHODIMP QueryId(LPWSTR *Id) override;
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE *pmt) override;
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes **ppEnum) override;
    STDMETHODIMP QueryInternalConnections(IPin **apPin, ULONG *nPin) override;
    STDMETHODIMP EndOfStream() override;
    STDMETHODIMP BeginFlush() override;
    STDMETHODIMP EndFlush() override;
    STDMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) override;

    // IAMStreamConfig
    STDMETHODIMP SetFormat(AM_MEDIA_TYPE *pmt) override;
    STDMETHODIMP GetFormat(AM_MEDIA_TYPE **ppmt) override;
    STDMETHODIMP GetNumberOfCapabilities(int *piCount, int *piSize) override;
    STDMETHODIMP GetStreamCaps(int iIndex, AM_MEDIA_TYPE **ppmt, BYTE *pSCC) override;

    // IKsPropertySet
    STDMETHODIMP Set(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData) override;
    STDMETHODIMP Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData, DWORD *pcbReturned) override;
    STDMETHODIMP QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD *pTypeSupport) override;

    // Streaming Control
    HRESULT Active();
    HRESULT Inactive();

private:
    static DWORD WINAPI ThreadProc(LPVOID pParam);
    void DoBufferProcessingLoop();

    std::atomic<long>       m_cRef;
    PS3EyeFilter           *m_pFilter;
    IPin                   *m_pConnectedPin;
    IMemInputPin           *m_pMemInputPin;
    IMemAllocator          *m_pAllocator;
    AM_MEDIA_TYPE           m_mtCurrent;
    bool                    m_bConnected;
    bool                    m_bActive;
    HANDLE                  m_hThread;
    HANDLE                  m_hStopEvent;
    CRITICAL_SECTION        m_csPin;
};

class PS3EyeFilter : public IBaseFilter
{
public:
    PS3EyeFilter();
    virtual ~PS3EyeFilter();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPersist
    STDMETHODIMP GetClassID(CLSID *pClassID) override;

    // IMediaFilter
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Run(REFERENCE_TIME tStart) override;
    STDMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE *State) override;
    STDMETHODIMP SetSyncSource(IReferenceClock *pClock) override;
    STDMETHODIMP GetSyncSource(IReferenceClock **pClock) override;

    // IBaseFilter
    STDMETHODIMP EnumPins(IEnumPins **ppEnum) override;
    STDMETHODIMP FindPin(LPCWSTR Id, IPin **ppPin) override;
    STDMETHODIMP QueryFilterInfo(FILTER_INFO *pInfo) override;
    STDMETHODIMP JoinFilterGraph(IFilterGraph *pGraph, LPCWSTR pName) override;
    STDMETHODIMP QueryVendorInfo(LPWSTR *pVendorInfo) override;

    // Internal helpers
    capture_session *GetSession() { return &m_session; }
    void NotifyEvent(long EventCode, LONG_PTR EventParam1, LONG_PTR EventParam2);
    void SetConfiguredMode(ps3eye_mode mode, uint16_t fps);
    HRESULT EnsureSessionOpen();

private:
    std::atomic<long>       m_cRef;
    FILTER_STATE            m_state;
    IReferenceClock        *m_pClock;
    IFilterGraph           *m_pGraph;
    PS3EyePin              *m_pPin;
    capture_session         m_session;
    bool                    m_bSessionOpened;
    ps3eye_mode             m_configuredMode;
    uint16_t                m_configuredFps;
    CRITICAL_SECTION        m_csFilter;
    WCHAR                   m_wszName[128];
};

#endif /* PS3EYE_FILTER_H */
