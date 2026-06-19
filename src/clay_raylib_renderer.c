// Unity-build Clay renderer for raylib. Include this from main.c after
// raylib.h, clay.h, and the AppMetrics typedef are available.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLAY_RAYLIB_RENDERER_MAX_OVERLAY_DEPTH 64

typedef void (*ClayCounterRendererCustomRenderFn)(
    const Clay_RenderCommand* command, Rectangle bounds, AppMetrics metrics,
    void* user_data);

static Font*   clay_raylib_renderer_fonts           = NULL;
static int32_t clay_raylib_renderer_font_count      = 0;
static char*   clay_raylib_renderer_text_buffer     = NULL;
static int32_t clay_raylib_renderer_text_buffer_len = 0;

static Shader clay_raylib_renderer_overlay_shader    = {0};
static int    clay_raylib_renderer_overlay_color_loc = -1;
static bool   clay_raylib_renderer_overlay_ready     = false;
static bool   clay_raylib_renderer_overlay_enabled   = false;
static Clay_Color
    clay_raylib_renderer_overlay_stack[CLAY_RAYLIB_RENDERER_MAX_OVERLAY_DEPTH] =
        {0};
static int32_t clay_raylib_renderer_overlay_depth = 0;

static ClayCounterRendererCustomRenderFn clay_raylib_renderer_custom_fn = NULL;
static void* clay_raylib_renderer_custom_user_data                      = NULL;

#if defined(PLATFORM_WEB) || defined(GRAPHICS_API_OPENGL_ES2)
static const char* clay_raylib_renderer_overlay_shader_code =
    "precision mediump float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 overlayColor;\n"
    "void main()\n"
    "{\n"
    "  vec4 texelColor = texture2D(texture0, fragTexCoord) * fragColor;\n"
    "  vec3 blendedRGB = mix(texelColor.rgb, overlayColor.rgb, "
    "overlayColor.a);\n"
    "  gl_FragColor = vec4(blendedRGB, texelColor.a);\n"
    "}\n";
#else
static const char* clay_raylib_renderer_overlay_shader_code =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 overlayColor;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "  vec4 texelColor = texture(texture0, fragTexCoord) * fragColor;\n"
    "  vec3 blendedRGB = mix(texelColor.rgb, overlayColor.rgb, "
    "overlayColor.a);\n"
    "  finalColor = vec4(blendedRGB, texelColor.a);\n"
    "}\n";
#endif

static uint8_t clay_raylib_renderer_color_byte(float value) {
  if (value < 0.0f) return 0;
  if (value > 255.0f) return 255;
  return (uint8_t)(value + 0.5f);
}

Color clay_raylib_renderer_color(Clay_Color color) {
  return (Color){clay_raylib_renderer_color_byte(color.r),
                 clay_raylib_renderer_color_byte(color.g),
                 clay_raylib_renderer_color_byte(color.b),
                 clay_raylib_renderer_color_byte(color.a)};
}

static float clay_raylib_renderer_radius_scale(AppMetrics metrics) {
  return fmaxf(metrics.scale_x, metrics.scale_y);
}

static Rectangle clay_raylib_renderer_scale_box(Clay_BoundingBox box,
                                                AppMetrics       metrics) {
  return (Rectangle){box.x * metrics.scale_x, box.y * metrics.scale_y,
                     box.width * metrics.scale_x, box.height * metrics.scale_y};
}

static Clay_CornerRadius clay_raylib_renderer_scale_radius(
    Clay_CornerRadius radius, AppMetrics metrics) {
  float scale = clay_raylib_renderer_radius_scale(metrics);
  return (Clay_CornerRadius){radius.topLeft * scale, radius.topRight * scale,
                             radius.bottomLeft * scale,
                             radius.bottomRight * scale};
}

static float clay_raylib_renderer_roundness(Rectangle         rect,
                                            Clay_CornerRadius radius,
                                            AppMetrics        metrics) {
  float scaled_radius =
      radius.topLeft * clay_raylib_renderer_radius_scale(metrics);
  float smallest_side = fminf(rect.width, rect.height);

  if ((scaled_radius <= 0.0f) || (smallest_side <= 0.0f)) return 0.0f;

  return fminf((scaled_radius * 2.0f) / smallest_side, 1.0f);
}

