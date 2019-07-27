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

#include "wdisplay.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <epoxy/gl.h>

#define CANVAS_MARGIN 100

#define BT_UV_VERT_SIZE (2 + 2)
#define BT_UV_QUAD_SIZE (6 * BT_UV_VERT_SIZE)
#define BT_UV_MAX (BT_COLOR_QUAD_SIZE * HEADS_MAX)

#define BT_COLOR_VERT_SIZE (2 + 4)
#define BT_COLOR_QUAD_SIZE (6 * BT_COLOR_VERT_SIZE)
#define BT_COLOR_MAX (BT_COLOR_QUAD_SIZE * HEADS_MAX)

struct wd_gl_data {
  GLuint color_program;
  GLuint color_vertex_shader;
  GLuint color_fragment_shader;
  GLuint color_position_attribute;
  GLuint color_color_attribute;
  GLuint color_screen_size_uniform;

  GLuint texture_program;
  GLuint texture_vertex_shader;
  GLuint texture_fragment_shader;
  GLuint texture_position_attribute;
  GLuint texture_uv_attribute;
  GLuint texture_screen_size_uniform;
  GLuint texture_texture_uniform;

  GLuint buffers[2];

  unsigned texture_count;
  GLuint textures[HEADS_MAX];

  float tris[BT_COLOR_MAX];
};

static const char *color_vertex_shader_src = "\
attribute vec2 position;\n\
attribute vec4 color;\n\
varying vec4 color_out;\n\
uniform vec2 screen_size;\n\
void main(void) {\n\
  vec2 screen_pos = (position / screen_size * 2. - 1.) * vec2(1., -1.);\n\
  gl_Position = vec4(screen_pos, 0., 1.);\n\
  color_out = color;\n\
}";

static const char *color_fragment_shader_src = "\
varying vec4 color_out;\n\
void main(void) {\n\
  gl_FragColor = color_out;\n\
}";

static const char *texture_vertex_shader_src = "\
attribute vec2 position;\n\
attribute vec2 uv;\n\
varying vec2 uv_out;\n\
uniform vec2 screen_size;\n\
void main(void) {\n\
  vec2 screen_pos = (position / screen_size * 2. - 1.) * vec2(1., -1.);\n\
  gl_Position = vec4(screen_pos, 0., 1.);\n\
  uv_out = uv;\n\
}";

static const char *texture_fragment_shader_src = "\
varying vec2 uv_out;\n\
uniform sampler2D texture;\n\
void main(void) {\n\
  gl_FragColor = texture2D(texture, uv_out);\n\
}";

static GLuint gl_make_shader(GLenum type, const char *src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);
  GLint status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLsizei length;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    GLchar *log = "Failed";
    if (length > 0) {
      log = malloc(length);
      glGetShaderInfoLog(shader, length, NULL, log);
    }
    fprintf(stderr, "glCompileShader: %s\n", log);
    if (length > 0) {
      free(log);
    }
  }
  return shader;
}

static void gl_link_and_validate(GLint program) {
  GLint status;

  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    GLsizei length;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    GLchar *log = malloc(length);
    glGetProgramInfoLog(program, length, NULL, log);
    fprintf(stderr, "glLinkProgram: %s\n", log);
    free(log);
    return;
  }
  glValidateProgram(program);
  glGetProgramiv(program, GL_VALIDATE_STATUS, &status);
  if (status == GL_FALSE) {
    GLsizei length;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    GLchar *log = malloc(length);
    glGetProgramInfoLog(program, length, NULL, log);
    fprintf(stderr, "glValidateProgram: %s\n", log);
    free(log);
  }
}

struct wd_gl_data *wd_gl_setup(void) {
  struct wd_gl_data *res = calloc(1, sizeof(struct wd_gl_data));
  res->color_program = glCreateProgram();

  res->color_vertex_shader = gl_make_shader(GL_VERTEX_SHADER,
      color_vertex_shader_src);
  glAttachShader(res->color_program, res->color_vertex_shader);
  res->color_fragment_shader = gl_make_shader(GL_FRAGMENT_SHADER,
      color_fragment_shader_src);
  glAttachShader(res->color_program, res->color_fragment_shader);
  gl_link_and_validate(res->color_program);

