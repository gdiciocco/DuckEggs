#include "schiva/platform.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <unordered_set>

#include <sqlite3.h>

namespace schiva {
namespace {

class Database {
public:
    Database() = default;
    ~Database() {
        if (handle_ != nullptr) {
            sqlite3_close(handle_);
        }
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void open(const std::filesystem::path& path, int flags) {
        const std::string utf8 = path_to_utf8(path);
        const int result = sqlite3_open_v2(
            utf8.c_str(), &handle_, flags, nullptr);
        if (result != SQLITE_OK) {
            const std::string detail = handle_ == nullptr
                ? sqlite3_errstr(result)
                : sqlite3_errmsg(handle_);
            if (handle_ != nullptr) {
                sqlite3_close(handle_);
                handle_ = nullptr;
            }
            throw PlatformError("unable to open learning store: " + detail);
        }
        sqlite3_busy_timeout(handle_, 2000);
    }

    sqlite3* get() const {
        return handle_;
    }

    void close() {
        if (handle_ != nullptr) {
            sqlite3_close(handle_);
            handle_ = nullptr;
        }
    }

private:
    sqlite3* handle_{};
};

class Statement {
public:
    Statement(sqlite3* database, const char* sql) {
        const int result =
            sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            throw PlatformError(
                std::string("learning-store query failed: ") +
                sqlite3_errmsg(database));
        }
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() const {
        return statement_;
    }

private:
    sqlite3_stmt* statement_{};
};

int read_schema_version(sqlite3* database) {
    Statement statement(database, "PRAGMA user_version");
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        throw PlatformError("unable to read learning-store schema version");
    }
    return sqlite3_column_int(statement.get(), 0);
}

void check_integrity(sqlite3* database) {
    Statement statement(database, "PRAGMA quick_check");
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        throw PlatformError("learning-store integrity check failed");
    }
    const unsigned char* value = sqlite3_column_text(statement.get(), 0);
    if (value == nullptr || std::string(
            reinterpret_cast<const char*>(value)) != "ok") {
        throw PlatformError("learning-store integrity check failed");
    }
}

std::unordered_set<std::string> table_columns(
    sqlite3* database,
    std::string_view table) {
    Statement statement(
        database,
        ("PRAGMA table_info(" + std::string(table) + ")").c_str());
    std::unordered_set<std::string> columns;
    for (;;) {
        const int result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            throw PlatformError("unable to inspect learning-store schema");
        }
        const unsigned char* name = sqlite3_column_text(statement.get(), 1);
        if (name != nullptr) {
            columns.emplace(reinterpret_cast<const char*>(name));
        }
    }
    return columns;
}

void require_columns(
    sqlite3* database,
    std::string_view table,
    std::initializer_list<std::string_view> required) {
    const auto columns = table_columns(database, table);
    for (const std::string_view column : required) {
        if (!columns.contains(std::string(column))) {
            throw PlatformError(
                "learning store table " + std::string(table) +
                " has an incompatible schema");
        }
    }
}

void validate_schema(sqlite3* database) {
    const int version = read_schema_version(database);
    if (version < 0 || version != 1) {
        throw PlatformError(
            "learning store schema " + std::to_string(version) +
            " is not supported (expected 1)");
    }
    check_integrity(database);
    require_columns(
        database,
        "profiles",
        {"profile_key", "app_id", "role", "layout_bin",
         "short_map", "long_map", "manual_map"});
    require_columns(
        database,
        "decision_records",
        {"id", "profile_key", "features_json", "action", "reward"});
    require_columns(
        database,
        "model_state",
        {"id", "state_json", "updates"});
}

std::int64_t count_rows(sqlite3* database, std::string_view table) {
    Statement statement(
        database,
        ("SELECT COUNT(*) FROM " + std::string(table)).c_str());
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        throw PlatformError(
            "unable to count " + std::string(table) +
            " in learning store");
    }
    return sqlite3_column_int64(statement.get(), 0);
}

void backup_database(
    sqlite3* source,
    sqlite3* destination) {
    sqlite3_backup* backup =
        sqlite3_backup_init(destination, "main", source, "main");
    if (backup == nullptr) {
        throw PlatformError(
            std::string("unable to start SQLite backup: ") +
            sqlite3_errmsg(destination));
    }
    int result = sqlite3_backup_step(backup, -1);
    const int finish_result = sqlite3_backup_finish(backup);
    if (result != SQLITE_DONE || finish_result != SQLITE_OK) {
        if (result == SQLITE_OK) {
            result = finish_result;
        }
        throw PlatformError(
            std::string("SQLite backup failed: ") + sqlite3_errstr(result));
    }
}

bool directory_empty(const std::filesystem::path& path) {
    return std::filesystem::is_directory(path) &&
           std::filesystem::directory_iterator(path) ==
               std::filesystem::directory_iterator{};
}

}  // namespace

ImportReport import_python_backup(
    const std::filesystem::path& source,
    const std::filesystem::path& data_dir) {
    std::error_code error;
    const auto absolute_source = std::filesystem::absolute(source, error);
    if (error) {
        throw PlatformError("the Python learning-store path is invalid");
    }
    if (!std::filesystem::is_regular_file(absolute_source, error) || error) {
        throw PlatformError("the Python learning-store file does not exist");
    }

    const auto absolute_destination_dir =
        std::filesystem::absolute(data_dir, error);
    if (error) {
        throw PlatformError("the C++ data directory path is invalid");
    }
    const auto destination =
        absolute_destination_dir / "learning.sqlite3";
    if (std::filesystem::equivalent(
            absolute_source, destination, error) && !error) {
        throw PlatformError(
            "the source and C++ destination learning stores are the same file");
    }

    if (std::filesystem::exists(
            absolute_destination_dir, error) && error) {
        throw PlatformError("the C++ data directory cannot be inspected");
    }
    if (std::filesystem::exists(absolute_destination_dir)) {
        if (!std::filesystem::is_directory(absolute_destination_dir)) {
            throw PlatformError("the C++ data path is not a directory");
        }
        if (!directory_empty(absolute_destination_dir)) {
            throw PlatformError(
                "Python import requires an empty C++ data directory");
        }
    } else if (!std::filesystem::create_directories(
                   absolute_destination_dir, error) ||
               error) {
        throw PlatformError("the C++ data directory could not be created");
    }
    if (std::filesystem::exists(destination)) {
        throw PlatformError(
            "the C++ learning store already exists; import will not overwrite it");
    }

    Database source_database;
    source_database.open(
        absolute_source,
        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX);
    validate_schema(source_database.get());
    const auto profiles = count_rows(source_database.get(), "profiles");
    const auto decisions =
        count_rows(source_database.get(), "decision_records");
    const auto models = count_rows(source_database.get(), "model_state");

    Database destination_database;
    bool created_destination = false;
    try {
        destination_database.open(
            destination,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_EXRESCODE);
        created_destination = true;
        backup_database(
            source_database.get(), destination_database.get());
        validate_schema(destination_database.get());
        if (count_rows(destination_database.get(), "profiles") != profiles ||
            count_rows(destination_database.get(), "decision_records") !=
                decisions ||
            count_rows(destination_database.get(), "model_state") != models) {
            throw PlatformError(
                "imported learning-store row counts do not match the backup");
        }
        destination_database.close();
    } catch (...) {
        destination_database.close();
        source_database.close();
        if (created_destination) {
            std::filesystem::remove(destination, error);
        }
        throw;
    }
    source_database.close();

    return ImportReport{
        absolute_source,
        destination,
        profiles,
        decisions,
        models,
        1};
}

}  // namespace schiva
