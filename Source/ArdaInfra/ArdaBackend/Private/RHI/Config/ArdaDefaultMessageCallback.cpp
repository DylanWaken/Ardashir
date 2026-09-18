#include "RHI/Config/ArdaDefaultMessageCallback.h"
#include "ArdaLog.h"

namespace arda
{
	ARDA_DEFINE_LOG_CATEGORY_NAMED(LogArdaBackend, "ArdaBackend", Log);
	void FArdaDefaultMessageCallback::Message(EArdaDiagnosticSeverity severity, const char* messageText)
	{
		switch (severity)
		{
		case EArdaDiagnosticSeverity::Warning:
			ARDA_LOG(LogArdaBackend, Warning, "%s", messageText ? messageText : "");
			break;
		case EArdaDiagnosticSeverity::Error:
			ARDA_LOG(LogArdaBackend, Error, "%s", messageText ? messageText : "");
			break;
		case EArdaDiagnosticSeverity::Fatal:
			ARDA_LOG(LogArdaBackend, Fatal, "%s", messageText ? messageText : "");
			break;
		default:
			ARDA_LOG(LogArdaBackend, Log, "%s", messageText ? messageText : "");
			break;
		}
	}
}
