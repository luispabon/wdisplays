/*
 * Copyright (C) 2019 cyclopsian
 * Copyright (C) 2017-2019 emersion

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

/*
 * Parts of this file are taken from emersion/kanshi:
 * https://github.com/emersion/kanshi/blob/38d27474b686fcc8324cc5e454741a49577c0988/include/kanshi.h
 * https://github.com/emersion/kanshi/blob/38d27474b686fcc8324cc5e454741a49577c0988/include/config.h
 */

#ifndef WDISPLAY_WDISPLAY_H
#define WDISPLAY_WDISPLAY_H

#include <stdbool.h>
#include <wayland-client.h>

struct zwlr_output_mode_v1;
struct zwlr_output_head_v1;
struct zwlr_output_manager_v1;
struct _GtkWidget;
typedef struct _GtkWidget GtkWidget;
struct _GtkBuilder;
typedef struct _GtkBuilder GtkBuilder;

enum wd_head_fields {
  WD_FIELD_NAME           = 1 << 0,
  WD_FIELD_ENABLED        = 1 << 1,
  WD_FIELD_DESCRIPTION    = 1 << 2,
  WD_FIELD_PHYSICAL_SIZE  = 1 << 3,
  WD_FIELD_SCALE          = 1 << 4,
  WD_FIELD_POSITION       = 1 << 5,
  WD_FIELD_MODE           = 1 << 6,
  WD_FIELD_TRANSFORM      = 1 << 7,
  WD_FIELDS_ALL           = (1 << 8) - 1
};

struct wd_head_config {
  struct wl_list link;

  struct wd_head *head;
  bool enabled;
  int32_t width;
  int32_t height;
  int32_t refresh; // mHz
  int32_t x;
  int32_t y;
  double scale;
  enum wl_output_transform transform;
};

struct wd_mode {
  struct wd_head *head;
  struct zwlr_output_mode_v1 *wlr_mode;
  struct wl_list link;

  int32_t width, height;
  int32_t refresh; // mHz
  bool preferred;
};

struct wd_head {
  struct wd_state *state;
  struct zwlr_output_head_v1 *wlr_head;
  struct wl_list link;

  char *name, *description;
  int32_t phys_width, phys_height; // mm
  struct wl_list modes;

  bool enabled;
  struct wd_mode *mode;
  struct {
    int32_t width, height;
    int32_t refresh;
  } custom_mode;
  int32_t x, y;
  enum wl_output_transform transform;
  double scale;
};

struct wd_state {
  struct zwlr_output_manager_v1 *output_manager;
  struct wl_list heads;
  uint32_t serial;

  double zoom;
  int xorigin;
  int yorigin;

  GtkWidget *header_stack;
  GtkWidget *stack_switcher;
  GtkWidget *stack;
  GtkWidget *scroller;
  GtkWidget *canvas;
  GtkWidget *spinner;
  GtkWidget *zoom_out;
  GtkWidget *zoom_reset;
  GtkWidget *zoom_in;
  GtkWidget *overlay;
  GtkWidget *info_bar;
  GtkWidget *info_label;
};

/*
 * Displays an error message and then exits the program.
 */
void wd_fatal_error(int status, const char *message);

/*
 * Starts listening for output management events from the compositor.
 */
void wd_add_output_management_listener(struct wd_state *state, struct wl_display *display);

/*
 * Sends updated display configuration back to the compositor.
 */
void wd_apply_state(struct wd_state *state, struct wl_list *new_outputs);

/*
 * Updates the UI stack of all heads. Does not update individual head forms.
 * Useful for when a display is plugged/unplugged and we want to add/remove
 * a page, but we don't want to wipe out user's changes on the other pages.
 */
void wd_ui_reset_heads(struct wd_state *state);

/*
 * Updates a form with head configuration from the server. Only updates specified fields.
 */
void wd_ui_reset_head(const struct wd_head *head, unsigned int fields);

/*
 * Updates the stack and all forms to the last known server state.
 */
void wd_ui_reset_all(struct wd_state *state);

/*
 * Reactivates the GUI after the display configuration updates.
 */
void wd_ui_apply_done(struct wd_state *state, struct wl_list *outputs);

/*
 * Reactivates the GUI after the display configuration updates.
 */
void wd_ui_show_error(struct wd_state *state, const char *message);

#endif
