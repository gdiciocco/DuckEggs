#pragma once

#include <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "schiva/types.hpp"

namespace schiva::detail {

// Convert one retained CGImage into the small top-left-origin grayscale frame
// consumed by the portable visual learner. The image itself is never stored.
inline std::optional<VisualFrame> macos_visual_frame_from_image(
    CGImageRef image, int width, int height, const Rect& bounds,
    const std::vector<Rect>& masks) {
    if (image == nullptr || width <= 0 || height <= 0 ||
        CGImageGetWidth(image) == 0 || CGImageGetHeight(image) == 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    CGColorSpaceRef color = CGColorSpaceCreateDeviceGray();
    if (color == nullptr) {
        return std::nullopt;
    }
    CGContextRef context = CGBitmapContextCreate(
        pixels.data(), static_cast<std::size_t>(width),
        static_cast<std::size_t>(height), 8, static_cast<std::size_t>(width),
        color, kCGImageAlphaNone);
    CGColorSpaceRelease(color);
    if (context == nullptr) {
        return std::nullopt;
    }

    CGContextSetInterpolationQuality(context, kCGInterpolationLow);
    CGContextDrawImage(
        context, CGRectMake(0, 0, width, height), image);
    CGContextRelease(context);

    std::vector<Rect> excluded;
    for (const Rect& mask : masks) {
        const Rect clipped{std::max(mask.left, bounds.left),
                           std::max(mask.top, bounds.top),
                           std::min(mask.right, bounds.right),
                           std::min(mask.bottom, bounds.bottom)};
        if (clipped.width() > 0 && clipped.height() > 0) {
            excluded.push_back(clipped);
        }
    }
    return VisualFrame{width, height, std::move(pixels), bounds,
                       std::move(excluded)};
}

}  // namespace schiva::detail
