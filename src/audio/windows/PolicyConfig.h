#pragma once
// IPolicyConfig: the undocumented COM interface the Windows Sound control
// panel uses (Windows 7 .. 11) to change an endpoint's default format and to
// enable / disable it. Only the vtable order matters; the methods Audioslave
// does not call are declared with opaque parameters.

#include "platform/windows/WinCommon.h"

#include <mmreg.h>

namespace audioslave::win
{
MIDL_INTERFACE ("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat (PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat (PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat (PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat (PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod (PCWSTR, INT, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod (PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode (PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode (PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue (PCWSTR, INT, const void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue (PCWSTR, INT, const void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint (PCWSTR, INT) = 0;
    // TRUE = enabled, FALSE = disabled (the Sound panel's "Disable").
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility (PCWSTR, INT) = 0;
};

// CLSID_CPolicyConfigClient {870af99c-171d-4f9e-af0d-e63df40c2bc9}
inline constexpr CLSID policyConfigClientClsid = {
    0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 }
};
} // namespace audioslave::win
