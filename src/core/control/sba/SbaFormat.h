/*
 * Xournal++
 *
 * Shared constants and helpers for the Saber (.sba) import and export
 *
 * A .sba file is a zip archive holding `main.sbn2` (the note, as BSON) and its assets `main.sbn2.0`, `main.sbn2.1`, ...
 * (images and PDFs). The format is described by Saber's source code (EditorCoreInfo, EditorPage, Stroke, EditorImage).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>   // for uint32_t
#include <optional>  // for optional
#include <string>    // for string

#include <cairo.h>  // for cairo_surface_t

class PageType;

namespace xoj::sba {

/// The newest Saber file format version we know
constexpr int SBN_VERSION = 19;

/// The name of the note inside the archive
constexpr const char* MAIN_FILE = "main.sbn2";

/// Saber's default page is 1000 units wide. We map that to the width of an A4 page (in points), so that a Saber page
/// becomes roughly A4 and comes back to 1000 units on export.
constexpr double SCALE = 595.2755905511812 / 1000.0;

constexpr double DEFAULT_PAGE_WIDTH = 1000.0;
constexpr double DEFAULT_PAGE_HEIGHT = 1400.0;
constexpr int DEFAULT_LINE_HEIGHT = 40;
constexpr int DEFAULT_LINE_THICKNESS = 3;
constexpr double DEFAULT_STROKE_SIZE = 10.0;
constexpr double DEFAULT_THINNING = 0.5;

/// Saber's highlighter color alpha
constexpr uint32_t HIGHLIGHTER_ALPHA = 100;

/// Saber's typed text: lines are `lineHeight` apart, start `1.2 * lineHeight` from the top and `0.5 * lineHeight` from
/// the left
constexpr double TEXT_TOP = 1.2;
constexpr double TEXT_LEFT = 0.5;
/// Font size (in units of lineHeight). Saber's handwriting font (Neucha) is set at one lineHeight but its letters are
/// small, so a regular font looks the same at about 60%.
constexpr double TEXT_FONT_SIZE = 0.6;
/// The baseline of typed text sits this far (in units of lineHeight) above the ruling line below it
constexpr double TEXT_BASELINE_GAP = 0.15;
/// Saber's ruling lines are at y = (2 + k) * lineHeight
constexpr double FIRST_RULING_LINE = 2.0;

/**
 * Saber draws the first line of lined paper 2 lineHeights from the top, Xournal++ 80pt from the top. On such pages, the
 * content is moved down by the returned amount (in points) on import and back up on export, so that handwriting stays
 * on the lines. Returns 0 for all other backgrounds.
 */
double rulingShift(const PageType& type);
constexpr const char* TEXT_FONT = "Sans";

/// Flutter's BoxFit.contain, the fit Saber uses for page backgrounds
constexpr int BOX_FIT_CONTAIN = 1;

/// Encodes a cairo surface as PNG
std::optional<std::string> surfaceToPng(cairo_surface_t* surface);

}  // namespace xoj::sba
