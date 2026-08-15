#pragma once

#include "ArdaRHIFwd.h"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace arda::rhi
{
    template <typename T>
    class TArdaRHIRef
    {
    public:
        constexpr TArdaRHIRef() noexcept = default;
        constexpr TArdaRHIRef(std::nullptr_t) noexcept {}

        explicit TArdaRHIRef(T* Pointer) noexcept
            : mPointer(Pointer)
        {
            AddRef();
        }

        TArdaRHIRef(const TArdaRHIRef& Other) noexcept
            : mPointer(Other.mPointer)
        {
            AddRef();
        }

        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        TArdaRHIRef(const TArdaRHIRef<U>& Other) noexcept
            : mPointer(Other.Get())
        {
            AddRef();
        }

        TArdaRHIRef(TArdaRHIRef&& Other) noexcept
            : mPointer(Other.Detach())
        {
        }

        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        TArdaRHIRef(TArdaRHIRef<U>&& Other) noexcept
            : mPointer(Other.Detach())
        {
        }

        ~TArdaRHIRef()
        {
            Release();
        }

        TArdaRHIRef& operator=(const TArdaRHIRef& Other) noexcept
        {
            TArdaRHIRef Copy(Other);
            Swap(Copy);
            return *this;
        }

        TArdaRHIRef& operator=(TArdaRHIRef&& Other) noexcept
        {
            TArdaRHIRef Moved(std::move(Other));
            Swap(Moved);
            return *this;
        }

        TArdaRHIRef& operator=(std::nullptr_t) noexcept
        {
            Reset();
            return *this;
        }

        [[nodiscard]] T* Get() const noexcept { return mPointer; }
        [[nodiscard]] T* operator->() const noexcept { return mPointer; }
        [[nodiscard]] T& operator*() const noexcept { return *mPointer; }
        [[nodiscard]] explicit operator bool() const noexcept { return mPointer != nullptr; }

        void Reset(T* Pointer = nullptr) noexcept
        {
            TArdaRHIRef Replacement(Pointer);
            Swap(Replacement);
        }

        [[nodiscard]] T* Detach() noexcept
        {
            T* Result = mPointer;
            mPointer = nullptr;
            return Result;
        }

        void Swap(TArdaRHIRef& Other) noexcept
        {
            std::swap(mPointer, Other.mPointer);
        }

        friend bool operator==(const TArdaRHIRef& A, const TArdaRHIRef& B) noexcept
        {
            return A.mPointer == B.mPointer;
        }

        friend bool operator!=(const TArdaRHIRef& A, const TArdaRHIRef& B) noexcept
        {
            return !(A == B);
        }

    private:
        void AddRef() noexcept
        {
            if (mPointer)
                mPointer->AddRef();
        }

        void Release() noexcept
        {
            if (mPointer)
                mPointer->Release();
        }

        T* mPointer = nullptr;
    };
}
