#include "platform/windows/WinCommon.h"
#include "platform/windows/Elevation.h"
#include "platform/windows/WinHandles.h"

#include <shellapi.h>

namespace audioslave::win
{
bool isElevated()
{
    UniqueHandle token;
    if (! ::OpenProcessToken (::GetCurrentProcess(), TOKEN_QUERY, token.put()))
        return false;

    TOKEN_ELEVATION elevation {};
    DWORD size = 0;
    if (! ::GetTokenInformation (token.get(), TokenElevation, &elevation, sizeof (elevation), &size))
        return false;
    return elevation.TokenIsElevated != 0;
}

juce::String quoteArgument (const juce::String& argument)
{
    if (argument.isNotEmpty() && ! argument.containsAnyOf (" \t\"\n"))
        return argument;

    juce::String quoted ("\"");
    int backslashes = 0;
    for (auto c : argument)
    {
        if (c == '\\')
        {
            ++backslashes;
            continue;
        }
        if (c == '"')
            quoted << juce::String::repeatedString ("\\", backslashes * 2 + 1) << "\"";
        else
            quoted << juce::String::repeatedString ("\\", backslashes) << juce::String::charToString (c);
        backslashes = 0;
    }
    quoted << juce::String::repeatedString ("\\", backslashes * 2) << "\"";
    return quoted;
}

bool launchElevated (const juce::File& executable, const juce::StringArray& arguments)
{
    juce::StringArray quoted;
    for (const auto& a : arguments)
        quoted.add (quoteArgument (a));

    const auto parameters = quoted.joinIntoString (" ");
    const auto result = ::ShellExecuteW (nullptr, L"runas", executable.getFullPathName().toWideCharPointer(),
                                         parameters.toWideCharPointer(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR> (result) > 32;
}
} // namespace audioslave::win
