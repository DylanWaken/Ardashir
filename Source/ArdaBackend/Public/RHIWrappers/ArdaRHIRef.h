/** @file ArdaRHIRef.h
 * Defines the intrusive smart-reference type used to retain RHI objects.
 */

#pragma once

#include "ArdaRHIFwd.h"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace arda::rhi
{
    /** Intrusive smart reference that retains RHI objects through AddRef and Release. */
    template <typename T>
    class TArdaRHIRef
    {
    public:
        /** Constructs an empty or retained RHI object reference. */
        constexpr TArdaRHIRef() noexcept = default;
        /** Constructs an empty or retained RHI object reference. */
        constexpr TArdaRHIRef(std::nullptr_t) noexcept {}

        /**
         * Constructs an empty or retained RHI object reference.
         * @param Pointer The pointer.
         */
        explicit TArdaRHIRef(T* Pointer) noexcept
            : mPointer(Pointer)
        {
            AddRef();
        }

        /**
         * Constructs an empty or retained RHI object reference.
         * @param Other The other.
         */
        TArdaRHIRef(const TArdaRHIRef& Other) noexcept
            : mPointer(Other.mPointer)
        {
            AddRef();
        }

        /**
         * Constructs an empty or retained RHI object reference.
         * @param Other The other.
         */
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        TArdaRHIRef(const TArdaRHIRef<U>& Other) noexcept
            : mPointer(Other.Get())
        {
            AddRef();
        }

        /**
         * Constructs an empty or retained RHI object reference.
         * @param Other The other.
         */
        TArdaRHIRef(TArdaRHIRef&& Other) noexcept
            : mPointer(Other.Detach())
        {
        }

        /**
         * Constructs an empty or retained RHI object reference.
         * @param Other The other.
         */
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
        TArdaRHIRef(TArdaRHIRef<U>&& Other) noexcept
            : mPointer(Other.Detach())
        {
        }

        /** Releases the retained RHI object. */
        ~TArdaRHIRef()
        {
            Release();
        }

        /**
         * Assigns another reference to this reference.
         * @param Other The other.
         * @return A reference to the requested value.
         */
        TArdaRHIRef& operator=(const TArdaRHIRef& Other) noexcept
        {
            TArdaRHIRef Copy(Other);
            Swap(Copy);
            return *this;
        }

        /**
         * Assigns another reference to this reference.
         * @param Other The other.
         * @return A reference to the requested value.
         */
        TArdaRHIRef& operator=(TArdaRHIRef&& Other) noexcept
        {
            TArdaRHIRef Moved(std::move(Other));
            Swap(Moved);
            return *this;
        }

        /**
         * Assigns another reference to this reference.
         * @return A reference to the requested value.
         */
        TArdaRHIRef& operator=(std::nullptr_t) noexcept
        {
            Reset();
            return *this;
        }

        /**
         * Returns the RHI object.
         * @return The requested object pointer.
         */
        [[nodiscard]] T* Get() const noexcept { return mPointer; }
        /**
         * Provides the operator-> operation.
         * @return The requested object pointer.
         */
        [[nodiscard]] T* operator->() const noexcept { return mPointer; }
        /**
         * Provides the operator* operation.
         * @return A reference to the requested value.
         */
        [[nodiscard]] T& operator*() const noexcept { return *mPointer; }
        /**
         * Tests whether this reference holds an object.
         * @return True when the reference or result is valid; otherwise false.
         */
        [[nodiscard]] explicit operator bool() const noexcept { return mPointer != nullptr; }

        /**
         * Performs the reset operation.
         * @param Pointer The pointer.
         */
        void Reset(T* Pointer = nullptr) noexcept
        {
            TArdaRHIRef Replacement(Pointer);
            Swap(Replacement);
        }

        /**
         * Performs the detach operation.
         * @return The requested object pointer.
         */
        [[nodiscard]] T* Detach() noexcept
        {
            T* Result = mPointer;
            mPointer = nullptr;
            return Result;
        }

        /**
         * Performs the swap operation.
         * @param Other The other.
         */
        void Swap(TArdaRHIRef& Other) noexcept
        {
            std::swap(mPointer, Other.mPointer);
        }

        /**
         * Compares two values for equality.
         * @param A The a.
         * @param B The b.
         * @return True when the condition is satisfied; otherwise false.
         */
        friend bool operator==(const TArdaRHIRef& A, const TArdaRHIRef& B) noexcept
        {
            return A.mPointer == B.mPointer;
        }

        /**
         * Compares two values for inequality.
         * @param A The a.
         * @param B The b.
         * @return True when the condition is satisfied; otherwise false.
         */
        friend bool operator!=(const TArdaRHIRef& A, const TArdaRHIRef& B) noexcept
        {
            return !(A == B);
        }

    private:
        /** Performs the add operation. */
        void AddRef() noexcept
        {
            if (mPointer)
                mPointer->AddRef();
        }

        /** Performs the release operation. */
        void Release() noexcept
        {
            if (mPointer)
                mPointer->Release();
        }

        /** Stores the pointer. */
        T* mPointer = nullptr;
    };
}