  res->color_position_attribute = glGetAttribLocation(res->color_program,
      "position");
  res->color_color_attribute = glGetAttribLocation(res->color_program,
      "color");
  res->color_screen_size_uniform = glGetUniformLocation(res->color_program,
      "screen_size");

  res->texture_program = glCreateProgram();

  res->texture_vertex_shader = gl_make_shader(GL_VERTEX_SHADER,
      texture_vertex_shader_src);
  glAttachShader(res->texture_program, res->texture_vertex_shader);
  res->texture_fragment_shader = gl_make_shader(GL_FRAGMENT_SHADER,
      texture_fragment_shader_src);
  glAttachShader(res->texture_program, res->texture_fragment_shader);
  gl_link_and_validate(res->texture_program);

  res->texture_position_attribute = glGetAttribLocation(res->texture_program,
      "position");
  res->texture_uv_attribute = glGetAttribLocation(res->texture_program,
      "uv");
  res->texture_screen_size_uniform = glGetUniformLocation(res->texture_program,
      "screen_size");
  res->texture_texture_uniform = glGetUniformLocation(res->texture_program,
      "texture");

  glGenBuffers(2, res->buffers);
  glBindBuffer(GL_ARRAY_BUFFER, res->buffers[0]);
  glBufferData(GL_ARRAY_BUFFER, BT_UV_MAX * sizeof(float),
      NULL, GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, res->buffers[1]);
  glBufferData(GL_ARRAY_BUFFER, BT_COLOR_MAX * sizeof(float),
      NULL, GL_DYNAMIC_DRAW);

  return res;
}

#define PUSH_POINT(_start, _a, _b) \
    *((_start)++) = (_a);\
    *((_start)++) = (_b);

#define PUSH_COLOR(_start, _a, _b, _c, _d) \
    *((_start)++) = (_a);\
    *((_start)++) = (_b);\
    *((_start)++) = (_c);\
    *((_start)++) = (_d);

#define PUSH_POINT_UV(_start, _a, _b, _c, _d) \
    PUSH_COLOR(_start, _a, _b, _c, _d)

