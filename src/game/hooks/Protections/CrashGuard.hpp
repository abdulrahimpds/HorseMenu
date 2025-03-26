#pragma once

#include <eh.h>
#include <stdexcept>
#include <functional>
#include <sstream>
#include "util/Chat.hpp"    // Logging/Chat utilities for output

// **IMPORTANT: Compilation Requirement**
// For CrashGuard to reliably handle Structured Exception Handling (SEH) exceptions,
// especially asynchronous exceptions like access violations, this code **MUST** be
// compiled with the `/EHa` compiler flag in Microsoft Visual C++ (MSVC).
// Without /EHa, SEH translation might not work correctly, and CrashGuard may fail
// to prevent crashes in certain scenarios.

namespace YimMenu {
    class CrashException : public std::runtime_error {
    public:
        explicit CrashException(const std::string& message) : std::runtime_error(message) {}
        explicit CrashException(const char* message) : std::runtime_error(message) {}
    };
}

namespace YimMenu::CrashGuard {

    // Structured Exception Translator (converts SEH to C++ exception)
    // Called by the system when an SEH exception occurs.
    inline void SehTranslator(unsigned int code, EXCEPTION_POINTERS* /*info*/) { // 'info' is currently unused but could be used for more detailed reporting in the future.
        std::ostringstream oss;
        oss << "SEH Exception occurred with code 0x" << std::hex << code;
        // Throw as CrashException to identify exceptions originating from SEH translation
        throw YimMenu::CrashException(oss.str());
    }

    // Initialize the SEH translator once.
    // **Requires /EHa compiler flag in MSVC for full functionality.**
    inline void InitializeSehTranslator() {
        static bool initialized = false;
        if (!initialized) {
            _set_se_translator(SehTranslator);
            initialized = true;
        }
    }

    // SafeZone executes the passed function and catches both C++ and translated SEH exceptions.
    // 'label' is used for logging purposes to identify the code section being protected.
    inline void SafeZone(const char* label, const std::function<void()>& fn) {
        InitializeSehTranslator(); // Ensure SEH translator is initialized (requires /EHa)
        try {
            fn();  // Execute the protected code
        }
        catch (const std::exception& e) {
            // Log any C++ exceptions (including CrashException thrown by SehTranslator)
            LOG(WARNING) << "[CRASH PROTECTION] Exception caught in " << label << ": " << e.what();
        }
        catch (...) {
            // Catch any other non-standard exceptions
            LOG(WARNING) << "[CRASH PROTECTION] Unknown exception caught in " << label;
        }
    }

} // namespace YimMenu::CrashGuard