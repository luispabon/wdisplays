
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
 * https://github.com/emersion/kanshi/blob/38d27474b686fcc8324cc5e454741a49577c0988/main.c
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "wdisplay.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

#define HEADS_MAX 64

struct wd_pending_config {
  struct wd_state *state;
  struct wl_list *outputs;
};

static void destroy_pending(struct wd_pending_config *pending) {
  struct wd_head_config *output, *tmp;
  wl_list_for_each_safe(output, tmp, pending->outputs, link) {
    wl_list_remove(&output->link);
    free(output);
  }
  free(pending->outputs);
  free(pending);
}

static void config_handle_succeeded(void *data,
    struct zwlr_output_configuration_v1 *config) {
  struct wd_pending_config *pending = data;
  zwlr_output_configuration_v1_destroy(config);
  wd_ui_apply_done(pending->state, pending->outputs);
  destroy_pending(pending);
}

static void config_handle_failed(void *data,
    struct zwlr_output_configuration_v1 *config) {
  struct wd_pending_config *pending = data;
  zwlr_output_configuration_v1_destroy(config);
  wd_ui_reset_all(pending->state);
  wd_ui_apply_done(pending->state, NULL);
  wd_ui_show_error(pending->state,
      "The display server was not able to process your changes.");
  destroy_pending(pending);
}

static void config_handle_cancelled(void *data,
    struct zwlr_output_configuration_v1 *config) {
  struct wd_pending_config *pending = data;
  zwlr_output_configuration_v1_destroy(config);
  wd_ui_reset_all(pending->state);
  wd_ui_apply_done(pending->state, NULL);
  wd_ui_show_error(pending->state,
      "The display configuration was modified by the server before updates were processed. "
      "Please check the configuration and apply the changes again.");
  destroy_pending(pending);
}

static const struct zwlr_output_configuration_v1_listener config_listener = {
  .succeeded = config_handle_succeeded,
  .failed = config_handle_failed,
  .cancelled = config_handle_cancelled,
};

void wd_apply_state(struct wd_state *state, struct wl_list *new_outputs) {
  struct zwlr_output_configuration_v1 *config =
    zwlr_output_manager_v1_create_configuration(state->output_manager, state->serial);

  struct wd_pending_config *pending = calloc(1, sizeof(*pending));
  pending->state = state;
  pending->outputs = new_outputs;

  zwlr_output_configuration_v1_add_listener(config, &config_listener, pending);

  ssize_t i = -1;
  struct wd_head_config *output;
  wl_list_for_each(output, new_outputs, link) {
    i++;
    struct wd_head *head = output->head;

    if (!output->enabled && output->enabled != head->enabled) {
      zwlr_output_configuration_v1_disable_head(config, head->wlr_head);
      continue;
    }

    struct zwlr_output_configuration_head_v1 *config_head = zwlr_output_configuration_v1_enable_head(config, head->wlr_head);

    const struct wd_mode *selected_mode = NULL;
    const struct wd_mode *mode;
    wl_list_for_each(mode, &head->modes, link) {
      if (mode->width == output->width && mode->height == output->height && mode->refresh == output->refresh) {
        selected_mode = mode;
        break;
      }
    }
    if (selected_mode != NULL) {
      if (selected_mode != head->mode) {
        zwlr_output_configuration_head_v1_set_mode(config_head, selected_mode->wlr_mode);
      }
    } else if (output->width != head->custom_mode.width
        || output->height != head->custom_mode.height
        || output->refresh != head->custom_mode.refresh) {
      zwlr_output_configuration_head_v1_set_custom_mode(config_head,
          output->width, output->height, output->refresh);
    }
    if (output->x != head->x || output->y != head->y) {
      zwlr_output_configuration_head_v1_set_position(config_head, output->x, output->y);
    }
    if (output->scale != head->scale) {
      zwlr_output_configuration_head_v1_set_scale(config_head, wl_fixed_from_double(output->scale));
    }
    if (output->transform != head->transform) {
      zwlr_output_configuration_head_v1_set_transform(config_head, output->transform);
    }
  }

  zwlr_output_configuration_v1_apply(config);
}

static void mode_handle_size(void *data, struct zwlr_output_mode_v1 *wlr_mode,
    int32_t width, int32_t height) {
  struct wd_mode *mode = data;
  mode->width = width;
  mode->height = height;
}

static void mode_handle_refresh(void *data,
    struct zwlr_output_mode_v1 *wlr_mode, int32_t refresh) {
  struct wd_mode *mode = data;
  mode->refresh = refresh;
}

static void mode_handle_preferred(void *data,
    struct zwlr_output_mode_v1 *wlr_mode) {
  struct wd_mode *mode = data;
  mode->preferred = true;
}

static void mode_handle_finished(void *data,
    struct zwlr_output_mode_v1 *wlr_mode) {
  struct wd_mode *mode = data;
  wl_list_remove(&mode->link);
  zwlr_output_mode_v1_destroy(mode->wlr_mode);
  free(mode);
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
  .size = mode_handle_size,
  .refresh = mode_handle_refresh,
  .preferred = mode_handle_preferred,
  .finished = mode_handle_finished,
};

static void head_handle_name(void *data,
    struct zwlr_output_head_v1 *wlr_head, const char *name) {
  struct wd_head *head = data;
  head->name = strdup(name);
  wd_ui_reset_head(head, WD_FIELD_NAME);
}

