# Xournal# — GTK4 port plan

Status: phase 0 done 2026-11-26 (branch `gtk4-port` set up); phase 1 next. Research base: upstream `master` at
`c9e7786` (2026-11-25), our `modern-ui` branch at `a5a0f93`.

## 1. Recommendation in one paragraph

Do **not** start a port from scratch, and do not use either of the two old upstream branches. Upstream maintainer
Benjamin Hennion (bhennion) runs a real, maintained GTK4 port in **PR xournalpp/xournalpp#5615** (branch
`bhennion/xournalpp:pr/GTK4`, open since April 2024). It was rebased onto master two weeks ago (merge base
`4d6810b`, 2026-11-10, only 13 commits behind), compiles, starts and is usable on Linux, macOS and Windows,
with a known list of open problems. The plan is to **base Xournal#'s GTK4 work on that branch**, close the open
items that matter on Linux/GNOME, move it to the GTK 4.10+ API, and then re-land the Xournal# UI on top of it using
**libadwaita**, which replaces most of the custom code we wrote for GTK3 (accent colour, dark mode, the
libadwaita stylesheet, the alert dialog).

## 2. What already exists

| Effort | Where | State | Usable? |
|---|---|---|---|
| `gtk4` branch | upstream, Fabian Keßler (Febbe), Aug 2021 – Nov 2021 | 6 small prep commits (no `GdkScreen`, no `GdkDeviceManager`, no direct `GdkEvent` struct access, no `destroy` vfunc) + one 33k-line WIP commit full of `.orig` files + one more WIP commit. Based on Feb 2022 master. | No. Master has since been restructured (GAction-based actions, GMenu menus, popup wrappers, new toolbar code). Reference only; its ideas were re-done upstream. |
| `gtk4test` branch | upstream, Julius Lehmann, May 2020 | An experiment rewriting the canvas widget and data store ("libxop3") as a separate demo app. | No. Not a port. |
| GTK4 groundwork merged into master | upstream PRs #3306, #4763, #4796, #5077–#5079, #5481, #5500, #5585, #5602, #5630, #5631, #6440 (2022–2025) | All dialogs made async-ready (`PopupWindowWrapper`, `XojMsgBox` callbacks, no `gtk_dialog_run`), open-file logic and image/background dialogs reworked, a `gtk4_helper.h` compatibility layer (323 lines), 28 `todo(gtk4)` markers in 13 files. #6440 backported 22 simple commits from the GTK4 branch to shrink its diff. | Already in our base. |
| **PR #5615 "GTK4 compiling branch"** | `bhennion/xournalpp:pr/GTK4` | 44 commits, 260 files, +11,497 / −17,671 lines on top of master. Compiles, runs, tested by rolandlo on Linux, macOS (Intel/ARM) and Windows in 2024; still being rebased (last commits Feb–Jul 2026, rebased Nov 2026). Maintainer's stated scope: "compile with GTK 4 and look like before"; no libadwaita. | **Yes — our base.** |
| `cou645/xournalpp-gtk4` | standalone repo (not a GitHub fork), Aug 2026 | One 300-file snapshot commit "Port to GTK4/libadwaita build (in-progress)" (+39.6k / −11.7k, mostly regenerated icon files) on June 2026 master, plus one fix commit. No history, no review, unclear testing. | Reference only — useful to compare individual widget ports, not mergeable. |
| `rolandlo/xournalpp-libadwaita-mockup` | Jan 2024 | Blueprint/Workbench mockups for a libadwaita redesign, by an upstream maintainer. | UI reference for phase 4. |

Fork search limits: GitHub has 1,190 forks; I checked repository search for "xournalpp gtk4", the 100 most recently
created forks, and upstream issues/PRs with "gtk4" in the title (18 hits). No other fork showed a significant
GTK4 effort. Forks that only push branches without naming them "gtk4" could have been missed.

### How far PR #5615 got (from its checklist, the code and the PR discussion)

