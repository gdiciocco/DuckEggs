#include "internal.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using schiva::Json;

class Database {
public:
    explicit Database(const std::filesystem::path& path) {
        const std::u8string encoded = path.u8string();
        const std::string name(encoded.begin(), encoded.end());
        if (sqlite3_open_v2(name.c_str(), &db_, SQLITE_OPEN_READONLY,
                            nullptr) != SQLITE_OK) {
            const std::string detail =
                db_ == nullptr ? "unknown error" : sqlite3_errmsg(db_);
            if (db_ != nullptr) {
                sqlite3_close(db_);
                db_ = nullptr;
            }
            throw std::runtime_error("cannot open temporary database: " +
                                     detail);
        }
    }
    ~Database() { sqlite3_close(db_); }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    sqlite3* get() const { return db_; }

private:
    sqlite3* db_{};
};

Json query(sqlite3* db, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error("temporary database query failed");
    }
    const int step = sqlite3_step(statement);
    const auto* value = sqlite3_column_text(statement, 0);
    const std::string raw =
        value == nullptr
            ? std::string{}
            : std::string(reinterpret_cast<const char*>(value));
    sqlite3_finalize(statement);
    if (step != SQLITE_ROW) {
        throw std::runtime_error("temporary database read failed");
    }
    return Json::parse(raw);
}

void add_scores(Json& item, const schiva::detail::LinUCBModel& model,
                const std::vector<double>& features) {
    item["prediction"] = model.predict(features);
    item["score"] = model.score(features);
}

Json inspect(const std::filesystem::path& path) {
    schiva::LearningStore store(path);
    const auto persisted =
        schiva::detail::StoreAccess::load_model(store);
    if (!persisted.has_value()) {
        throw std::runtime_error("model_state row is missing or invalid");
    }
    const schiva::detail::LinUCBModel model =
        schiva::detail::LinUCBModel::from_state(*persisted);
    Database database(path);
    const Json model_rows =
        query(database.get(),
              "SELECT json_group_array(json_object("
              "'id',id,'updates',updates)) FROM model_state");
    if (model_rows.size() != 1) {
        throw std::runtime_error(
            "model_state must contain exactly one row");
    }

    Json result = {
        {"schema_version",
         query(database.get(),
               "SELECT json_object('version',user_version) "
               "FROM pragma_user_version")["version"]},
        {"model",
         {{"row_id", model_rows[0]["id"]},
          {"row_updates", model_rows[0]["updates"]},
          {"state", model.state()}}},
        {"profiles", Json::array()},
        {"decisions", Json::array()},
        {"vectors", Json::array()}};
    for (const auto& profile :
         schiva::detail::StoreAccess::list_profiles(store)) {
        result["profiles"].push_back(
            {{"profile_key", profile.profile_key},
             {"app_id", profile.app_id},
             {"role", profile.role},
             {"layout_bin", profile.layout_bin},
             {"short_map", profile.short_map},
             {"long_map", profile.long_map},
             {"manual_map", profile.manual_map}});
    }

    Json ranking = Json::array();
    Json profile_counts = Json::object();
    for (const auto& [key, count] :
         schiva::detail::StoreAccess::decision_counts(store)) {
        profile_counts[key] = count;
    }
    for (const Json& row :
         query(database.get(),
               "SELECT json_group_array(json_object("
               "'id',id,'profile_key',profile_key,'features',json(features_json),"
               "'action',action,'reward',reward)) FROM "
               "(SELECT id,profile_key,features_json,action,reward "
               "FROM decision_records ORDER BY id)")) {
        const Json& parsed = row["features"];
        Json decision = {
            {"id", row["id"]},
            {"profile_key", row["profile_key"]},
            {"features", parsed},
            {"action", row["action"]},
            {"reward", row["reward"]}};
        add_scores(decision, model,
                   parsed.get<std::vector<double>>());
        result["decisions"].push_back(decision);
        ranking.push_back(std::move(decision));
    }
    result["decision_counts"] = {
        {"records", static_cast<int>(result["decisions"].size())},
        {"by_profile", profile_counts}};
    const auto by_score = [](const Json& left, const Json& right) {
        const double first = left["score"].get<double>();
        const double second = right["score"].get<double>();
        return first != second
                   ? first > second
                   : left["id"].get<int>() < right["id"].get<int>();
    };
    std::sort(ranking.begin(), ranking.end(), by_score);
    result["action_ranking"] = std::move(ranking);

    const std::vector<std::pair<std::string, std::vector<double>>> vectors = {
        {"northwest", {1, -1, -1, -1, -1, -1, -1, 0, 0, .15, .05, 0}},
        {"center", {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, .20, 0}},
        {"southeast-low-risk", {1, 1, 1, 1, 1, 1, 1, 0, 0, .60, 0, .10}}};
    Json vector_ranking = Json::array();
    for (const auto& [label, features] : vectors) {
        Json item = {{"label", label}, {"features", features}};
        add_scores(item, model, features);
        result["vectors"].push_back(item);
        vector_ranking.push_back(std::move(item));
    }
    std::sort(vector_ranking.begin(), vector_ranking.end(),
              [](const Json& left, const Json& right) {
                  const double first = left["score"].get<double>();
                  const double second = right["score"].get<double>();
                  return first != second
                             ? first > second
                             : left["label"].get<std::string>() <
                                   right["label"].get<std::string>();
              });
    result["fixed_vector_ranking"] = std::move(vector_ranking);
    store.close();
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: inspect_backup TEMP_COPY.sqlite3\n";
        return 2;
    }
    try {
        std::cout << inspect(std::filesystem::path(argv[1])).dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "inspect_backup: " << error.what() << '\n';
        return 1;
    }
}
