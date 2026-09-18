/** @file ArdaRHINativeResourceImports.h
 * Declares NativeResourceImports definitions for the RHI interop module.
 */

#pragma once

#include "RHI/Interop/ArdaRHINativeResourceTypes.h"
#include "RHI/Memory/ArdaRHIMemoryTypes.h"
#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Scheduling/ArdaRHIResourceStates.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/shared_ptr.h>
#include <cstdint>

namespace arda
{
	/** Backend-neutral description of a native texture import. */
	struct FArdaRHINativeTextureImportDesc
	{
		/** Stores the native object. */
		uintptr_t mNativeObject = 0;
		/** Stores the native type. */
		EArdaRHINativeResourceType mNativeType = EArdaRHINativeResourceType::BackendDefined;
		/** Stable native type name required when mNativeType is BackendDefined. */
		eastl::string mNativeTypeName;
		/** Immutable backend-specific metadata copied into the import request. */
		eastl::vector<uint8_t> mBackendData;
		/** Stores the ownership. */
		EArdaRHINativeOwnership mOwnership = EArdaRHINativeOwnership::Borrowed;
		/** Stores the texture. */
		FArdaRHITextureDesc mTexture;
		/** Stores the initial state. */
		EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
		/**
         * Optional shared token retained by the imported wrapper.
         * The token, rather than Arda, owns any native lifetime it represents.
         */
		eastl::shared_ptr<void> mLifetimeToken;
		/** Optional complete allocation metadata supplied by the native allocation owner. */
		FArdaRHIMemoryAllocationInfo mMemoryAllocationInfo;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHINativeTextureImportDesc& O) const noexcept
		{
			return mMemoryAllocationInfo == O.mMemoryAllocationInfo && mNativeObject == O.mNativeObject &&
			    mNativeType == O.mNativeType && mNativeTypeName == O.mNativeTypeName &&
			    mBackendData == O.mBackendData && mOwnership == O.mOwnership && mTexture == O.mTexture &&
			    mInitialState == O.mInitialState && !mLifetimeToken.owner_before(O.mLifetimeToken) &&
			    !O.mLifetimeToken.owner_before(mLifetimeToken);
		}
	};

	/** Backend-neutral description of a native buffer import. */
	struct FArdaRHINativeBufferImportDesc
	{
		/** Stores the native object. */
		uintptr_t mNativeObject = 0;
		/** Stores the native type. */
		EArdaRHINativeResourceType mNativeType = EArdaRHINativeResourceType::BackendDefined;
		/** Stable native type name required when mNativeType is BackendDefined. */
		eastl::string mNativeTypeName;
		/** Immutable backend-specific metadata copied into the import request. */
		eastl::vector<uint8_t> mBackendData;
		/** Stores the ownership. */
		EArdaRHINativeOwnership mOwnership = EArdaRHINativeOwnership::Borrowed;
		/** Stores the buffer. */
		FArdaRHIBufferDesc mBuffer;
		/** Stores the initial state. */
		EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
		/**
         * Optional shared token retained by the imported wrapper.
         * The token, rather than Arda, owns any native lifetime it represents.
         */
		eastl::shared_ptr<void> mLifetimeToken;
		/** Optional complete allocation metadata supplied by the native allocation owner. */
		FArdaRHIMemoryAllocationInfo mMemoryAllocationInfo;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHINativeBufferImportDesc& O) const noexcept
		{
			return mMemoryAllocationInfo == O.mMemoryAllocationInfo && mNativeObject == O.mNativeObject &&
			    mNativeType == O.mNativeType && mNativeTypeName == O.mNativeTypeName &&
			    mBackendData == O.mBackendData && mOwnership == O.mOwnership && mBuffer == O.mBuffer &&
			    mInitialState == O.mInitialState && !mLifetimeToken.owner_before(O.mLifetimeToken) &&
			    !O.mLifetimeToken.owner_before(mLifetimeToken);
		}
	};
}