Done: GTK4 build (`gtk4 >= 4.6.1`, `gtksourceview-5`), toolbars rebuilt on a `GtkBox`-based `ToolbarBox`
(replacing `GtkToolbar`/`GtkToolItem`), toolbar customisation incl. drag-and-drop, sidebar, clipboard
(`GdkClipboard`), drag-and-drop, all `.ui` files converted (`gtk4-builder-tool simplify --3to4`), canvas widget
using `measure`/`size_allocate`/`snapshot`, scroll-zoom and touchscreen pinch zoom, double/triple click,
stylus event history (replaces GTK3's `gdk_window_set_event_compression(false)`), plugins re-enabled with an async
dialog API (deprecated Lua functions removed), menu accelerator priority, GtkUriLauncher for help, translations
from `.ui` files, device configuration saved separately for GTK4, the Windows wrapper, CI scripts.

Open (unchecked or reported in the PR):

- **Input**
  - On X11, GDK4 exposes only the "Virtual core pointer", so input devices can't be told apart.
  - Mouse and touchpad report pressure 0.
  - Touch always draws, even when touch drawing is disabled.
  - Touchscreen zoom gestures draw strokes.
  - Upstream GTK issue #7359. Wayland is less affected but not confirmed.
- **Gestures and toolbox:** touchpad pinch zoom is missing, and touchpad tap doesn't open the floating toolbox.
- **Device detection:** device tool classification needs refactoring.
- **Rendering**
  - "TODO restore partial draws": the canvas is redrawn in full through a Cairo node on every frame.
  - Cursor creation is slow (Cairo → pixbuf → texture → cursor).
  - `GdkPixbuf` → `GdkTexture` migration not done.
  - Drawing is jittery on macOS.
  - HiDPI sidebar miniatures use a low-DPI workaround.
- **Background jobs:** closing or switching documents freezes until all sidebar preview jobs finish (`XournalScheduler::removeAllJobs()` blocks).
- **Colours:** `GdkRGBA` is float-based in GTK4, and the double/float conversions aren't fixed yet.
- **Settings:** the "Use stock icons" option is disabled.
- **Platform bugs (not our target, but upstream needs them)**
  - Windows: inserting images, and toolbar-to-toolbar drag-and-drop.
  - macOS: clipboard between instances, and opening the help URL.
  - Copy-paste between two instances can freeze.
- **API debt:** the branch builds against GTK 4.0 API (`GDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_0`). Classes deprecated in GTK 4.10 are still used:

  | Deprecated in 4.10 | References |
  |---|---|
  | `GtkComboBox` | ~170 |
  | `GtkTreeView` / list stores / cell renderers | ~100 |
  | `GtkFileChooser` | 44 |
  | colour chooser / button | 43 |
  | `GtkDialog` / `GtkMessageDialog` | 16 |
  | `GtkStyleContext` | 7 |
  | font chooser | 4 |

## 3. The technical changes, area by area

| Area | GTK3 (master / modern-ui) | GTK4 | In PR #5615 | Our work |
|---|---|---|---|---|
| Build | `gtk+-3.0`, `gtksourceview-4` | `gtk4`, `gtksourceview-5`, + `libadwaita-1` for Xournal# | done (no libadwaita) | add libadwaita, raise minimums (phase 3/4) |
| Main loop / app | `GtkApplication`, `gtk_widget_show_all`, `GtkContainer` | `GtkApplication`/`AdwApplication`, `gtk_window_present`, no containers API | done | switch to `AdwApplication` |
| Canvas | `draw` signal with Cairo, `GtkLayout` scrolling | `snapshot` vfunc, `GtkScrollable`, render nodes | done, but full redraw via Cairo node | restore partial redraws / cache page renders as `GdkTexture` |
| Input | raw `GdkEvent` structs, `event` signal, event compression off, X11 touch-disable | event controllers (`GtkGestureStylus`, `GtkGestureClick`, `GtkGestureZoom`, `GtkEventControllerLegacy`), `gdk_event_get_history()` | done, with the input gaps listed above | verify on Wayland + Wacom/touch; fix touch-drawing setting, pinch zoom |
| Toolbars | `GtkToolbar`, `GtkToolItem`, overflow proxies | custom `ToolbarBox` on `GtkBox` | done | re-apply our toolbar styling |
| Menus | `GtkMenuBar` from GMenu XML, `GtkMenu` popups | `GtkPopoverMenuBar`, `GtkPopoverMenu` | mostly done (21 refs left) | keep classic menubar (no hamburger), style as libadwaita |
| Dialogs | `GtkDialog`, `GtkMessageDialog`, async wrapper | `AdwAlertDialog`/`AdwDialog` (or `GtkAlertDialog`), `GtkWindow` | ported but still `GtkDialog`-based | move `XojMsgBox` to `AdwAlertDialog`; other dialogs to `AdwDialog` gradually |
| File choosers | `GtkFileChooserNative` | `GtkFileDialog` (4.10) | uses deprecated `GtkFileChooser` | port (open/save/export/image/PDF background) |
| Lists, combos | `GtkTreeView`, `GtkComboBox(Text)` | `GtkListView`/`GtkColumnView`, `GtkDropDown` | deprecated widgets still used | port (Preferences, plugin list, toolbar manager, palettes, layer combo) |
| Colour/font pickers | `GtkColorButton`, `GtkFontButton` | `GtkColorDialogButton`, `GtkFontDialogButton` (4.10) | deprecated | port |
| Clipboard | `GtkClipboard`, targets | `GdkClipboard`, `GdkContentProvider`, async reads | done | test cross-app copy/paste |
| Drag and drop | `gtk_drag_*`, `GtkTargetEntry` | `GtkDragSource`, `GtkDropTarget` | done | — |
| Windows/surfaces | `GdkWindow`, `GdkScreen` | `GdkSurface`, `GdkDisplay`, `GdkMonitor` | done | — |
| Cursors | Cairo-drawn surfaces | `GdkCursor` from `GdkTexture` | done, slow | cache cursors as textures |
| Images | `GdkPixbuf` everywhere (84 refs) | `GdkTexture`/`GdkPaintable` where shown | not started | only where it helps (icons, previews) |
| Styling | CSS + `GtkStyleContext`, `-gtk-icon-style` | CSS via `gtk_widget_add_css_class`, no `-gtk-icon-style`, `AdwStyleManager` | plain GTK4 CSS | replace `adwaita.css` + generated palette with libadwaita |
| Plugins (Lua) | sync `msgbox`, old action API | async dialogs, new action API | done, **breaks old plugins** | check bundled plugins; document for users |
| Settings/config | `settings.xml`, device config | device config stored separately for GTK4 | done | keep `XOURNALPP_APP_ID` / `XOURNALPP_CONFIG_FOLDER` |

## 4. How the Xournal# UI work maps to GTK4 + libadwaita

| Xournal# GTK3 change | GTK4 replacement | Effect |
|---|---|---|
| `SystemAppearance` (XDG portal accent + dark) | `AdwStyleManager` (`accent-color`, `dark`; accent needs libadwaita ≥ 1.6) | delete our portal code |
| `ui/adwaita.css` (libadwaita look for GTK3) + generated palette CSS | libadwaita's own stylesheet | delete; keep a small app CSS (canvas background, swatches, floating toolbox, bottom bar) |
| Accent colour for selection/border (`Settings::systemAccentColor`) | read `AdwStyleManager` accent (`adw_style_manager_get_accent_color_rgba`) | same setting, new source |
| Window shadow/rounding fixes | libadwaita/GTK4 CSD | delete |
| GNOME-style "Save Changes?" dialog (`XojMsgBox` button styles) | `AdwAlertDialog` with `ADW_RESPONSE_SUGGESTED` / `ADW_RESPONSE_DESTRUCTIVE` | native, simpler |
| Symbolic icon set (`ui/iconsSymbolic`, `-gtk-icon-style: symbolic`) | same icon files; request `*-symbolic` names explicitly (GTK4 has no `-gtk-icon-style`) | change `IconNameHelper` to append `-symbolic` |
| Outlined zoom icons via `xopp-` stock overrides | same (`ToolbarBox` items) | port the lookup |
| Colour swatches with ring (per-button CSS provider, `color-swatch` classes) | small custom widget drawing swatch + ring in `snapshot`, or CSS `outline` with per-widget classes | cleaner in GTK4 |
| Zoom slider value position, bottom bar styling | CSS on the GTK4 widgets | re-apply |
| Preferences "View" page checkboxes | later: `AdwPreferencesDialog` with `AdwSwitchRow`s | optional big redesign |
| App ID / config folder / RPM side-by-side install | unchanged | carry over |

Decision needed (phase 0): libadwaita **required** for Xournal# (recommended — it is our UI's foundation), with
minimums GTK ≥ 4.14 and libadwaita ≥ 1.6 (Fedora 41+, Ubuntu 25.04+, Debian 13). Upstream can't take that
dependency, so Xournal# UI commits stay separate from fixes we send upstream.

## 5. Phased plan

Effort estimates assume one person part-time; they're rough.

### Phase 0 — Set-up (½ day) — done 2026-11-26
1. Build deps installed: GTK 4.24.2, gtksourceview 5.22.0, libadwaita 1.10.0 (Fedora 45).
2. Branch `gtk4-port` in our fork created from `bhennion/xournalpp:pr/GTK4` at
   `c2407d7aa63c89031621558a4cf3b69214c8cf02` ("Run gtk4-demo"), whose merge base with upstream master is
   `4d6810b37` (2026-11-10). `modern-ui` (GTK3) stays the shipped Xournal# until GTK4 reaches parity.
3. Branch policy:
   - `gtk4-port` = bhennion's `pr/GTK4` + our commits on top. When bhennion updates `pr/GTK4`, rebase onto it
     (`git fetch bhennion pr/GTK4 && git rebase --onto bhennion/pr/GTK4 <old base> gtk4-port`); record each new base here.
   - The fork's existing `gtk4` and `gtk4test` branches are untouched copies of upstream's stale 2021/2020 branches.
   - Commits are kept in two kinds, never mixed in one commit:
     - **port fixes** (generic GTK4 work, no libadwaita, no Xournal# naming) — prefix `gtk4:`; these can be offered
       upstream as PRs against `bhennion/xournalpp:pr/GTK4`;
     - **Xournal# UI** (libadwaita, styling, app ID, packaging) — prefix `xournal#:`; fork-only.
   - Phases 1–3 produce mostly port fixes, phase 4 mostly Xournal# UI.
   - The GTK3 `modern-ui` work is not merged into `gtk4-port`; it's re-implemented in phase 4 (most of it is replaced by
     libadwaita), using `modern-ui` as the reference.

### Phase 1 — Baseline (1–2 days)
1. Build PR #5615 unchanged on Fedora 45 (GTK 4.24); run `ctest`.
2. Smoke-test against a checklist and compare with GTK3 Xournal#:
   - open/save `.xopp`, PDF annotate, export
   - pen pressure (Wacom), eraser, highlighter, selection, text with IME, LaTeX
   - audio record/playback, plugins, toolbar customisation, sidebar
   - fullscreen/presentation, touch and palm rejection, touchpad scroll and zoom
3. Note every regression in a tracking list (GitHub issues on our fork). Measure drawing latency and CPU against GTK3.

Exit: a written regression list with priorities.

### Phase 2 — Fix blockers for daily use on GNOME/Wayland (1–3 weeks)
1. **Input:**
   - honour the "touch drawing" setting and palm rejection
   - touchpad pinch zoom (`GtkGestureZoom` on the canvas)
   - touchpad tap for the floating toolbox
   - check stylus/eraser/touch tool classification on Wayland and refactor the device classification as noted in the PR review
2. **Rendering performance:**
   - restore partial redraws (only invalidate dirty page regions, cache rendered pages/layers as `GdkTexture` and compose with `gtk_snapshot_append_texture` instead of one big Cairo node)
   - cache cursors
3. **Scheduler freeze:** make `removeAllJobs()` not block on running jobs; only queue sidebar previews for visible pages.
4. **Colour precision:** fix `GdkRGBA` float conversions.
5. **HiDPI:** sidebar miniatures at full resolution.

Exit: Xournal# GTK4 usable for a week of real note-taking without falling back to GTK3.

### Phase 3 — Move to the GTK 4.10+ API (1–2 weeks)
1. Raise `GDK_VERSION_MIN_REQUIRED` to the chosen floor (4.14) so deprecations show as warnings; work through them:
   - `GtkFileChooser*` → `GtkFileDialog`
   - `GtkColorButton` → `GtkColorDialogButton`
   - `GtkFontButton` → `GtkFontDialogButton`
   - `GtkComboBox*` → `GtkDropDown`
   - `GtkTreeView` + stores → `GtkListView`/`GtkColumnView` + `GListModel`
   - `GtkStyleContext` → widget CSS classes
   - `gtk_show_uri` → `GtkUriLauncher`
2. Dialogs off `GtkDialog`: `XojMsgBox` → `AdwAlertDialog` (phase 4) and the rest → `GtkWindow`/`AdwDialog`.

Exit: builds warning-free with deprecations enabled.

### Phase 4 — libadwaita and the Xournal# UI (1–2 weeks)
1. Add `libadwaita-1` to CMake; `AdwApplication`; `AdwApplicationWindow` + `AdwToolbarView` holding the classic menubar and toolbars at the top and the page/zoom bar at the bottom.
2. Theme: follow `AdwStyleManager` (dark + accent); remove `SystemAppearance`, `adwaita.css`, the generated palette; keep the "use system accent colour" and canvas-background behaviour.
3. Icons: explicit `-symbolic` names in `IconNameHelper`; outlined zoom icons; default icon set stays "Symbolic (GNOME)".
4. `XojMsgBox` on `AdwAlertDialog` (Save Changes?, replace file, errors).
5. Colour palette swatch widget (round swatch, ring in its colour, rainbow custom-colour button).
6. Re-apply small CSS: bottom bar, zoom slider, floating toolbox card.
7. Optional, later: Preferences as `AdwPreferencesDialog`.

Exit: GTK4 Xournal# looks at least as native as the GTK3 `modern-ui` build.

### Phase 5 — Compatibility (2–3 days)
1. Lua plugins: run the bundled plugins; list API changes (sync `msgbox` removed, new dialog/action API) in release notes.
2. Settings: Xournal# GTK4 reads the existing `~/.config/xournal-sharp`; device config migration (GTK4 stores device config separately — devices need re-assigning once).
3. File format unchanged; open old `.xopp`/`.xoj` test files.

### Phase 6 — Packaging (1 day)
1. Spec: `BuildRequires: pkgconfig(gtk4) pkgconfig(gtksourceview-5) pkgconfig(libadwaita-1)`; version bump (e.g. `1.4.0~gtk4`), Release `modernui`.
2. Keep side-by-side install (`/opt/xournal-sharp`, own app ID). Build, install, test.
3. Optional: publish RPMs as GitHub release assets instead of committing them.

### Phase 7 — Staying in sync (ongoing)
1. Rebase our `gtk4` branch whenever bhennion updates `pr/GTK4`; when upstream merges GTK4 into master, rebase onto master.
2. Offer generic fixes back: comment on PR #5615 or open PRs against `bhennion/xournalpp:pr/GTK4`.

## 6. Risks

- **Upstream timing:** PR #5615 has been open 2½ years; it may change shape or be merged differently, which costs us rebases. Mitigation: small, separate commits; follow the PR.
- **X11 device limitation is a GTK issue** (#7359) — we can't fix it; on GNOME/Wayland it should not matter, but verify with a Wacom early (phase 1).
- **Drawing latency** is the app's core; GTK4's snapshot pipeline can be slower if pages are re-rendered with Cairo each frame. Phase 2's texture caching is essential, not optional.
- **Plugin breakage** for users with custom Lua plugins.
- **Distro floor:** requiring libadwaita ≥ 1.6 drops older LTS distros — fine for Xournal# (Fedora), not for upstream.
- **Testing scope:** we only test Linux/GNOME; Windows/macOS problems in the PR remain upstream's.

## 7. Sources

- PR #5615 and discussion: https://github.com/xournalpp/xournalpp/pull/5615
- Backport PR #6440: https://github.com/xournalpp/xournalpp/pull/6440
- GTK4 prep issue #2722: https://github.com/xournalpp/xournalpp/issues/2722
- GTK X11 device issue: https://gitlab.gnome.org/GNOME/gtk/-/issues/7359
- Old branches: `origin/gtk4`, `origin/gtk4test` (fetched locally)
- `cou645/xournalpp-gtk4`: https://github.com/cou645/xournalpp-gtk4
- libadwaita mockups: https://github.com/rolandlo/xournalpp-libadwaita-mockup
