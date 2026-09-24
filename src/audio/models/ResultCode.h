#pragma once
// Result codes carried through the platform-independent layers (core, IPC,
// tests). They are HRESULT values, stored without including <windows.h>, so
// the Windows implementations can pass their HRESULTs through unchanged.

#include <cstdint>

namespace audioslave
{
using ResultCode = std::int32_t;

namespace result
{
inline constexpr ResultCode ok = 0;
inline constexpr ResultCode fail = static_cast<ResultCode> (0x80004005);         // E_FAIL
inline constexpr ResultCode notFound = static_cast<ResultCode> (0x80070490);     // E_NOTFOUND
inline constexpr ResultCode accessDenied = static_cast<ResultCode> (0x80070005); // E_ACCESSDENIED
inline constexpr ResultCode noInterface = static_cast<ResultCode> (0x80004002);  // E_NOINTERFACE
inline constexpr ResultCode invalidArg = static_cast<ResultCode> (0x80070057);   // E_INVALIDARG
inline constexpr ResultCode invalidData = static_cast<ResultCode> (0x8007000D);  // HRESULT_FROM_WIN32(ERROR_INVALID_DATA)
} // namespace result

inline constexpr bool succeeded (ResultCode code) noexcept { return code >= 0; }
inline constexpr bool failed (ResultCode code) noexcept { return code < 0; }
} // namespace audioslave
