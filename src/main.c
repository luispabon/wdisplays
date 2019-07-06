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

#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>

#include "wdisplay.h"

__attribute__((noreturn)) void wd_fatal_error(int status, const char *message) {
  GtkWindow *parent = gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default()));
  GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  exit(status);
}

#define DEFAULT_ZOOM 0.1
#define MIN_ZOOM (1./1000.)
#define MAX_ZOOM 1000.
#define CANVAS_MARGIN 100

static const char *MODE_PREFIX = "mode";
static const char *TRANSFORM_PREFIX = "transform";
static const char *APP_PREFIX = "app";

#define NUM_ROTATIONS 4
static const char *ROTATE_IDS[NUM_ROTATIONS] = {
  "rotate_0", "rotate_90", "rotate_180", "rotate_270"
};

static int get_rotate_index(enum wl_output_transform transform) {
  if (transform == WL_OUTPUT_TRANSFORM_90 || transform == WL_OUTPUT_TRANSFORM_FLIPPED_90) {
    return 1;
  } else if (transform == WL_OUTPUT_TRANSFORM_180 || transform == WL_OUTPUT_TRANSFORM_FLIPPED_180) {
    return 2;
  } else if (transform == WL_OUTPUT_TRANSFORM_270 || transform == WL_OUTPUT_TRANSFORM_FLIPPED_270) {
    return 3;
  }
  return 0;
}

static bool has_changes(const struct wd_state *state) {
  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    GtkBuilder *builder = GTK_BUILDER(g_object_get_data(G_OBJECT(form_iter->data), "builder"));
    const struct wd_head *head = g_object_get_data(G_OBJECT(form_iter->data), "head");
    if (head->enabled != gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "enabled")))) {
      return TRUE;
    }
    double old_scale = round(head->scale * 100.) / 100.;
    double new_scale = round(gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "scale"))) * 100.) / 100.;
    if (old_scale != new_scale) {
      return TRUE;
    }
    if (head->x != gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "pos_x")))) {
      return TRUE;
    }
    if (head->y != gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "pos_y")))) {
      return TRUE;
    }
    int w = head->mode != NULL ? head->mode->width : head->custom_mode.width;
    if (w != gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "width")))) {
      return TRUE;
    }
    int h = head->mode != NULL ? head->mode->height : head->custom_mode.height;
    if (h != gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "height")))) {
      return TRUE;
    }
    int r = head->mode != NULL ? head->mode->refresh : head->custom_mode.refresh;
    if (r / 1000. != gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "refresh")))) {
      return TRUE;
    }
    for (int i = 0; i < NUM_ROTATIONS; i++) {
      GtkWidget *rotate = GTK_WIDGET(gtk_builder_get_object(builder, ROTATE_IDS[i]));
      gboolean selected;
      g_object_get(rotate, "active", &selected, NULL);
      if (selected) {
        if (i != get_rotate_index(head->transform)) {
          return TRUE;
        }
        break;
      }
    }
    bool flipped = head->transform == WL_OUTPUT_TRANSFORM_FLIPPED
      || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_90
      || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_180
      || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
    if (flipped != gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "flipped")))) {
      return TRUE;
    }
  }
  return FALSE;
}

