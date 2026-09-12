#pragma once
#include "Nodes/ArdaPixelSortNodes.h"

#include "ArdaExamplePaths.h"
#include <mutex>
#include <stdexcept>

namespace arda
{
	inline void Check(FArdaRHIStatus Status)
	{
		if (!Status)
		{
			throw std::runtime_error(Status.mMessage.c_str());
		}
	}

	template <class T>
	T Take(TArdaRHIResult<T> Result)
	{
		Check(Result.mStatus);
		return eastl::move(Result.mValue);
	}

	template <class T>
	void Append(eastl::string& Key, T Value)
	{
		Key.append(reinterpret_cast<const char*>(&Value), sizeof(Value));
	}

	inline void AppendResource(eastl::string& Key, FArdaDependencyResourceHandle Resource)
	{
		Append(Key, Resource.mGraph);
		Append(Key, Resource.mIndex);
		Append(Key, Resource.mGeneration);
	}

	inline eastl::string MakePixelSortNodeKey(const FArdaPixelSortNodeParameters& P)
	{
		eastl::string Key;
		for (auto R : {P.mConstants, P.mNoise, P.mSorted, P.mColor})
		{
			AppendResource(Key, R);
		}
		Append(Key, reinterpret_cast<uintptr_t>(P.mInput.get()));
		Append(Key, P.mWidth);
		Append(Key, P.mHeight);
		return Key;
	}

	FArdaRHIShaderRef LoadArdaPixelSortShader(FArdaRHIDeviceRef Device,
	    const std::filesystem::path& Directory,
	    const char* Name,
	    const char* Entry,
	    EArdaRHIShaderStage Stage);
}