void wd_gl_render(struct wd_gl_data *res, struct wd_render_data *info,
    uint64_t tick) {
  unsigned int tris = 0;

  if (info->head_count > res->texture_count) {
    glGenTextures(info->head_count - res->texture_count,
        res->textures + res->texture_count);
    for (int i = res->texture_count; i < info->head_count; i++) {
      glBindTexture(GL_TEXTURE_2D, res->textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    res->texture_count = info->head_count;
  }

  for (int i = 0; i < info->head_count; i++) {
    struct wd_render_head_data *head = &info->heads[i];
    float *tri_ptr = res->tris + i * BT_UV_QUAD_SIZE;
    float x1 = head->x1;
    float y1 = head->y1;
    float x2 = head->x2;
    float y2 = head->y2;

    float t1 = head->y_invert ? 1.f : 0.f;
    float t2 = head->y_invert ? 0.f : 1.f;

    PUSH_POINT_UV(tri_ptr, x1, y1, 0.f, t1)
    PUSH_POINT_UV(tri_ptr, x2, y1, 1.f, t1)
    PUSH_POINT_UV(tri_ptr, x1, y2, 0.f, t2)
    PUSH_POINT_UV(tri_ptr, x1, y2, 0.f, t2)
    PUSH_POINT_UV(tri_ptr, x2, y1, 1.f, t1)
    PUSH_POINT_UV(tri_ptr, x2, y2, 1.f, t2)

    tris += 6;
  }

  glClearColor(info->bg_color[0], info->bg_color[1], info->bg_color[2], 1.f);
  glClear(GL_COLOR_BUFFER_BIT);

  float screen_size[2] = { info->viewport_width, info->viewport_height };

  if (tris > 0) {
    glUseProgram(res->texture_program);
    glBindBuffer(GL_ARRAY_BUFFER, res->buffers[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        tris * BT_UV_VERT_SIZE * sizeof(float), res->tris);
    glEnableVertexAttribArray(res->texture_position_attribute);
    glEnableVertexAttribArray(res->texture_uv_attribute);
    glVertexAttribPointer(res->texture_position_attribute,
        2, GL_FLOAT, GL_FALSE,
        BT_UV_VERT_SIZE * sizeof(float), (void *) (0 * sizeof(float)));
    glVertexAttribPointer(res->texture_uv_attribute, 2, GL_FLOAT, GL_FALSE,
        BT_UV_VERT_SIZE * sizeof(float), (void *) (2 * sizeof(float)));
    glUniform2fv(res->texture_screen_size_uniform, 1, screen_size);
    glUniform1i(res->texture_texture_uniform, 0);
    glActiveTexture(GL_TEXTURE0);

    for (int i = 0; i < info->head_count; i++) {
      struct wd_render_head_data *head = &info->heads[i];
      glBindTexture(GL_TEXTURE_2D, res->textures[i]);
      if (head->updated_at == tick) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, head->tex_stride / 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
            head->tex_width, head->tex_height,
            0, GL_RGBA, GL_UNSIGNED_BYTE, head->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
      }
      glDrawArrays(GL_TRIANGLES, i * 6, 6);
    }
  }

  tris = 0;

  int j = 0;
  for (int i = 0; i < info->head_count; i++) {
    struct wd_render_head_data *head = &info->heads[i];
    if (head->hovered || tick < head->transition_begin + HOVER_USECS) {
      float *tri_ptr = res->tris + j++ * BT_COLOR_QUAD_SIZE;
      float x1 = head->x1;
      float y1 = head->y1;
      float x2 = head->x2;
      float y2 = head->y2;

      float *color = info->selection_color;
      float d = fminf(
          (tick - head->transition_begin) / (double) HOVER_USECS, 1.f);
      if (!head->hovered) {
        d = 1.f - d;
      }
      d *= 2.f;
      if (d <= 1.f) {
        d = d * d;
      } else {
        d -= 1.f;
        d = d * (2.f - d) + 1.f;
      }
      d /= 2.f;
      float alpha = color[3] * d * .5f;

      PUSH_POINT(tri_ptr, x1, y1)
      PUSH_COLOR(tri_ptr, color[0], color[1], color[2], alpha)
      PUSH_POINT(tri_ptr, x2, y1)
      PUSH_COLOR(tri_ptr, color[0], color[1], color[2], alpha)
      PUSH_POINT(tri_ptr, x1, y2)
      PUSH_COLOR(tri_ptr, color[0], color[1], color[2], alpha)
      PUSH_POINT(tri_ptr, x1, y2)
      PUSH_COLOR(tri_ptr, color[0], color[1], color[2], alpha)
      PUSH_POINT(tri_ptr, x2, y1)
      PUSH_COLOR(tri_ptr, color[0], color[1], color[2], alpha)
      PUSH_POINT(tri_ptr, x2, y2)
      PUSH_COLOR(tri_ptr, color[0], color[1], color[2], alpha)

      tris += 6;
    }
  }

  if (tris > 0) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(res->color_program);
    glBindBuffer(GL_ARRAY_BUFFER, res->buffers[1]);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        tris * BT_COLOR_VERT_SIZE * sizeof(float), res->tris);
    glEnableVertexAttribArray(res->color_position_attribute);
    glEnableVertexAttribArray(res->color_color_attribute);
    glVertexAttribPointer(res->color_position_attribute, 2, GL_FLOAT, GL_FALSE,
        BT_COLOR_VERT_SIZE * sizeof(float), (void *) (0 * sizeof(float)));
    glVertexAttribPointer(res->color_color_attribute, 4, GL_FLOAT, GL_FALSE,
        BT_COLOR_VERT_SIZE * sizeof(float), (void *) (2 * sizeof(float)));
    glUniform2fv(res->color_screen_size_uniform, 1, screen_size);
    glDrawArrays(GL_TRIANGLES, 0, tris);
    glDisable(GL_BLEND);
  }
}

void wd_gl_cleanup(struct wd_gl_data *res) {
  glDeleteBuffers(2, res->buffers);
  glDeleteShader(res->texture_fragment_shader);
  glDeleteShader(res->texture_vertex_shader);
  glDeleteProgram(res->texture_program);

  glDeleteShader(res->color_fragment_shader);
  glDeleteShader(res->color_vertex_shader);
  glDeleteProgram(res->color_program);

  free(res);
}