void fill_output_from_form(struct wd_head_config *output, GtkWidget *form) {
  GtkBuilder *builder = GTK_BUILDER(g_object_get_data(G_OBJECT(form), "builder"));
  output->head = g_object_get_data(G_OBJECT(form), "head");
  output->enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "enabled")));
  output->scale = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "scale")));
  output->x = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "pos_x")));
  output->y = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "pos_y")));
  output->width = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "width")));
  output->height = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "height")));
  output->refresh = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "refresh"))) * 1000.;
  gboolean flipped = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "flipped")));
  for (int i = 0; i < NUM_ROTATIONS; i++) {
    GtkWidget *rotate = GTK_WIDGET(gtk_builder_get_object(builder, ROTATE_IDS[i]));
    gboolean selected;
    g_object_get(rotate, "active", &selected, NULL);
    if (selected) {
      switch (i) {
        case 0: output->transform = flipped ? WL_OUTPUT_TRANSFORM_FLIPPED : WL_OUTPUT_TRANSFORM_NORMAL; break;
        case 1: output->transform = flipped ? WL_OUTPUT_TRANSFORM_FLIPPED_90 : WL_OUTPUT_TRANSFORM_90; break;
        case 2: output->transform = flipped ? WL_OUTPUT_TRANSFORM_FLIPPED_180 : WL_OUTPUT_TRANSFORM_180; break;
        case 3: output->transform = flipped ? WL_OUTPUT_TRANSFORM_FLIPPED_270 : WL_OUTPUT_TRANSFORM_270; break;
      }
      break;
    }
  }
}

static gboolean send_apply(gpointer data) {
  struct wd_state *state = data;
  struct wl_list *outputs = calloc(1, sizeof(*outputs));
  wl_list_init(outputs);
  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    struct wd_head_config *output = calloc(1, sizeof(*output));
    wl_list_insert(outputs, &output->link);
    fill_output_from_form(output, GTK_WIDGET(form_iter->data));
  }
  wd_apply_state(state, outputs);
  state->apply_pending = false;
  return FALSE;
}

static void apply_state(struct wd_state *state) {
  gtk_stack_set_visible_child_name(GTK_STACK(state->header_stack), "title");
  if (!state->autoapply) {
    gtk_style_context_add_class(gtk_widget_get_style_context(state->spinner), "visible");
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(state->overlay), state->spinner, FALSE);

    gtk_widget_set_sensitive(state->stack_switcher, FALSE);
    gtk_widget_set_sensitive(state->stack, FALSE);
    gtk_widget_set_sensitive(state->zoom_in, FALSE);
    gtk_widget_set_sensitive(state->zoom_reset, FALSE);
    gtk_widget_set_sensitive(state->zoom_out, FALSE);
    gtk_widget_set_sensitive(state->menu_button, FALSE);
  }

  /* queue this once per iteration in order to prevent duplicate updates */
  if (!state->apply_pending) {
    state->apply_pending = true;
    g_idle_add(send_apply, state);
  }
}

static gboolean apply_done_reset(gpointer data) {
  wd_ui_reset_all(data);
  return FALSE;
}

// BEGIN FORM CALLBACKS
static void show_apply(struct wd_state *state) {
  const gchar *page = "title";
  if (has_changes(state)) {
    if (state->autoapply) {
      apply_state(state);
    } else {
      page = "apply";
    }
  }
  gtk_stack_set_visible_child_name(GTK_STACK(state->header_stack), page);
  gtk_widget_queue_draw(state->canvas);
}

static void update_sensitivity(GtkWidget *form) {
  GtkBuilder *builder = GTK_BUILDER(g_object_get_data(G_OBJECT(form), "builder"));
  GtkWidget *enabled = GTK_WIDGET(gtk_builder_get_object(builder, "enabled"));
  bool enabled_toggled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled));

  g_autoptr(GList) children = gtk_container_get_children(GTK_CONTAINER(form));
  for (GList *child = children; child != NULL; child = child->next) {
    GtkWidget *widget = GTK_WIDGET(child->data);
    if (widget != enabled) {
      gtk_widget_set_sensitive(widget, enabled_toggled);
    }
  }
}

static void select_rotate_option(GtkWidget *form, GtkWidget *model_button) {
  GtkBuilder *builder = GTK_BUILDER(g_object_get_data(G_OBJECT(form), "builder"));
  GtkWidget *rotate_button = GTK_WIDGET(gtk_builder_get_object(builder, "rotate_button"));
  for (int i = 0; i < NUM_ROTATIONS; i++) {
    GtkWidget *rotate = GTK_WIDGET(gtk_builder_get_object(builder, ROTATE_IDS[i]));
    gboolean selected = model_button == rotate;
    g_object_set(rotate, "active", selected, NULL);
    if (selected) {
      g_autofree gchar *rotate_text = NULL;
      g_object_get(rotate, "text", &rotate_text, NULL);
      gtk_button_set_label(GTK_BUTTON(rotate_button), rotate_text);
    }
  }
}

