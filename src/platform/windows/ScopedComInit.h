#pragma once
// RAII for CoInitializeEx / CoUninitialize on the current thread.
//
// CoUninitialize is only balanced against a *successful* CoInitializeEx
// (S_OK / S_FALSE). RPC_E_CHANGED_MODE means COM is already initialised on
// this thread with another apartment model: COM is usable, but this object
// did not initialise it and must not uninitialise it.

#include "platform/windows/WinCommon.h"

namespace audioslave::win
{
class ScopedComInit
{
public:
    explicit ScopedComInit (DWORD model = COINIT_MULTITHREADED) noexcept
        : result_ (::CoInitializeEx (nullptr, model))
    {
    }

    ~ScopedComInit() noexcept
    {
        if (SUCCEEDED (result_))
            ::CoUninitialize();
    }

    ScopedComInit (const ScopedComInit&) = delete;
    ScopedComInit& operator= (const ScopedComInit&) = delete;

    // True when COM can be used on this thread.
    [[nodiscard]] bool isUsable() const noexcept { return SUCCEEDED (result_) || result_ == RPC_E_CHANGED_MODE; }
    [[nodiscard]] HRESULT result() const noexcept { return result_; }

private:
    HRESULT result_;
};
} // namespace audioslave::win
