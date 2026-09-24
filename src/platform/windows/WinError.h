#pragma once
// Readable text for HRESULT / Win32 error codes (for logs and CLI output).

#include "platform/windows/WinCommon.h"

namespace audioslave::win
{
// "0x8889000E AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED" or
// "0x80070005 (Access is denied.)".
juce::String hresultText (HRESULT hr);

// System message for a Win32 error code, e.g. "Access is denied." (falls back
// to "error 1234").
juce::String win32ErrorText (DWORD code);

// win32ErrorText (GetLastError()).
juce::String lastErrorText();
} // namespace audioslave::win
