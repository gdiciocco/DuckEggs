#ifndef SCHIVA_RUNTIME_BRAND_MARK_HPP
#define SCHIVA_RUNTIME_BRAND_MARK_HPP

#ifdef _WIN32

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace schiva::ui::brand {

constexpr int kAppIconResourceId = 101;

namespace detail {

constexpr Gdiplus::ARGB kSurface = 0xff1b222d;
constexpr Gdiplus::ARGB kBackground = 0xff11151c;
constexpr Gdiplus::ARGB kBorderStrong = 0xff3b4a5c;
constexpr Gdiplus::ARGB kAccent = 0xff73dcc5;
constexpr Gdiplus::ARGB kText = 0xffe8eef5;
constexpr Gdiplus::ARGB kWarm = 0xfff4c969;

inline Gdiplus::Color with_alpha(
    Gdiplus::ARGB value,
    BYTE alpha) {
    return Gdiplus::Color(
        alpha,
        static_cast<BYTE>((value >> 16U) & 0xffU),
        static_cast<BYTE>((value >> 8U) & 0xffU),
        static_cast<BYTE>(value & 0xffU));
}

inline void add_rounded_rect(
    Gdiplus::GraphicsPath& path,
    const Gdiplus::RectF& bounds,
    Gdiplus::REAL radius) {
    const Gdiplus::REAL diameter = std::min(
        radius * 2.0F,
        std::min(bounds.Width, bounds.Height));
    path.Reset();
    path.AddArc(
        bounds.X,
        bounds.Y,
        diameter,
        diameter,
        180.0F,
        90.0F);
    path.AddArc(
        bounds.GetRight() - diameter,
        bounds.Y,
        diameter,
        diameter,
        270.0F,
        90.0F);
    path.AddArc(
        bounds.GetRight() - diameter,
        bounds.GetBottom() - diameter,
        diameter,
        diameter,
        0.0F,
        90.0F);
    path.AddArc(
        bounds.X,
        bounds.GetBottom() - diameter,
        diameter,
        diameter,
        90.0F,
        90.0F);
    path.CloseFigure();
}

}  // namespace detail

inline void draw_mark(
    Gdiplus::Graphics& graphics,
    const Gdiplus::RectF& bounds,
    Gdiplus::REAL stroke_width) {
    using namespace Gdiplus;

    const REAL size = std::min(bounds.Width, bounds.Height);
    if (size <= 0.0F || stroke_width <= 0.0F) {
        return;
    }
    const REAL scale = size / 64.0F;
    const REAL left =
        bounds.X + (bounds.Width - size) / 2.0F;
    const REAL top =
        bounds.Y + (bounds.Height - size) / 2.0F;
    const auto point = [left, top, scale](REAL x, REAL y) {
        return PointF(left + x * scale, top + y * scale);
    };
    const auto add_bezier = [&point](
                                GraphicsPath& path,
                                REAL x1,
                                REAL y1,
                                REAL x2,
                                REAL y2,
                                REAL x3,
                                REAL y3,
                                REAL x4,
                                REAL y4) {
        const PointF first = point(x1, y1);
        const PointF second = point(x2, y2);
        const PointF third = point(x3, y3);
        const PointF fourth = point(x4, y4);
        path.AddBezier(first, second, third, fourth);
    };
    const auto transformed = [](PointF value) {
        constexpr REAL cosine = 0.9510565F;
        constexpr REAL sine = -0.3090170F;
        return PointF(
            25.5F + value.X * cosine - value.Y * sine,
            28.0F + value.X * sine + value.Y * cosine);
    };

    const PointF outer = point(1.5F, 1.5F);
    GraphicsPath tile;
    detail::add_rounded_rect(
        tile,
        RectF(
            outer.X,
            outer.Y,
            61.0F * scale,
            61.0F * scale),
        13.5F * scale);
    SolidBrush tile_fill(Color(detail::kSurface));
    graphics.FillPath(&tile_fill, &tile);
    Pen tile_pen(
        Color(detail::kBorderStrong),
        stroke_width * 0.75F);
    graphics.DrawPath(&tile_pen, &tile);

    GraphicsPath shoulders;
    shoulders.StartFigure();
    add_bezier(
        shoulders,
        13.0F, 55.0F,
        14.0F, 48.0F,
        20.0F, 44.0F,
        28.0F, 44.0F);
    add_bezier(
        shoulders,
        28.0F, 44.0F,
        36.0F, 44.0F,
        42.0F, 48.0F,
        45.0F, 55.0F);
    SolidBrush shoulder_fill(
        detail::with_alpha(detail::kAccent, 31));
    graphics.FillPath(&shoulder_fill, &shoulders);
    Pen shoulder_pen(
        Color(detail::kAccent),
        stroke_width * 1.45F);
    shoulder_pen.SetStartCap(LineCapRound);
    shoulder_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&shoulder_pen, &shoulders);

    constexpr REAL head_rx = 10.7F;
    constexpr REAL head_ry = 11.5F;
    constexpr REAL kappa = 0.55228475F;
    const std::array<PointF, 13> head_points{
        transformed({0.0F, -head_ry}),
        transformed({head_rx * kappa, -head_ry}),
        transformed({head_rx, -head_ry * kappa}),
        transformed({head_rx, 0.0F}),
        transformed({head_rx, head_ry * kappa}),
        transformed({head_rx * kappa, head_ry}),
        transformed({0.0F, head_ry}),
        transformed({-head_rx * kappa, head_ry}),
        transformed({-head_rx, head_ry * kappa}),
        transformed({-head_rx, 0.0F}),
        transformed({-head_rx, -head_ry * kappa}),
        transformed({-head_rx * kappa, -head_ry}),
        transformed({0.0F, -head_ry})};
    GraphicsPath head;
    head.StartFigure();
    for (std::size_t index = 0; index + 3 < head_points.size();
         index += 3) {
        head.AddBezier(
            point(head_points[index].X, head_points[index].Y),
            point(head_points[index + 1].X, head_points[index + 1].Y),
            point(head_points[index + 2].X, head_points[index + 2].Y),
            point(head_points[index + 3].X, head_points[index + 3].Y));
    }
    head.CloseFigure();
    SolidBrush head_fill(Color(detail::kBackground));
    Pen head_pen(
        Color(detail::kText),
        stroke_width * 1.15F);
    head_pen.SetLineJoin(LineJoinRound);
    graphics.FillPath(&head_fill, &head);
    graphics.DrawPath(&head_pen, &head);

    SolidBrush face(Color(detail::kText));
    const PointF left_eye_logical = transformed({-4.2F, -1.2F});
    const PointF right_eye_logical = transformed({4.0F, 0.2F});
    const PointF left_eye =
        point(left_eye_logical.X, left_eye_logical.Y);
    const PointF right_eye =
        point(right_eye_logical.X, right_eye_logical.Y);
    const REAL eye_radius = 1.0F * scale;
    graphics.FillEllipse(
        &face,
        left_eye.X - eye_radius,
        left_eye.Y - eye_radius,
        eye_radius * 2.0F,
        eye_radius * 2.0F);
    graphics.FillEllipse(
        &face,
        right_eye.X - eye_radius,
        right_eye.Y - eye_radius,
        eye_radius * 2.0F,
        eye_radius * 2.0F);

    const PointF smile_start = transformed({-3.0F, 6.0F});
    const PointF smile_control_1 = transformed({-1.7F, 7.8F});
    const PointF smile_control_2 = transformed({2.0F, 7.8F});
    const PointF smile_end = transformed({3.6F, 6.0F});
    GraphicsPath smile;
    smile.AddBezier(
        point(smile_start.X, smile_start.Y),
        point(smile_control_1.X, smile_control_1.Y),
        point(smile_control_2.X, smile_control_2.Y),
        point(smile_end.X, smile_end.Y));
    Pen smile_pen(
        Color(detail::kAccent),
        stroke_width * 0.75F);
    smile_pen.SetStartCap(LineCapRound);
    smile_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&smile_pen, &smile);

    constexpr REAL egg_cosine = 0.7071068F;
    constexpr REAL egg_sine = 0.7071068F;
    const auto egg_point = [egg_cosine, egg_sine](REAL x, REAL y) {
        return PointF(
            46.0F + x * egg_cosine - y * egg_sine,
            14.0F + x * egg_sine + y * egg_cosine);
    };
    const auto egg_path_point = [&point, &egg_point](REAL x, REAL y) {
        const PointF local = egg_point(x, y);
        return point(local.X, local.Y);
    };
    GraphicsPath egg;
    egg.StartFigure();
    egg.AddBezier(
        egg_path_point(0.0F, 12.0F),
        egg_path_point(-4.5F, 12.0F),
        egg_path_point(-8.5F, 6.0F),
        egg_path_point(-8.5F, 0.0F));
    egg.AddBezier(
        egg_path_point(-8.5F, 0.0F),
        egg_path_point(-8.5F, -6.0F),
        egg_path_point(-5.0F, -12.0F),
        egg_path_point(0.0F, -12.0F));
    egg.AddBezier(
        egg_path_point(0.0F, -12.0F),
        egg_path_point(5.0F, -12.0F),
        egg_path_point(8.5F, -6.0F),
        egg_path_point(8.5F, 0.0F));
    egg.AddBezier(
        egg_path_point(8.5F, 0.0F),
        egg_path_point(8.5F, 6.0F),
        egg_path_point(4.5F, 12.0F),
        egg_path_point(0.0F, 12.0F));
    egg.CloseFigure();
    SolidBrush shell_fill(
        detail::with_alpha(detail::kText, 36));
    Pen shell_pen(
        Color(detail::kText),
        stroke_width * 1.0F);
    shell_pen.SetLineJoin(LineJoinRound);
    graphics.FillPath(&shell_fill, &egg);
    graphics.DrawPath(&shell_pen, &egg);

    const PointF yolk_center = point(
        egg_point(0.0F, -2.0F).X,
        egg_point(0.0F, -2.0F).Y);
    const REAL yolk_radius = 4.5F * scale;
    SolidBrush yolk(Color(detail::kWarm));
    graphics.FillEllipse(
        &yolk,
        yolk_center.X - yolk_radius,
        yolk_center.Y - yolk_radius,
        yolk_radius * 2.0F,
        yolk_radius * 2.0F);

    GraphicsPath crack;
    crack.AddLine(
        egg_path_point(-6.5F, 5.0F),
        egg_path_point(-3.0F, 6.5F));
    crack.AddLine(
        egg_path_point(-3.0F, 6.5F),
        egg_path_point(-0.5F, 4.5F));
    crack.AddLine(
        egg_path_point(-0.5F, 4.5F),
        egg_path_point(2.0F, 7.0F));
    crack.AddLine(
        egg_path_point(2.0F, 7.0F),
        egg_path_point(5.5F, 5.0F));
    Pen crack_pen(
        Color(detail::kText),
        stroke_width * 0.68F);
    crack_pen.SetStartCap(LineCapRound);
    crack_pen.SetEndCap(LineCapRound);
    crack_pen.SetLineJoin(LineJoinRound);
    graphics.DrawPath(&crack_pen, &crack);

    GraphicsPath impact;
    impact.AddLine(point(34.0F, 23.0F), point(30.5F, 20.5F));
    impact.AddLine(point(36.0F, 19.0F), point(35.0F, 15.0F));
    impact.AddLine(point(37.0F, 27.5F), point(34.0F, 29.5F));
    Pen impact_pen(
        Color(detail::kWarm),
        stroke_width * 1.1F);
    impact_pen.SetStartCap(LineCapRound);
    impact_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&impact_pen, &impact);
}

}  // namespace schiva::ui::brand

#endif

#endif
