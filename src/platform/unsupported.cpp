#include "schiva/platform.hpp"

namespace schiva {
namespace {

[[noreturn]] void unsupported_native() {
    const std::string platform = current_platform();
    if (platform == "linux") {
        throw UnsupportedPlatformError(
            "Schiva Adattivo C++ has no Linux native adapter yet. A Wayland "
            "or X11 adapter with explicit screen-capture permissions is "
            "required.");
    }
    if (platform == "macos") {
        throw UnsupportedPlatformError(
            "Schiva Adattivo C++ has no macOS native adapter yet. "
            "Accessibility and screen-recording permission adapters are "
            "required.");
    }
    throw UnsupportedPlatformError(
        "Schiva Adattivo C++ does not support this platform (" +
        platform + ").");
}

class UnsupportedSignals final : public RuntimeSignals {
public:
    bool already_running() const override {
        return false;
    }
    bool stop_requested() const override {
        return true;
    }
    void sign_stop() override {}
    void close() override {}
};

}  // namespace

std::unique_ptr<Backend> create_backend() {
    unsupported_native();
}

bool python_reference_active() {
    return false;
}

std::unique_ptr<RuntimeSignals> create_runtime_signals() {
    unsupported_native();
}

bool request_stop() {
    unsupported_native();
}

}  // namespace schiva