static void rotate_selected(GSimpleAction *action, GVariant *param, gpointer data) {
  select_rotate_option(GTK_WIDGET(data), g_object_get_data(G_OBJECT(action), "widget"));
  const struct wd_head *head = g_object_get_data(G_OBJECT(data), "head");
  show_apply(head->state);
}

static void select_mode_option(GtkWidget *form, int32_t w, int32_t h, int32_t r) {
  GtkBuilder *builder = GTK_BUILDER(g_object_get_data(G_OBJECT(form), "builder"));
  GtkWidget *mode_box = GTK_WIDGET(gtk_builder_get_object(builder, "mode_box"));
  g_autoptr(GList) children = gtk_container_get_children(GTK_CONTAINER(mode_box));
  for (GList *child = children; child != NULL; child = child->next) {
    const struct wd_mode *mode = g_object_get_data(G_OBJECT(child->data), "mode");
    g_object_set(child->data, "active", w == mode->width && h == mode->height && r == mode->refresh, NULL);
  }
}

static void update_mode_entries(GtkWidget *form, int32_t w, int32_t h, int32_t r) {
  GtkBuilder *builder = GTK_BUILDER(g_object_get_data(G_OBJECT(form), "builder"));
  GtkWidget *width = GTK_WIDGET(gtk_builder_get_object(builder, "width"));
  GtkWidget *height = GTK_WIDGET(gtk_builder_get_object(builder, "height"));
  GtkWidget *refresh = GTK_WIDGET(gtk_builder_get_object(builder, "refresh"));

  gtk_spin_button_set_value(GTK_SPIN_BUTTON(width), w);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(height), h);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(refresh), r / 1000.);
}

static void mode_selected(GSimpleAction *action, GVariant *param, gpointer data) {
  GtkWidget *form = data;
  const struct wd_head *head = g_object_get_data(G_OBJECT(form), "head");
  const struct wd_mode *mode = g_object_get_data(G_OBJECT(action), "mode");

  update_mode_entries(form, mode->width, mode->height, mode->refresh);
  select_mode_option(form, mode->width, mode->height, mode->refresh);
  show_apply(head->state);
}
// END FORM CALLBACKS

/*
 * Recalculates the desired canvas size, accounting for zoom + margins.
 */
static void update_canvas_size(struct wd_state *state) {
  int xmin = 0;
  int xmax = 0;
  int ymin = 0;
  int ymax = 0;

  struct wd_head *head;
  wl_list_for_each(head, &state->heads, link) {
    int w = head->custom_mode.width;
    int h = head->custom_mode.height;
    if (head->enabled && head->mode != NULL) {
      w = head->mode->width;
      h = head->mode->height;
    }
    if (head->scale > 0.) {
      w /= head->scale;
      h /= head->scale;
    }

    int x2 = head->x + w;
    int y2 = head->y + h;
    xmin = MIN(xmin, head->x);
    xmax = MAX(xmax, x2);
    ymin = MIN(ymin, head->y);
    ymax = MAX(ymax, y2);
  }
  // update canvas sizings
  state->xorigin = floor(xmin * state->zoom) - CANVAS_MARGIN;
  state->yorigin = floor(ymin * state->zoom) - CANVAS_MARGIN;
  int heads_width = ceil((xmax - xmin) * state->zoom) + CANVAS_MARGIN * 2;
  int heads_height = ceil((ymax - ymin) * state->zoom) + CANVAS_MARGIN * 2;
  gtk_layout_set_size(GTK_LAYOUT(state->canvas), heads_width, heads_height);
}

