#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace schiva::detail {

inline constexpr wchar_t kManualPickerClass[] =
    L"DuckEggsManualPicker";

struct ManualWindowIdentity {
    std::uintptr_t handle{};
    std::uint32_t process_id{};
    std::uint64_t process_creation_time{};
    std::wstring class_name;
    std::wstring executable;
    std::uintptr_t lifetime_marker{};
};

struct ManualTopmostLease {
    bool originally_topmost{};
    bool owns_promotion{};
};

enum class ManualTopmostAction {
    None,
    Promote,
    Restore,
};

// A new lease only remembers the original style. Promotion ownership is
// committed immediately after SetWindowPos reports success, before its result
// is verified, so a failed verification remains eligible for rollback.
constexpr ManualTopmostLease manual_topmost_lease(
    bool originally_topmost) noexcept {
    return {originally_topmost, false};
}

constexpr void manual_topmost_commit_promotion(
    ManualTopmostLease& lease) noexcept {
    if (!lease.originally_topmost) {
        lease.owns_promotion = true;
    }
}

// While a binding is active its target must be topmost, including while
// DuckEggs is paused. On release, only a promotion that DuckEggs actually owns
// on an originally non-topmost window may be undone.
constexpr ManualTopmostAction manual_topmost_action(
    const ManualTopmostLease& lease,
    bool binding_active,
    bool currently_topmost) noexcept {
    if (binding_active) {
        return currently_topmost ? ManualTopmostAction::None
                                 : ManualTopmostAction::Promote;
    }
    if (lease.originally_topmost || !lease.owns_promotion) {
        return ManualTopmostAction::None;
    }
    return currently_topmost ? ManualTopmostAction::Restore
                             : ManualTopmostAction::None;
}

inline wchar_t manual_identity_fold(wchar_t value) {
    if (value >= L'A' && value <= L'Z') {
        return static_cast<wchar_t>(value + (L'a' - L'A'));
    }
    return value;
}

inline bool manual_identity_executable_matches(
    std::wstring_view first,
    std::wstring_view second) {
    return first.size() == second.size() &&
           std::equal(
               first.begin(),
               first.end(),
               second.begin(),
               [](wchar_t left, wchar_t right) {
                   return manual_identity_fold(left) ==
                          manual_identity_fold(right);
               });
}

inline bool manual_window_identity_matches(
    const ManualWindowIdentity& bound,
    const ManualWindowIdentity& current) {
    return bound.handle != 0 &&
           current.handle == bound.handle &&
           bound.process_id != 0 &&
           current.process_id == bound.process_id &&
           bound.process_creation_time != 0 &&
           current.process_creation_time == bound.process_creation_time &&
           !bound.class_name.empty() &&
           current.class_name == bound.class_name &&
           !bound.executable.empty() &&
           manual_identity_executable_matches(
               bound.executable, current.executable) &&
           bound.lifetime_marker != 0 &&
           current.lifetime_marker == bound.lifetime_marker;
}

}  // namespace schiva::detail
