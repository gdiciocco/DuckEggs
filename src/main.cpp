#include "schiva/platform.hpp"
#include "schiva/runtime.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

constexpr std::string_view kHelp = R"(DuckEggs - adaptive placement for Chrome Picture-in-Picture

DuckEggs learns where a Picture-in-Picture window fits your work and moves it
only when the shared adaptive model identifies a safe destination.

Windows and macOS have native window backends. Linux builds include the
portable core, command-line actions and tests, but report unsupported native
access.

Usage:
  DuckEggs [OPTIONS]

Actions:
  --help                  Show this help without starting DuckEggs
  --version               Show the version without starting DuckEggs
  --status                Read the last local status snapshot
  --stop                  Ask a running DuckEggs instance to stop
  --probe                 Check native capabilities without capture
  --import-python PATH    Copy a compatible Python backup into an empty
                          directory selected with --data-dir

Runtime:
  --start-paused          Start paused (the default)
  --resume                Start active; refuses to run alongside Schiva
  --observe-only          Learn without moving the PiP window
  --no-visual             Disable visual learning
  --no-gui                Run without the DuckEggs window
  --duration SECONDS      Exit after a finite positive duration
  --data-dir PATH         Use a different local data directory

There is no --mode option. Default data directory:
  Windows  %LOCALAPPDATA%\DuckEggs
  Linux    $XDG_DATA_HOME/duckeggs or ~/.local/share/duckeggs
  macOS    ~/Library/Application Support/DuckEggs
)";

struct Arguments {
    bool help{};
    bool version{};
    bool status{};
    bool stop{};
    bool probe{};
    bool start_paused{};
    bool resume{};
    bool observe_only{};
    bool no_visual{};
    bool no_gui{};
    std::optional<double> duration;
    std::optional<std::filesystem::path> data_dir;
    std::optional<std::filesystem::path> import_python;
};

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

bool is_option_with_value(std::string_view option, std::string_view name) {
    return option == name ||
           (starts_with(option, name) && option.size() > name.size() &&
            option[name.size()] == '=');
}

std::optional<std::string> option_value(
    std::size_t& index,
    const std::vector<std::string>& args,
    std::string_view option) {
    const std::string_view argument(args[index]);
    const std::string prefix = std::string(option) + "=";
    if (starts_with(argument, prefix)) {
        return std::string(argument.substr(prefix.size()));
    }
    if (argument == option) {
        if (index + 1 >= args.size()) {
            return std::nullopt;
        }
        return args[++index];
    }
    return std::nullopt;
}

bool parse_arguments(
    const std::vector<std::string>& args,
    Arguments& arguments,
    std::string& error) {
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view option(args[index]);
        if (option == "--help" || option == "-h") {
            arguments.help = true;
        } else if (option == "--version") {
            arguments.version = true;
        } else if (option == "--status") {
            arguments.status = true;
        } else if (option == "--stop") {
            arguments.stop = true;
        } else if (option == "--probe") {
            arguments.probe = true;
        } else if (option == "--start-paused") {
            arguments.start_paused = true;
        } else if (option == "--resume") {
            arguments.resume = true;
        } else if (option == "--observe-only") {
            arguments.observe_only = true;
        } else if (option == "--no-visual") {
            arguments.no_visual = true;
        } else if (option == "--no-gui") {
            arguments.no_gui = true;
        } else if (is_option_with_value(option, "--duration")) {
            const auto value =
                option_value(index, args, "--duration");
            if (!value.has_value() || value->empty()) {
                error = "--duration requires a value";
                return false;
            }
            try {
                std::size_t consumed = 0;
                const double duration = std::stod(*value, &consumed);
                if (consumed != value->size() ||
                    !std::isfinite(duration) || duration <= 0.0) {
                    throw std::invalid_argument("invalid");
                }
                arguments.duration = duration;
            } catch (...) {
                error = "--duration must be a finite positive number";
                return false;
            }
        } else if (is_option_with_value(option, "--data-dir")) {
            const auto value =
                option_value(index, args, "--data-dir");
            if (!value.has_value() || value->empty()) {
                error = "--data-dir requires a path";
                return false;
            }
            try {
                arguments.data_dir = schiva::path_from_utf8(*value);
            } catch (const std::exception& exception) {
                error = schiva::clean_error(exception.what());
                return false;
            }
        } else if (is_option_with_value(option, "--import-python")) {
            const auto value =
                option_value(index, args, "--import-python");
            if (!value.has_value() || value->empty()) {
                error = "--import-python requires a database path";
                return false;
            }
            try {
                arguments.import_python =
                    schiva::path_from_utf8(*value);
            } catch (const std::exception& exception) {
                error = schiva::clean_error(exception.what());
                return false;
            }
        } else if (is_option_with_value(option, "--mode")) {
            error =
                "--mode is not supported; DuckEggs has one adaptive mode";
            return false;
        } else {
            error = "unknown option: " + std::string(option);
            return false;
        }
    }
    const int actions =
        static_cast<int>(arguments.status) +
        static_cast<int>(arguments.stop) +
        static_cast<int>(arguments.probe) +
        static_cast<int>(arguments.import_python.has_value());
    if (actions > 1) {
        error = "--status, --stop, --probe and --import-python are exclusive";
        return false;
    }
    if (arguments.start_paused && arguments.resume) {
        error = "--start-paused and --resume are mutually exclusive";
        return false;
    }
    if (arguments.import_python.has_value() &&
        !arguments.data_dir.has_value()) {
        error = "--import-python requires an explicit empty --data-dir";
        return false;
    }
    return true;
}