static void clear_menu(GtkWidget *box, GActionMap *action_map) {
  g_autoptr(GList) children = gtk_container_get_children(GTK_CONTAINER(box));
  for (GList *child = children; child != NULL; child = child->next) {
    g_action_map_remove_action(action_map, strchr(gtk_actionable_get_action_name(GTK_ACTIONABLE(child->data)), '.') + 1);
    gtk_container_remove(GTK_CONTAINER(box), GTK_WIDGET(child->data));
  }
}
static void update_head_form(GtkWidget *form, unsigned int fields) {
  GtkBuilder *builder = GTK_BUILDER(g_object_get_data(G_OBJECT(form), "builder"));
  GtkWidget *description = GTK_WIDGET(gtk_builder_get_object(builder, "description"));
  GtkWidget *physical_size = GTK_WIDGET(gtk_builder_get_object(builder, "physical_size"));
  GtkWidget *enabled = GTK_WIDGET(gtk_builder_get_object(builder, "enabled"));
  GtkWidget *scale = GTK_WIDGET(gtk_builder_get_object(builder, "scale"));
  GtkWidget *pos_x = GTK_WIDGET(gtk_builder_get_object(builder, "pos_x"));
  GtkWidget *pos_y = GTK_WIDGET(gtk_builder_get_object(builder, "pos_y"));
  GtkWidget *mode_box = GTK_WIDGET(gtk_builder_get_object(builder, "mode_box"));
  GtkWidget *flipped = GTK_WIDGET(gtk_builder_get_object(builder, "flipped"));
  const struct wd_head *head = g_object_get_data(G_OBJECT(form), "head");

  if (fields & WD_FIELD_NAME) {
    gtk_container_child_set(GTK_CONTAINER(head->state->stack), form, "name", head->name, "title", head->name, NULL);
  }
  if (fields & WD_FIELD_DESCRIPTION) {
    gtk_label_set_text(GTK_LABEL(description), head->description);
  }
  if (fields & WD_FIELD_PHYSICAL_SIZE) {
    g_autofree gchar *physical_str = g_strdup_printf("%dmm × %dmm", head->phys_width, head->phys_height);
    gtk_label_set_text(GTK_LABEL(physical_size), physical_str);
  }
  if (fields & WD_FIELD_ENABLED) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), head->enabled);
  }
  if (fields & WD_FIELD_SCALE) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(scale), head->scale);
  }
  if (fields & WD_FIELD_POSITION) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pos_x), head->x);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pos_y), head->y);
  }

  if (fields & WD_FIELD_MODE) {
    GActionMap *mode_actions = G_ACTION_MAP(g_object_get_data(G_OBJECT(form), "mode-group"));
    clear_menu(mode_box, mode_actions);
    struct wd_mode *mode;
    wl_list_for_each(mode, &head->modes, link) {
      g_autofree gchar *name = g_strdup_printf("%d×%d@%0.3fHz", mode->width, mode->height, mode->refresh / 1000.);
      GSimpleAction *action = g_simple_action_new(name, NULL);
      g_action_map_add_action(G_ACTION_MAP(mode_actions), G_ACTION(action));
      g_signal_connect(action, "activate", G_CALLBACK(mode_selected), form);
      g_object_set_data(G_OBJECT(action), "mode", mode);
      g_object_unref(action);

      GtkWidget *button = gtk_model_button_new();
      g_autoptr(GString) prefixed_name = g_string_new(MODE_PREFIX);
      g_string_append(prefixed_name, ".");
      g_string_append(prefixed_name, name);
      gtk_actionable_set_action_name(GTK_ACTIONABLE(button), prefixed_name->str);
      g_object_set(button, "role", GTK_BUTTON_ROLE_RADIO, "text", name, NULL);
      gtk_box_pack_start(GTK_BOX(mode_box), button, FALSE, FALSE, 0);
      g_object_set_data(G_OBJECT(button), "mode", mode);
      gtk_widget_show_all(button);
    }
    // Mode entries
    int w = head->custom_mode.width;
    int h = head->custom_mode.height;
    int r = head->custom_mode.refresh;
    if (head->enabled && head->mode != NULL) {
      w = head->mode->width;
      h = head->mode->height;
      r = head->mode->refresh;
    }
    update_mode_entries(form, w, h, r);
    select_mode_option(form, w, h, r);
    gtk_widget_show_all(mode_box);
  }

  if (fields & WD_FIELD_TRANSFORM) {
    int active_rotate = get_rotate_index(head->transform);
    select_rotate_option(form, GTK_WIDGET(gtk_builder_get_object(builder, ROTATE_IDS[active_rotate])));

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(flipped),
        head->transform == WL_OUTPUT_TRANSFORM_FLIPPED
        || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_90
        || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_180
        || head->transform == WL_OUTPUT_TRANSFORM_FLIPPED_270);
  }

  // Sync state
  if (fields & WD_FIELD_ENABLED) {
    update_sensitivity(form);
  }
  show_apply(head->state);
  gtk_widget_queue_draw(head->state->canvas);
}