static bool clay_raylib_renderer_ensure_text_buffer(int32_t length) {
  if (length < 0) length = 0;
  length += 1;

  if (length <= clay_raylib_renderer_text_buffer_len) return true;

  char* new_buffer = realloc(clay_raylib_renderer_text_buffer, (size_t)length);
  if (!new_buffer) return false;

  clay_raylib_renderer_text_buffer     = new_buffer;
  clay_raylib_renderer_text_buffer_len = length;
  return true;
}

static const char* clay_raylib_renderer_copy_text(Clay_StringSlice text) {
  int32_t length = text.length;
  if (length < 0) length = 0;
  if (!text.chars) length = 0;

  if (!clay_raylib_renderer_ensure_text_buffer(length)) return NULL;
  if (length > 0 && text.chars) {
    memcpy(clay_raylib_renderer_text_buffer, text.chars, (size_t)length);
  }
  clay_raylib_renderer_text_buffer[length] = '\0';

  return clay_raylib_renderer_text_buffer;
}

void clay_raylib_renderer_set_fonts(Font* fonts, int32_t count) {
  clay_raylib_renderer_fonts      = fonts;
  clay_raylib_renderer_font_count = count;
}

void clay_raylib_renderer_set_custom_handler(
    ClayCounterRendererCustomRenderFn custom_fn, void* user_data) {
  clay_raylib_renderer_custom_fn        = custom_fn;
  clay_raylib_renderer_custom_user_data = user_data;
}

static Font clay_raylib_renderer_font(uint16_t font_id) {
  if (clay_raylib_renderer_fonts && font_id < clay_raylib_renderer_font_count) {
    Font font = clay_raylib_renderer_fonts[font_id];
    if (font.glyphs && font.recs && font.texture.id != 0) return font;
  }

  return GetFontDefault();
}

Clay_Dimensions clay_raylib_renderer_measure_text(
    Clay_StringSlice text, Clay_TextElementConfig* config, void* user_data) {
  (void)user_data;

  const char* buffer = clay_raylib_renderer_copy_text(text);
  if (!buffer) return (Clay_Dimensions){0.0f, (float)config->fontSize};

  Font    font = clay_raylib_renderer_font(config->fontId);
  Vector2 size = MeasureTextEx(font, buffer, (float)config->fontSize,
                               (float)config->letterSpacing);

  return (Clay_Dimensions){size.x, size.y};
}

void clay_raylib_renderer_init(void) {
  if (clay_raylib_renderer_overlay_ready) return;

  clay_raylib_renderer_overlay_shader =
      LoadShaderFromMemory(NULL, clay_raylib_renderer_overlay_shader_code);
  clay_raylib_renderer_overlay_color_loc =
      GetShaderLocation(clay_raylib_renderer_overlay_shader, "overlayColor");
  clay_raylib_renderer_overlay_ready =
      (clay_raylib_renderer_overlay_shader.id != 0) &&
      (clay_raylib_renderer_overlay_color_loc >= 0);
}

static void clay_raylib_renderer_apply_overlay_color(Clay_Color color) {
  float color_float[4] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                          color.a / 255.0f};

  SetShaderValue(clay_raylib_renderer_overlay_shader,
                 clay_raylib_renderer_overlay_color_loc, color_float,
                 SHADER_UNIFORM_VEC4);
}

static void clay_raylib_renderer_start_overlay(Clay_Color color) {
  clay_raylib_renderer_init();
  if (!clay_raylib_renderer_overlay_ready) return;

  if (clay_raylib_renderer_overlay_depth <
      CLAY_RAYLIB_RENDERER_MAX_OVERLAY_DEPTH) {
    clay_raylib_renderer_overlay_stack[clay_raylib_renderer_overlay_depth++] =
        color;
  }

  clay_raylib_renderer_apply_overlay_color(color);

  if (!clay_raylib_renderer_overlay_enabled) {
    BeginShaderMode(clay_raylib_renderer_overlay_shader);
    clay_raylib_renderer_overlay_enabled = true;
  }
}

