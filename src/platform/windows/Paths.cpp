#include "platform/windows/WinCommon.h"
#include "platform/windows/Paths.h"
#include "platform/windows/WinError.h"
#include "platform/windows/WinHandles.h"
#include "common/Branding.h"

#include <aclapi.h>
#include <sddl.h>

namespace audioslave::paths
{
juce::File executableFile()
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile);
}

juce::File installDir()
{
    return executableFile().getParentDirectory();
}

juce::File siblingExecutable (const juce::String& fileName)
{
    return installDir().getChildFile (fileName);
}

juce::File programDataDir()
{
    return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
        .getChildFile (brand::productName);
}

juce::File configFile()
{
    return programDataDir().getChildFile ("config.ini");
}

juce::File legacyConfigFile()
{
    return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
        .getChildFile (juce::String (brand::legacyProductName))
        .getChildFile ("config.ini");
}

juce::File logsDir()
{
    return installDir().getChildFile ("logs");
}

juce::File serviceLogFile()
{
    return logsDir().getChildFile ("audioslave.log");
}

juce::File trayLogFile()
{
    return logsDir().getChildFile ("tray.log");
}

juce::File setupLogFile (const juce::File& installFolder)
{
    return installFolder.getChildFile ("logs").getChildFile ("setup.log");
}

bool ensureDirectory (const juce::File& dir)
{
    return dir.isDirectory() || dir.createDirectory().wasOk();
}
} // namespace audioslave::paths

namespace audioslave::win
{
namespace
{
bool applySddl (const juce::File& dir, const wchar_t* sddl, juce::String* error)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (! ::ConvertStringSecurityDescriptorToSecurityDescriptorW (sddl, SDDL_REVISION_1, &descriptor, nullptr))
    {
        if (error != nullptr)
            *error = lastErrorText();
        return false;
    }
    LocalMemory owner (descriptor);

    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    PACL dacl = nullptr;
    if (! ::GetSecurityDescriptorDacl (descriptor, &present, &dacl, &defaulted) || ! present)
    {
        if (error != nullptr)
            *error = "invalid security descriptor";
        return false;
    }

    std::wstring path (dir.getFullPathName().toWideCharPointer());
    const DWORD rc = ::SetNamedSecurityInfoW (path.data(), SE_FILE_OBJECT,
                                              DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                              nullptr, nullptr, dacl, nullptr);
    if (rc != ERROR_SUCCESS)
    {
        if (error != nullptr)
            *error = win32ErrorText (rc);
        return false;
    }
    return true;
}
} // namespace

bool applyConfigDirAcl (const juce::File& dir, juce::String* error)
{
    return applySddl (dir, L"D:(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;AU)", error);
}

bool applyLogsDirAcl (const juce::File& dir, juce::String* error)
{
    // 0x1301bf = "Modify" (read, write, execute, delete) for authenticated users.
    return applySddl (dir, L"D:(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;AU)", error);
}
} // namespace audioslave::win
