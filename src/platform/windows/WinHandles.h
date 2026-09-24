#pragma once
// RAII owners for the Win32 resources used by Audioslave. Each type owns
// exactly one handle and releases it with the matching API, on every path.

#include "platform/windows/WinCommon.h"

#include <utility>

namespace audioslave::win
{
template <typename Traits>
class UniqueResource
{
public:
    using Type = typename Traits::Type;

    UniqueResource() noexcept = default;
    explicit UniqueResource (Type value) noexcept : value_ (value) {}
    ~UniqueResource() noexcept { reset(); }

    UniqueResource (UniqueResource&& other) noexcept : value_ (other.release()) {}
    UniqueResource& operator= (UniqueResource&& other) noexcept
    {
        if (this != &other)
            reset (other.release());
        return *this;
    }

    UniqueResource (const UniqueResource&) = delete;
    UniqueResource& operator= (const UniqueResource&) = delete;

    [[nodiscard]] Type get() const noexcept { return value_; }
    [[nodiscard]] bool isValid() const noexcept { return Traits::isValid (value_); }
    explicit operator bool() const noexcept { return isValid(); }

    void reset (Type value = Traits::invalid()) noexcept
    {
        if (Traits::isValid (value_))
            Traits::close (value_);
        value_ = value;
    }

    [[nodiscard]] Type release() noexcept { return std::exchange (value_, Traits::invalid()); }

    // Releases the current value and returns the address for an out-parameter.
    [[nodiscard]] Type* put() noexcept
    {
        reset();
        return &value_;
    }

private:
    Type value_ = Traits::invalid();
};

struct KernelHandleTraits
{
    using Type = HANDLE;
    static Type invalid() noexcept { return nullptr; }
    static bool isValid (Type h) noexcept { return h != nullptr && h != INVALID_HANDLE_VALUE; }
    static void close (Type h) noexcept { ::CloseHandle (h); }
};

struct ServiceHandleTraits
{
    using Type = SC_HANDLE;
    static Type invalid() noexcept { return nullptr; }
    static bool isValid (Type h) noexcept { return h != nullptr; }
    static void close (Type h) noexcept { ::CloseServiceHandle (h); }
};

struct RegistryKeyTraits
{
    using Type = HKEY;
    static Type invalid() noexcept { return nullptr; }
    static bool isValid (Type h) noexcept { return h != nullptr; }
    static void close (Type h) noexcept { ::RegCloseKey (h); }
};

struct LocalMemoryTraits
{
    using Type = HLOCAL;
    static Type invalid() noexcept { return nullptr; }
    static bool isValid (Type h) noexcept { return h != nullptr; }
    static void close (Type h) noexcept { ::LocalFree (h); }
};

struct EventSourceTraits
{
    using Type = HANDLE;
    static Type invalid() noexcept { return nullptr; }
    static bool isValid (Type h) noexcept { return h != nullptr; }
    static void close (Type h) noexcept { ::DeregisterEventSource (h); }
};

using UniqueHandle = UniqueResource<KernelHandleTraits>;
using ServiceHandle = UniqueResource<ServiceHandleTraits>;
using RegistryKey = UniqueResource<RegistryKeyTraits>;
using LocalMemory = UniqueResource<LocalMemoryTraits>;
using EventSourceHandle = UniqueResource<EventSourceTraits>;

// Memory returned by COM APIs (IMMDevice::GetId, GetMixFormat, ...).
template <typename T>
class CoTaskMemPtr
{
public:
    CoTaskMemPtr() noexcept = default;
    ~CoTaskMemPtr() noexcept { ::CoTaskMemFree (ptr_); }

    CoTaskMemPtr (const CoTaskMemPtr&) = delete;
    CoTaskMemPtr& operator= (const CoTaskMemPtr&) = delete;

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T** put() noexcept
    {
        ::CoTaskMemFree (std::exchange (ptr_, nullptr));
        return &ptr_;
    }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    T* ptr_ = nullptr;
};

// PROPVARIANT with PropVariantInit / PropVariantClear.
class PropVariant
{
public:
    PropVariant() noexcept { ::PropVariantInit (&value_); }
    ~PropVariant() noexcept { ::PropVariantClear (&value_); }

    PropVariant (const PropVariant&) = delete;
    PropVariant& operator= (const PropVariant&) = delete;

    [[nodiscard]] PROPVARIANT& get() noexcept { return value_; }
    [[nodiscard]] const PROPVARIANT& get() const noexcept { return value_; }
    [[nodiscard]] PROPVARIANT* put() noexcept
    {
        ::PropVariantClear (&value_);
        return &value_;
    }

private:
    PROPVARIANT value_;
};
} // namespace audioslave::win
