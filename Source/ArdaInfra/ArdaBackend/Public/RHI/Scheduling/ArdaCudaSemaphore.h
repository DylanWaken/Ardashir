/** Imported graphics synchronization retained through CUDA completion. */
#pragma once


namespace arda
{
	/** Imported graphics synchronization primitive; retained until the CUDA stream completes. */
	class IArdaCudaSemaphore
	{
	public:
		virtual ~IArdaCudaSemaphore() = default;
		virtual void* GetNativeHandle() const = 0;
	};
}
