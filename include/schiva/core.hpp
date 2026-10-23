#pragma once

#include "schiva/types.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace schiva {

namespace detail {
class StoreAccess;
}

class LearningStore {
public:
    explicit LearningStore(const std::filesystem::path& path);
    ~LearningStore();

    LearningStore(const LearningStore&) = delete;
    LearningStore& operator=(const LearningStore&) = delete;
    LearningStore(LearningStore&&) = delete;
    LearningStore& operator=(LearningStore&&) = delete;

    [[nodiscard]] Json stats() const;
    void flush();
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    friend class Controller;
    friend class detail::StoreAccess;
};

class Controller {
public:
    explicit Controller(LearningStore& store, bool observe_only = false);
    ~Controller();

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    [[nodiscard]] bool paused() const;
    void set_paused(bool paused);
    [[nodiscard]] Decision tick(double monotonic_seconds,
                                 const Observation& observation);
    void movement_failed();
    [[nodiscard]] Json status() const;
    [[nodiscard]] Json insights(
        std::optional<std::string> profile_key = std::nullopt) const;
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace schiva