static void clay_raylib_renderer_end_overlay(void) {
  if (!clay_raylib_renderer_overlay_ready ||
      clay_raylib_renderer_overlay_depth <= 0) {
    return;
  }

  clay_raylib_renderer_overlay_depth--;

  if (clay_raylib_renderer_overlay_depth > 0) {
    clay_raylib_renderer_apply_overlay_color(
        clay_raylib_renderer_overlay_stack[clay_raylib_renderer_overlay_depth -
                                           1]);
    return;
  }

  if (!clay_raylib_renderer_overlay_enabled) return;

  EndShaderMode();
  clay_raylib_renderer_overlay_enabled = false;
}

static void clay_raylib_renderer_clear_overlay(void) {
  clay_raylib_renderer_overlay_depth = 0;

  if (!clay_raylib_renderer_overlay_enabled) return;

  EndShaderMode();
  clay_raylib_renderer_overlay_enabled = false;
}

void clay_raylib_renderer_shutdown(void) {
  clay_raylib_renderer_clear_overlay();

  if (clay_raylib_renderer_overlay_shader.id != 0) {
    UnloadShader(clay_raylib_renderer_overlay_shader);
  }

  clay_raylib_renderer_overlay_shader    = (Shader){0};
  clay_raylib_renderer_overlay_color_loc = -1;
  clay_raylib_renderer_overlay_ready     = false;

  free(clay_raylib_renderer_text_buffer);
  clay_raylib_renderer_text_buffer     = NULL;
  clay_raylib_renderer_text_buffer_len = 0;
}

static void clay_raylib_renderer_draw_text(Clay_TextRenderData text,
                                           Rectangle           bounds,
                                           AppMetrics          metrics) {
  const char* buffer = clay_raylib_renderer_copy_text(text.stringContents);
  if (!buffer) return;

  DrawTextEx(clay_raylib_renderer_font(text.fontId), buffer,
             (Vector2){bounds.x, bounds.y},
             (float)text.fontSize * metrics.scale_y,
             (float)text.letterSpacing * metrics.scale_x,
             clay_raylib_renderer_color(text.textColor));
}

static void clay_raylib_renderer_draw_image(Clay_ImageRenderData image,
                                            Rectangle            bounds) {
  if (!image.imageData) return;

  Texture2D  texture = *(Texture2D*)image.imageData;
  Clay_Color tint    = image.backgroundColor;

  if ((tint.r == 0.0f) && (tint.g == 0.0f) && (tint.b == 0.0f) &&
      (tint.a == 0.0f)) {
    tint = (Clay_Color){255, 255, 255, 255};
  }

  DrawTexturePro(texture, (Rectangle){0, 0, texture.width, texture.height},
                 bounds, (Vector2){0, 0}, 0.0f,
                 clay_raylib_renderer_color(tint));
}

static void clay_raylib_renderer_draw_rectangle(
    Clay_RectangleRenderData rectangle, Rectangle bounds, AppMetrics metrics) {
  float roundness =
      clay_raylib_renderer_roundness(bounds, rectangle.cornerRadius, metrics);
  Color color = clay_raylib_renderer_color(rectangle.backgroundColor);

  if (roundness > 0.0f)
    DrawRectangleRounded(bounds, roundness, 16, color);
  else
    DrawRectangleRec(bounds, color);
}