void wd_ui_reset_heads(struct wd_state *state) {
  if (state->stack == NULL) {
    return;
  }

  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  GList *form_iter = forms;
  struct wd_head *head;
  wl_list_for_each(head, &state->heads, link) {
    GtkBuilder *builder;
    GtkWidget *form;
    if (form_iter == NULL) {
      builder = gtk_builder_new_from_resource("/head.ui");
      form = GTK_WIDGET(gtk_builder_get_object(builder, "form"));
      g_object_set_data(G_OBJECT(form), "builder", builder);
      g_object_set_data(G_OBJECT(form), "head", head);
      gtk_stack_add_titled(GTK_STACK(state->stack), form, head->name, head->name);

      GtkWidget *mode_button = GTK_WIDGET(gtk_builder_get_object(builder, "mode_button"));
      GtkWidget *rotate_button = GTK_WIDGET(gtk_builder_get_object(builder, "rotate_button"));

      GSimpleActionGroup *mode_actions = g_simple_action_group_new();
      gtk_widget_insert_action_group(mode_button, MODE_PREFIX, G_ACTION_GROUP(mode_actions));
      g_object_set_data(G_OBJECT(form), "mode-group", mode_actions);
      g_object_unref(mode_actions);

      GSimpleActionGroup *transform_actions = g_simple_action_group_new();
      gtk_widget_insert_action_group(rotate_button, TRANSFORM_PREFIX, G_ACTION_GROUP(transform_actions));
      g_object_unref(transform_actions);

      for (int i = 0; i < NUM_ROTATIONS; i++) {
        GtkWidget *button = GTK_WIDGET(gtk_builder_get_object(builder, ROTATE_IDS[i]));
        g_object_set(button, "role", GTK_BUTTON_ROLE_RADIO, NULL);
        GSimpleAction *action = g_simple_action_new(ROTATE_IDS[i], NULL);
        g_action_map_add_action(G_ACTION_MAP(transform_actions), G_ACTION(action));
        g_signal_connect(action, "activate", G_CALLBACK(rotate_selected), form);
        g_object_set_data(G_OBJECT(action), "widget", button);
        g_object_unref(action);
      }
      update_head_form(form, WD_FIELDS_ALL);

      gtk_widget_show_all(form);

      g_signal_connect_swapped(gtk_builder_get_object(builder, "enabled"), "toggled", G_CALLBACK(update_sensitivity), form);
      g_signal_connect_swapped(gtk_builder_get_object(builder, "enabled"), "toggled", G_CALLBACK(show_apply), state);
      g_signal_connect_swapped(gtk_builder_get_object(builder, "scale"), "value-changed", G_CALLBACK(show_apply), state);
      g_signal_connect_swapped(gtk_builder_get_object(builder, "pos_x"), "value-changed", G_CALLBACK(show_apply), state);
      g_signal_connect_swapped(gtk_builder_get_object(builder, "pos_y"), "value-changed", G_CALLBACK(show_apply), state);
      g_signal_connect_swapped(gtk_builder_get_object(builder, "width"), "value-changed", G_CALLBACK(show_apply), state);
      g_signal_connect_swapped(gtk_builder_get_object(builder, "height"), "value-changed", G_CALLBACK(show_apply), state);
      g_signal_connect_swapped(gtk_builder_get_object(builder, "refresh"), "value-changed", G_CALLBACK(show_apply), state);
      g_signal_connect_swapped(gtk_builder_get_object(builder, "flipped"), "toggled", G_CALLBACK(show_apply), state);

    } else {
      form = form_iter->data;
      g_object_set_data(G_OBJECT(form), "head", head);
      form_iter = form_iter->next;
    }
  }
  // remove everything else
  for (; form_iter != NULL; form_iter = form_iter->next) {
    GtkBuilder *builder = GTK_BUILDER(g_object_get_data(G_OBJECT(form_iter->data), "builder"));
    g_object_unref(builder);
    gtk_container_remove(GTK_CONTAINER(state->stack), GTK_WIDGET(form_iter->data));
  }
  gtk_widget_queue_draw(state->canvas);
}

