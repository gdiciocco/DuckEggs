#include "internal.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace schiva {
namespace {

constexpr int kSchemaVersion = 1;
constexpr int kMaxProfiles = 100;
constexpr int kMaxDecisions = 1000;
constexpr int kBatchSize = 16;

std::runtime_error storage_error(const std::string& message) {
    return std::runtime_error(message);
}

std::string utf8_string(const std::filesystem::path& path) {
    const std::u8string encoded = path.u8string();
    return std::string(encoded.begin(), encoded.end());
}

class Statement {
public:
    Statement(sqlite3* database, const char* sql) : database_(database) {
        if (sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr) !=
            SQLITE_OK) {
            throw storage_error(std::string("SQLite prepare failed: ") +
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

    [[nodiscard]] sqlite3_stmt* get() const { return statement_; }

    void require_done() {
        const int result = sqlite3_step(statement_);
        if (result != SQLITE_DONE) {
            throw storage_error(std::string("SQLite write failed: ") +
                                sqlite3_errmsg(database_));
        }
    }

private:
    sqlite3* database_{};
    sqlite3_stmt* statement_{};
};

void bind_text(sqlite3_stmt* statement, int index,
               const std::string& value) {
    if (sqlite3_bind_text(statement, index, value.c_str(),
                          static_cast<int>(value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        throw storage_error("unable to bind SQLite text");
    }
}

std::string column_text(sqlite3_stmt* statement, int column) {
    const auto* value = sqlite3_column_text(statement, column);
    if (value == nullptr) {
        return {};
    }
    return reinterpret_cast<const char*>(value);
}

std::string compact_dump(const Json& value) {
    return value.dump(-1, ' ', false,
                      Json::error_handler_t::strict);
}

}  // namespace

struct LearningStore::Impl {
    struct PendingDecision {
        std::string profile_key;
        std::string features_json;
        std::string action;
        double reward{};
    };

    explicit Impl(std::filesystem::path input_path) {
        owner = std::this_thread::get_id();
        if (input_path == ":memory:") {
            path = ":memory:";
        } else {
            path = utf8_string(input_path.lexically_normal());
            const std::filesystem::path parent =
                std::filesystem::path(input_path).parent_path();
            if (!parent.empty()) {
                std::error_code error;
                std::filesystem::create_directories(parent, error);
                if (error) {
                    throw storage_error(
                        "unable to create learning store directory: " +
                        error.message());
                }
            }
        }

        const int flags =
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        if (sqlite3_open_v2(path.c_str(), &database, flags, nullptr) !=
            SQLITE_OK) {
            std::string message = "unable to open learning store";
            if (database != nullptr) {
                message += ": ";
                message += sqlite3_errmsg(database);
                sqlite3_close(database);
                database = nullptr;
            }
            throw storage_error(message);
        }
        sqlite3_busy_timeout(database, 2000);
        try {
            prepare_schema();
            profile_count_value = count("profiles");
            decision_count_value = count("decision_records");
        } catch (...) {
            sqlite3_close(database);
            database = nullptr;
            throw;
        }
    }

    ~Impl() {
        if (database != nullptr) {
            sqlite3_close(database);
        }
    }

    void ensure_open() const {
        if (closed) {
            throw storage_error("learning store is closed");
        }
    }

    void ensure_owner() const {
        if (std::this_thread::get_id() != owner) {
            throw storage_error(
                "learning store reads must use the owner worker thread");
        }
    }

    void execute(const char* sql) {
        char* message = nullptr;
        if (sqlite3_exec(database, sql, nullptr, nullptr, &message) !=
            SQLITE_OK) {
            const std::string detail =
                message == nullptr ? sqlite3_errmsg(database) : message;
            sqlite3_free(message);
            throw storage_error("SQLite operation failed: " + detail);
        }
    }

    void prepare_schema() {
        int version = 0;
        {
            Statement statement(database, "PRAGMA user_version");
            const int result = sqlite3_step(statement.get());
            if (result != SQLITE_ROW) {
                throw storage_error(
                    "invalid learning store schema version");
            }
            version = sqlite3_column_int(statement.get(), 0);
        }
        if (version > kSchemaVersion) {
            throw storage_error(
                "learning store schema " + std::to_string(version) +
                " is newer than supported " +
                std::to_string(kSchemaVersion));
        }
        if (version < 0) {
            throw storage_error(
                "learning store has an invalid schema version");
        }
        if (version == 0) {
            execute("BEGIN");
            try {
                execute(
                    "CREATE TABLE IF NOT EXISTS profiles ("
                    "profile_key TEXT PRIMARY KEY,"
                    "app_id TEXT NOT NULL,"
                    "role TEXT NOT NULL,"
                    "layout_bin TEXT NOT NULL,"
                    "short_map TEXT NOT NULL,"
                    "long_map TEXT NOT NULL,"
                    "manual_map TEXT NOT NULL)");
                execute(
                    "CREATE TABLE IF NOT EXISTS decision_records ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "profile_key TEXT NOT NULL,"
                    "features_json TEXT NOT NULL,"
                    "action TEXT NOT NULL,"
                    "reward REAL NOT NULL)");
                execute(
                    "CREATE TABLE IF NOT EXISTS model_state ("
                    "id INTEGER PRIMARY KEY CHECK (id = 1),"
                    "state_json TEXT NOT NULL,"
                    "updates INTEGER NOT NULL)");
                validate_tables();
                execute("PRAGMA user_version = 1");
                execute("COMMIT");
            } catch (...) {
                try {
                    execute("ROLLBACK");
                } catch (...) {
                }
                throw;
            }
        } else {
            validate_tables();
        }
    }

    std::set<std::string> table_columns(const std::string& table) {
        Statement statement(
            database, ("PRAGMA table_info(" + table + ")").c_str());
        std::set<std::string> columns;
        int result = SQLITE_ROW;
        while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
            columns.insert(column_text(statement.get(), 1));
        }
        if (result != SQLITE_DONE) {
            throw storage_error("unable to inspect learning store table " +
                                table);
        }
        return columns;
    }

    void validate_tables() {
        const std::map<std::string, std::set<std::string>> expected = {
            {"profiles",
             {"profile_key", "app_id", "role", "layout_bin", "short_map",
              "long_map", "manual_map"}},
            {"decision_records",
             {"id", "profile_key", "features_json", "action", "reward"}},
            {"model_state", {"id", "state_json", "updates"}}};
        for (const auto& [table, columns] : expected) {
            const auto present = table_columns(table);
            for (const std::string& column : columns) {
                if (!present.contains(column)) {
                    throw storage_error(
                        "learning store table " + table +
                        " has an incompatible schema");
                }
            }
        }
    }

    int count(const std::string& table) {
        Statement statement(
            database, ("SELECT COUNT(*) FROM " + table).c_str());
        const int result = sqlite3_step(statement.get());
        if (result != SQLITE_ROW) {
            throw storage_error("unable to read " + table);
        }
        return sqlite3_column_int(statement.get(), 0);
    }

    static Json decode_map(const std::string& raw) {
        Json result = Json::object();
        try {
            const Json parsed = Json::parse(raw);
            if (!parsed.is_object()) {
                return result;
            }
            for (auto iterator = parsed.begin(); iterator != parsed.end();
                 ++iterator) {
                if (!iterator.value().is_number()) {
                    continue;
                }
                const double value = iterator.value().get<double>();
                if (std::isfinite(value) && value >= 0 &&
                    value <= 1000) {
                    result[iterator.key()] = value;
                }
            }
        } catch (const std::exception&) {
        }
        return result;
    }

    static detail::ProfileRow read_profile(sqlite3_stmt* statement) {
        return {column_text(statement, 0), column_text(statement, 1),
                column_text(statement, 2), column_text(statement, 3),
                decode_map(column_text(statement, 4)),
                decode_map(column_text(statement, 5)),
                decode_map(column_text(statement, 6))};
    }

    std::optional<detail::ProfileRow> peek_profile_sql(const std::string& key) {
        Statement statement(
            database,
            "SELECT profile_key, app_id, role, layout_bin, short_map, "
            "long_map, manual_map FROM profiles WHERE profile_key = ?");
        bind_text(statement.get(), 1, key);
        const int result = sqlite3_step(statement.get());
        if (result == SQLITE_ROW) {
            return read_profile(statement.get());
        }
        if (result != SQLITE_DONE) {
            throw storage_error("unable to read learning profile");
        }
        return std::nullopt;
    }

    std::vector<detail::ProfileRow> list_profiles_sql() {
        Statement statement(
            database,
            "SELECT profile_key, app_id, role, layout_bin, short_map, "
            "long_map, manual_map FROM profiles");
        std::map<std::string, detail::ProfileRow> rows;
        int result = SQLITE_ROW;
        while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
            detail::ProfileRow profile = read_profile(statement.get());
            rows[profile.profile_key] = std::move(profile);
        }
        if (result != SQLITE_DONE) {
            throw storage_error("unable to list learning profiles");
        }
        for (const auto& [key, profile] : profile_cache) {
            rows[key] = profile;
        }
        std::vector<detail::ProfileRow> result_rows;
        result_rows.reserve(rows.size());
        for (auto& [key, profile] : rows) {
            (void)key;
            result_rows.push_back(std::move(profile));
        }
        return result_rows;
    }

    std::optional<Json> peek_model_sql() {
        if (model_cache.has_value()) {
            return *model_cache;
        }
        Statement statement(
            database,
            "SELECT state_json FROM model_state WHERE id = 1");
        const int result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            return std::nullopt;
        }
        if (result != SQLITE_ROW) {
            throw storage_error("unable to read adaptive model");
        }
        try {
            const Json value =
                Json::parse(column_text(statement.get(), 0));
            if (value.is_object()) {
                return value;
            }
        } catch (const std::exception&) {
        }
        return std::nullopt;
    }

