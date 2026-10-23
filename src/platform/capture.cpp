#include "schiva/platform.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace schiva {
namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

}  // namespace

class VisualCaptureWorker::Impl
    : public std::enable_shared_from_this<VisualCaptureWorker::Impl> {
public:
    Impl(
        CaptureFunction capture,
        double interval,
        double request_max_age,
        double result_max_age,
        double close_timeout)
        : capture_(std::move(capture)),
          interval_(interval),
          request_max_age_(request_max_age),
          result_max_age_(result_max_age),
          close_timeout_(close_timeout) {
        if (interval_ <= 0.0 || request_max_age_ <= 0.0 ||
            result_max_age_ <= 0.0 || close_timeout_ <= 0.0) {
            throw PlatformError("capture worker intervals must be positive");
        }
    }

    struct Request {
        std::uint64_t sequence{};
        int generation{};
        std::uint64_t validity{};
        Rect bounds;
        std::vector<Rect> masks;
        Clock::time_point queued_at;
    };

    struct Outcome {
        int generation{};
        std::optional<VisualFrame> frame;
        std::optional<std::string> error;
        Clock::time_point completed_at;
    };

    void invalidate(int generation) {
        std::lock_guard lock(mutex_);
        generation_ = generation;
        ++validity_;
        pending_.reset();
        result_.reset();
        condition_.notify_all();
    }

    void set_paused(bool paused) {
        std::lock_guard lock(mutex_);
        paused_ = paused;
        if (paused) {
            ++validity_;
            pending_.reset();
            result_.reset();
        }
        condition_.notify_all();
    }

    void set_suppressed(bool suppressed) {
        std::lock_guard lock(mutex_);
        if (suppressed_ == suppressed) {
            return;
        }
        suppressed_ = suppressed;
        if (suppressed) {
            ++validity_;
            pending_.reset();
            result_.reset();
        }
        condition_.notify_all();
    }

    bool request(int generation, Rect bounds, std::vector<Rect> masks) {
        std::lock_guard lock(mutex_);
        if (closed_ || paused_ || suppressed_ || generation != generation_) {
            return false;
        }
        ++sequence_;
        pending_ = Request{
            sequence_,
            generation_,
            validity_,
            bounds,
            std::move(masks),
            Clock::now()};
        if (!thread_.joinable()) {
            auto self = shared_from_this();
            thread_ = std::thread([self] { self->run(); });
        } else {
            condition_.notify_all();
        }
        return true;
    }

    std::optional<Outcome> poll(int generation) {
        std::optional<Outcome> outcome;
        {
            std::lock_guard lock(mutex_);
            outcome = std::move(result_);
            result_.reset();
            if (!outcome.has_value() || closed_ || paused_ || suppressed_ ||
                outcome->generation != generation ||
                seconds_since(outcome->completed_at) > result_max_age_) {
                return std::nullopt;
            }
            if (outcome->error.has_value()) {
                last_error_ = outcome->error;
                last_error_generation_ = generation;
            } else {
                last_error_.reset();
                last_error_generation_ = generation;
            }
        }
        return outcome;
    }

    std::optional<std::string> capture_error(int generation) const {
        std::lock_guard lock(mutex_);
        if (generation == generation_ &&
            generation == last_error_generation_) {
            return last_error_;
        }
        return std::nullopt;
    }

    bool thread_started() const {
        std::lock_guard lock(mutex_);
        return thread_.joinable();
    }

    Json metrics() const {
        std::lock_guard lock(mutex_);
        Json summary{
            {"latest", nullptr},
            {"avg", nullptr},
            {"max", nullptr},
            {"samples", 0}};
        if (!samples_.empty()) {
            const double latest = samples_.back();
            double total = 0.0;
            double maximum = 0.0;
            for (const double value : samples_) {
                total += value;
                maximum = std::max(maximum, value);
            }
            summary["latest"] = latest;
            summary["avg"] = total / static_cast<double>(samples_.size());
            summary["max"] = maximum;
            summary["samples"] = samples_.size();
        }
        return {
            {"capture_duration_ms", summary},
            {"close_timeout_ms", close_timeout_ * 1000.0}};
    }

    void close() {
        std::thread thread;
        {
            std::lock_guard lock(mutex_);
            if (closed_) {
                return;
            }
            closed_ = true;
            ++validity_;
            pending_.reset();
            result_.reset();
            condition_.notify_all();
            if (thread_.joinable()) {
                thread = std::move(thread_);
            }
        }
        if (thread.joinable()) {
            if (thread.get_id() == std::this_thread::get_id()) {
                thread.detach();
            } else {
                // The capture thread owns a shared reference to this state.
                // Detaching keeps shutdown bounded even if a native GDI call
                // is stuck; the thread releases its state when that call
                // eventually returns.
                thread.detach();
            }
        }
    }

private:
    void run() {
        for (;;) {
            Request request;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] {
                    return closed_ || pending_.has_value() ||
                           paused_ || suppressed_;
                });
                if (closed_) {
                    return;
                }
                if (paused_ || suppressed_ || !pending_.has_value()) {
                    continue;
                }
                const auto now = Clock::now();
                if (seconds_since(pending_->queued_at) >
                    request_max_age_) {
                    pending_.reset();
                    continue;
                }
                const auto earliest = last_capture_started_ +
                    std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(interval_));
                if (now < earliest) {
                    condition_.wait_until(lock, earliest);
                    continue;
                }
                request = *pending_;
                pending_.reset();
                if (request.generation != generation_ ||
                    request.validity != validity_) {
                    continue;
                }
                last_capture_started_ = now;
            }

            const auto started = Clock::now();
            std::optional<VisualFrame> frame;
            std::optional<std::string> error;
            try {
                frame = capture_(request.bounds, request.masks);
            } catch (const PlatformError& exception) {
                error = exception.what();
            } catch (const std::exception& exception) {
                error = std::string("desktop capture failed safely: ") +
                        exception.what();
            } catch (...) {
                error = "desktop capture failed safely";
            }
            const auto completed = Clock::now();
            const double elapsed = seconds_since(started);

            std::lock_guard lock(mutex_);
            samples_.push_back(elapsed * 1000.0);
            while (samples_.size() > 64) {
                samples_.pop_front();
            }
            if (closed_ || paused_ || suppressed_ ||
                request.generation != generation_ ||
                request.validity != validity_) {
                continue;
            }
            result_ = Outcome{
                request.generation,
                std::move(frame),
                std::move(error),
                completed};
            condition_.notify_all();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    CaptureFunction capture_;
    double interval_;
    double request_max_age_;
    double result_max_age_;
    double close_timeout_;
    std::optional<Request> pending_;
    std::optional<Outcome> result_;
    std::deque<double> samples_;
    std::optional<std::string> last_error_;
    int last_error_generation_{-1};
    Clock::time_point last_capture_started_{};
    std::uint64_t sequence_{};
    std::uint64_t validity_{};
    int generation_{};
    bool paused_{};
    bool suppressed_{};
    bool closed_{};
    std::thread thread_;
};

