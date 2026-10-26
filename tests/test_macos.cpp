#include "platform/macos_shared.hpp"
#include "platform/macos_visual.hpp"

#include <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_screen_coordinates() {
    using schiva::detail::cocoa_to_quartz_point;
    using schiva::detail::cocoa_to_quartz_rect;
    using schiva::detail::quartz_to_cocoa_point;

    const auto left = cocoa_to_quartz_rect(-1280, 0, 0, 800, 900);
    check(left == schiva::Rect{-1280, 100, 0, 900},
          "left-hand display keeps its negative horizontal origin");

    const auto above = cocoa_to_quartz_rect(0, 900, 1440, 1800, 900);
    check(above == schiva::Rect{0, -900, 1440, 0},
          "display above the primary keeps its negative Quartz y origin");

    const auto point = cocoa_to_quartz_point(125.5, 1234.5, 900);
    check(point == schiva::Point{126, -335},
          "point conversion rounds logical screen coordinates once");
    const auto round_trip = quartz_to_cocoa_point(point, 900);
    check(round_trip.first == 126.0 && round_trip.second == 1235.0,
          "point conversion round-trips through the shared inverse");
}

void test_gray_image_orientation() {
    constexpr std::size_t width = 64;
    constexpr std::size_t height = 36;
    std::vector<std::uint8_t> source(width * height);
    for (std::size_t y = 0; y < height; ++y) {
        const std::uint8_t value = y < height / 2 ? 24 : 208;
        std::fill_n(source.begin() + static_cast<std::ptrdiff_t>(y * width),
                    width, value);
    }
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        nullptr, source.data(), source.size(), nullptr);
    check(gray != nullptr && provider != nullptr,
          "synthetic grayscale image resources are available");
    if (gray == nullptr || provider == nullptr) {
        if (provider != nullptr)
            CGDataProviderRelease(provider);
        if (gray != nullptr)
            CGColorSpaceRelease(gray);
        return;
    }

    CGImageRef image = CGImageCreate(
        width, height, 8, 8, width, gray, kCGImageAlphaNone, provider,
        nullptr, false, kCGRenderingIntentDefault);
    check(image != nullptr, "synthetic CGImage is created");
    if (image != nullptr) {
        const schiva::Rect bounds{0, 0, 64, 36};
        const std::vector<schiva::Rect> masks{{-4, 2, 8, 7}};
        const auto frame = schiva::detail::macos_visual_frame_from_image(
            image, static_cast<int>(width), static_cast<int>(height), bounds,
            masks);
        check(frame.has_value(), "production CGImage converter returns a frame");
        if (frame) {
            check(frame->pixels[0] < 80 && frame->pixels[35 * width] > 160,
                  "raw row zero remains at the top of the converted frame");
            check(frame->pixels.size() == width * height,
                  "converted frame has the expected compact dimensions");
            check(frame->excluded == std::vector<schiva::Rect>{{0, 2, 8, 7}},
                  "capture masks are clipped to the captured bounds");
        }
        CGImageRelease(image);
    }
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(gray);
}
}  // namespace

int main() {
    test_screen_coordinates();
    test_gray_image_orientation();
    return failures == 0 ? 0 : 1;
}