    std::optional<detail::ProfileRow> load_profile(
        const std::string& key, const std::string& app_id,
        const std::string& role, const std::string& layout_bin) {
        auto cached = profile_cache.find(key);
        if (cached != profile_cache.end()) {
            return cached->second;
        }
        std::optional<detail::ProfileRow> existing = peek_profile_sql(key);
        if (existing.has_value()) {
            profile_cache[key] = *existing;
            flush_if_needed();
            return existing;
        }
        if (profile_count_value >= kMaxProfiles) {
            return std::nullopt;
        }
        detail::ProfileRow profile{key, app_id, role, layout_bin, Json::object(),
                           Json::object(), Json::object()};
        profile_cache[key] = profile;
        ++profile_count_value;
        dirty_profiles.insert(key);
        flush_if_needed();
        return profile;
    }

    bool save_profile(const detail::ProfileRow& profile) {
        const auto cached = profile_cache.find(profile.profile_key);
        if (cached == profile_cache.end() ||
            cached->second.app_id != profile.app_id) {
            return false;
        }
        profile_cache[profile.profile_key] = profile;
        dirty_profiles.insert(profile.profile_key);
        flush_if_needed();
        return true;
    }

    std::optional<Json> load_model() {
        if (model_cache.has_value()) {
            return *model_cache;
        }
        std::optional<Json> value = peek_model_sql();
        if (value.has_value()) {
            model_cache = *value;
            return value;
        }
        return std::nullopt;
    }