int fail(const std::string& message) {
    std::cerr << "DuckEggs: " << message << '\n';
    return 2;
}

int run(const std::vector<std::string>& args) {
    Arguments arguments;
    std::string parse_error;
    if (!parse_arguments(args, arguments, parse_error)) {
        return fail(parse_error);
    }
    if (arguments.help) {
        std::cout << kHelp;
        return 0;
    }
    if (arguments.version) {
        std::cout << "DuckEggs " << schiva::kVersion << '\n';
        return 0;
    }

    const std::filesystem::path data_dir =
        arguments.data_dir.value_or(schiva::default_data_dir());

    if (arguments.status) {
        try {
            schiva::print_json(schiva::StatusFile(data_dir).read());
            return 0;
        } catch (const schiva::PlatformError& exception) {
            return fail(schiva::clean_error(exception.what()));
        } catch (const std::exception& exception) {
            return fail(schiva::clean_error(exception.what()));
        }
    }

    if (arguments.stop) {
        try {
            if (schiva::request_stop()) {
                std::cout << "DuckEggs: stop request sent.\n";
                return 0;
            }
            std::cerr << "DuckEggs: no running instance answered.\n";
            return 1;
        } catch (const schiva::UnsupportedPlatformError& exception) {
            return fail(schiva::clean_error(exception.what()));
        }
    }

    if (arguments.probe) {
        try {
            schiva::print_json(schiva::probe_native());
            return 0;
        } catch (const schiva::UnsupportedPlatformError& exception) {
            schiva::print_json({
                {"platform", schiva::current_platform()},
                {"supported", false},
                {"capture_performed", false},
                {"reason", schiva::clean_error(exception.what())}});
            return 2;
        } catch (const std::exception& exception) {
            schiva::print_json({
                {"platform", schiva::current_platform()},
                {"supported", true},
                {"capture_performed", false},
                {"error", schiva::clean_error(exception.what())}});
            return 1;
        }
    }

    if (arguments.import_python.has_value()) {
        try {
            const schiva::ImportReport report =
                schiva::import_python_backup(
                    *arguments.import_python, data_dir);
            schiva::print_json({
                {"source", schiva::path_to_utf8(report.source)},
                {"destination",
                 schiva::path_to_utf8(report.destination)},
                {"schema_version", report.schema_version},
                {"profiles", report.profiles},
                {"decision_records", report.decision_records},
                {"model_rows", report.model_rows},
                {"source_modified", false},
                {"started_backend", false}});
            return 0;
        } catch (const std::exception& exception) {
            return fail(schiva::clean_error(exception.what()));
        }
    }

    try {
        schiva::RuntimeOptions options;
        options.data_dir = data_dir;
        options.observe_only = arguments.observe_only;
        options.no_visual = arguments.no_visual;
        options.no_gui = arguments.no_gui;
        options.resume = arguments.resume;
        options.start_paused = !arguments.resume;
        options.duration = arguments.duration;
        return schiva::run_application(options);
    } catch (const std::exception& exception) {
        return fail(schiva::clean_error(exception.what()));
    }
}

}  // namespace

#ifdef _WIN32

namespace {

std::string utf8_from_wide(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        throw std::runtime_error(
            "the command line could not be decoded as Unicode");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            size,
            nullptr,
            nullptr) <= 0) {
        throw std::runtime_error(
            "the command line could not be decoded as Unicode");
    }
    return result;
}

bool needs_console(const std::vector<std::string>& args) {
    if (args.size() <= 1) {
        return false;
    }
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view option(args[index]);
        if (option == "--help" || option == "-h" ||
            option == "--version" || option == "--status" ||
            option == "--stop" || option == "--probe" ||
            option == "--no-gui" ||
            is_option_with_value(option, "--import-python") ||
            is_option_with_value(option, "--mode")) {
            return true;
        }
        const bool gui_option =
            option == "--start-paused" || option == "--resume" ||
            option == "--observe-only" || option == "--no-visual" ||
            is_option_with_value(option, "--duration") ||
            is_option_with_value(option, "--data-dir");
        if (!gui_option) {
            return true;
        }
    }
    return false;
}

void attach_console_for_output() {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool output_available =
        output != nullptr && output != INVALID_HANDLE_VALUE &&
        GetFileType(output) != FILE_TYPE_UNKNOWN;
    if (output_available) {
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }
    SetConsoleOutputCP(CP_UTF8);
    FILE* stream = nullptr;
    if (freopen_s(&stream, "CONOUT$", "w", stdout) != 0) {
        std::cout.clear();
    }
    if (freopen_s(&stream, "CONOUT$", "w", stderr) != 0) {
        std::cerr.clear();
    }
}

}  // namespace

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR,
    int) {
    int wide_count = 0;
    PWSTR* wide_arguments =
        CommandLineToArgvW(GetCommandLineW(), &wide_count);
    if (wide_arguments == nullptr) {
        return fail("the command line could not be read");
    }
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(wide_count));
    try {
        for (int index = 0; index < wide_count; ++index) {
            args.push_back(utf8_from_wide(wide_arguments[index]));
        }
    } catch (const std::exception& exception) {
        LocalFree(wide_arguments);
        return fail(exception.what());
    }
    LocalFree(wide_arguments);

    if (needs_console(args)) {
        attach_console_for_output();
    }
    return run(args);
}

#else

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        args.emplace_back(argv[index] == nullptr ? "" : argv[index]);
    }
    return run(args);
}

#endif
