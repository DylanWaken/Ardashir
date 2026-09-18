/** Default backend logging adapter. */
#pragma once

#include "RHI/Config/ArdaBackendDiagnostics.h"

namespace arda
{
	class FArdaDefaultMessageCallback final : public IArdaDiagnosticCallback
	{
	public:
		void Message(EArdaDiagnosticSeverity severity, const char* messageText) override;
	};
}