    void save_model(const Json& state) {
        model_cache = state;
        model_dirty = true;
        flush_if_needed();
    }

    bool add_decision(const std::string& profile_key,
                      const std::vector<double>& features,
                      const std::string& action, double reward) {
        if (decision_count_value >= kMaxDecisions) {
            ++dropped_records_value;
            return false;
        }
        Json rounded = Json::array();
        for (double value : features) {
            rounded.push_back(
                static_cast<double>(detail::python_round(value * 1e8)) / 1e8);
        }
        pending_decisions.push_back(
            {profile_key, compact_dump(rounded), action, reward});
        ++decision_count_value;
        flush_if_needed();
        return true;
    }

    std::unordered_map<std::string, int> decision_counts() {
        Statement statement(
            database,
            "SELECT profile_key, COUNT(*) FROM decision_records "
            "GROUP BY profile_key");
        std::unordered_map<std::string, int> result_counts;
        int result = SQLITE_ROW;
        while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
            result_counts[column_text(statement.get(), 0)] =
                sqlite3_column_int(statement.get(), 1);
        }
        if (result != SQLITE_DONE) {
            throw storage_error(
                "unable to count learning decisions");
        }
        for (const PendingDecision& decision : pending_decisions) {
            ++result_counts[decision.profile_key];
        }
        return result_counts;
    }

