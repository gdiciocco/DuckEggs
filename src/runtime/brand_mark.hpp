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

constexpr Gdiplus::ARGB kIvory = 0xfff1e5c8;
constexpr Gdiplus::ARGB kIvoryLight = 0xfff9f1df;
constexpr Gdiplus::ARGB kInk = 0xff161513;
constexpr Gdiplus::ARGB kGold = 0xfff7c600;

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
    const auto add_oval = [&add_bezier](
                              GraphicsPath& path,
                              REAL center_x,
                              REAL center_y,
                              REAL radius_x,
                              REAL radius_y) {
        constexpr REAL kappa = 0.55228475F;
        path.StartFigure();
        add_bezier(
            path,
            center_x, center_y - radius_y,
            center_x + radius_x * kappa, center_y - radius_y,
            center_x + radius_x, center_y - radius_y * kappa,
            center_x + radius_x, center_y);
        add_bezier(
            path,
            center_x + radius_x, center_y,
            center_x + radius_x, center_y + radius_y * kappa,
            center_x + radius_x * kappa, center_y + radius_y,
            center_x, center_y + radius_y);
        add_bezier(
            path,
            center_x, center_y + radius_y,
            center_x - radius_x * kappa, center_y + radius_y,
            center_x - radius_x, center_y + radius_y * kappa,
            center_x - radius_x, center_y);
        add_bezier(
            path,
            center_x - radius_x, center_y,
            center_x - radius_x, center_y - radius_y * kappa,
            center_x - radius_x * kappa, center_y - radius_y,
            center_x, center_y - radius_y);
        path.CloseFigure();
    };

    // The 36/42 logical UI marks pass 1.5/1.8 logical stroke widths. Coupling
    // the compact limit to that DPI-scaled stroke keeps the same treatment as
    // the window moves between scale factors instead of switching at 150%.
    const REAL compact_limit =
        std::max(56.0F, stroke_width * 28.0F);
    if (size <= compact_limit) {
        GraphicsPath field;
        field.StartFigure();
        add_bezier(
            field,
            18.0F, 4.0F,
            31.0F, 0.0F,
            49.0F, 5.0F,
            57.0F, 17.0F);
        add_bezier(
            field,
            57.0F, 17.0F,
            64.0F, 30.0F,
            62.0F, 47.0F,
            52.0F, 57.0F);
        add_bezier(
            field,
            52.0F, 57.0F,
            42.0F, 65.0F,
            22.0F, 64.0F,
            11.0F, 55.0F);
        add_bezier(
            field,
            11.0F, 55.0F,
            2.0F, 46.0F,
            1.0F, 31.0F,
            6.0F, 20.0F);
        add_bezier(
            field,
            6.0F, 20.0F,
            9.0F, 12.0F,
            12.0F, 7.0F,
            18.0F, 4.0F);
        field.CloseFigure();
        SolidBrush field_fill{Color(detail::kIvory)};
        graphics.FillPath(&field_fill, &field);

        const std::array<PointF, 9> impact_points{
            point(31.0F, 13.0F),
            point(58.0F, 5.0F),
            point(48.0F, 13.0F),
            point(60.0F, 17.0F),
            point(46.0F, 20.0F),
            point(53.0F, 27.0F),
            point(39.0F, 23.0F),
            point(34.0F, 32.0F),
            point(30.0F, 22.0F)};
        GraphicsPath impact;
        impact.AddPolygon(
            impact_points.data(),
            static_cast<INT>(impact_points.size()));
        SolidBrush gold_fill{Color(detail::kGold)};
        graphics.FillPath(&gold_fill, &impact);

        const std::array<PointF, 3> lower_drop{
            point(49.0F, 29.0F),
            point(57.0F, 33.0F),
            point(51.0F, 37.0F)};
        GraphicsPath drop;
        drop.AddPolygon(
            lower_drop.data(),
            static_cast<INT>(lower_drop.size()));
        graphics.FillPath(&gold_fill, &drop);
        graphics.FillEllipse(
            &gold_fill,
            left + 57.4F * scale,
            top + 6.4F * scale,
            5.2F * scale,
            5.2F * scale);
        graphics.FillEllipse(
            &gold_fill,
            left + 43.3F * scale,
            top + 4.3F * scale,
            3.4F * scale,
            3.4F * scale);

        GraphicsPath shoulder;
        shoulder.StartFigure();
        add_bezier(
            shoulder,
            7.0F, 59.0F,
            9.0F, 49.0F,
            16.0F, 42.0F,
            26.0F, 39.0F);
        shoulder.AddLine(point(26.0F, 39.0F), point(35.0F, 44.0F));
        add_bezier(
            shoulder,
            35.0F, 44.0F,
            41.0F, 46.0F,
            48.0F, 50.0F,
            51.0F, 58.0F);
        shoulder.CloseFigure();
        SolidBrush ink_fill{Color(detail::kInk)};
        graphics.FillPath(&ink_fill, &shoulder);

        GraphicsPath hair;
        hair.StartFigure();
        add_bezier(
            hair,
            11.0F, 38.0F,
            5.0F, 32.0F,
            5.0F, 23.0F,
            10.0F, 17.0F);
        add_bezier(
            hair,
            10.0F, 17.0F,
            8.0F, 12.0F,
            12.0F, 8.0F,
            18.0F, 8.0F);
        add_bezier(
            hair,
            18.0F, 8.0F,
            20.0F, 3.0F,
            28.0F, 3.0F,
            31.0F, 7.0F);
        add_bezier(
            hair,
            31.0F, 7.0F,
            37.0F, 5.0F,
            42.0F, 10.0F,
            42.0F, 15.0F);
        add_bezier(
            hair,
            42.0F, 15.0F,
            35.0F, 15.0F,
            30.0F, 18.0F,
            28.0F, 23.0F);
        add_bezier(
            hair,
            28.0F, 23.0F,
            26.0F, 27.0F,
            27.0F, 32.0F,
            25.0F, 36.0F);
        add_bezier(
            hair,
            25.0F, 36.0F,
            21.0F, 41.0F,
            15.0F, 42.0F,
            11.0F, 38.0F);
        hair.CloseFigure();
        graphics.FillPath(&ink_fill, &hair);

        GraphicsPath face;
        face.StartFigure();
        add_bezier(
            face,
            26.0F, 18.0F,
            31.0F, 15.0F,
            38.0F, 15.0F,
            44.0F, 20.0F);
        add_bezier(
            face,
            44.0F, 20.0F,
            50.0F, 25.0F,
            53.0F, 33.0F,
            50.0F, 40.0F);
        add_bezier(
            face,
            50.0F, 40.0F,
            48.0F, 45.0F,
            43.0F, 47.0F,
            38.0F, 45.0F);
        add_bezier(
            face,
            38.0F, 45.0F,
            32.0F, 43.0F,
            29.0F, 37.0F,
            27.0F, 31.0F);
        add_bezier(
            face,
            27.0F, 31.0F,
            26.0F, 27.0F,
            25.0F, 22.0F,
            26.0F, 18.0F);
        face.CloseFigure();
        SolidBrush face_fill{Color(detail::kIvoryLight)};
        graphics.FillPath(&face_fill, &face);
        const REAL face_stroke =
            std::max(1.15F * scale, stroke_width * 0.58F);
        Pen face_pen{Color(detail::kInk), face_stroke};
        face_pen.SetLineJoin(LineJoinRound);
        graphics.DrawPath(&face_pen, &face);

        const REAL eye_width = 5.2F * scale;
        const REAL eye_height = 7.6F * scale;
        graphics.FillEllipse(
            &ink_fill,
            left + (35.0F - 2.6F) * scale,
            top + (25.0F - 3.8F) * scale,
            eye_width,
            eye_height);

        GraphicsPath squint;
        squint.StartFigure();
        add_bezier(
            squint,
            41.0F, 23.0F,
            43.0F, 20.0F,
            47.0F, 20.0F,
            49.0F, 23.0F);
        add_bezier(
            squint,
            49.0F, 23.0F,
            47.0F, 22.0F,
            44.0F, 23.0F,
            43.0F, 26.0F);
        Pen squint_pen{Color(detail::kInk), 2.3F * scale};
        squint_pen.SetStartCap(LineCapRound);
        squint_pen.SetEndCap(LineCapRound);
        graphics.DrawPath(&squint_pen, &squint);

        GraphicsPath mouth;
        add_oval(mouth, 41.5F, 37.5F, 3.5F, 2.7F);
        graphics.FillPath(&ink_fill, &mouth);

        const std::array<PointF, 6> shell_points{
            point(20.0F, 16.0F),
            point(24.0F, 9.0F),
            point(30.0F, 11.0F),
            point(34.0F, 8.0F),
            point(38.0F, 14.0F),
            point(34.0F, 19.0F)};
        GraphicsPath shell;
        shell.AddPolygon(
            shell_points.data(),
            static_cast<INT>(shell_points.size()));
        shell.CloseFigure();
        SolidBrush shell_fill{Color(0xfffff9ec)};
        graphics.FillPath(&shell_fill, &shell);
        Pen shell_pen{Color(detail::kInk), 2.0F * scale};
        shell_pen.SetLineJoin(LineJoinRound);
        graphics.DrawPath(&shell_pen, &shell);
        GraphicsPath crack;
        crack.StartFigure();
        crack.AddLine(point(25.0F, 11.0F), point(27.0F, 15.0F));
        crack.AddLine(point(27.0F, 15.0F), point(24.0F, 17.0F));
        Pen crack_pen{Color(detail::kInk), 1.7F * scale};
        crack_pen.SetStartCap(LineCapRound);
        crack_pen.SetEndCap(LineCapRound);
        crack_pen.SetLineJoin(LineJoinRound);
        graphics.DrawPath(&crack_pen, &crack);

        const std::array<PointF, 3> nape_fleck{
            point(13.0F, 42.0F),
            point(7.0F, 44.0F),
            point(17.0F, 45.0F)};
        GraphicsPath nape_path;
        nape_path.AddPolygon(
            nape_fleck.data(),
            static_cast<INT>(nape_fleck.size()));
        graphics.FillPath(&ink_fill, &nape_path);
        return;
    }

    // Larger native draws retain the loose paper edge, brush-built coat, hand,
    // shell shards, and isolated ink gestures from the expressive source art.
    GraphicsPath field;
    field.StartFigure();
    add_bezier(
        field,
        6.8F, 18.9F,
        5.4F, 10.4F,
        13.5F, 3.5F,
        25.6F, 3.1F);
    add_bezier(
        field,
        25.6F, 3.1F,
        39.0F, 0.3F,
        55.6F, 5.3F,
        59.8F, 16.1F);
    add_bezier(
        field,
        59.8F, 16.1F,
        64.1F, 27.4F,
        60.5F, 41.3F,
        55.5F, 50.6F);
    add_bezier(
        field,
        55.5F, 50.6F,
        49.8F, 60.5F,
        38.0F, 62.6F,
        27.0F, 59.8F);
    add_bezier(
        field,
        27.0F, 59.8F,
        14.6F, 61.6F,
        5.3F, 52.4F,
        3.8F, 40.8F);
    add_bezier(
        field,
        3.8F, 40.8F,
        2.9F, 32.3F,
        3.6F, 25.3F,
        6.8F, 18.9F);
    field.CloseFigure();
    SolidBrush ivory_fill{Color(detail::kIvory)};
    graphics.FillPath(&ivory_fill, &field);

    GraphicsPath splash;
    splash.StartFigure();
    add_bezier(
        splash,
        28.6F, 15.4F,
        32.4F, 12.6F,
        38.5F, 10.6F,
        44.1F, 11.0F);
    add_bezier(
        splash,
        44.1F, 11.0F,
        49.0F, 11.4F,
        53.9F, 9.9F,
        58.3F, 6.8F);
    add_bezier(
        splash,
        58.3F, 6.8F,
        55.4F, 11.4F,
        50.4F, 13.6F,
        45.1F, 13.9F);
    add_bezier(
        splash,
        45.1F, 13.9F,
        41.4F, 14.0F,
        38.3F, 15.5F,
        35.0F, 17.4F);
    add_bezier(
        splash,
        35.0F, 17.4F,
        33.3F, 18.4F,
        31.0F, 19.1F,
        29.0F, 18.8F);
    splash.CloseFigure();
    SolidBrush gold_fill{Color(detail::kGold)};
    graphics.FillPath(&gold_fill, &splash);

    GraphicsPath splash_low;
    splash_low.StartFigure();
    add_bezier(
        splash_low,
        31.6F, 18.9F,
        36.1F, 20.8F,
        42.4F, 20.8F,
        48.6F, 18.3F);
    add_bezier(
        splash_low,
        48.6F, 18.3F,
        44.8F, 22.1F,
        38.5F, 23.1F,
        33.3F, 21.2F);
    splash_low.CloseFigure();
    graphics.FillPath(&gold_fill, &splash_low);

    GraphicsPath splash_long;
    splash_long.StartFigure();
    add_bezier(
        splash_long,
        34.6F, 21.8F,
        41.0F, 24.3F,
        49.3F, 23.5F,
        55.4F, 19.5F);
    add_bezier(
        splash_long,
        55.4F, 19.5F,
        50.7F, 24.4F,
        42.3F, 26.2F,
        36.8F, 23.6F);
    splash_long.CloseFigure();
    graphics.FillPath(&gold_fill, &splash_long);

    SolidBrush gold_brush{Color(detail::kGold)};
    graphics.FillEllipse(
        &gold_brush,
        left + 52.8F * scale,
        top + 8.3F * scale,
        1.8F * scale,
        1.8F * scale);
    graphics.FillEllipse(
        &gold_brush,
        left + 58.8F * scale,
        top + 15.6F * scale,
        1.2F * scale,
        1.2F * scale);
    graphics.FillEllipse(
        &gold_brush,
        left + 44.0F * scale,
        top + 4.5F * scale,
        0.9F * scale,
        0.9F * scale);
    graphics.FillEllipse(
        &gold_brush,
        left + 49.7F * scale,
        top + 21.7F * scale,
        1.4F * scale,
        1.4F * scale);

    const auto add_gold_stroke = [
                                      &graphics,
                                      left,
                                      top,
                                      scale,
                                      stroke_width](
                                      REAL x1,
                                      REAL y1,
                                      REAL x2,
                                      REAL y2,
                                      REAL x3,
                                      REAL y3,
                                      REAL x4,
                                      REAL y4,
                                      REAL width) {
        GraphicsPath stroke;
        stroke.StartFigure();
        stroke.AddBezier(
            PointF(left + x1 * scale, top + y1 * scale),
            PointF(left + x2 * scale, top + y2 * scale),
            PointF(left + x3 * scale, top + y3 * scale),
            PointF(left + x4 * scale, top + y4 * scale));
        Pen pen{
            Color(detail::kGold),
            std::max(width * scale, stroke_width * 0.32F)};
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        graphics.DrawPath(&pen, &stroke);
    };
    add_gold_stroke(
        39.9F, 22.2F,
        41.3F, 25.9F,
        42.2F, 28.6F,
        43.6F, 31.4F,
        1.9F);
    add_gold_stroke(
        48.9F, 14.8F,
        50.6F, 17.9F,
        51.9F, 19.9F,
        53.8F, 21.9F,
        1.0F);
    add_gold_stroke(
        35.1F, 23.4F,
        35.6F, 25.6F,
        35.8F, 27.2F,
        35.0F, 29.2F,
        1.3F);
    add_gold_stroke(
        45.0F, 24.8F,
        46.8F, 25.6F,
        48.4F, 25.9F,
        50.1F, 25.8F,
        0.9F);

    GraphicsPath coat;
    coat.StartFigure();
    add_bezier(
        coat,
        8.5F, 49.8F,
        9.3F, 43.3F,
        12.6F, 39.5F,
        18.1F, 37.5F);
    add_bezier(
        coat,
        18.1F, 37.5F,
        22.6F, 35.9F,
        28.6F, 36.9F,
        32.3F, 39.5F);
    add_bezier(
        coat,
        32.3F, 39.5F,
        35.4F, 41.6F,
        37.4F, 44.3F,
        39.4F, 47.4F);
    add_bezier(
        coat,
        39.4F, 47.4F,
        42.9F, 45.8F,
        46.0F, 45.9F,
        48.6F, 48.1F);
    add_bezier(
        coat,
        48.6F, 48.1F,
        52.8F, 51.8F,
        54.6F, 56.1F,
        56.3F, 61.6F);
    coat.AddLine(point(56.3F, 61.6F), point(13.1F, 62.4F));
    add_bezier(
        coat,
        13.1F, 62.4F,
        11.0F, 57.5F,
        9.1F, 53.3F,
        8.5F, 49.8F);
    coat.CloseFigure();
    SolidBrush ink_fill{Color(detail::kInk)};
    graphics.FillPath(&ink_fill, &coat);

    GraphicsPath coat_dry_cuts;
    coat_dry_cuts.StartFigure();
    add_bezier(
        coat_dry_cuts,
        11.9F, 54.8F,
        14.8F, 53.3F,
        17.9F, 52.3F,
        21.1F, 52.1F);
    add_bezier(
        coat_dry_cuts,
        21.1F, 52.1F,
        18.1F, 53.8F,
        15.1F, 55.0F,
        12.6F, 56.8F);
    coat_dry_cuts.CloseFigure();
    coat_dry_cuts.StartFigure();
    add_bezier(
        coat_dry_cuts,
        15.3F, 59.0F,
        18.9F, 56.9F,
        22.9F, 55.8F,
        27.1F, 55.6F);
    add_bezier(
        coat_dry_cuts,
        27.1F, 55.6F,
        23.0F, 57.5F,
        19.4F, 59.2F,
        16.5F, 61.6F);
    coat_dry_cuts.CloseFigure();
    coat_dry_cuts.StartFigure();
    add_bezier(
        coat_dry_cuts,
        32.6F, 45.8F,
        35.0F, 44.0F,
        37.8F, 42.9F,
        40.6F, 43.3F);
    add_bezier(
        coat_dry_cuts,
        40.6F, 43.3F,
        37.8F, 44.3F,
        35.4F, 46.1F,
        33.8F, 48.3F);
    coat_dry_cuts.CloseFigure();
    graphics.FillPath(&ivory_fill, &coat_dry_cuts);

    const std::array<PointF, 4> coat_fiber_a{
        point(6.5F, 50.6F),
        point(4.1F, 51.8F),
        point(2.6F, 53.8F),
        point(7.8F, 52.3F)};
    const std::array<PointF, 4> coat_fiber_b{
        point(8.0F, 56.4F),
        point(6.4F, 58.1F),
        point(5.9F, 60.2F),
        point(10.3F, 57.8F)};
    GraphicsPath coat_fibers;
    coat_fibers.AddPolygon(
        coat_fiber_a.data(),
        static_cast<INT>(coat_fiber_a.size()));
    coat_fibers.AddPolygon(
        coat_fiber_b.data(),
        static_cast<INT>(coat_fiber_b.size()));
    graphics.FillPath(&ink_fill, &coat_fibers);

    GraphicsPath shirt;
    shirt.StartFigure();
    add_bezier(
        shirt,
        28.5F, 38.4F,
        31.1F, 36.1F,
        35.1F, 35.8F,
        38.3F, 37.6F);
    add_bezier(
        shirt,
        38.3F, 37.6F,
        40.4F, 39.0F,
        41.8F, 41.1F,
        41.8F, 43.9F);
    add_bezier(
        shirt,
        41.8F, 43.9F,
        38.8F, 44.9F,
        35.4F, 47.3F,
        33.8F, 50.1F);
    add_bezier(
        shirt,
        33.8F, 50.1F,
        31.6F, 47.5F,
        29.4F, 44.8F,
        27.3F, 43.0F);
    add_bezier(
        shirt,
        27.3F, 43.0F,
        27.5F, 40.8F,
        27.8F, 39.4F,
        28.5F, 38.4F);
    shirt.CloseFigure();
    SolidBrush shirt_fill{Color(0xfff6ebd3)};
    graphics.FillPath(&shirt_fill, &shirt);

    GraphicsPath hand;
    hand.StartFigure();
    add_bezier(
        hand,
        41.3F, 44.3F,
        43.8F, 42.3F,
        46.5F, 41.3F,
        49.0F, 42.1F);
    add_bezier(
        hand,
        49.0F, 42.1F,
        50.9F, 42.8F,
        52.3F, 44.5F,
        52.3F, 46.4F);
    add_bezier(
        hand,
        52.3F, 46.4F,
        52.5F, 48.0F,
        51.3F, 49.1F,
        50.1F, 49.8F);
    add_bezier(
        hand,
        50.1F, 49.8F,
        51.4F, 50.3F,
        52.1F, 51.5F,
        51.6F, 53.0F);
    add_bezier(
        hand,
        51.6F, 53.0F,
        51.0F, 55.1F,
        48.3F, 56.1F,
        45.6F, 56.1F);
    add_bezier(
        hand,
        45.6F, 56.1F,
        42.9F, 56.1F,
        40.4F, 54.5F,
        39.1F, 52.0F);
    add_bezier(
        hand,
        39.1F, 52.0F,
        38.1F, 49.9F,
        39.1F, 46.7F,
        41.3F, 44.3F);
    hand.CloseFigure();
    SolidBrush hand_fill{Color(0xfff2e1be)};
    graphics.FillPath(&hand_fill, &hand);
    Pen hand_outline{Color(detail::kInk), 0.9F * scale};
    hand_outline.SetLineJoin(LineJoinRound);
    graphics.DrawPath(&hand_outline, &hand);

    GraphicsPath hand_lines;
    hand_lines.StartFigure();
    add_bezier(
        hand_lines,
        43.0F, 47.8F,
        43.6F, 50.1F,
        45.1F, 52.1F,
        47.3F, 53.1F);
    hand_lines.StartFigure();
    add_bezier(
        hand_lines,
        45.6F, 44.6F,
        45.9F, 47.5F,
        46.9F, 49.9F,
        48.6F, 51.1F);
    hand_lines.StartFigure();
    add_bezier(
        hand_lines,
        48.6F, 42.9F,
        48.5F, 45.3F,
        49.1F, 47.1F,
        50.5F, 48.3F);
    hand_lines.StartFigure();
    add_bezier(
        hand_lines,
        50.9F, 43.5F,
        50.6F, 45.4F,
        51.1F, 46.9F,
        52.1F, 47.9F);
    Pen hand_detail{Color(detail::kInk), 0.72F * scale};
    hand_detail.SetStartCap(LineCapRound);
    hand_detail.SetEndCap(LineCapRound);
    graphics.DrawPath(&hand_detail, &hand_lines);
    GraphicsPath forearm;
    forearm.StartFigure();
    add_bezier(
        forearm,
        41.0F, 44.9F,
        38.8F, 43.4F,
        37.0F, 43.0F,
        35.1F, 43.6F);
    Pen forearm_pen{Color(detail::kInk), 1.6F * scale};
    forearm_pen.SetStartCap(LineCapRound);
    forearm_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&forearm_pen, &forearm);

    GraphicsPath hair;
    hair.StartFigure();
    add_bezier(
        hair,
        12.8F, 30.8F,
        9.3F, 28.4F,
        7.8F, 24.6F,
        8.9F, 20.9F);
    add_bezier(
        hair,
        8.9F, 20.9F,
        9.8F, 17.9F,
        12.0F, 15.9F,
        14.0F, 13.9F);
    add_bezier(
        hair,
        14.0F, 13.9F,
        12.9F, 11.6F,
        14.0F, 9.1F,
        16.5F, 8.3F);
    add_bezier(
        hair,
        16.5F, 8.3F,
        16.5F, 5.9F,
        18.9F, 4.6F,
        21.6F, 5.4F);
    add_bezier(
        hair,
        21.6F, 5.4F,
        23.3F, 2.4F,
        27.0F, 2.5F,
        29.0F, 4.5F);
    add_bezier(
        hair,
        29.0F, 4.5F,
        31.4F, 2.6F,
        34.5F, 4.3F,
        35.1F, 7.0F);
    add_bezier(
        hair,
        35.1F, 7.0F,
        37.6F, 6.9F,
        39.4F, 9.5F,
        38.6F, 12.0F);
    add_bezier(
        hair,
        38.6F, 12.0F,
        36.4F, 12.6F,
        34.8F, 14.1F,
        33.8F, 16.0F);
    add_bezier(
        hair,
        33.8F, 16.0F,
        31.3F, 12.9F,
        28.3F, 12.1F,
        25.6F, 13.5F);
    add_bezier(
        hair,
        25.6F, 13.5F,
        23.4F, 14.8F,
        22.4F, 17.9F,
        22.9F, 21.1F);
    add_bezier(
        hair,
        22.9F, 21.1F,
        21.3F, 24.3F,
        18.9F, 27.5F,
        16.3F, 30.1F);
    add_bezier(
        hair,
        16.3F, 30.1F,
        15.1F, 31.4F,
        13.9F, 31.6F,
        12.8F, 30.8F);
    hair.CloseFigure();
    graphics.FillPath(&ink_fill, &hair);

    GraphicsPath hair_dry_cuts;
    hair_dry_cuts.StartFigure();
    add_bezier(
        hair_dry_cuts,
        9.8F, 24.9F,
        11.4F, 23.3F,
        13.4F, 21.6F,
        15.6F, 20.5F);
    add_bezier(
        hair_dry_cuts,
        15.6F, 20.5F,
        14.1F, 22.5F,
        12.4F, 24.1F,
        10.5F, 25.9F);
    hair_dry_cuts.CloseFigure();
    hair_dry_cuts.StartFigure();
    add_bezier(
        hair_dry_cuts,
        14.8F, 29.9F,
        16.4F, 28.3F,
        18.5F, 27.0F,
        20.8F, 26.4F);
    add_bezier(
        hair_dry_cuts,
        20.8F, 26.4F,
        18.9F, 27.9F,
        17.3F, 29.5F,
        15.9F, 31.3F);
    hair_dry_cuts.CloseFigure();
    hair_dry_cuts.StartFigure();
    add_bezier(
        hair_dry_cuts,
        18.8F, 9.4F,
        19.6F, 10.3F,
        20.4F, 11.5F,
        20.8F, 13.0F);
    add_bezier(
        hair_dry_cuts,
        20.8F, 13.0F,
        20.0F, 11.9F,
        19.6F, 11.5F,
        19.0F, 11.0F);
    hair_dry_cuts.CloseFigure();
    hair_dry_cuts.StartFigure();
    add_bezier(
        hair_dry_cuts,
        13.1F, 14.0F,
        14.4F, 15.1F,
        15.5F, 16.8F,
        16.0F, 18.5F);
    add_bezier(
        hair_dry_cuts,
        16.0F, 18.5F,
        14.9F, 17.3F,
        13.9F, 16.6F,
        12.6F, 16.0F);
    hair_dry_cuts.CloseFigure();
    graphics.FillPath(&ivory_fill, &hair_dry_cuts);

    const std::array<PointF, 4> hair_fiber_a{
        point(8.4F, 14.6F),
        point(6.0F, 13.1F),
        point(4.5F, 10.9F),
        point(9.6F, 13.6F)};
    const std::array<PointF, 4> hair_fiber_b{
        point(12.0F, 31.6F),
        point(10.6F, 33.8F),
        point(10.8F, 36.1F),
        point(12.0F, 38.0F)};
    GraphicsPath hair_fibers;
    hair_fibers.AddPolygon(
        hair_fiber_a.data(),
        static_cast<INT>(hair_fiber_a.size()));
    hair_fibers.AddPolygon(
        hair_fiber_b.data(),
        static_cast<INT>(hair_fiber_b.size()));
    graphics.FillPath(&ink_fill, &hair_fibers);

    GraphicsPath hair_strokes;
    hair_strokes.StartFigure();
    hair_strokes.AddLine(point(14.0F, 14.1F), point(6.5F, 17.1F));
    add_bezier(
        hair_strokes,
        14.0F, 14.1F,
        10.4F, 12.4F,
        8.0F, 13.5F,
        6.5F, 17.1F);
    hair_strokes.StartFigure();
    add_bezier(
        hair_strokes,
        16.3F, 10.4F,
        13.4F, 7.9F,
        10.5F, 8.8F,
        9.4F, 11.4F);
    hair_strokes.StartFigure();
    add_bezier(
        hair_strokes,
        20.1F, 7.3F,
        17.8F, 4.4F,
        14.9F, 3.9F,
        12.6F, 5.6F);
    hair_strokes.StartFigure();
    add_bezier(
        hair_strokes,
        24.0F, 6.1F,
        22.9F, 3.5F,
        20.6F, 2.3F,
        18.4F, 2.9F);
    hair_strokes.StartFigure();
    add_bezier(
        hair_strokes,
        13.4F, 19.3F,
        10.6F, 19.8F,
        9.1F, 22.0F,
        10.1F, 24.3F);
    hair_strokes.StartFigure();
    add_bezier(
        hair_strokes,
        12.8F, 25.3F,
        10.5F, 26.8F,
        9.8F, 29.0F,
        11.4F, 31.0F);
    hair_strokes.StartFigure();
    add_bezier(
        hair_strokes,
        15.0F, 29.8F,
        14.3F, 32.5F,
        15.0F, 34.8F,
        17.4F, 35.6F);
    Pen hair_pen{Color(detail::kInk), 1.0F * scale};
    hair_pen.SetStartCap(LineCapRound);
    hair_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&hair_pen, &hair_strokes);

    const std::array<PointF, 4> hair_fleck_a{
        point(9.6F, 18.9F),
        point(6.9F, 19.3F),
        point(5.4F, 21.1F),
        point(11.3F, 20.5F)};
    const std::array<PointF, 4> hair_fleck_b{
        point(10.5F, 23.8F),
        point(7.8F, 25.1F),
        point(7.1F, 27.3F),
        point(12.8F, 25.5F)};
    GraphicsPath hair_flecks;
    hair_flecks.AddPolygon(
        hair_fleck_a.data(),
        static_cast<INT>(hair_fleck_a.size()));
    hair_flecks.AddPolygon(
        hair_fleck_b.data(),
        static_cast<INT>(hair_fleck_b.size()));
    graphics.FillPath(&ink_fill, &hair_flecks);

    GraphicsPath face;
    face.StartFigure();
    add_bezier(
        face,
        22.9F, 15.4F,
        25.9F, 12.6F,
        30.3F, 12.6F,
        33.5F, 14.6F);
    add_bezier(
        face,
        33.5F, 14.6F,
        36.5F, 16.5F,
        38.4F, 19.8F,
        39.4F, 23.4F);
    add_bezier(
        face,
        39.4F, 23.4F,
        40.0F, 25.6F,
        39.6F, 27.0F,
        38.4F, 28.0F);
    face.AddLine(point(38.4F, 28.0F), point(40.1F, 30.3F));
    add_bezier(
        face,
        40.1F, 30.3F,
        37.5F, 30.9F,
        35.5F, 32.1F,
        34.3F, 34.0F);
    add_bezier(
        face,
        34.3F, 34.0F,
        33.1F, 36.0F,
        31.3F, 37.4F,
        29.3F, 37.0F);
    add_bezier(
        face,
        29.3F, 37.0F,
        27.0F, 36.6F,
        25.8F, 34.5F,
        25.5F, 31.8F);
    add_bezier(
        face,
        25.5F, 31.8F,
        25.1F, 28.9F,
        23.1F, 26.6F,
        23.0F, 23.8F);
    add_bezier(
        face,
        23.0F, 23.8F,
        22.9F, 21.8F,
        22.6F, 18.4F,
        22.9F, 15.4F);
    face.CloseFigure();
    SolidBrush face_fill{Color(0xfff6ebd3)};
    graphics.FillPath(&face_fill, &face);

    GraphicsPath cheek_shadow;
    cheek_shadow.StartFigure();
    add_bezier(
        cheek_shadow,
        23.4F, 16.9F,
        25.4F, 15.1F,
        28.1F, 14.6F,
        30.5F, 15.6F);
    add_bezier(
        cheek_shadow,
        30.5F, 15.6F,
        32.4F, 16.4F,
        33.9F, 17.9F,
        34.9F, 19.9F);
    add_bezier(
        cheek_shadow,
        34.9F, 19.9F,
        32.5F, 18.5F,
        29.9F, 18.5F,
        27.6F, 19.8F);
    add_bezier(
        cheek_shadow,
        27.6F, 19.8F,
        25.8F, 20.8F,
        24.6F, 22.5F,
        24.3F, 24.9F);
    add_bezier(
        cheek_shadow,
        24.3F, 24.9F,
        23.5F, 22.3F,
        23.1F, 19.4F,
        23.4F, 16.9F);
    cheek_shadow.CloseFigure();
    Pen cheek_pen{Color(detail::kInk), 0.9F * scale};
    cheek_pen.SetStartCap(LineCapRound);
    cheek_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&cheek_pen, &cheek_shadow);

    GraphicsPath open_eye;
    open_eye.StartFigure();
    add_bezier(
        open_eye,
        27.4F, 21.6F,
        28.9F, 20.1F,
        30.9F, 20.5F,
        31.6F, 22.3F);
    add_bezier(
        open_eye,
        31.6F, 22.3F,
        32.1F, 23.6F,
        31.1F, 25.3F,
        29.6F, 25.6F);
    add_bezier(
        open_eye,
        29.6F, 25.6F,
        28.0F, 26.0F,
        26.6F, 24.9F,
        26.5F, 23.4F);
    add_bezier(
        open_eye,
        26.5F, 23.4F,
        26.5F, 22.7F,
        26.8F, 22.1F,
        27.4F, 21.6F);
    open_eye.CloseFigure();
    graphics.FillPath(&face_fill, &open_eye);
    Pen eye_outline{Color(detail::kInk), 0.8F * scale};
    graphics.DrawPath(&eye_outline, &open_eye);
    graphics.FillEllipse(
        &ink_fill,
        left + 28.8F * scale,
        top + 21.7F * scale,
        1.8F * scale,
        2.6F * scale);
    graphics.FillEllipse(
        &face_fill,
        left + 29.2F * scale,
        top + 22.0F * scale,
        0.5F * scale,
        0.5F * scale);

    GraphicsPath squeezed_eye;
    squeezed_eye.StartFigure();
    add_bezier(
        squeezed_eye,
        33.5F, 22.3F,
        34.5F, 20.8F,
        36.4F, 20.5F,
        37.6F, 21.8F);
    add_bezier(
        squeezed_eye,
        37.6F, 21.8F,
        36.6F, 21.3F,
        35.3F, 21.9F,
        34.9F, 23.3F);
    Pen squeezed_pen{Color(detail::kInk), 0.95F * scale};
    squeezed_pen.SetStartCap(LineCapRound);
    squeezed_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&squeezed_pen, &squeezed_eye);

    GraphicsPath nose;
    nose.StartFigure();
    add_bezier(
        nose,
        37.1F, 23.5F,
        38.1F, 25.1F,
        38.5F, 26.9F,
        37.9F, 28.4F);
    nose.AddBezier(
        point(37.9F, 28.4F),
        point(37.5F, 29.2F),
        point(36.6F, 30.5F),
        point(35.8F, 30.9F));
    Pen nose_pen{Color(detail::kInk), 0.9F * scale};
    nose_pen.SetStartCap(LineCapRound);
    nose_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&nose_pen, &nose);

    GraphicsPath mouth;
    mouth.StartFigure();
    add_bezier(
        mouth,
        34.1F, 32.9F,
        34.9F, 31.4F,
        36.5F, 31.1F,
        37.4F, 32.3F);
    add_bezier(
        mouth,
        37.4F, 32.3F,
        38.3F, 33.5F,
        37.9F, 35.4F,
        36.6F, 36.4F);
    add_bezier(
        mouth,
        36.6F, 36.4F,
        35.3F, 37.3F,
        34.0F, 36.3F,
        33.8F, 35.0F);
    add_bezier(
        mouth,
        33.8F, 35.0F,
        33.7F, 34.2F,
        33.8F, 33.5F,
        34.1F, 32.9F);
    mouth.CloseFigure();
    graphics.FillPath(&ink_fill, &mouth);
    GraphicsPath mouth_glint;
    mouth_glint.StartFigure();
    add_bezier(
        mouth_glint,
        35.0F, 33.9F,
        35.7F, 33.4F,
        36.5F, 33.7F,
        36.6F, 34.5F);
    Pen glint_pen{Color(0xff6c5740), 0.5F * scale};
    glint_pen.SetStartCap(LineCapRound);
    glint_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&glint_pen, &mouth_glint);

    GraphicsPath cheek_hatching;
    cheek_hatching.StartFigure();
    add_bezier(
        cheek_hatching,
        25.5F, 27.4F,
        26.6F, 28.2F,
        27.4F, 29.2F,
        27.8F, 30.5F);
    cheek_hatching.StartFigure();
    add_bezier(
        cheek_hatching,
        26.7F, 31.0F,
        27.8F, 32.2F,
        28.2F, 33.5F,
        28.3F, 34.8F);
    cheek_hatching.StartFigure();
    add_bezier(
        cheek_hatching,
        29.1F, 36.1F,
        30.3F, 36.3F,
        31.3F, 36.8F,
        32.2F, 37.6F);
    Pen cheek_hatch_pen{Color(detail::kInk), 0.5F * scale};
    cheek_hatch_pen.SetStartCap(LineCapRound);
    cheek_hatch_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&cheek_hatch_pen, &cheek_hatching);

    const std::array<PointF, 4> cheek_cut_a{
        point(24.8F, 34.4F),
        point(23.9F, 35.2F),
        point(23.6F, 36.7F),
        point(26.2F, 35.2F)};
    const std::array<PointF, 4> cheek_cut_b{
        point(29.8F, 37.1F),
        point(31.0F, 37.8F),
        point(31.3F, 39.0F),
        point(30.2F, 38.2F)};
    GraphicsPath cheek_cuts;
    cheek_cuts.AddPolygon(
        cheek_cut_a.data(),
        static_cast<INT>(cheek_cut_a.size()));
    cheek_cuts.AddPolygon(
        cheek_cut_b.data(),
        static_cast<INT>(cheek_cut_b.size()));
    graphics.FillPath(&ivory_fill, &cheek_cuts);

    const std::array<PointF, 12> shell_points{
        point(25.1F, 16.4F),
        point(26.4F, 13.9F),
        point(28.3F, 14.8F),
        point(29.3F, 12.6F),
        point(30.9F, 13.4F),
        point(32.6F, 12.0F),
        point(33.5F, 14.5F),
        point(34.9F, 15.5F),
        point(32.6F, 17.5F),
        point(30.6F, 16.6F),
        point(28.9F, 17.9F),
        point(27.1F, 17.0F)};
    GraphicsPath shell;
    shell.AddPolygon(
        shell_points.data(),
        static_cast<INT>(shell_points.size()));
    shell.CloseFigure();
    SolidBrush shell_fill{Color(0xfffff9ec)};
    graphics.FillPath(&shell_fill, &shell);
    Pen shell_outline{Color(0xff211e19), 0.7F * scale};
    shell_outline.SetLineJoin(LineJoinRound);
    graphics.DrawPath(&shell_outline, &shell);

    GraphicsPath shell_crack;
    shell_crack.StartFigure();
    shell_crack.AddLine(point(27.4F, 14.1F), point(28.1F, 15.3F));
    shell_crack.AddLine(point(28.1F, 15.3F), point(27.3F, 15.9F));
    shell_crack.AddLine(point(27.3F, 15.9F), point(28.9F, 16.8F));
    Pen shell_crack_pen{Color(detail::kInk), 0.75F * scale};
    shell_crack_pen.SetStartCap(LineCapRound);
    shell_crack_pen.SetEndCap(LineCapRound);
    shell_crack_pen.SetLineJoin(LineJoinRound);
    graphics.DrawPath(&shell_crack_pen, &shell_crack);

    const std::array<PointF, 5> shard_a{
        point(32.4F, 9.8F),
        point(34.3F, 8.1F),
        point(35.8F, 9.5F),
        point(34.5F, 11.4F),
        point(32.9F, 11.1F)};
    const std::array<PointF, 4> shard_b{
        point(36.8F, 13.3F),
        point(38.3F, 12.3F),
        point(39.3F, 13.9F),
        point(37.8F, 15.0F)};
    const std::array<PointF, 4> shard_c{
        point(39.5F, 8.8F),
        point(40.8F, 8.0F),
        point(41.3F, 9.5F),
        point(39.9F, 10.1F)};
    GraphicsPath shards;
    shards.AddPolygon(
        shard_a.data(),
        static_cast<INT>(shard_a.size()));
    shards.AddPolygon(
        shard_b.data(),
        static_cast<INT>(shard_b.size()));
    shards.AddPolygon(
        shard_c.data(),
        static_cast<INT>(shard_c.size()));
    graphics.FillPath(&shell_fill, &shards);
    graphics.DrawPath(&shell_outline, &shards);

    GraphicsPath accents;
    accents.StartFigure();
    add_bezier(
        accents,
        18.9F, 38.8F,
        17.8F, 41.1F,
        17.8F, 43.5F,
        18.9F, 45.9F);
    accents.StartFigure();
    add_bezier(
        accents,
        21.8F, 41.3F,
        20.4F, 43.6F,
        20.3F, 46.0F,
        21.3F, 47.9F);
    accents.StartFigure();
    add_bezier(
        accents,
        43.5F, 58.9F,
        46.8F, 57.3F,
        49.9F, 57.1F,
        52.6F, 58.1F);
    accents.StartFigure();
    add_bezier(
        accents,
        11.5F, 57.5F,
        13.5F, 55.6F,
        16.0F, 54.9F,
        18.6F, 55.3F);
    Pen accent_pen{Color(detail::kInk), 1.1F * scale};
    accent_pen.SetStartCap(LineCapRound);
    accent_pen.SetEndCap(LineCapRound);
    graphics.DrawPath(&accent_pen, &accents);
}

}  // namespace schiva::ui::brand

#endif

#endif
