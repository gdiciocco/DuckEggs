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

constexpr Gdiplus::ARGB kSurface = 0xff17212b;
constexpr Gdiplus::ARGB kInk = 0xff111a22;
constexpr Gdiplus::ARGB kAccent = 0xff73dcc5;
constexpr Gdiplus::ARGB kText = 0xfff7fafc;
constexpr Gdiplus::ARGB kWarm = 0xffffd34e;

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
        path.AddBezier(
            point(x1, y1),
            point(x2, y2),
            point(x3, y3),
            point(x4, y4));
    };

    // The head supplies the backward recoil in the upper-right impact line.
    const auto head_point = [&point](REAL x, REAL y) {
        constexpr REAL cosine = 0.9135455F;
        constexpr REAL sine = -0.4067366F;
        return point(
            24.2F + x * cosine - y * sine,
            35.5F + x * sine + y * cosine);
    };
    const auto add_head_bezier = [
                                      &head_point](
                                      GraphicsPath& path,
                                      REAL x1,
                                      REAL y1,
                                      REAL x2,
                                      REAL y2,
                                      REAL x3,
                                      REAL y3,
                                      REAL x4,
                                      REAL y4) {
        path.AddBezier(
            head_point(x1, y1),
            head_point(x2, y2),
            head_point(x3, y3),
            head_point(x4, y4));
    };
    const auto add_head_oval = [&add_head_bezier](
                                   GraphicsPath& path,
                                   REAL center_x,
                                   REAL center_y,
                                   REAL radius_x,
                                   REAL radius_y) {
        constexpr REAL kappa = 0.55228475F;
        path.StartFigure();
        add_head_bezier(
            path,
            center_x, center_y - radius_y,
            center_x + radius_x * kappa, center_y - radius_y,
            center_x + radius_x, center_y - radius_y * kappa,
            center_x + radius_x, center_y);
        add_head_bezier(
            path,
            center_x + radius_x, center_y,
            center_x + radius_x, center_y + radius_y * kappa,
            center_x + radius_x * kappa, center_y + radius_y,
            center_x, center_y + radius_y);
        add_head_bezier(
            path,
            center_x, center_y + radius_y,
            center_x - radius_x * kappa, center_y + radius_y,
            center_x - radius_x, center_y + radius_y * kappa,
            center_x - radius_x, center_y);
        add_head_bezier(
            path,
            center_x - radius_x, center_y,
            center_x - radius_x, center_y - radius_y * kappa,
            center_x - radius_x * kappa, center_y - radius_y,
            center_x, center_y - radius_y);
        path.CloseFigure();
    };

    // An asymmetric field reads as energy, not a generic avatar tile.
    GraphicsPath field;
    field.StartFigure();
    add_bezier(
        field,
        31.5F, 2.0F,
        44.2F, 2.0F,
        54.3F, 8.6F,
        59.2F, 19.7F);
    add_bezier(
        field,
        59.2F, 19.7F,
        64.1F, 30.8F,
        61.6F, 44.5F,
        51.2F, 54.0F);
    add_bezier(
        field,
        51.2F, 54.0F,
        40.6F, 61.8F,
        22.2F, 62.1F,
        11.0F, 54.0F);
    add_bezier(
        field,
        11.0F, 54.0F,
        1.4F, 47.0F,
        0.2F, 32.3F,
        6.4F, 19.8F);
    add_bezier(
        field,
        6.4F, 19.8F,
        11.7F, 9.0F,
        20.1F, 2.0F,
        31.5F, 2.0F);
    field.CloseFigure();
    SolidBrush field_fill{Color(detail::kSurface)};
    graphics.FillPath(&field_fill, &field);

    // The torso is one solid shape so it stays legible at 16 px.
    GraphicsPath torso;
    torso.StartFigure();
    add_bezier(
        torso,
        4.2F, 61.0F,
        5.2F, 53.8F,
        7.6F, 47.0F,
        12.2F, 42.6F);
    torso.AddLine(point(12.2F, 42.6F), point(19.3F, 47.1F));
    add_bezier(
        torso,
        19.3F, 47.1F,
        24.5F, 48.8F,
        30.1F, 49.4F,
        34.4F, 51.6F);
    add_bezier(
        torso,
        34.4F, 51.6F,
        37.9F, 53.4F,
        39.7F, 56.8F,
        40.4F, 61.0F);
    torso.CloseFigure();
    SolidBrush torso_fill{Color(detail::kAccent)};
    graphics.FillPath(&torso_fill, &torso);

    GraphicsPath head;
    head.StartFigure();
    add_head_bezier(
        head,
        0.0F, -12.2F,
        6.3F, -12.2F,
        10.2F, -7.1F,
        10.2F, 0.0F);
    add_head_bezier(
        head,
        10.2F, 0.0F,
        10.2F, 7.2F,
        6.3F, 12.2F,
        0.0F, 12.2F);
    add_head_bezier(
        head,
        0.0F, 12.2F,
        -6.3F, 12.2F,
        -10.2F, 7.2F,
        -10.2F, 0.0F);
    add_head_bezier(
        head,
        -10.2F, 0.0F,
        -10.2F, -7.1F,
        -6.3F, -12.2F,
        0.0F, -12.2F);
    head.CloseFigure();
    SolidBrush head_fill{Color(detail::kText)};
    graphics.FillPath(&head_fill, &head);

    SolidBrush face_fill{Color(detail::kInk)};
    GraphicsPath near_eye;
    add_head_oval(near_eye, -3.8F, -1.1F, 1.25F, 1.85F);
    graphics.FillPath(&face_fill, &near_eye);

    GraphicsPath squint;
    squint.StartFigure();
    squint.AddLine(head_point(2.5F, -2.7F), head_point(4.4F, -1.0F));
    squint.AddLine(head_point(4.4F, -1.0F), head_point(2.6F, 0.9F));
    Pen squint_pen(
        Color(detail::kInk),
        stroke_width * 0.60F);
    squint_pen.SetStartCap(LineCapRound);
    squint_pen.SetEndCap(LineCapRound);
    squint_pen.SetLineJoin(LineJoinRound);
    graphics.DrawPath(&squint_pen, &squint);

    GraphicsPath mouth;
    add_head_oval(mouth, 0.7F, 6.1F, 2.15F, 2.9F);
    graphics.FillPath(&face_fill, &mouth);

    // The egg's diagonal is the composition's direction of travel.
    const auto egg_point = [&point](REAL x, REAL y) {
        constexpr REAL cosine = 0.7880108F;
        constexpr REAL sine = 0.6156615F;
        return point(
            47.0F + x * cosine - y * sine,
            18.0F + x * sine + y * cosine);
    };
    const auto add_egg_bezier = [
                                     &egg_point](
                                     GraphicsPath& path,
                                     REAL x1,
                                     REAL y1,
                                     REAL x2,
                                     REAL y2,
                                     REAL x3,
                                     REAL y3,
                                     REAL x4,
                                     REAL y4) {
        path.AddBezier(
            egg_point(x1, y1),
            egg_point(x2, y2),
            egg_point(x3, y3),
            egg_point(x4, y4));
    };
    GraphicsPath shell;
    shell.StartFigure();
    add_egg_bezier(
        shell,
        0.0F, 13.5F,
        -5.7F, 13.5F,
        -9.0F, 7.2F,
        -9.0F, 0.0F);
    add_egg_bezier(
        shell,
        -9.0F, 0.0F,
        -9.0F, -7.5F,
        -5.0F, -13.5F,
        0.0F, -13.5F);
    add_egg_bezier(
        shell,
        0.0F, -13.5F,
        5.0F, -13.5F,
        9.0F, -7.5F,
        9.0F, 0.0F);
    add_egg_bezier(
        shell,
        9.0F, 0.0F,
        9.0F, 7.2F,
        5.7F, 13.5F,
        0.0F, 13.5F);
    shell.CloseFigure();
    SolidBrush shell_fill{Color(detail::kText)};
    graphics.FillPath(&shell_fill, &shell);

    const PointF yolk_center = egg_point(-2.2F, 4.2F);
    const REAL yolk_radius = 4.8F * scale;
    SolidBrush yolk{Color(detail::kWarm)};
    graphics.FillEllipse(
        &yolk,
        yolk_center.X - yolk_radius,
        yolk_center.Y - yolk_radius,
        yolk_radius * 2.0F,
        yolk_radius * 2.0F);

    GraphicsPath crack;
    crack.StartFigure();
    crack.AddLine(egg_point(-8.2F, -3.7F), egg_point(-5.5F, -2.2F));
    crack.AddLine(egg_point(-5.5F, -2.2F), egg_point(-7.0F, 0.2F));
    crack.AddLine(egg_point(-7.0F, 0.2F), egg_point(-4.3F, 2.1F));
    Pen crack_pen(
        Color(detail::kInk),
        stroke_width * 0.68F);
    crack_pen.SetStartCap(LineCapRound);
    crack_pen.SetEndCap(LineCapRound);
    crack_pen.SetLineJoin(LineJoinRound);
    graphics.DrawPath(&crack_pen, &crack);

    // A compact burst bridges the egg and forehead at small icon sizes.
    const std::array<PointF, 10> burst_points{
        point(31.2F, 21.8F),
        point(33.1F, 24.1F),
        point(36.0F, 24.3F),
        point(33.7F, 26.2F),
        point(34.5F, 29.1F),
        point(31.7F, 27.5F),
        point(28.9F, 29.4F),
        point(29.5F, 26.4F),
        point(27.0F, 24.7F),
        point(30.1F, 24.3F)};
    GraphicsPath burst;
    burst.AddPolygon(
        burst_points.data(),
        static_cast<INT>(burst_points.size()));
    SolidBrush burst_fill{Color(detail::kWarm)};
    graphics.FillPath(&burst_fill, &burst);

    GraphicsPath splashes;
    splashes.StartFigure();
    splashes.AddLine(point(26.9F, 22.2F), point(23.4F, 20.6F));
    splashes.StartFigure();
    splashes.AddLine(point(31.1F, 18.5F), point(31.2F, 15.1F));
    splashes.StartFigure();
    splashes.AddLine(point(35.3F, 27.8F), point(38.3F, 30.4F));
    Pen splash_pen(
        Color(detail::kWarm),
        stroke_width * 0.76F);
    splash_pen.SetStartCap(LineCapRound);
    splash_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&splash_pen, &splashes);
}

}  // namespace schiva::ui::brand

#endif

#endif