VisualCaptureWorker::VisualCaptureWorker(
    CaptureFunction capture,
    double interval_seconds,
    double request_max_age_seconds,
    double result_max_age_seconds,
    double close_timeout_seconds)
    : impl_(std::make_shared<Impl>(
          std::move(capture),
          interval_seconds,
          request_max_age_seconds,
          result_max_age_seconds,
          close_timeout_seconds)) {}

VisualCaptureWorker::~VisualCaptureWorker() {
    close();
}

void VisualCaptureWorker::invalidate(int generation) {
    impl_->invalidate(generation);
}

void VisualCaptureWorker::pause() {
    impl_->set_paused(true);
}

void VisualCaptureWorker::resume() {
    impl_->set_paused(false);
}

void VisualCaptureWorker::set_suppressed(bool suppressed) {
    impl_->set_suppressed(suppressed);
}

bool VisualCaptureWorker::request(
    int generation,
    const Rect& bounds,
    std::vector<Rect> masks) {
    return impl_->request(generation, bounds, std::move(masks));
}

std::optional<VisualFrame> VisualCaptureWorker::poll(int generation) {
    const auto outcome = impl_->poll(generation);
    if (!outcome.has_value()) {
        return std::nullopt;
    }
    return outcome->frame;
}

std::optional<VisualFrame> VisualCaptureWorker::wait_for_result(
    int generation,
    double timeout_seconds) {
    const auto deadline = Clock::now() +
        std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(std::max(0.0, timeout_seconds)));
    while (Clock::now() < deadline) {
        auto result = poll(generation);
        if (result.has_value()) {
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return poll(generation);
}

std::optional<std::string> VisualCaptureWorker::capture_error(
    int generation) const {
    return impl_->capture_error(generation);
}

bool VisualCaptureWorker::thread_started() const {
    return impl_->thread_started();
}

Json VisualCaptureWorker::metrics() const {
    return impl_->metrics();
}

void VisualCaptureWorker::close() {
    impl_->close();
}

}  // namespace schiva
