#include "platform/windows/WinCommon.h"
#include "audio/windows/WindowsFormatSupport.h"
#include "platform/windows/WinHandles.h"

#include <ks.h>
#include <ksmedia.h>

#include <vector>

namespace audioslave::win
{
WAVEFORMATEXTENSIBLE toWaveFormat (const AudioFormat& format)
{
    WAVEFORMATEXTENSIBLE w {};
    w.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    w.Format.nChannels = format.channels;
    w.Format.nSamplesPerSec = format.sampleRate;
    w.Format.wBitsPerSample = format.containerBits;
    w.Format.nBlockAlign = static_cast<WORD> (format.channels * format.containerBits / 8);
    w.Format.nAvgBytesPerSec = format.sampleRate * w.Format.nBlockAlign;
    w.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
    w.Samples.wValidBitsPerSample = format.validBits;
    w.dwChannelMask = format.channelMask;
    w.SubFormat = format.isFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
    return w;
}

HRESULT WindowsFormatSupport::open (IMMDevice* device, WindowsFormatSupport& out)
{
    if (device == nullptr)
        return E_POINTER;

    juce::ComSmartPtr<IDeviceTopology> endpointTopology;
    HRESULT hr = device->Activate (__uuidof (IDeviceTopology), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**> (endpointTopology.resetAndGetPointerAddress()));
    if (FAILED (hr))
        return hr;

    juce::ComSmartPtr<IConnector> endpointConnector;
    if (FAILED (hr = endpointTopology->GetConnector (0, endpointConnector.resetAndGetPointerAddress())))
        return hr;
    juce::ComSmartPtr<IConnector> adapterConnector;
    if (FAILED (hr = endpointConnector->GetConnectedTo (adapterConnector.resetAndGetPointerAddress())))
        return hr;
    juce::ComSmartPtr<IPart> adapterPart;
    if (FAILED (hr = adapterConnector.QueryInterface (adapterPart)))
        return hr;

    std::vector<juce::ComSmartPtr<IDeviceTopology>> queue;
    {
        juce::ComSmartPtr<IDeviceTopology> first;
        if (FAILED (hr = adapterPart->GetTopologyObject (first.resetAndGetPointerAddress())))
            return hr;
        queue.push_back (first);
    }

    // Breadth-first over the KS filters (real graphs are 1-3 filters deep;
    // the bound protects against cycles in odd drivers).
    juce::StringArray visited;
    for (size_t head = 0; head < queue.size() && head < 8; ++head)
    {
        IDeviceTopology* topology = queue[head];
        CoTaskMemPtr<wchar_t> rawId;
        if (FAILED (topology->GetDeviceId (rawId.put())) || ! rawId)
            continue;
        const juce::String filterId (rawId.get());
        if (visited.contains (filterId))
            continue;
        visited.add (filterId);

        UINT count = 0;
        if (FAILED (topology->GetConnectorCount (&count)))
            continue;

        for (UINT i = 0; i < count; ++i)
        {
            juce::ComSmartPtr<IConnector> connector;
            if (FAILED (topology->GetConnector (i, connector.resetAndGetPointerAddress())))
                continue;
            ConnectorType type = Unknown_Connector;
            if (FAILED (connector->GetType (&type)))
                continue;

            if (type == Software_IO)
            {
                juce::ComSmartPtr<IPart> part;
                if (SUCCEEDED (connector.QueryInterface (part))
                    && SUCCEEDED (part->Activate (CLSCTX_ALL, __uuidof (IKsFormatSupport),
                                                  reinterpret_cast<void**> (out.formatSupport_.resetAndGetPointerAddress()))))
                {
                    return S_OK;
                }
                continue;
            }

            juce::ComSmartPtr<IConnector> other;
            if (FAILED (connector->GetConnectedTo (other.resetAndGetPointerAddress())))
                continue;
            juce::ComSmartPtr<IPart> otherPart;
            if (FAILED (other.QueryInterface (otherPart)))
                continue;
            juce::ComSmartPtr<IDeviceTopology> next;
            if (SUCCEEDED (otherPart->GetTopologyObject (next.resetAndGetPointerAddress())))
                queue.push_back (next);
        }
    }
    return E_NOINTERFACE;
}

HRESULT WindowsFormatSupport::isSupported (const AudioFormat& format, bool& supported) const
{
    supported = false;
    if (formatSupport_ == nullptr)
        return E_POINTER;

    struct KsWaveFormat
    {
        KSDATAFORMAT header;
        WAVEFORMATEXTENSIBLE wave;
    } ks {};
    ks.wave = toWaveFormat (format);
    ks.header.FormatSize = sizeof (KsWaveFormat);
    ks.header.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    ks.header.SubFormat = ks.wave.SubFormat;
    ks.header.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    ks.header.SampleSize = ks.wave.Format.nBlockAlign;

    BOOL ok = FALSE;
    const HRESULT hr = formatSupport_->IsFormatSupported (&ks.header, sizeof (ks), &ok);
    if (SUCCEEDED (hr))
        supported = ok != FALSE;
    return hr;
}
} // namespace audioslave::win