/*
 * Updates the UI form for a single head. Useful for when the compositor notifies us of
 * updated configuration caused by another program.
 */
void wd_ui_reset_head(const struct wd_head *head, unsigned int fields) {
  if (head->state->stack == NULL) {
    return;
  }
  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(head->state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    const struct wd_head *other = g_object_get_data(G_OBJECT(form_iter->data), "head");
    if (head == other) {
      update_head_form(GTK_WIDGET(form_iter->data), fields);
    }
  }
}

void wd_ui_reset_all(struct wd_state *state) {
  wd_ui_reset_heads(state);
  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    update_head_form(GTK_WIDGET(form_iter->data), WD_FIELDS_ALL);
  }
}

void wd_ui_apply_done(struct wd_state *state, struct wl_list *outputs) {
  gtk_style_context_remove_class(gtk_widget_get_style_context(state->spinner), "visible");
  gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(state->overlay), state->spinner, TRUE);

  gtk_widget_set_sensitive(state->stack_switcher, TRUE);
  gtk_widget_set_sensitive(state->stack, TRUE);
  gtk_widget_set_sensitive(state->zoom_in, TRUE);
  gtk_widget_set_sensitive(state->zoom_reset, TRUE);
  gtk_widget_set_sensitive(state->zoom_out, TRUE);
  gtk_widget_set_sensitive(state->menu_button, TRUE);
  if (!state->autoapply) {
    show_apply(state);
  }
  g_idle_add(apply_done_reset, state);
}

void wd_ui_show_error(struct wd_state *state, const char *message) {
  gtk_label_set_text(GTK_LABEL(state->info_label), message);
  gtk_widget_show(state->info_bar);
  gtk_info_bar_set_revealed(GTK_INFO_BAR(state->info_bar), TRUE);
}

// BEGIN GLOBAL CALLBACKS
static void cleanup(GtkWidget *window, gpointer state) {
  g_free(state);
}

