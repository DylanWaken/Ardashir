#include "Modules/ModuleManager.h"

namespace arda
{
	class FArdaUnrealEditorModule final : public IModuleInterface
	{
	};
}

IMPLEMENT_MODULE(arda::FArdaUnrealEditorModule, ArdaUnrealEditor)
