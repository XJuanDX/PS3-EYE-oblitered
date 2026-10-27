/* ps3eye_dshow.h — Master header for the PS3 Eye DirectShow Filter.
 */
#ifndef PS3EYE_DSHOW_H
#define PS3EYE_DSHOW_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <strmif.h>
#include <uuids.h>
#include <atomic>

#ifdef __cplusplus
extern "C" {
#endif
#include "capture_session.h"
#ifdef __cplusplus
}
#endif

#include "ps3eye_guids.h"

// Reference counting helper for module locks in in-proc DLL
extern LONG g_cLock;

HRESULT CreateMediaType(AM_MEDIA_TYPE *pmt);
void FreeMediaType(AM_MEDIA_TYPE *pmt);
HRESULT CopyMediaType(AM_MEDIA_TYPE *pmtTarget, const AM_MEDIA_TYPE *pmtSource);

#endif /* PS3EYE_DSHOW_H */
