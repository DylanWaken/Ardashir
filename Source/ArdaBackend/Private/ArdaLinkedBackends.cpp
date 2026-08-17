#include "ArdaBackendCorePch.h"

#include "ArdaLinkedBackends.h"

#include <mutex>

namespace arda::backend
{
    #define ARDA_LINKED_BACKEND(Function) [[nodiscard]] bool Function();
    #include "ArdaLinkedBackendRegistrations.inl"
    #undef ARDA_LINKED_BACKEND

    namespace private_api
    {
        void RegisterLinkedBackendModules()
        {
            static std::once_flag RegistrationFlag;
            std::call_once(
                RegistrationFlag,
                []
                {
                    #define ARDA_LINKED_BACKEND(Function) (void)Function();
                    #include "ArdaLinkedBackendRegistrations.inl"
                    #undef ARDA_LINKED_BACKEND
                });
        }
    }
}
