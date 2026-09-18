#include "RHI/Context/ArdaAssertContext.h"
namespace arda
{
	FArdaAssertContext& GetAssertContext() noexcept
	{
		static FArdaAssertContext Context;
		return Context;
	}
}