static void head_handle_description(void *data,
    struct zwlr_output_head_v1 *wlr_head, const char *description) {
  struct wd_head *head = data;
  head->description = strdup(description);
  wd_ui_reset_head(head, WD_FIELD_DESCRIPTION);
}

static void head_handle_physical_size(void *data,
    struct zwlr_output_head_v1 *wlr_head, int32_t width, int32_t height) {
  struct wd_head *head = data;
  head->phys_width = width;
  head->phys_height = height;
  wd_ui_reset_head(head, WD_FIELD_PHYSICAL_SIZE);
}

static void head_handle_mode(void *data,
    struct zwlr_output_head_v1 *wlr_head,
    struct zwlr_output_mode_v1 *wlr_mode) {
  struct wd_head *head = data;

  struct wd_mode *mode = calloc(1, sizeof(*mode));
  mode->head = head;
  mode->wlr_mode = wlr_mode;
  wl_list_insert(head->modes.prev, &mode->link);

  zwlr_output_mode_v1_add_listener(wlr_mode, &mode_listener, mode);
}

static void head_handle_enabled(void *data,
    struct zwlr_output_head_v1 *wlr_head, int32_t enabled) {
  struct wd_head *head = data;
  head->enabled = !!enabled;
  if (!enabled) {
    head->mode = NULL;
  }
  wd_ui_reset_head(head, WD_FIELD_ENABLED);
}

static void head_handle_current_mode(void *data,
    struct zwlr_output_head_v1 *wlr_head,
    struct zwlr_output_mode_v1 *wlr_mode) {
  struct wd_head *head = data;
  struct wd_mode *mode;
  wl_list_for_each(mode, &head->modes, link) {
    if (mode->wlr_mode == wlr_mode) {
      head->mode = mode;
      wd_ui_reset_head(head, WD_FIELD_MODE);
      return;
    }
  }
  fprintf(stderr, "received unknown current_mode\n");
  head->mode = NULL;
}

static void head_handle_position(void *data,
    struct zwlr_output_head_v1 *wlr_head, int32_t x, int32_t y) {
  struct wd_head *head = data;
  head->x = x;
  head->y = y;
  wd_ui_reset_head(head, WD_FIELD_POSITION);
}

static void head_handle_transform(void *data,
    struct zwlr_output_head_v1 *wlr_head, int32_t transform) {
  struct wd_head *head = data;
  head->transform = transform;
  wd_ui_reset_head(head, WD_FIELD_TRANSFORM);
}

static void head_handle_scale(void *data,
    struct zwlr_output_head_v1 *wlr_head, wl_fixed_t scale) {
  struct wd_head *head = data;
  head->scale = wl_fixed_to_double(scale);
  wd_ui_reset_head(head, WD_FIELD_SCALE);
}

static void head_handle_finished(void *data,
    struct zwlr_output_head_v1 *wlr_head) {
  struct wd_head *head = data;
  wl_list_remove(&head->link);
  zwlr_output_head_v1_destroy(head->wlr_head);
  free(head->name);
  free(head->description);
  free(head);
}

static const struct zwlr_output_head_v1_listener head_listener = {
  .name = head_handle_name,
  .description = head_handle_description,
  .physical_size = head_handle_physical_size,
  .mode = head_handle_mode,
  .enabled = head_handle_enabled,
  .current_mode = head_handle_current_mode,
  .position = head_handle_position,
  .transform = head_handle_transform,
  .scale = head_handle_scale,
  .finished = head_handle_finished,
};

static void output_manager_handle_head(void *data,
    struct zwlr_output_manager_v1 *manager,
    struct zwlr_output_head_v1 *wlr_head) {
  struct wd_state *state = data;

  struct wd_head *head = calloc(1, sizeof(*head));
  head->state = state;
  head->wlr_head = wlr_head;
  head->scale = 1.0;
  wl_list_init(&head->modes);
  wl_list_insert(&state->heads, &head->link);

  zwlr_output_head_v1_add_listener(wlr_head, &head_listener, head);
}

static void output_manager_handle_done(void *data,
    struct zwlr_output_manager_v1 *manager, uint32_t serial) {
  struct wd_state *state = data;
  state->serial = serial;

  assert(wl_list_length(&state->heads) <= HEADS_MAX);
  wd_ui_reset_heads(state);
}

static void output_manager_handle_finished(void *data,
    struct zwlr_output_manager_v1 *manager) {
  // This space is intentionally left blank
}

static const struct zwlr_output_manager_v1_listener output_manager_listener = {
  .head = output_manager_handle_head,
  .done = output_manager_handle_done,
  .finished = output_manager_handle_finished,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version) {
  struct wd_state *state = data;

  if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
    state->output_manager = wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, 1);
    zwlr_output_manager_v1_add_listener(state->output_manager, &output_manager_listener, state);
  }
}

static void registry_handle_global_remove(void *data,
    struct wl_registry *registry, uint32_t name) {
  // This space is intentionally left blank
}

static const struct wl_registry_listener registry_listener = {
  .global = registry_handle_global,
  .global_remove = registry_handle_global_remove,
};

void wd_add_output_management_listener(struct wd_state *state, struct wl_display *display) {
  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, state);

  wl_display_dispatch(display);
  wl_display_roundtrip(display);
}
