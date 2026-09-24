#include "platform/windows/WinCommon.h"
#include "audio/windows/WindowsAudioEndpointEnumerator.h"
#include "audio/windows/ComHelpers.h"

namespace audioslave::win
{
namespace
{
EDataFlow toDataFlow (EndpointFlow flow)
{
    return flow == EndpointFlow::capture ? eCapture : eRender;
}

EndpointState toEndpointState (DWORD state)
{
    switch (state)
    {
        case DEVICE_STATE_ACTIVE:    return EndpointState::active;
        case DEVICE_STATE_DISABLED:  return EndpointState::disabled;
        case DEVICE_STATE_UNPLUGGED: return EndpointState::unplugged;
        default:                     return EndpointState::notPresent;
    }
}

juce::String defaultId (IMMDeviceEnumerator* enumerator, EndpointFlow flow)
{
    juce::ComSmartPtr<IMMDevice> device;
    juce::String id;
    if (SUCCEEDED (enumerator->GetDefaultAudioEndpoint (toDataFlow (flow), eConsole, device.resetAndGetPointerAddress())))
        getDeviceId (device, id);
    return id;
}
} // namespace

ResultCode WindowsAudioEndpointEnumerator::enumerate (std::vector<AudioEndpoint>& out)
{
    juce::ComSmartPtr<IMMDeviceEnumerator> enumerator;
    if (const HRESULT hr = createDeviceEnumerator (enumerator); FAILED (hr))
        return hr;

    HRESULT lastFailure = S_OK;
    for (auto flow : { EndpointFlow::render, EndpointFlow::capture })
    {
        juce::ComSmartPtr<IMMDeviceCollection> collection;
        HRESULT hr = enumerator->EnumAudioEndpoints (toDataFlow (flow), DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED,
                                                     collection.resetAndGetPointerAddress());
        UINT count = 0;
        if (SUCCEEDED (hr))
            hr = collection->GetCount (&count);
        if (FAILED (hr))
        {
            lastFailure = hr;
            continue;
        }

        const auto defaultForFlow = defaultId (enumerator, flow);
        for (UINT i = 0; i < count; ++i)
        {
            juce::ComSmartPtr<IMMDevice> device;
            if (FAILED (collection->Item (i, device.resetAndGetPointerAddress())))
                continue;

            AudioEndpoint endpoint;
            endpoint.flow = flow;
            if (FAILED (getDeviceId (device, endpoint.id)))
                continue;

            DWORD state = 0;
            endpoint.state = SUCCEEDED (device->GetState (&state)) ? toEndpointState (state) : EndpointState::notPresent;
            getFriendlyName (device, endpoint.name);
            endpoint.isDefault = endpoint.id == defaultForFlow;
            out.push_back (std::move (endpoint));
        }
    }

    if (! out.empty())
        return S_OK;
    return FAILED (lastFailure) ? lastFailure : static_cast<HRESULT> (result::notFound);
}

juce::String WindowsAudioEndpointEnumerator::defaultEndpointId (EndpointFlow flow)
{
    juce::ComSmartPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED (createDeviceEnumerator (enumerator)))
        return {};
    return defaultId (enumerator, flow);
}
} // namespace audioslave::win