    void flush_if_needed() {
        const std::size_t dirty =
            dirty_profiles.size() + pending_decisions.size() +
            (model_dirty ? 1U : 0U);
        if (dirty >= static_cast<std::size_t>(kBatchSize)) {
            flush();
        }
    }

    void flush() {
        ensure_open();
        ensure_owner();
        if (dirty_profiles.empty() && pending_decisions.empty() &&
            !model_dirty) {
            return;
        }
        execute("BEGIN");
        try {
            for (const std::string& key : dirty_profiles) {
                const detail::ProfileRow& profile = profile_cache.at(key);
                Statement statement(
                    database,
                    "INSERT INTO profiles "
                    "(profile_key, app_id, role, layout_bin, short_map, "
                    "long_map, manual_map) VALUES (?, ?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(profile_key) DO UPDATE SET "
                    "short_map = excluded.short_map, "
                    "long_map = excluded.long_map, "
                    "manual_map = excluded.manual_map");
                bind_text(statement.get(), 1, profile.profile_key);
                bind_text(statement.get(), 2, profile.app_id);
                bind_text(statement.get(), 3, profile.role);
                bind_text(statement.get(), 4, profile.layout_bin);
                bind_text(statement.get(), 5, compact_dump(profile.short_map));
                bind_text(statement.get(), 6, compact_dump(profile.long_map));
                bind_text(statement.get(), 7,
                          compact_dump(profile.manual_map));
                statement.require_done();
            }
            for (const PendingDecision& decision : pending_decisions) {
                Statement statement(
                    database,
                    "INSERT INTO decision_records "
                    "(profile_key, features_json, action, reward) "
                    "VALUES (?, ?, ?, ?)");
                bind_text(statement.get(), 1, decision.profile_key);
                bind_text(statement.get(), 2, decision.features_json);
                bind_text(statement.get(), 3, decision.action);
                if (sqlite3_bind_double(statement.get(), 4,
                                        decision.reward) != SQLITE_OK) {
                    throw storage_error(
                        "unable to bind decision reward");
                }
                statement.require_done();
            }
            if (model_dirty && model_cache.has_value()) {
                const int updates =
                    model_cache->value("updates", 0);
                Statement statement(
                    database,
                    "INSERT INTO model_state (id, state_json, updates) "
                    "VALUES (1, ?, ?) ON CONFLICT(id) DO UPDATE SET "
                    "state_json = excluded.state_json, "
                    "updates = excluded.updates");
                bind_text(statement.get(), 1,
                          compact_dump(*model_cache));
                sqlite3_bind_int(statement.get(), 2, updates);
                statement.require_done();
            }
            execute("COMMIT");
        } catch (const std::exception& error) {
            try {
                execute("ROLLBACK");
            } catch (...) {
            }
            throw storage_error(std::string("unable to flush learning state: ") +
                                error.what());
        }
        dirty_profiles.clear();
        pending_decisions.clear();
        model_dirty = false;
    }

    void close() {
        if (closed) {
            return;
        }
        try {
            flush();
        } catch (...) {
            closed = true;
            sqlite3_close(database);
            database = nullptr;
            throw;
        }
        closed = true;
        sqlite3_close(database);
        database = nullptr;
    }

    std::string path;
    sqlite3* database{};
    std::thread::id owner;
    bool closed{};
    std::set<std::string> dirty_profiles;
    bool model_dirty{};
    std::vector<PendingDecision> pending_decisions;
    std::map<std::string, detail::ProfileRow> profile_cache;
    std::optional<Json> model_cache;
    int profile_count_value{};
    int decision_count_value{};
    int dropped_records_value{};
};

