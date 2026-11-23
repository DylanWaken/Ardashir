#pragma once

#include <cstdint>

namespace arda
{
	// ================================
	// Compute resource access
	// ================================

	/** Access to the declared view, including writes that preserve part of its contents. */
	enum class EArdaComputeAccess : uint8_t
	{
		Read,
		Write,
		ReadWrite
	};
}
