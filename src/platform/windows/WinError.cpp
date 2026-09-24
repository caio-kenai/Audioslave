#include "platform/windows/WinError.h"
#include "platform/windows/WinHandles.h"

#include <audioclient.h>

namespace audioslave::win
{
namespace
{
// Core Audio codes have no system message text; name them explicitly.
const char* knownAudioError (HRESULT hr)
{
    switch (hr)
    {
        case AUDCLNT_E_NOT_INITIALIZED:              return "AUDCLNT_E_NOT_INITIALIZED";
        case AUDCLNT_E_ALREADY_INITIALIZED:          return "AUDCLNT_E_ALREADY_INITIALIZED";
        case AUDCLNT_E_WRONG_ENDPOINT_TYPE:          return "AUDCLNT_E_WRONG_ENDPOINT_TYPE";
        case AUDCLNT_E_DEVICE_INVALIDATED:           return "AUDCLNT_E_DEVICE_INVALIDATED";
        case AUDCLNT_E_UNSUPPORTED_FORMAT:           return "AUDCLNT_E_UNSUPPORTED_FORMAT";
        case AUDCLNT_E_DEVICE_IN_USE:                return "AUDCLNT_E_DEVICE_IN_USE";
        case AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED:   return "AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED";
        case AUDCLNT_E_SERVICE_NOT_RUNNING:          return "AUDCLNT_E_SERVICE_NOT_RUNNING";
        case AUDCLNT_E_ENDPOINT_CREATE_FAILED:       return "AUDCLNT_E_ENDPOINT_CREATE_FAILED";
        case AUDCLNT_E_EXCLUSIVE_MODE_ONLY:          return "AUDCLNT_E_EXCLUSIVE_MODE_ONLY";
        case AUDCLNT_E_RESOURCES_INVALIDATED:        return "AUDCLNT_E_RESOURCES_INVALIDATED";
        case static_cast<HRESULT> (0x80070490):      return "E_NOTFOUND";
        default:                                     return nullptr;
    }
}

juce::String systemMessage (DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = ::FormatMessageW (flags, nullptr, code, 0, reinterpret_cast<wchar_t*> (&buffer), 0, nullptr);
    LocalMemory owner (buffer);
    if (length == 0 || buffer == nullptr)
        return {};
    return juce::String (buffer, length).trim();
}
} // namespace

juce::String hresultText (HRESULT hr)
{
    const auto hex = "0x" + juce::String::toHexString (static_cast<juce::uint32> (hr)).toUpperCase().paddedLeft ('0', 8);

    if (const char* name = knownAudioError (hr))
        return hex + " " + name;

    const auto message = systemMessage (static_cast<DWORD> (hr));
    return message.isEmpty() ? hex : hex + " (" + message + ")";
}

juce::String win32ErrorText (DWORD code)
{
    const auto message = systemMessage (code);
    return message.isEmpty() ? "error " + juce::String (static_cast<juce::uint32> (code)) : message;
}

juce::String lastErrorText()
{
    return win32ErrorText (::GetLastError());
}
} // namespace audioslave::win
