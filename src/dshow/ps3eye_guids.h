/* ps3eye_guids.h — GUID definitions for the PS3 Eye DirectShow filter.
 */
#ifndef PS3EYE_GUIDS_H
#define PS3EYE_GUIDS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dshow.h>
#include <uuids.h>
#include <initguid.h>

// Static, frozen CLSID for PS3 Eye Filter:
// {E85B697D-4F12-4C67-B029-7928C3D82490}
DEFINE_GUID(CLSID_PS3EyeFilter,
0xe85b697d, 0x4f12, 0x4c67, 0xb0, 0x29, 0x79, 0x28, 0xc3, 0xd8, 0x24, 0x90);


#endif /* PS3EYE_GUIDS_H */
