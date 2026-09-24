#include "platform/windows/WinCommon.h"
#include "platform/windows/EventLog.h"
#include "platform/windows/WinHandles.h"
#include "common/Branding.h"

namespace audioslave::win
{
bool writeEventLog (EventType type, const juce::String& message)
{
    EventSourceHandle source (::RegisterEventSourceW (nullptr, brand::eventSource));
    if (! source)
        return false;

    WORD nativeType = EVENTLOG_INFORMATION_TYPE;
    if (type == EventType::warning)
        nativeType = EVENTLOG_WARNING_TYPE;
    else if (type == EventType::error)
        nativeType = EVENTLOG_ERROR_TYPE;

    const wchar_t* strings[] = { message.toWideCharPointer() };
    return ::ReportEventW (source.get(), nativeType, 0, 0, nullptr, 1, 0, strings, nullptr) != FALSE;
}
} // namespace audioslave::win