static void clay_raylib_renderer_draw_border(Clay_BorderRenderData border,
                                             Rectangle             bounds,
                                             AppMetrics            metrics) {
  Color             color = clay_raylib_renderer_color(border.color);
  Clay_CornerRadius radius =
      clay_raylib_renderer_scale_radius(border.cornerRadius, metrics);

  float left   = (float)border.width.left * metrics.scale_x;
  float right  = (float)border.width.right * metrics.scale_x;
  float top    = (float)border.width.top * metrics.scale_y;
  float bottom = (float)border.width.bottom * metrics.scale_y;

  if (left > 0.0f) {
    DrawRectangleRec(
        (Rectangle){bounds.x, bounds.y + radius.topLeft, left,
                    bounds.height - radius.topLeft - radius.bottomLeft},
        color);
  }

  if (right > 0.0f) {
    DrawRectangleRec(
        (Rectangle){bounds.x + bounds.width - right, bounds.y + radius.topRight,
                    right,
                    bounds.height - radius.topRight - radius.bottomRight},
        color);
  }

  if (top > 0.0f) {
    DrawRectangleRec(
        (Rectangle){bounds.x + radius.topLeft, bounds.y,
                    bounds.width - radius.topLeft - radius.topRight, top},
        color);
  }

  if (bottom > 0.0f) {
    DrawRectangleRec(
        (Rectangle){
            bounds.x + radius.bottomLeft, bounds.y + bounds.height - bottom,
            bounds.width - radius.bottomLeft - radius.bottomRight, bottom},
        color);
  }

  if (radius.topLeft > 0.0f) {
    DrawRing((Vector2){roundf(bounds.x + radius.topLeft),
                       roundf(bounds.y + radius.topLeft)},
             fmaxf(0.0f, radius.topLeft - top), radius.topLeft, 180, 270, 16,
             color);
  }

  if (radius.topRight > 0.0f) {
    DrawRing((Vector2){roundf(bounds.x + bounds.width - radius.topRight),
                       roundf(bounds.y + radius.topRight)},
             fmaxf(0.0f, radius.topRight - top), radius.topRight, 270, 360, 16,
             color);
  }

  if (radius.bottomLeft > 0.0f) {
    DrawRing((Vector2){roundf(bounds.x + radius.bottomLeft),
                       roundf(bounds.y + bounds.height - radius.bottomLeft)},
             fmaxf(0.0f, radius.bottomLeft - bottom), radius.bottomLeft, 90,
             180, 16, color);
  }

  if (radius.bottomRight > 0.0f) {
    DrawRing((Vector2){roundf(bounds.x + bounds.width - radius.bottomRight),
                       roundf(bounds.y + bounds.height - radius.bottomRight)},
             fmaxf(0.0f, radius.bottomRight - bottom), radius.bottomRight, 0.1f,
             90, 16, color);
  }
}

void clay_raylib_renderer_render(Clay_RenderCommandArray commands,
                                 AppMetrics              metrics) {
  static bool custom_warning_printed  = false;
  static bool unknown_warning_printed = false;

  for (int32_t i = 0; i < commands.length; i++) {
    Clay_RenderCommand* command = Clay_RenderCommandArray_Get(&commands, i);
    Rectangle           bounds =
        clay_raylib_renderer_scale_box(command->boundingBox, metrics);

    switch (command->commandType) {
      case CLAY_RENDER_COMMAND_TYPE_NONE:
        break;

      case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
        clay_raylib_renderer_draw_rectangle(command->renderData.rectangle,
                                            bounds, metrics);
        break;

      case CLAY_RENDER_COMMAND_TYPE_BORDER:
        clay_raylib_renderer_draw_border(command->renderData.border, bounds,
                                         metrics);
        break;

      case CLAY_RENDER_COMMAND_TYPE_TEXT:
        clay_raylib_renderer_draw_text(command->renderData.text, bounds,
                                       metrics);
        break;

      case CLAY_RENDER_COMMAND_TYPE_IMAGE:
        clay_raylib_renderer_draw_image(command->renderData.image, bounds);
        break;

      case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
        BeginScissorMode((int)roundf(bounds.x), (int)roundf(bounds.y),
                         (int)roundf(bounds.width), (int)roundf(bounds.height));
        break;

      case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
        EndScissorMode();
        break;

      case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_START:
        clay_raylib_renderer_start_overlay(
            command->renderData.overlayColor.color);
        break;

      case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_END:
        clay_raylib_renderer_end_overlay();
        break;

      case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
        if (clay_raylib_renderer_custom_fn) {
          clay_raylib_renderer_custom_fn(command, bounds, metrics,
                                         clay_raylib_renderer_custom_user_data);
        } else if (!custom_warning_printed) {
          fprintf(stderr, "Unhandled Clay custom render command\n");
          custom_warning_printed = true;
        }
        break;

      default:
        if (!unknown_warning_printed) {
          fprintf(stderr, "Unknown Clay render command: %d\n",
                  command->commandType);
          unknown_warning_printed = true;
        }
        break;
    }
  }

  clay_raylib_renderer_clear_overlay();
}
