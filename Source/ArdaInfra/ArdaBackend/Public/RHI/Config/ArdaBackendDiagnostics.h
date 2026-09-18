/** Backend diagnostic callback contract. */
#pragma once

#include <cstdint>

namespace arda
{
	/** Identifies the severity of a backend diagnostic message. */
	enum class EArdaDiagnosticSeverity : uint8_t
	{
		Info,
		Warning,
		Error,
		Fatal
	};

	/** Receives diagnostic messages emitted by the backend. */
	class IArdaDiagnosticCallback
	{
	public:
		/** Destroys the diagnostic callback. */
		virtual ~IArdaDiagnosticCallback() = default;

		/**
         * Handles a backend diagnostic message.
         * @param Severity Severity assigned to the message.
         * @param Text Null-terminated diagnostic text.
         */
		virtual void Message(EArdaDiagnosticSeverity Severity, const char* Text) = 0;
	};
}
