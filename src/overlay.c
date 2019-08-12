/*
 * Copyright (C) 2019 cyclopsian

 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:

 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>

#include "wdisplays.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define TEXT_SIZE 128
#define SCREEN_MARGIN_PERCENT 0.02

static void layer_surface_configure(void *data,
    struct zwlr_layer_surface_v1 *surface,
    uint32_t serial, uint32_t width, uint32_t height) {
  struct wd_output *output = data;
  gtk_widget_set_size_request(output->overlay_window, width, height);
  zwlr_layer_surface_v1_ack_configure(surface, serial);
}

static void layer_surface_closed(void *data,
    struct zwlr_layer_surface_v1 *surface) {
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
  .configure = layer_surface_configure,
  .closed = layer_surface_closed,
};

static inline int min(int a, int b) {
  return a < b ? a : b;
}

static char *make_overlay_string(struct wd_head *head) {
  return g_strdup_printf("%d", head->id + 1);
}

static PangoLayout *create_text_layout(struct wd_head *head,
    PangoContext *pango) {
  g_autofree gchar *str = make_overlay_string(head);
  PangoLayout *layout = pango_layout_new(pango);

  PangoAttrList *attrs = pango_attr_list_new();
  pango_attr_list_insert(attrs, pango_attr_size_new(TEXT_SIZE * PANGO_SCALE));
  pango_layout_set_attributes(layout, attrs);
  pango_attr_list_unref(attrs);
  pango_layout_set_text(layout, str, -1);
  pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
  return layout;
}

static void resize(struct wd_output *output) {
  struct wd_head *head = wd_find_head(output->state, output);

  uint32_t screen_width = head->custom_mode.width;
  uint32_t screen_height = head->custom_mode.height;
  if (head->mode != NULL) {
    screen_width = head->mode->width;
    screen_height = head->mode->height;
  }
  uint32_t margin =  min(screen_width, screen_height) * SCREEN_MARGIN_PERCENT;

  GdkWindow *window = gtk_widget_get_window(output->overlay_window);
  PangoContext *pango = gtk_widget_get_pango_context(output->overlay_window);
  PangoLayout *layout = create_text_layout(head, pango);

  int width;
  int height;
  pango_layout_get_pixel_size(layout, &width, &height);
  g_object_unref(layout);
  width = min(width, screen_width - margin * 2);
  height = min(height, screen_height - margin * 2);

  zwlr_layer_surface_v1_set_margin(output->overlay_layer_surface,
      margin, margin, margin, margin);
  zwlr_layer_surface_v1_set_size(output->overlay_layer_surface,
      width, height);

  struct wl_surface *surface = gdk_wayland_window_get_wl_surface(window);
  wl_surface_commit(surface);

  GdkDisplay *display = gdk_window_get_display(window);
  wl_display_roundtrip(gdk_wayland_display_get_wl_display(display));
}

void wd_redraw_overlay(struct wd_output *output) {
  if (output->overlay_window != NULL) {
    resize(output);
    gtk_widget_queue_draw(output->overlay_window);
  }
}

void window_realize(GtkWidget *widget, gpointer data) {
  GdkWindow *window = gtk_widget_get_window(widget);
  gdk_wayland_window_set_use_custom_surface(window);
}

void window_map(GtkWidget *widget, gpointer data) {
  struct wd_output *output = data;

  GdkWindow *window = gtk_widget_get_window(widget);
  cairo_region_t *region = cairo_region_create();
  gdk_window_input_shape_combine_region(window, region, 0, 0);
  cairo_region_destroy(region);

  struct wl_surface *surface = gdk_wayland_window_get_wl_surface(window);

  output->overlay_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      output->state->layer_shell, surface, output->wl_output,
      ZWLR_LAYER_SHELL_V1_LAYER_TOP, "output-overlay");

  zwlr_layer_surface_v1_add_listener(output->overlay_layer_surface,
      &layer_surface_listener, output);

  zwlr_layer_surface_v1_set_anchor(output->overlay_layer_surface,
      ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);

  resize(output);
}

void window_unmap(GtkWidget *widget, gpointer data) {
  struct wd_output *output = data;
  zwlr_layer_surface_v1_destroy(output->overlay_layer_surface);
}

gboolean window_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  struct wd_output *output = data;
  struct wd_head *head = wd_find_head(output->state, output);

  GtkStyleContext *style_ctx = gtk_widget_get_style_context(widget);
  GdkRGBA fg;
  gtk_style_context_get_color(style_ctx, GTK_STATE_FLAG_NORMAL, &fg);

  int width = gtk_widget_get_allocated_width(widget);
  int height = gtk_widget_get_allocated_height(widget);
  gtk_render_background(style_ctx, cr, 0, 0, width, height);

  PangoContext *pango = gtk_widget_get_pango_context(widget);
  PangoLayout *layout = create_text_layout(head, pango);

  gdk_cairo_set_source_rgba(cr, &fg);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
  return TRUE;
}

void wd_create_overlay(struct wd_output *output) {
  output->overlay_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_decorated(GTK_WINDOW(output->overlay_window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(output->overlay_window), FALSE);
  gtk_widget_add_events(output->overlay_window, GDK_STRUCTURE_MASK);

  g_signal_connect(output->overlay_window, "realize",
      G_CALLBACK(window_realize), output);
  g_signal_connect(output->overlay_window, "map",
      G_CALLBACK(window_map), output);
  g_signal_connect(output->overlay_window, "unmap",
      G_CALLBACK(window_unmap), output);
  g_signal_connect(output->overlay_window, "draw",
      G_CALLBACK(window_draw), output);

  GtkStyleContext *style_ctx = gtk_widget_get_style_context(
      output->overlay_window);
  gtk_style_context_add_class(style_ctx, "output-overlay");
  gtk_widget_show(output->overlay_window);
}

void wd_destroy_overlay(struct wd_output *output) {
  if (output->overlay_window != NULL) {
    gtk_widget_destroy(output->overlay_window);
    output->overlay_window = NULL;
  }
}
