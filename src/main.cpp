#include "schiva/platform.hpp"
#include "schiva/runtime.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kHelp = R"(Schiva Adattivo C++ - adaptive placement of Chrome's native PiP window

Windows is the only native backend. Linux and macOS provide source-level
help, status and portable tests, plus an explicit unsupported-native error.

Usage:
  Schiva-Adattivo-Cpp.exe [OPTIONS]

Actions:
  --help                  Show this help without starting the backend
  --version               Show the version without starting the backend
  --status                Read the last local status JSON
  --stop                  Signal a running C++ instance (never Python)
  --probe                 Inspect capabilities without capture or data creation
  --import-python PATH    Backup a Python schema-1 database into an empty
                          --data-dir; never modifies the source

Runtime:
  --start-paused          Start paused (the default)
  --resume                Explicitly start active; fails closed while the
                          Python Schiva named mutex is alive
  --observe-only          Learn but never move the PiP window
  --no-visual             Disable desktop sampling
  --no-gui                Run without the native control panel
  --duration SECONDS      Stop after a finite positive duration
  --data-dir PATH         Override the isolated C++ local-data directory

There is no --mode option. Default data directory:
  Windows  %LOCALAPPDATA%\SchivaAdaptiveCpp
  Linux    $XDG_STATE_HOME/schiva-cpp or ~/.local/state/schiva-cpp
  macOS    ~/Library/Application Support/SchivaCpp
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

std::optional<std::string> option_value(
    int& index,
    int argc,
    char** argv,
    std::string_view option) {
    const std::string_view argument(argv[index]);
    const std::string prefix = std::string(option) + "=";
    if (argument.substr(0, prefix.size()) == prefix) {
        return std::string(argument.substr(prefix.size()));
    }
    if (argument == option) {
        if (index + 1 >= argc) {
            return std::nullopt;
        }
        return std::string(argv[++index]);
    }
    return std::nullopt;
}

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

bool parse_arguments(
    int argc,
    char** argv,
    Arguments& arguments,
    std::string& error) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
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
        } else if (option == "--duration" ||
                   starts_with(option, "--duration=")) {
            const auto value = option_value(
                index, argc, argv, "--duration");
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
                error =
                    "--duration must be a finite positive number";
                return false;
            }
        } else if (option == "--data-dir" ||
                   starts_with(option, "--data-dir=")) {
            const auto value = option_value(
                index, argc, argv, "--data-dir");
            if (!value.has_value() || value->empty()) {
                error = "--data-dir requires a path";
                return false;
            }
            arguments.data_dir = schiva::path_from_utf8(*value);
        } else if (option == "--import-python" ||
                   starts_with(option, "--import-python=")) {
            const auto value = option_value(
                index, argc, argv, "--import-python");
            if (!value.has_value() || value->empty()) {
                error = "--import-python requires a database path";
                return false;
            }
            arguments.import_python = schiva::path_from_utf8(*value);
        } else if (option == "--mode" ||
                   starts_with(option, "--mode=")) {
            error =
                "--mode is not supported; Schiva has one adaptive mode";
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
        error =
            "--import-python requires an explicit empty --data-dir";
        return false;
    }
    return true;
}

int fail(const std::string& message) {
    std::cerr << "Schiva Adattivo C++: " << message << '\n';
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    Arguments arguments;
    std::string parse_error;
    if (!parse_arguments(argc, argv, arguments, parse_error)) {
        return fail(parse_error);
    }
    if (arguments.help) {
        std::cout << kHelp;
        return 0;
    }
    if (arguments.version) {
        std::cout << schiva::kVersion << '\n';
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
                std::cout
                    << "Schiva Adattivo C++: stop request sent.\n";
                return 0;
            }
            std::cerr
                << "Schiva Adattivo C++: no running C++ instance "
                   "answered.\n";
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