gboolean draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  struct wd_state *state = data;
  update_canvas_size(state);
  GtkStyleContext *style_ctx = gtk_widget_get_style_context(widget);
  GtkAdjustment *scroll_x_adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  GtkAdjustment *scroll_y_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(state->scroller));
  double scroll_x = gtk_adjustment_get_value(scroll_x_adj);
  double scroll_y = gtk_adjustment_get_value(scroll_y_adj);
  int width = gtk_widget_get_allocated_width(widget);
  int height = gtk_widget_get_allocated_height(widget);

  GdkRGBA border;
  gtk_style_context_lookup_color(style_ctx, "borders", &border);

  gdk_cairo_set_source_rgba(cr, &border);
  cairo_set_line_width(cr, .5);

  gtk_render_background(style_ctx, cr, 0, 0, width, height);
  g_autoptr(GList) forms = gtk_container_get_children(GTK_CONTAINER(state->stack));
  for (GList *form_iter = forms; form_iter != NULL; form_iter = form_iter->next) {
    GtkBuilder *builder = GTK_BUILDER(g_object_get_data(G_OBJECT(form_iter->data), "builder"));
    gboolean enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "enabled")));
    if (enabled) {
      int x = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "pos_x")));
      int y = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "pos_y")));
      int w = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "width")));
      int h = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "height")));
      double scale = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "scale")));
      if (scale <= 0.) scale = 1.;
      cairo_rectangle(cr,
          x * state->zoom + .5 - scroll_x - state->xorigin,
          y * state->zoom + .5 - scroll_y - state->yorigin,
          w * state->zoom / scale,
          h * state->zoom / scale);
      cairo_stroke(cr);
    }
  }

  return TRUE;
}

static void cancel_changes(GtkButton *button, gpointer data) {
  struct wd_state *state = data;
  gtk_stack_set_visible_child_name(GTK_STACK(state->header_stack), "title");
  wd_ui_reset_all(state);
}

static void apply_changes(GtkButton *button, gpointer data) {
  apply_state(data);
}

static void update_zoom(struct wd_state *state) {
  g_autofree gchar *zoom_percent = g_strdup_printf("%.f%%", state->zoom * 100.);
  gtk_button_set_label(GTK_BUTTON(state->zoom_reset), zoom_percent);
  gtk_widget_set_sensitive(state->zoom_in, state->zoom < MAX_ZOOM);
  gtk_widget_set_sensitive(state->zoom_out, state->zoom > MIN_ZOOM);
  gtk_widget_queue_draw(state->canvas);
}

static void zoom_out(GtkButton *button, gpointer data) {
  struct wd_state *state = data;
  state->zoom *= 0.75;
  state->zoom = MAX(state->zoom, MIN_ZOOM);
  update_zoom(state);
}

static void zoom_reset(GtkButton *button, gpointer data) {
  struct wd_state *state = data;
  state->zoom = DEFAULT_ZOOM;
  update_zoom(state);
}

static void zoom_in(GtkButton *button, gpointer data) {
  struct wd_state *state = data;
  state->zoom /= 0.75;
  state->zoom = MIN(state->zoom, MAX_ZOOM);
  update_zoom(state);
}

static void info_response(GtkInfoBar *info_bar, gint response_id, gpointer data) {
  gtk_info_bar_set_revealed(info_bar, FALSE);
}

static void info_bar_animation_done(GObject *object, GParamSpec *pspec, gpointer data) {
  gboolean done = gtk_revealer_get_child_revealed(GTK_REVEALER(object));
  if (!done) {
    struct wd_state *state = data;
    gtk_widget_set_visible(state->info_bar, gtk_revealer_get_reveal_child(GTK_REVEALER(object)));
  }
}

static void auto_apply_selected(GSimpleAction *action, GVariant *param, gpointer data) {
  struct wd_state *state = data;
  state->autoapply = !state->autoapply;
  g_simple_action_set_state(action, g_variant_new_boolean(state->autoapply));
}

