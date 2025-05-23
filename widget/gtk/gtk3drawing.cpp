/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This file contains painting functions for each of the gtk2 widgets.
 * Adapted from the gtkdrawing.c, and gtk+2.0 source.
 */

#include <gtk/gtk.h>
#include <gdk/gdkprivate.h>
#include <string.h>
#include "gdk/gdk.h"
#include "gtkdrawing.h"
#include "mozilla/Assertions.h"
#include "mozilla/ScopeExit.h"
#include "prinrval.h"
#include "WidgetStyleCache.h"
#include "nsString.h"
#include "nsDebug.h"
#include "WidgetUtilsGtk.h"

#include <math.h>
#include <dlfcn.h>

static gboolean checkbox_check_state;
static gboolean notebook_has_tab_gap;

static ToolbarGTKMetrics sToolbarMetrics;

using mozilla::Span;

#define ARROW_UP 0
#define ARROW_DOWN G_PI
#define ARROW_RIGHT G_PI_2
#define ARROW_LEFT (G_PI + G_PI_2)

#if 0
// It's used for debugging only to compare Gecko widget style with
// the ones used by Gtk+ applications.
static void
style_path_print(GtkStyleContext *context)
{
    const GtkWidgetPath* path = gtk_style_context_get_path(context);

    static auto sGtkWidgetPathToStringPtr =
        (char * (*)(const GtkWidgetPath *))
        dlsym(RTLD_DEFAULT, "gtk_widget_path_to_string");

    fprintf(stderr, "Style path:\n%s\n\n", sGtkWidgetPathToStringPtr(path));
}
#endif

static gint moz_gtk_get_tab_thickness(GtkStyleContext* style);

static void moz_gtk_add_style_border(GtkStyleContext* style, gint* left,
                                     gint* top, gint* right, gint* bottom) {
  GtkBorder border;

  gtk_style_context_get_border(style, gtk_style_context_get_state(style),
                               &border);

  *left += border.left;
  *right += border.right;
  *top += border.top;
  *bottom += border.bottom;
}

static void moz_gtk_add_style_padding(GtkStyleContext* style, gint* left,
                                      gint* top, gint* right, gint* bottom) {
  GtkBorder padding;

  gtk_style_context_get_padding(style, gtk_style_context_get_state(style),
                                &padding);

  *left += padding.left;
  *right += padding.right;
  *top += padding.top;
  *bottom += padding.bottom;
}

static void moz_gtk_add_border_padding(GtkStyleContext* style, gint* left,
                                       gint* top, gint* right, gint* bottom) {
  moz_gtk_add_style_border(style, left, top, right, bottom);
  moz_gtk_add_style_padding(style, left, top, right, bottom);
}

// GetStateFlagsFromGtkWidgetState() can be safely used for the specific
// GtkWidgets that set both prelight and active flags.  For other widgets,
// either the GtkStateFlags or Gecko's GtkWidgetState need to be carefully
// adjusted to match GTK behavior.  Although GTK sets insensitive and focus
// flags in the generic GtkWidget base class, GTK adds prelight and active
// flags only to widgets that are expected to demonstrate prelight or active
// states.  This contrasts with HTML where any element may have :active and
// :hover states, and so Gecko's GtkStateFlags do not necessarily map to GTK
// flags.  Failure to restrict the flags in the same way as GTK can cause
// generic CSS selectors from some themes to unintentionally match elements
// that are not expected to change appearance on hover or mouse-down.
static GtkStateFlags GetStateFlagsFromGtkWidgetState(GtkWidgetState* state) {
  GtkStateFlags stateFlags = GTK_STATE_FLAG_NORMAL;

  if (state->disabled)
    stateFlags = GTK_STATE_FLAG_INSENSITIVE;
  else {
    if (state->depressed || state->active)
      stateFlags =
          static_cast<GtkStateFlags>(stateFlags | GTK_STATE_FLAG_ACTIVE);
    if (state->inHover)
      stateFlags =
          static_cast<GtkStateFlags>(stateFlags | GTK_STATE_FLAG_PRELIGHT);
    if (state->focused)
      stateFlags =
          static_cast<GtkStateFlags>(stateFlags | GTK_STATE_FLAG_FOCUSED);
    if (state->backdrop)
      stateFlags =
          static_cast<GtkStateFlags>(stateFlags | GTK_STATE_FLAG_BACKDROP);
  }

  return stateFlags;
}

static GtkStateFlags GetStateFlagsFromGtkTabFlags(GtkTabFlags flags) {
  return ((flags & MOZ_GTK_TAB_SELECTED) == 0) ? GTK_STATE_FLAG_NORMAL
                                               : GTK_STATE_FLAG_ACTIVE;
}

gint moz_gtk_init() {
  if (gtk_major_version > 3 ||
      (gtk_major_version == 3 && gtk_minor_version >= 14))
    checkbox_check_state = GTK_STATE_FLAG_CHECKED;
  else
    checkbox_check_state = GTK_STATE_FLAG_ACTIVE;

  moz_gtk_refresh();

  return MOZ_GTK_SUCCESS;
}

void moz_gtk_refresh() {
  if (gtk_check_version(3, 20, 0) != nullptr) {
    // Deprecated for Gtk >= 3.20+
    GtkStyleContext* style = GetStyleContext(MOZ_GTK_TAB_TOP);
    gtk_style_context_get_style(style, "has-tab-gap", &notebook_has_tab_gap,
                                NULL);
  } else {
    notebook_has_tab_gap = true;
  }

  sToolbarMetrics.initialized = false;

  /* This will destroy all of our widgets */
  ResetWidgetCache();
}

gint moz_gtk_splitter_get_metrics(gint orientation, gint* size) {
  GtkStyleContext* style;
  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    style = GetStyleContext(MOZ_GTK_SPLITTER_HORIZONTAL);
  } else {
    style = GetStyleContext(MOZ_GTK_SPLITTER_VERTICAL);
  }
  gtk_style_context_get_style(style, "handle_size", size, NULL);
  return MOZ_GTK_SUCCESS;
}