LearningStore::LearningStore(const std::filesystem::path& path)
    : impl_(std::make_unique<Impl>(path)) {}

LearningStore::~LearningStore() {
    if (impl_ != nullptr) {
        try {
            impl_->close();
        } catch (...) {
        }
    }
}

Json LearningStore::stats() const {
    impl_->ensure_open();
    impl_->ensure_owner();
    return {{"schema_version", kSchemaVersion},
            {"profiles", impl_->profile_count_value},
            {"profile_limit", kMaxProfiles},
            {"decision_records", impl_->decision_count_value},
            {"decision_limit", kMaxDecisions},
            {"dropped_records", impl_->dropped_records_value},
            {"dirty_profiles",
             static_cast<int>(impl_->dirty_profiles.size())},
            {"dirty_decisions",
             static_cast<int>(impl_->pending_decisions.size())}};
}

void LearningStore::flush() {
    impl_->flush();
}

void LearningStore::close() {
    impl_->close();
}

namespace detail {

std::optional<detail::ProfileRow> StoreAccess::load_profile(
    LearningStore& store, const std::string& key, const std::string& app_id,
    const std::string& role, const std::string& layout_bin) {
    store.impl_->ensure_open();
    store.impl_->ensure_owner();
    return store.impl_->load_profile(key, app_id, role, layout_bin);
}

bool StoreAccess::save_profile(LearningStore& store,
                               const detail::ProfileRow& profile) {
    store.impl_->ensure_open();
    store.impl_->ensure_owner();
    return store.impl_->save_profile(profile);
}

std::vector<detail::ProfileRow> StoreAccess::list_profiles(
    const LearningStore& store) {
    store.impl_->ensure_open();
    store.impl_->ensure_owner();
    return store.impl_->list_profiles_sql();
}

std::optional<detail::ProfileRow> StoreAccess::peek_profile(
    const LearningStore& store, const std::string& key) {
    store.impl_->ensure_open();
    store.impl_->ensure_owner();
    const auto cached = store.impl_->profile_cache.find(key);
    if (cached != store.impl_->profile_cache.end()) {
        return cached->second;
    }
    return store.impl_->peek_profile_sql(key);
}

std::optional<Json> StoreAccess::load_model(LearningStore& store) {
    store.impl_->ensure_open();
    store.impl_->ensure_owner();
    return store.impl_->load_model();
}

std::optional<Json> StoreAccess::peek_model(const LearningStore& store) {
    store.impl_->ensure_open();
    store.impl_->ensure_owner();
    return store.impl_->peek_model_sql();
}

void StoreAccess::save_model(LearningStore& store, const Json& state) {
    store.impl_->ensure_open();
    store.impl_->ensure_owner();
    store.impl_->save_model(state);
}

bool StoreAccess::add_decision(LearningStore& store,
                               const std::string& profile_key,
                               const std::vector<double>& features,
                               const std::string& action, double reward) {
    store.impl_->ensure_open();
    store.impl_->ensure_owner();
    return store.impl_->add_decision(profile_key, features, action, reward);
}

std::unordered_map<std::string, int> StoreAccess::decision_counts(
    const LearningStore& store) {
    store.impl_->ensure_open();
    store.impl_->ensure_owner();
    return store.impl_->decision_counts();
}

int StoreAccess::profile_count(const LearningStore& store) {
    return store.impl_->profile_count_value;
}

int StoreAccess::decision_count(const LearningStore& store) {
    return store.impl_->decision_count_value;
}

int StoreAccess::dropped_records(const LearningStore& store) {
    return store.impl_->dropped_records_value;
}

int StoreAccess::dirty_profiles(const LearningStore& store) {
    return static_cast<int>(store.impl_->dirty_profiles.size());
}

int StoreAccess::dirty_decisions(const LearningStore& store) {
    return static_cast<int>(store.impl_->pending_decisions.size());
}

}  // namespace detail
}  // namespace schiva