static void activate(GtkApplication* app, gpointer user_data) {
  GdkDisplay *gdk_display = gdk_display_get_default();
  if (!GDK_IS_WAYLAND_DISPLAY(gdk_display)) {
    wd_fatal_error(1, "This program is only usable on Wayland sessions.");
  }

  struct wd_state *state = g_new0(struct wd_state, 1);
  state->zoom = DEFAULT_ZOOM;
  wl_list_init(&state->heads);

  GtkCssProvider *css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_resource(css_provider, "/style.css");
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  GtkBuilder *builder = gtk_builder_new_from_resource("/wdisplay.ui");
  GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "heads_window"));
  state->header_stack = GTK_WIDGET(gtk_builder_get_object(builder, "header_stack"));
  state->stack_switcher = GTK_WIDGET(gtk_builder_get_object(builder, "heads_stack_switcher"));
  state->stack = GTK_WIDGET(gtk_builder_get_object(builder, "heads_stack"));
  state->scroller = GTK_WIDGET(gtk_builder_get_object(builder, "heads_scroll"));
  state->canvas = GTK_WIDGET(gtk_builder_get_object(builder, "heads_layout"));
  state->spinner = GTK_WIDGET(gtk_builder_get_object(builder, "spinner"));
  state->zoom_out = GTK_WIDGET(gtk_builder_get_object(builder, "zoom_out"));
  state->zoom_reset = GTK_WIDGET(gtk_builder_get_object(builder, "zoom_reset"));
  state->zoom_in = GTK_WIDGET(gtk_builder_get_object(builder, "zoom_in"));
  state->overlay = GTK_WIDGET(gtk_builder_get_object(builder, "overlay"));
  state->info_bar = GTK_WIDGET(gtk_builder_get_object(builder, "heads_info"));
  state->info_label = GTK_WIDGET(gtk_builder_get_object(builder, "heads_info_label"));
  state->menu_button = GTK_WIDGET(gtk_builder_get_object(builder, "menu_button"));
  gtk_builder_add_callback_symbol(builder, "heads_draw", G_CALLBACK(draw));
  gtk_builder_add_callback_symbol(builder, "apply_changes", G_CALLBACK(apply_changes));
  gtk_builder_add_callback_symbol(builder, "cancel_changes", G_CALLBACK(cancel_changes));
  gtk_builder_add_callback_symbol(builder, "zoom_out", G_CALLBACK(zoom_out));
  gtk_builder_add_callback_symbol(builder, "zoom_reset", G_CALLBACK(zoom_reset));
  gtk_builder_add_callback_symbol(builder, "zoom_in", G_CALLBACK(zoom_in));
  gtk_builder_add_callback_symbol(builder, "info_response", G_CALLBACK(info_response));
  gtk_builder_add_callback_symbol(builder, "destroy", G_CALLBACK(cleanup));
  gtk_builder_connect_signals(builder, state);
  gtk_box_set_homogeneous(GTK_BOX(gtk_builder_get_object(builder, "zoom_box")), FALSE);
  update_zoom(state);

  GSimpleActionGroup *main_actions = g_simple_action_group_new();
  gtk_widget_insert_action_group(state->menu_button, APP_PREFIX, G_ACTION_GROUP(main_actions));
  g_object_unref(main_actions);

  GSimpleAction *autoapply_action = g_simple_action_new_stateful("auto-apply", NULL,
      g_variant_new_boolean(state->autoapply));
  g_signal_connect(autoapply_action, "activate", G_CALLBACK(auto_apply_selected), state);
  g_action_map_add_action(G_ACTION_MAP(main_actions), G_ACTION(autoapply_action));

  /* first child of GtkInfoBar is always GtkRevealer */
  g_autoptr(GList) info_children = gtk_container_get_children(GTK_CONTAINER(state->info_bar));
  g_signal_connect(info_children->data, "notify::child-revealed", G_CALLBACK(info_bar_animation_done), state);

  struct wl_display *display = gdk_wayland_display_get_wl_display(gdk_display);
  wd_add_output_management_listener(state, display);

  if (state->output_manager == NULL) {
    wd_fatal_error(1, "Compositor doesn't support wlr-output-management-unstable-v1");
  }

  gtk_application_add_window(app, GTK_WINDOW(window));
  gtk_widget_show_all(window);
  g_object_unref(builder);
}
// END GLOBAL CALLBACKS

int main(int argc, char *argv[]) {
  GtkApplication *app = gtk_application_new("org.swaywm.sway-outputs", G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  return status;
}