static void CalculateToolbarButtonMetrics(WidgetNodeType aAppearance,
                                          ToolbarButtonGTKMetrics* aMetrics,
                                          gint* aMaxInlineMargin) {
  gint iconWidth, iconHeight;
  if (!gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &iconWidth, &iconHeight)) {
    NS_WARNING("Failed to get Gtk+ icon size for titlebar button!");
    // Use some reasonable fallback size
    iconWidth = 16;
    iconHeight = 16;
  }

  GtkStyleContext* style = GetStyleContext(aAppearance);
  gint width = 0, height = 0;
  if (!gtk_check_version(3, 20, 0)) {
    gtk_style_context_get(style, gtk_style_context_get_state(style),
                          "min-width", &width, "min-height", &height, NULL);
  }

  // Cover cases when min-width/min-height is not set, it's invalid
  // or we're running on Gtk+ < 3.20.
  if (width < iconWidth) width = iconWidth;
  if (height < iconHeight) height = iconHeight;

  gint left = 0, top = 0, right = 0, bottom = 0;
  moz_gtk_add_border_padding(style, &left, &top, &right, &bottom);

  // Button size is calculated as min-width/height + border/padding.
  width += left + right;
  height += top + bottom;

  // Place icon at button center.
  aMetrics->iconXPosition = (width - iconWidth) / 2;
  aMetrics->iconYPosition = (height - iconHeight) / 2;
  aMetrics->minSizeWithBorder = {width, height};

  GtkBorder margin = {0};
  gtk_style_context_get_margin(style, gtk_style_context_get_state(style),
                               &margin);
  *aMaxInlineMargin = std::max(*aMaxInlineMargin, margin.left + margin.right);
}

size_t GetGtkHeaderBarButtonLayout(Span<ButtonLayout> aButtonLayout,
                                   bool* aReversedButtonsPlacement) {
  gchar* decorationLayoutSetting = nullptr;
  GtkSettings* settings = gtk_settings_get_default();
  g_object_get(settings, "gtk-decoration-layout", &decorationLayoutSetting,
               nullptr);
  auto free = mozilla::MakeScopeExit([&] { g_free(decorationLayoutSetting); });

  // Use a default layout
  const gchar* decorationLayout = "menu:minimize,maximize,close";
  if (decorationLayoutSetting) {
    decorationLayout = decorationLayoutSetting;
  }

  // "minimize,maximize,close:" layout means buttons are on the opposite
  // titlebar side. close button is always there.
  if (aReversedButtonsPlacement) {
    const char* closeButton = strstr(decorationLayout, "close");
    const char* separator = strchr(decorationLayout, ':');
    *aReversedButtonsPlacement =
        closeButton && separator && closeButton < separator;
  }

  // We check what position a button string is stored in decorationLayout.
  //
  // decorationLayout gets its value from the GNOME preference:
  // org.gnome.desktop.vm.preferences.button-layout via the
  // gtk-decoration-layout property.
  //
  // Documentation of the gtk-decoration-layout property can be found here:
  // https://developer.gnome.org/gtk3/stable/GtkSettings.html#GtkSettings--gtk-decoration-layout
  if (aButtonLayout.IsEmpty()) {
    return 0;
  }

  nsDependentCSubstring layout(decorationLayout, strlen(decorationLayout));

  size_t activeButtons = 0;
  for (const auto& part : layout.Split(':')) {
    for (const auto& button : part.Split(',')) {
      if (button.EqualsLiteral("close")) {
        aButtonLayout[activeButtons++] = {MOZ_GTK_HEADER_BAR_BUTTON_CLOSE};
      } else if (button.EqualsLiteral("minimize")) {
        aButtonLayout[activeButtons++] = {MOZ_GTK_HEADER_BAR_BUTTON_MINIMIZE};
      } else if (button.EqualsLiteral("maximize")) {
        aButtonLayout[activeButtons++] = {MOZ_GTK_HEADER_BAR_BUTTON_MAXIMIZE};
      }
      if (activeButtons == aButtonLayout.Length()) {
        return activeButtons;
      }
    }
  }
  return activeButtons;
}

static void EnsureToolbarMetrics() {
  if (sToolbarMetrics.initialized) {
    return;
  }
  sToolbarMetrics = {};

  // Calculate titlebar button visibility and positions.
  ButtonLayout buttonLayout[TOOLBAR_BUTTONS];
  size_t activeButtonNums =
      GetGtkHeaderBarButtonLayout(Span(buttonLayout), nullptr);

  for (const auto& layout : Span(buttonLayout, activeButtonNums)) {
    int buttonIndex = layout.mType - MOZ_GTK_HEADER_BAR_BUTTON_CLOSE;
    ToolbarButtonGTKMetrics* metrics = &sToolbarMetrics.button[buttonIndex];
    CalculateToolbarButtonMetrics(layout.mType, metrics,
                                  &sToolbarMetrics.inlineSpacing);
  }

  // Account for the spacing property in the header bar.
  // Default to 6 pixels (gtk/gtkheaderbar.c)
  gint spacing = 6;
  g_object_get(GetWidget(MOZ_GTK_HEADER_BAR), "spacing", &spacing, nullptr);
  sToolbarMetrics.inlineSpacing += spacing;
  sToolbarMetrics.initialized = true;
}

const ToolbarButtonGTKMetrics* GetToolbarButtonMetrics(
    WidgetNodeType aAppearance) {
  EnsureToolbarMetrics();

  int buttonIndex = (aAppearance - MOZ_GTK_HEADER_BAR_BUTTON_CLOSE);
  NS_ASSERTION(buttonIndex >= 0 && buttonIndex <= TOOLBAR_BUTTONS,
               "GetToolbarButtonMetrics(): Wrong titlebar button!");
  return sToolbarMetrics.button + buttonIndex;
}

gint moz_gtk_get_titlebar_button_spacing() {
  EnsureToolbarMetrics();
  return sToolbarMetrics.inlineSpacing;
}

static gint moz_gtk_window_decoration_paint(cairo_t* cr,
                                            const GdkRectangle* rect,
                                            GtkWidgetState* state,
                                            GtkTextDirection direction) {
  if (mozilla::widget::GdkIsWaylandDisplay()) {
    // Doesn't seem to be needed.
    return MOZ_GTK_SUCCESS;
  }
  GtkStateFlags state_flags = GetStateFlagsFromGtkWidgetState(state);
  GtkStyleContext* windowStyle =
      GetStyleContext(MOZ_GTK_HEADERBAR_WINDOW, state->image_scale);
  const bool solidDecorations =
      gtk_style_context_has_class(windowStyle, "solid-csd");
  GtkStyleContext* decorationStyle =
      GetStyleContext(solidDecorations ? MOZ_GTK_WINDOW_DECORATION_SOLID
                                       : MOZ_GTK_WINDOW_DECORATION,
                      state->image_scale, GTK_TEXT_DIR_LTR, state_flags);

  gtk_render_background(decorationStyle, cr, rect->x, rect->y, rect->width,
                        rect->height);
  gtk_render_frame(decorationStyle, cr, rect->x, rect->y, rect->width,
                   rect->height);
  return MOZ_GTK_SUCCESS;
}

gint moz_gtk_button_get_default_overflow(gint* border_top, gint* border_left,
                                         gint* border_bottom,
                                         gint* border_right) {
  GtkBorder* default_outside_border;

  GtkStyleContext* style = GetStyleContext(MOZ_GTK_BUTTON);
  gtk_style_context_get_style(style, "default-outside-border",
                              &default_outside_border, NULL);

  if (default_outside_border) {
    *border_top = default_outside_border->top;
    *border_left = default_outside_border->left;
    *border_bottom = default_outside_border->bottom;
    *border_right = default_outside_border->right;
    gtk_border_free(default_outside_border);
  } else {
    *border_top = *border_left = *border_bottom = *border_right = 0;
  }
  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_button_get_default_border(gint* border_top,
                                              gint* border_left,
                                              gint* border_bottom,
                                              gint* border_right) {
  GtkBorder* default_border;

  GtkStyleContext* style = GetStyleContext(MOZ_GTK_BUTTON);
  gtk_style_context_get_style(style, "default-border", &default_border, NULL);

  if (default_border) {
    *border_top = default_border->top;
    *border_left = default_border->left;
    *border_bottom = default_border->bottom;
    *border_right = default_border->right;
    gtk_border_free(default_border);
  } else {
    /* see gtkbutton.c */
    *border_top = *border_left = *border_bottom = *border_right = 1;
  }
  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_button_paint(cairo_t* cr, const GdkRectangle* rect,
                                 GtkWidgetState* state, GtkReliefStyle relief,
                                 GtkWidget* widget,
                                 GtkTextDirection direction) {
  if (!widget) {
    return MOZ_GTK_UNKNOWN_WIDGET;
  }

  GtkStateFlags state_flags = GetStateFlagsFromGtkWidgetState(state);
  GtkStyleContext* style = gtk_widget_get_style_context(widget);
  gint x = rect->x, y = rect->y, width = rect->width, height = rect->height;

  gtk_widget_set_direction(widget, direction);

  gtk_style_context_save(style);
  StyleContextSetScale(style, state->image_scale);
  gtk_style_context_set_state(style, state_flags);

  if (state->isDefault && relief == GTK_RELIEF_NORMAL && !state->focused &&
      !(state_flags & GTK_STATE_FLAG_PRELIGHT)) {
    /* handle default borders both outside and inside the button */
    gint default_top, default_left, default_bottom, default_right;
    moz_gtk_button_get_default_overflow(&default_top, &default_left,
                                        &default_bottom, &default_right);
    x -= default_left;
    y -= default_top;
    width += default_left + default_right;
    height += default_top + default_bottom;
    gtk_render_background(style, cr, x, y, width, height);
    gtk_render_frame(style, cr, x, y, width, height);
    moz_gtk_button_get_default_border(&default_top, &default_left,
                                      &default_bottom, &default_right);
    x += default_left;
    y += default_top;
    width -= (default_left + default_right);
    height -= (default_top + default_bottom);
  } else if (relief != GTK_RELIEF_NONE || state->depressed ||
             (state_flags & GTK_STATE_FLAG_PRELIGHT)) {
    /* the following line can trigger an assertion (Crux theme)
       file ../../gdk/gdkwindow.c: line 1846 (gdk_window_clear_area):
       assertion `GDK_IS_WINDOW (window)' failed */
    gtk_render_background(style, cr, x, y, width, height);
    gtk_render_frame(style, cr, x, y, width, height);
  }

  if (state->focused) {
    GtkBorder border;
    gtk_style_context_get_border(style, state_flags, &border);
    x += border.left;
    y += border.top;
    width -= (border.left + border.right);
    height -= (border.top + border.bottom);
    gtk_render_focus(style, cr, x, y, width, height);
  }
  gtk_style_context_restore(style);
  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_header_bar_button_paint(cairo_t* cr, GdkRectangle* aRect,
                                            GtkWidgetState* state,
                                            GtkReliefStyle relief,
                                            WidgetNodeType aIconWidgetType,
                                            GtkTextDirection direction) {
  GtkWidget* buttonWidget = GetWidget(aIconWidgetType);
  if (!buttonWidget) {
    return MOZ_GTK_UNKNOWN_WIDGET;
  }

  const ToolbarButtonGTKMetrics* metrics = GetToolbarButtonMetrics(
      aIconWidgetType == MOZ_GTK_HEADER_BAR_BUTTON_MAXIMIZE_RESTORE
          ? MOZ_GTK_HEADER_BAR_BUTTON_MAXIMIZE
          : aIconWidgetType);
  // Vertically center and clamp the rect to the desired size.
  if (aRect->height > metrics->minSizeWithBorder.height) {
    gint diff = aRect->height - metrics->minSizeWithBorder.height;
    aRect->y += diff / 2;
    aRect->height = metrics->minSizeWithBorder.height;
  }
  moz_gtk_button_paint(cr, aRect, state, relief, buttonWidget, direction);

  GtkWidget* iconWidget =
      gtk_bin_get_child(GTK_BIN(GetWidget(aIconWidgetType)));
  if (!iconWidget) {
    return MOZ_GTK_UNKNOWN_WIDGET;
  }
  cairo_surface_t* surface =
      GetWidgetIconSurface(iconWidget, state->image_scale);

  if (surface) {
    GtkStyleContext* style = gtk_widget_get_style_context(buttonWidget);
    GtkStateFlags state_flags = GetStateFlagsFromGtkWidgetState(state);

    gtk_style_context_save(style);
    StyleContextSetScale(style, state->image_scale);
    gtk_style_context_set_state(style, state_flags);

    /* This is available since Gtk+ 3.10 as well as GtkHeaderBar */
    gtk_render_icon_surface(style, cr, surface,
                            aRect->x + metrics->iconXPosition,
                            aRect->y + metrics->iconYPosition);
    gtk_style_context_restore(style);
  }

  return MOZ_GTK_SUCCESS;
}

/**
 * Get minimum widget size as sum of margin, padding, border and
 * min-width/min-height.
 */
static void moz_gtk_get_widget_min_size(GtkStyleContext* style, int* width,
                                        int* height) {
  GtkStateFlags state_flags = gtk_style_context_get_state(style);
  gtk_style_context_get(style, state_flags, "min-height", height, "min-width",
                        width, nullptr);

  GtkBorder border, padding, margin;
  gtk_style_context_get_border(style, state_flags, &border);
  gtk_style_context_get_padding(style, state_flags, &padding);
  gtk_style_context_get_margin(style, state_flags, &margin);

  *width += border.left + border.right + margin.left + margin.right +
            padding.left + padding.right;
  *height += border.top + border.bottom + margin.top + margin.bottom +
             padding.top + padding.bottom;
}

/* See gtk_range_draw() for reference. */
static gint moz_gtk_scale_paint(cairo_t* cr, GdkRectangle* rect,
                                GtkWidgetState* state, GtkOrientation flags,
                                GtkTextDirection direction) {
  GtkStateFlags state_flags = GetStateFlagsFromGtkWidgetState(state);
  gint x, y, width, height, min_width, min_height;
  GtkStyleContext* style;
  GtkBorder margin;

  moz_gtk_get_scale_metrics(flags, &min_width, &min_height);

  WidgetNodeType widget = (flags == GTK_ORIENTATION_HORIZONTAL)
                              ? MOZ_GTK_SCALE_TROUGH_HORIZONTAL
                              : MOZ_GTK_SCALE_TROUGH_VERTICAL;
  style = GetStyleContext(widget, state->image_scale, direction, state_flags);
  gtk_style_context_get_margin(style, state_flags, &margin);

  // Clamp the dimension perpendicular to the direction that the slider crosses
  // to the minimum size.
  if (flags == GTK_ORIENTATION_HORIZONTAL) {
    width = rect->width - (margin.left + margin.right);
    height = min_height - (margin.top + margin.bottom);
    x = rect->x + margin.left;
    y = rect->y + (rect->height - height) / 2;
  } else {
    width = min_width - (margin.left + margin.right);
    height = rect->height - (margin.top + margin.bottom);
    x = rect->x + (rect->width - width) / 2;
    y = rect->y + margin.top;
  }

  gtk_render_background(style, cr, x, y, width, height);
  gtk_render_frame(style, cr, x, y, width, height);

  if (state->focused)
    gtk_render_focus(style, cr, rect->x, rect->y, rect->width, rect->height);

  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_scale_thumb_paint(cairo_t* cr, GdkRectangle* rect,
                                      GtkWidgetState* state,
                                      GtkOrientation flags,
                                      GtkTextDirection direction) {
  GtkStateFlags state_flags = GetStateFlagsFromGtkWidgetState(state);
  GtkStyleContext* style;
  gint thumb_width, thumb_height, x, y;

  /* determine the thumb size, and position the thumb in the center in the
   * opposite axis
   */
  if (flags == GTK_ORIENTATION_HORIZONTAL) {
    moz_gtk_get_scalethumb_metrics(GTK_ORIENTATION_HORIZONTAL, &thumb_width,
                                   &thumb_height);
    x = rect->x;
    y = rect->y + (rect->height - thumb_height) / 2;
  } else {
    moz_gtk_get_scalethumb_metrics(GTK_ORIENTATION_VERTICAL, &thumb_height,
                                   &thumb_width);
    x = rect->x + (rect->width - thumb_width) / 2;
    y = rect->y;
  }

  WidgetNodeType widget = (flags == GTK_ORIENTATION_HORIZONTAL)
                              ? MOZ_GTK_SCALE_THUMB_HORIZONTAL
                              : MOZ_GTK_SCALE_THUMB_VERTICAL;
  style = GetStyleContext(widget, state->image_scale, direction, state_flags);
  gtk_render_slider(style, cr, x, y, thumb_width, thumb_height, flags);

  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_hpaned_paint(cairo_t* cr, GdkRectangle* rect,
                                 GtkWidgetState* state) {
  GtkStyleContext* style =
      GetStyleContext(MOZ_GTK_SPLITTER_SEPARATOR_HORIZONTAL, state->image_scale,
                      GTK_TEXT_DIR_LTR, GetStateFlagsFromGtkWidgetState(state));
  gtk_render_handle(style, cr, rect->x, rect->y, rect->width, rect->height);
  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_vpaned_paint(cairo_t* cr, GdkRectangle* rect,
                                 GtkWidgetState* state) {
  GtkStyleContext* style =
      GetStyleContext(MOZ_GTK_SPLITTER_SEPARATOR_VERTICAL, state->image_scale,
                      GTK_TEXT_DIR_LTR, GetStateFlagsFromGtkWidgetState(state));
  gtk_render_handle(style, cr, rect->x, rect->y, rect->width, rect->height);
  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_treeview_paint(cairo_t* cr, GdkRectangle* rect,
                                   GtkWidgetState* state,
                                   GtkTextDirection direction) {
  gint xthickness, ythickness;
  GtkStyleContext* style;
  GtkStyleContext* style_tree;
  GtkStateFlags state_flags;
  GtkBorder border;

  /* only handle disabled and normal states, otherwise the whole background
   * area will be painted differently with other states */
  state_flags =
      state->disabled ? GTK_STATE_FLAG_INSENSITIVE : GTK_STATE_FLAG_NORMAL;

  style =
      GetStyleContext(MOZ_GTK_SCROLLED_WINDOW, state->image_scale, direction);
  gtk_style_context_get_border(style, state_flags, &border);
  xthickness = border.left;
  ythickness = border.top;

  style_tree =
      GetStyleContext(MOZ_GTK_TREEVIEW_VIEW, state->image_scale, direction);
  gtk_render_background(style_tree, cr, rect->x + xthickness,
                        rect->y + ythickness, rect->width - 2 * xthickness,
                        rect->height - 2 * ythickness);

  style =
      GetStyleContext(MOZ_GTK_SCROLLED_WINDOW, state->image_scale, direction);
  gtk_render_frame(style, cr, rect->x, rect->y, rect->width, rect->height);
  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_resizer_paint(cairo_t* cr, GdkRectangle* rect,
                                  GtkWidgetState* state,
                                  GtkTextDirection direction) {
  GtkStyleContext* style =
      GetStyleContext(MOZ_GTK_RESIZER, state->image_scale, GTK_TEXT_DIR_LTR,
                      GetStateFlagsFromGtkWidgetState(state));

  // Workaround unico not respecting the text direction for resizers.
  // See bug 1174248.
  cairo_save(cr);
  if (direction == GTK_TEXT_DIR_RTL) {
    cairo_matrix_t mat;
    cairo_matrix_init_translate(&mat, 2 * rect->x + rect->width, 0);
    cairo_matrix_scale(&mat, -1, 1);
    cairo_transform(cr, &mat);
  }

  gtk_render_handle(style, cr, rect->x, rect->y, rect->width, rect->height);
  cairo_restore(cr);

  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_frame_paint(cairo_t* cr, GdkRectangle* rect,
                                GtkWidgetState* state,
                                GtkTextDirection direction) {
  GtkStyleContext* style =
      GetStyleContext(MOZ_GTK_FRAME, state->image_scale, direction);
  gtk_render_frame(style, cr, rect->x, rect->y, rect->width, rect->height);
  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_progressbar_paint(cairo_t* cr, GdkRectangle* rect,
                                      GtkWidgetState* state,
                                      GtkTextDirection direction) {
  GtkStyleContext* style =
      GetStyleContext(MOZ_GTK_PROGRESS_TROUGH, state->image_scale, direction);
  gtk_render_background(style, cr, rect->x, rect->y, rect->width, rect->height);
  gtk_render_frame(style, cr, rect->x, rect->y, rect->width, rect->height);

  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_progress_chunk_paint(cairo_t* cr, GdkRectangle* rect,
                                         GtkWidgetState* state,
                                         GtkTextDirection direction,
                                         WidgetNodeType widget) {
  GtkStyleContext* style =
      GetStyleContext(MOZ_GTK_PROGRESS_CHUNK, state->image_scale, direction);

  if (widget == MOZ_GTK_PROGRESS_CHUNK_INDETERMINATE ||
      widget == MOZ_GTK_PROGRESS_CHUNK_VERTICAL_INDETERMINATE) {
    /**
     * The bar's size and the bar speed are set depending of the progress'
     * size. These could also be constant for all progress bars easily.
     */
    gboolean vertical =
        (widget == MOZ_GTK_PROGRESS_CHUNK_VERTICAL_INDETERMINATE);

    /* The size of the dimension we are going to use for the animation. */
    const gint progressSize = vertical ? rect->height : rect->width;

    /* The bar is using a fifth of the element size, based on GtkProgressBar
     * activity-blocks property. */
    const gint barSize = MAX(1, progressSize / 5);

    /* Represents the travel that has to be done for a complete cycle. */
    const gint travel = 2 * (progressSize - barSize);

    /* period equals to travel / pixelsPerMillisecond
     * where pixelsPerMillisecond equals progressSize / 1000.0.
     * This is equivalent to 1600. */
    static const guint period = 1600;
    const gint t = PR_IntervalToMilliseconds(PR_IntervalNow()) % period;
    const gint dx = travel * t / period;

    if (vertical) {
      rect->y += (dx < travel / 2) ? dx : travel - dx;
      rect->height = barSize;
    } else {
      rect->x += (dx < travel / 2) ? dx : travel - dx;
      rect->width = barSize;
    }
  }

  gtk_render_background(style, cr, rect->x, rect->y, rect->width, rect->height);
  gtk_render_frame(style, cr, rect->x, rect->y, rect->width, rect->height);

  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_get_tab_thickness(GtkStyleContext* style) {
  if (!notebook_has_tab_gap)
    return 0; /* tabs do not overdraw the tabpanel border with "no gap" style */

  GtkBorder border;
  gtk_style_context_get_border(style, gtk_style_context_get_state(style),
                               &border);
  if (border.top < 2) return 2; /* some themes don't set ythickness correctly */

  return border.top;
}

gint moz_gtk_get_tab_thickness(WidgetNodeType aNodeType) {
  GtkStyleContext* style = GetStyleContext(aNodeType);
  int thickness = moz_gtk_get_tab_thickness(style);
  return thickness;
}

/* actual small tabs */
static gint moz_gtk_tab_paint(cairo_t* cr, GdkRectangle* rect,
                              GtkWidgetState* state, GtkTabFlags flags,
                              GtkTextDirection direction,
                              WidgetNodeType widget) {
  /* When the tab isn't selected, we just draw a notebook extension.
   * When it is selected, we overwrite the adjacent border of the tabpanel
   * touching the tab with a pierced border (called "the gap") to make the
   * tab appear physically attached to the tabpanel; see details below. */

  GtkStyleContext* style;
  GdkRectangle tabRect;
  GdkRectangle focusRect;
  GdkRectangle backRect;
  int initial_gap = 0;
  bool isBottomTab = (widget == MOZ_GTK_TAB_BOTTOM);

  style = GetStyleContext(widget, state->image_scale, direction,
                          GetStateFlagsFromGtkTabFlags(flags));
  tabRect = *rect;

  if (flags & MOZ_GTK_TAB_FIRST) {
    gtk_style_context_get_style(style, "initial-gap", &initial_gap, NULL);
    tabRect.width -= initial_gap;

    if (direction != GTK_TEXT_DIR_RTL) {
      tabRect.x += initial_gap;
    }
  }

  focusRect = backRect = tabRect;

  if (notebook_has_tab_gap) {
    if ((flags & MOZ_GTK_TAB_SELECTED) == 0) {
      /* Only draw the tab */
      gtk_render_extension(style, cr, tabRect.x, tabRect.y, tabRect.width,
                           tabRect.height,
                           isBottomTab ? GTK_POS_TOP : GTK_POS_BOTTOM);
    } else {
      /* Draw the tab and the gap
       * We want the gap to be positioned exactly on the tabpanel top
       * border; since tabbox.css may set a negative margin so that the tab
       * frame rect already overlaps the tabpanel frame rect, we need to take
       * that into account when drawing. To that effect, nsNativeThemeGTK
       * passes us this negative margin (bmargin in the graphic below) in the
       * lowest bits of |flags|.  We use it to set gap_voffset, the distance
       * between the top of the gap and the bottom of the tab (resp. the
       * bottom of the gap and the top of the tab when we draw a bottom tab),
       * while ensuring that the gap always touches the border of the tab,
       * i.e. 0 <= gap_voffset <= gap_height, to avoid surprinsing results
       * with big negative or positive margins.
       * Here is a graphical explanation in the case of top tabs:
       *             ___________________________
       *            /                           \
       *           |            T A B            |
       * ----------|. . . . . . . . . . . . . . .|----- top of tabpanel
       *           :    ^       bmargin          :  ^
       *           :    | (-negative margin,     :  |
       *  bottom   :    v  passed in flags)      :  |       gap_height
       *    of  -> :.............................:  |    (the size of the
       * the tab   .       part of the gap       .  |  tabpanel top border)
       *           .      outside of the tab     .  v
       * ----------------------------------------------
       *
       * To draw the gap, we use gtk_render_frame_gap(), see comment in
       * moz_gtk_tabpanels_paint(). This gap is made 3 * gap_height tall,
       * which should suffice to ensure that the only visible border is the
       * pierced one.  If the tab is in the middle, we make the box_gap begin
       * a bit to the left of the tab and end a bit to the right, adjusting
       * the gap position so it still is under the tab, because we want the
       * rendering of a gap in the middle of a tabpanel.  This is the role of
       * the gints gap_{l,r}_offset. On the contrary, if the tab is the
       * first, we align the start border of the box_gap with the start
       * border of the tab (left if LTR, right if RTL), by setting the
       * appropriate offset to 0.*/
      gint gap_loffset, gap_roffset, gap_voffset, gap_height;

      /* Get height needed by the gap */
      gap_height = moz_gtk_get_tab_thickness(style);

      /* Extract gap_voffset from the first bits of flags */
      gap_voffset = flags & MOZ_GTK_TAB_MARGIN_MASK;
      if (gap_voffset > gap_height) gap_voffset = gap_height;

      /* Set gap_{l,r}_offset to appropriate values */
      gap_loffset = gap_roffset = 20; /* should be enough */
      if (flags & MOZ_GTK_TAB_FIRST) {
        if (direction == GTK_TEXT_DIR_RTL)
          gap_roffset = initial_gap;
        else
          gap_loffset = initial_gap;
      }

      GtkStyleContext* panelStyle =
          GetStyleContext(MOZ_GTK_TABPANELS, state->image_scale, direction);

      if (isBottomTab) {
        /* Draw the tab on bottom */
        focusRect.y += gap_voffset;
        focusRect.height -= gap_voffset;

        gtk_render_extension(style, cr, tabRect.x, tabRect.y + gap_voffset,
                             tabRect.width, tabRect.height - gap_voffset,
                             GTK_POS_TOP);

        backRect.y += (gap_voffset - gap_height);
        backRect.height = gap_height;

        /* Draw the gap; erase with background color before painting in
         * case theme does not */
        gtk_render_background(panelStyle, cr, backRect.x, backRect.y,
                              backRect.width, backRect.height);
        cairo_save(cr);
        cairo_rectangle(cr, backRect.x, backRect.y, backRect.width,
                        backRect.height);
        cairo_clip(cr);

        gtk_render_frame_gap(panelStyle, cr, tabRect.x - gap_loffset,
                             tabRect.y + gap_voffset - 3 * gap_height,
                             tabRect.width + gap_loffset + gap_roffset,
                             3 * gap_height, GTK_POS_BOTTOM, gap_loffset,
                             gap_loffset + tabRect.width);
        cairo_restore(cr);
      } else {
        /* Draw the tab on top */
        focusRect.height -= gap_voffset;
        gtk_render_extension(style, cr, tabRect.x, tabRect.y, tabRect.width,
                             tabRect.height - gap_voffset, GTK_POS_BOTTOM);

        backRect.y += (tabRect.height - gap_voffset);
        backRect.height = gap_height;

        /* Draw the gap; erase with background color before painting in
         * case theme does not */
        gtk_render_background(panelStyle, cr, backRect.x, backRect.y,
                              backRect.width, backRect.height);

        cairo_save(cr);
        cairo_rectangle(cr, backRect.x, backRect.y, backRect.width,
                        backRect.height);
        cairo_clip(cr);

        gtk_render_frame_gap(panelStyle, cr, tabRect.x - gap_loffset,
                             tabRect.y + tabRect.height - gap_voffset,
                             tabRect.width + gap_loffset + gap_roffset,
                             3 * gap_height, GTK_POS_TOP, gap_loffset,
                             gap_loffset + tabRect.width);
        cairo_restore(cr);
      }
    }
  } else {
    gtk_render_background(style, cr, tabRect.x, tabRect.y, tabRect.width,
                          tabRect.height);
    gtk_render_frame(style, cr, tabRect.x, tabRect.y, tabRect.width,
                     tabRect.height);
  }

  if (state->focused) {
    /* Paint the focus ring */
    GtkBorder padding;
    gtk_style_context_get_padding(style, GetStateFlagsFromGtkWidgetState(state),
                                  &padding);

    focusRect.x += padding.left;
    focusRect.width -= (padding.left + padding.right);
    focusRect.y += padding.top;
    focusRect.height -= (padding.top + padding.bottom);

    gtk_render_focus(style, cr, focusRect.x, focusRect.y, focusRect.width,
                     focusRect.height);
  }

  return MOZ_GTK_SUCCESS;
}

/* tab area*/
static gint moz_gtk_tabpanels_paint(cairo_t* cr, GdkRectangle* rect,
                                    GtkWidgetState* state,
                                    GtkTextDirection direction) {
  GtkStyleContext* style =
      GetStyleContext(MOZ_GTK_TABPANELS, state->image_scale, direction);
  gtk_render_background(style, cr, rect->x, rect->y, rect->width, rect->height);
  /*
   * The gap size is not needed in moz_gtk_tabpanels_paint because
   * the gap will be painted with the foreground tab in moz_gtk_tab_paint.
   *
   * However, if moz_gtk_tabpanels_paint just uses gtk_render_frame(),
   * the theme will think that there are no tabs and may draw something
   * different.Hence the trick of using two clip regions, and drawing the
   * gap outside each clip region, to get the correct frame for
   * a tabpanel with tabs.
   */
  /* left side */
  cairo_save(cr);
  cairo_rectangle(cr, rect->x, rect->y, rect->x + rect->width / 2,
                  rect->y + rect->height);
  cairo_clip(cr);
  gtk_render_frame_gap(style, cr, rect->x, rect->y, rect->width, rect->height,
                       GTK_POS_TOP, rect->width - 1, rect->width);
  cairo_restore(cr);

  /* right side */
  cairo_save(cr);
  cairo_rectangle(cr, rect->x + rect->width / 2, rect->y, rect->x + rect->width,
                  rect->y + rect->height);
  cairo_clip(cr);
  gtk_render_frame_gap(style, cr, rect->x, rect->y, rect->width, rect->height,
                       GTK_POS_TOP, 0, 1);
  cairo_restore(cr);

  return MOZ_GTK_SUCCESS;
}

static gint moz_gtk_header_bar_paint(WidgetNodeType widgetType, cairo_t* cr,
                                     GdkRectangle* rect,
                                     GtkWidgetState* state) {
  GtkStateFlags state_flags = GetStateFlagsFromGtkWidgetState(state);
  GtkStyleContext* style = GetStyleContext(widgetType, state->image_scale,
                                           GTK_TEXT_DIR_NONE, state_flags);

  // Some themes like Elementary's style the container of the headerbar rather
  // than the header bar itself.
  if (HeaderBarShouldDrawContainer(widgetType)) {
    auto containerType = widgetType == MOZ_GTK_HEADER_BAR
                             ? MOZ_GTK_HEADERBAR_FIXED
                             : MOZ_GTK_HEADERBAR_FIXED_MAXIMIZED;
    style = GetStyleContext(containerType, state->image_scale,
                            GTK_TEXT_DIR_NONE, state_flags);
  }

  gtk_render_background(style, cr, rect->x, rect->y, rect->width, rect->height);
  gtk_render_frame(style, cr, rect->x, rect->y, rect->width, rect->height);

  return MOZ_GTK_SUCCESS;
}

gint moz_gtk_get_widget_border(WidgetNodeType widget, gint* left, gint* top,
                               gint* right, gint* bottom,
                               // NOTE: callers depend on direction being used
                               // only for MOZ_GTK_DROPDOWN widgets.
                               GtkTextDirection direction) {
  GtkWidget* w;
  GtkStyleContext* style;
  *left = *top = *right = *bottom = 0;

  switch (widget) {
    case MOZ_GTK_TREEVIEW: {
      style = GetStyleContext(MOZ_GTK_SCROLLED_WINDOW);
      moz_gtk_add_style_border(style, left, top, right, bottom);
      return MOZ_GTK_SUCCESS;
    }
    case MOZ_GTK_TABPANELS:
      w = GetWidget(MOZ_GTK_TABPANELS);
      break;
    case MOZ_GTK_PROGRESSBAR:
      w = GetWidget(MOZ_GTK_PROGRESSBAR);
      break;
    case MOZ_GTK_SCALE_HORIZONTAL:
    case MOZ_GTK_SCALE_VERTICAL:
      w = GetWidget(widget);
      break;
    case MOZ_GTK_FRAME:
      w = GetWidget(MOZ_GTK_FRAME);
      break;
    /* These widgets have no borders, since they are not containers. */
    case MOZ_GTK_SPLITTER_HORIZONTAL:
    case MOZ_GTK_SPLITTER_VERTICAL:
    case MOZ_GTK_SCALE_THUMB_HORIZONTAL:
    case MOZ_GTK_SCALE_THUMB_VERTICAL:
    case MOZ_GTK_PROGRESS_CHUNK:
    case MOZ_GTK_PROGRESS_CHUNK_INDETERMINATE:
    case MOZ_GTK_PROGRESS_CHUNK_VERTICAL_INDETERMINATE:
    case MOZ_GTK_HEADER_BAR:
    case MOZ_GTK_HEADER_BAR_MAXIMIZED:
    case MOZ_GTK_HEADER_BAR_BUTTON_CLOSE:
    case MOZ_GTK_HEADER_BAR_BUTTON_MINIMIZE:
    case MOZ_GTK_HEADER_BAR_BUTTON_MAXIMIZE:
    case MOZ_GTK_HEADER_BAR_BUTTON_MAXIMIZE_RESTORE:
    /* These widgets have no borders.*/
    case MOZ_GTK_WINDOW_DECORATION:
    case MOZ_GTK_WINDOW_DECORATION_SOLID:
    case MOZ_GTK_RESIZER:
    case MOZ_GTK_TOOLBARBUTTON_ARROW:
      return MOZ_GTK_SUCCESS;
    default:
      g_warning("Unsupported widget type: %d", widget);
      return MOZ_GTK_UNKNOWN_WIDGET;
  }
  /* TODO - we're still missing some widget implementations */
  if (w) {
    moz_gtk_add_style_border(gtk_widget_get_style_context(w), left, top, right,
                             bottom);
  }
  return MOZ_GTK_SUCCESS;
}

gint moz_gtk_get_tab_border(gint* left, gint* top, gint* right, gint* bottom,
                            GtkTextDirection direction, GtkTabFlags flags,
                            WidgetNodeType widget) {
  GtkStyleContext* style = GetStyleContext(widget, 1, direction,
                                           GetStateFlagsFromGtkTabFlags(flags));

  *left = *top = *right = *bottom = 0;
  moz_gtk_add_style_padding(style, left, top, right, bottom);

  // Gtk >= 3.20 does not use those styles
  if (gtk_check_version(3, 20, 0) != nullptr) {
    int tab_curvature;

    gtk_style_context_get_style(style, "tab-curvature", &tab_curvature, NULL);
    *left += tab_curvature;
    *right += tab_curvature;

    if (flags & MOZ_GTK_TAB_FIRST) {
      int initial_gap = 0;
      gtk_style_context_get_style(style, "initial-gap", &initial_gap, NULL);
      if (direction == GTK_TEXT_DIR_RTL)
        *right += initial_gap;
      else
        *left += initial_gap;
    }
  } else {
    GtkBorder margin;

    gtk_style_context_get_margin(style, gtk_style_context_get_state(style),
                                 &margin);
    *left += margin.left;
    *right += margin.right;

    if (flags & MOZ_GTK_TAB_FIRST) {
      style = GetStyleContext(MOZ_GTK_NOTEBOOK_HEADER, direction);
      gtk_style_context_get_margin(style, gtk_style_context_get_state(style),
                                   &margin);
      *left += margin.left;
      *right += margin.right;
    }
  }

  return MOZ_GTK_SUCCESS;
}

gint moz_gtk_get_tab_scroll_arrow_size(gint* width, gint* height) {
  gint arrow_size;

  GtkStyleContext* style = GetStyleContext(MOZ_GTK_TABPANELS);
  gtk_style_context_get_style(style, "scroll-arrow-hlength", &arrow_size, NULL);

  *height = *width = arrow_size;

  return MOZ_GTK_SUCCESS;
}

void moz_gtk_get_scale_metrics(GtkOrientation orient, gint* scale_width,
                               gint* scale_height) {
  if (gtk_check_version(3, 20, 0) != nullptr) {
    WidgetNodeType widget = (orient == GTK_ORIENTATION_HORIZONTAL)
                                ? MOZ_GTK_SCALE_HORIZONTAL
                                : MOZ_GTK_SCALE_VERTICAL;

    gint thumb_length, thumb_height, trough_border;
    moz_gtk_get_scalethumb_metrics(orient, &thumb_length, &thumb_height);

    GtkStyleContext* style = GetStyleContext(widget);
    gtk_style_context_get_style(style, "trough-border", &trough_border, NULL);

    if (orient == GTK_ORIENTATION_HORIZONTAL) {
      *scale_width = thumb_length + trough_border * 2;
      *scale_height = thumb_height + trough_border * 2;
    } else {
      *scale_width = thumb_height + trough_border * 2;
      *scale_height = thumb_length + trough_border * 2;
    }
  } else {
    WidgetNodeType widget = (orient == GTK_ORIENTATION_HORIZONTAL)
                                ? MOZ_GTK_SCALE_TROUGH_HORIZONTAL
                                : MOZ_GTK_SCALE_TROUGH_VERTICAL;
    moz_gtk_get_widget_min_size(GetStyleContext(widget), scale_width,
                                scale_height);
  }
}

gint moz_gtk_get_scalethumb_metrics(GtkOrientation orient, gint* thumb_length,
                                    gint* thumb_height) {
  if (gtk_check_version(3, 20, 0) != nullptr) {
    WidgetNodeType widget = (orient == GTK_ORIENTATION_HORIZONTAL)
                                ? MOZ_GTK_SCALE_HORIZONTAL
                                : MOZ_GTK_SCALE_VERTICAL;
    GtkStyleContext* style = GetStyleContext(widget);
    gtk_style_context_get_style(style, "slider_length", thumb_length,
                                "slider_width", thumb_height, NULL);
  } else {
    WidgetNodeType widget = (orient == GTK_ORIENTATION_HORIZONTAL)
                                ? MOZ_GTK_SCALE_THUMB_HORIZONTAL
                                : MOZ_GTK_SCALE_THUMB_VERTICAL;
    GtkStyleContext* style = GetStyleContext(widget);

    gint min_width, min_height;
    GtkStateFlags state = gtk_style_context_get_state(style);
    gtk_style_context_get(style, state, "min-width", &min_width, "min-height",
                          &min_height, nullptr);
    GtkBorder margin;
    gtk_style_context_get_margin(style, state, &margin);
    gint margin_width = margin.left + margin.right;
    gint margin_height = margin.top + margin.bottom;

    // Negative margin of slider element also determines its minimal size
    // so use bigger of those two values.
    if (min_width < -margin_width) min_width = -margin_width;
    if (min_height < -margin_height) min_height = -margin_height;

    *thumb_length = min_width;
    *thumb_height = min_height;
  }

  return MOZ_GTK_SUCCESS;
}

/* cairo_t *cr argument has to be a system-cairo. */
gint moz_gtk_widget_paint(WidgetNodeType widget, cairo_t* cr,
                          GdkRectangle* rect, GtkWidgetState* state, gint flags,
                          GtkTextDirection direction) {
  /* A workaround for https://bugzilla.gnome.org/show_bug.cgi?id=694086
   */
  cairo_new_path(cr);

  switch (widget) {
    case MOZ_GTK_HEADER_BAR_BUTTON_CLOSE:
    case MOZ_GTK_HEADER_BAR_BUTTON_MINIMIZE:
    case MOZ_GTK_HEADER_BAR_BUTTON_MAXIMIZE:
    case MOZ_GTK_HEADER_BAR_BUTTON_MAXIMIZE_RESTORE:
      return moz_gtk_header_bar_button_paint(
          cr, rect, state, (GtkReliefStyle)flags, widget, direction);
    case MOZ_GTK_SCALE_HORIZONTAL:
    case MOZ_GTK_SCALE_VERTICAL:
      return moz_gtk_scale_paint(cr, rect, state, (GtkOrientation)flags,
                                 direction);
    case MOZ_GTK_SCALE_THUMB_HORIZONTAL:
    case MOZ_GTK_SCALE_THUMB_VERTICAL:
      return moz_gtk_scale_thumb_paint(cr, rect, state, (GtkOrientation)flags,
                                       direction);
    case MOZ_GTK_TREEVIEW:
      return moz_gtk_treeview_paint(cr, rect, state, direction);
    case MOZ_GTK_FRAME:
      return moz_gtk_frame_paint(cr, rect, state, direction);
    case MOZ_GTK_RESIZER:
      return moz_gtk_resizer_paint(cr, rect, state, direction);
    case MOZ_GTK_PROGRESSBAR:
      return moz_gtk_progressbar_paint(cr, rect, state, direction);
    case MOZ_GTK_PROGRESS_CHUNK:
    case MOZ_GTK_PROGRESS_CHUNK_INDETERMINATE:
    case MOZ_GTK_PROGRESS_CHUNK_VERTICAL_INDETERMINATE:
      return moz_gtk_progress_chunk_paint(cr, rect, state, direction, widget);
    case MOZ_GTK_TAB_TOP:
    case MOZ_GTK_TAB_BOTTOM:
      return moz_gtk_tab_paint(cr, rect, state, (GtkTabFlags)flags, direction,
                               widget);
    case MOZ_GTK_TABPANELS:
      return moz_gtk_tabpanels_paint(cr, rect, state, direction);
    case MOZ_GTK_SPLITTER_HORIZONTAL:
      return moz_gtk_vpaned_paint(cr, rect, state);
    case MOZ_GTK_SPLITTER_VERTICAL:
      return moz_gtk_hpaned_paint(cr, rect, state);
    case MOZ_GTK_WINDOW_DECORATION:
      return moz_gtk_window_decoration_paint(cr, rect, state, direction);
    case MOZ_GTK_HEADER_BAR:
    case MOZ_GTK_HEADER_BAR_MAXIMIZED:
      return moz_gtk_header_bar_paint(widget, cr, rect, state);
    default:
      g_warning("Unknown widget type: %d", widget);
  }

  return MOZ_GTK_UNKNOWN_WIDGET;
}

gint moz_gtk_shutdown() {
  /* This will destroy all of our widgets */
  ResetWidgetCache();

  return MOZ_GTK_SUCCESS;
}
