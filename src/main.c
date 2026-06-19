#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc11-extensions"
#pragma clang diagnostic ignored "-Wc23-extensions"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wgnu-empty-initializer"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#endif
#define CLAY_IMPLEMENTATION
#include "clay.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

typedef struct AppMetrics {
  float   logical_width;
  float   logical_height;
  int32_t render_width;
  int32_t render_height;
  float   scale_x;
  float   scale_y;
} AppMetrics;

static const Clay_Color COLOR_BACKGROUND    = {246, 248, 252, 255};
static const Clay_Color COLOR_PANEL         = {255, 255, 255, 255};
static const Clay_Color COLOR_PANEL_BORDER  = {210, 216, 226, 255};
static const Clay_Color COLOR_TEXT          = {29, 36, 48, 255};
static const Clay_Color COLOR_MUTED         = {92, 103, 118, 255};
static const Clay_Color COLOR_BUTTON        = {30, 122, 108, 255};
static const Clay_Color COLOR_BUTTON_HOVER  = {37, 145, 128, 255};
static const Clay_Color COLOR_BUTTON_BORDER = {17, 88, 78, 255};
static const Clay_Color COLOR_WHITE         = {255, 255, 255, 255};

static RenderTexture2D  render_target       = {0};
static Clay_ElementId   button_id           = {0};
static Clay_BoundingBox button_bounds       = {0};
static bool             button_bounds_ready = false;
static int32_t          counter             = 0;
static char             counter_text[64]    = {0};

static uint8_t color_byte(float value) {
  if (value < 0.0f) return 0;
  if (value > 255.0f) return 255;
  return (uint8_t)(value + 0.5f);
}

static Color to_raylib_color(Clay_Color color) {
  return (Color){color_byte(color.r), color_byte(color.g), color_byte(color.b),
                 color_byte(color.a)};
}

static Rectangle scale_box(Clay_BoundingBox box, float scale_x, float scale_y) {
  return (Rectangle){box.x * scale_x, box.y * scale_y, box.width * scale_x,
                     box.height * scale_y};
}

static float rounded_rect_amount(Rectangle rect, Clay_CornerRadius radius,
                                 float scale_x, float scale_y) {
  float scaled_radius =
      fmaxf(radius.topLeft * scale_x, radius.topLeft * scale_y);
  float smallest_side = fminf(rect.width, rect.height);

  if ((scaled_radius <= 0.0f) || (smallest_side <= 0.0f)) return 0.0f;

  float roundness = (scaled_radius * 2.0f) / smallest_side;
  return fminf(roundness, 1.0f);
}

static void copy_clay_text(Clay_StringSlice text, char* buffer,
                           size_t capacity) {
  int32_t length = text.length;
  if (length < 0) length = 0;
  if ((size_t)length >= capacity) length = (int32_t)capacity - 1;

  memcpy(buffer, text.chars, (size_t)length);
  buffer[length] = '\0';
}

static float text_spacing(uint16_t font_size, uint16_t letter_spacing) {
  if (letter_spacing > 0) return (float)letter_spacing;
  return fmaxf(1.0f, (float)font_size * 0.08f);
}

static Clay_Dimensions measure_clay_text(Clay_StringSlice        text,
                                         Clay_TextElementConfig* config,
                                         void*                   user_data) {
  (void)user_data;

  char buffer[1024];
  copy_clay_text(text, buffer, sizeof(buffer));

  Font    font = GetFontDefault();
  Vector2 size =
      MeasureTextEx(font, buffer, (float)config->fontSize,
                    text_spacing(config->fontSize, config->letterSpacing));

  return (Clay_Dimensions){size.x, size.y};
}

static void handle_clay_error(Clay_ErrorData error_data) {
  fprintf(stderr, "Clay error: %.*s\n", error_data.errorText.length,
          error_data.errorText.chars);
}

static AppMetrics get_app_metrics(void) {
#ifdef PLATFORM_WEB
  EM_ASM({
    const canvas = Module.canvas || document.getElementById("canvas");
    const width =
        window.innerWidth || document.documentElement.clientWidth || 960;
    const height =
        window.innerHeight || document.documentElement.clientHeight || 540;
    canvas.style.width  = width + "px";
    canvas.style.height = height + "px";
  });

  double css_width  = 0.0;
  double css_height = 0.0;
  emscripten_get_element_css_size("#canvas", &css_width, &css_height);

  if (css_width <= 0.0) css_width = 960.0;
  if (css_height <= 0.0) css_height = 540.0;

  double dpi = emscripten_get_device_pixel_ratio();
  if (dpi < 1.0) dpi = 1.0;

  int32_t render_width  = (int32_t)ceil(css_width * dpi);
  int32_t render_height = (int32_t)ceil(css_height * dpi);

  if (render_width < 1) render_width = 1;
  if (render_height < 1) render_height = 1;

  if ((GetScreenWidth() != render_width) ||
      (GetScreenHeight() != render_height)) {
    SetWindowSize(render_width, render_height);
    emscripten_set_canvas_element_size("#canvas", render_width, render_height);
    emscripten_set_element_css_size("#canvas", css_width, css_height);
  }

  return (AppMetrics){(float)css_width,
                      (float)css_height,
                      render_width,
                      render_height,
                      (float)((double)render_width / css_width),
                      (float)((double)render_height / css_height)};
#else
  int32_t logical_width  = GetScreenWidth();
  int32_t logical_height = GetScreenHeight();
  int32_t render_width   = GetRenderWidth();
  int32_t render_height  = GetRenderHeight();

  if (logical_width < 1) logical_width = 1;
  if (logical_height < 1) logical_height = 1;
  if (render_width < 1) render_width = logical_width;
  if (render_height < 1) render_height = logical_height;

  return (AppMetrics){(float)logical_width,
                      (float)logical_height,
                      render_width,
                      render_height,
                      (float)render_width / (float)logical_width,
                      (float)render_height / (float)logical_height};
#endif
}

static Vector2 get_logical_mouse(AppMetrics metrics) {
  Vector2 mouse = GetMousePosition();

#ifdef PLATFORM_WEB
  mouse.x /= metrics.scale_x;
  mouse.y /= metrics.scale_y;
#else
  (void)metrics;
#endif

  return mouse;
}

static bool point_in_box(Vector2 point, Clay_BoundingBox box) {
  return (point.x >= box.x) && (point.x <= (box.x + box.width)) &&
         (point.y >= box.y) && (point.y <= (box.y + box.height));
}

static void ensure_render_target(AppMetrics metrics) {
  if ((render_target.id != 0) &&
      ((render_target.texture.width != metrics.render_width) ||
       (render_target.texture.height != metrics.render_height))) {
    UnloadRenderTexture(render_target);
    render_target = (RenderTexture2D){0};
  }

  if (render_target.id == 0) {
    render_target =
        LoadRenderTexture(metrics.render_width, metrics.render_height);
    SetTextureFilter(render_target.texture, TEXTURE_FILTER_BILINEAR);
  }
}

static void draw_clay_text(Clay_TextRenderData text, Clay_BoundingBox box,
                           float scale_x, float scale_y) {
  char buffer[1024];
  copy_clay_text(text.stringContents, buffer, sizeof(buffer));

  DrawTextEx(GetFontDefault(), buffer,
             (Vector2){box.x * scale_x, box.y * scale_y},
             (float)text.fontSize * scale_y,
             text_spacing(text.fontSize, text.letterSpacing) * scale_x,
             to_raylib_color(text.textColor));
}

static void draw_clay_border(Clay_BorderRenderData border, Rectangle rect,
                             float scale_x, float scale_y) {
  Color            color = to_raylib_color(border.color);
  Clay_BorderWidth width = border.width;

  if ((width.left == width.right) && (width.left == width.top) &&
      (width.left == width.bottom)) {
    float roundness =
        rounded_rect_amount(rect, border.cornerRadius, scale_x, scale_y);
    DrawRectangleRoundedLinesEx(rect, roundness, 16,
                                (float)width.left * scale_x, color);
    return;
  }

  if (width.left > 0)
    DrawRectangleRec(
        (Rectangle){rect.x, rect.y, (float)width.left * scale_x, rect.height},
        color);
  if (width.right > 0)
    DrawRectangleRec(
        (Rectangle){rect.x + rect.width - ((float)width.right * scale_x),
                    rect.y, (float)width.right * scale_x, rect.height},
        color);
  if (width.top > 0)
    DrawRectangleRec(
        (Rectangle){rect.x, rect.y, rect.width, (float)width.top * scale_y},
        color);
  if (width.bottom > 0)
    DrawRectangleRec(
        (Rectangle){rect.x,
                    rect.y + rect.height - ((float)width.bottom * scale_y),
                    rect.width, (float)width.bottom * scale_y},
        color);
}

static void render_clay_commands(Clay_RenderCommandArray commands,
                                 AppMetrics              metrics) {
  for (int32_t i = 0; i < commands.length; i++) {
    Clay_RenderCommand* command = &commands.internalArray[i];
    Rectangle           rect =
        scale_box(command->boundingBox, metrics.scale_x, metrics.scale_y);

    switch (command->commandType) {
      case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
        float roundness = rounded_rect_amount(
            rect, command->renderData.rectangle.cornerRadius, metrics.scale_x,
            metrics.scale_y);
        Color color =
            to_raylib_color(command->renderData.rectangle.backgroundColor);

        if (roundness > 0.0f)
          DrawRectangleRounded(rect, roundness, 16, color);
        else
          DrawRectangleRec(rect, color);
      } break;

      case CLAY_RENDER_COMMAND_TYPE_BORDER: {
        draw_clay_border(command->renderData.border, rect, metrics.scale_x,
                         metrics.scale_y);
      } break;

      case CLAY_RENDER_COMMAND_TYPE_TEXT: {
        draw_clay_text(command->renderData.text, command->boundingBox,
                       metrics.scale_x, metrics.scale_y);
      } break;

      case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
        BeginScissorMode((int)rect.x, (int)rect.y, (int)rect.width,
                         (int)rect.height);
      } break;

      case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
        EndScissorMode();
      } break;

      default:
        fprintf(stderr, "Unsupported clay render command: %d",
                command->commandType);
        break;
    }
  }
}

static void build_counter_ui(void) {
  snprintf(counter_text, sizeof(counter_text), "Count: %" PRId32, counter);
  Clay_String count_string = {.isStaticallyAllocated = false,
                              .length = (int32_t)strlen(counter_text),
                              .chars  = counter_text};

  CLAY(
      CLAY_ID("Root"),
      {.layout = {.sizing         = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                  .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
       .backgroundColor = COLOR_BACKGROUND}) {
    CLAY(CLAY_ID("CounterPanel"),
         {.layout = {.sizing   = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIT(0)},
                     .padding  = CLAY_PADDING_ALL(24),
                     .childGap = 18,
                     .childAlignment  = {CLAY_ALIGN_X_CENTER,
                                         CLAY_ALIGN_Y_CENTER},
                     .layoutDirection = CLAY_TOP_TO_BOTTOM},
          .backgroundColor = COLOR_PANEL,
          .cornerRadius    = CLAY_CORNER_RADIUS(18),
          .border          = {.color = COLOR_PANEL_BORDER,
                              .width = CLAY_BORDER_OUTSIDE(1)}}) {
      CLAY_TEXT(CLAY_STRING("Clay + Raylib"),
                {.textColor = COLOR_MUTED, .fontSize = 20});

      CLAY_TEXT(count_string, {.textColor = COLOR_TEXT, .fontSize = 40});

      CLAY(
          button_id,
          {.layout = {.sizing = {CLAY_SIZING_FIXED(220), CLAY_SIZING_FIXED(56)},
                      .childAlignment = {CLAY_ALIGN_X_CENTER,
                                         CLAY_ALIGN_Y_CENTER}},
           .backgroundColor =
               Clay_Hovered() ? COLOR_BUTTON_HOVER : COLOR_BUTTON,
           .cornerRadius = CLAY_CORNER_RADIUS(12),
           .border       = {.color = COLOR_BUTTON_BORDER,
                            .width = CLAY_BORDER_OUTSIDE(1)}}) {
        CLAY_TEXT(CLAY_STRING("Increment"),
                  {.textColor = COLOR_WHITE, .fontSize = 22});
      }
    }
  }
}

static void frame(void) {
  AppMetrics metrics = get_app_metrics();
  ensure_render_target(metrics);

  float   delta_time = GetFrameTime();
  Vector2 mouse      = get_logical_mouse(metrics);

  if (button_bounds_ready && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
      point_in_box(mouse, button_bounds))
    counter++;

  Clay_SetLayoutDimensions(
      (Clay_Dimensions){metrics.logical_width, metrics.logical_height});
  Clay_SetPointerState((Clay_Vector2){mouse.x, mouse.y},
                       IsMouseButtonDown(MOUSE_BUTTON_LEFT));
  Clay_UpdateScrollContainers(true, (Clay_Vector2){0.0f, GetMouseWheelMove()},
                              delta_time);

  Clay_BeginLayout();
  build_counter_ui();
  Clay_RenderCommandArray commands = Clay_EndLayout(delta_time);

  Clay_ElementData button_data = Clay_GetElementData(button_id);
  button_bounds_ready          = button_data.found;
  if (button_bounds_ready) button_bounds = button_data.boundingBox;

  BeginDrawing();

  BeginTextureMode(render_target);
  ClearBackground(to_raylib_color(COLOR_BACKGROUND));
  render_clay_commands(commands, metrics);
  EndTextureMode();

  ClearBackground(BLACK);
  DrawTexturePro(render_target.texture,
                 (Rectangle){0.0f, 0.0f, (float)render_target.texture.width,
                             (float)-render_target.texture.height},
#ifdef PLATFORM_WEB
                 (Rectangle){0.0f, 0.0f, (float)metrics.render_width,
                             (float)metrics.render_height},
#else
                 (Rectangle){0.0f, 0.0f, metrics.logical_width,
                             metrics.logical_height},
#endif
                 (Vector2){0.0f, 0.0f}, 0.0f, WHITE);

  EndDrawing();
}

int main(void) {
  button_id = CLAY_ID("CounterButton");

  uint32_t flags = FLAG_VSYNC_HINT;
#ifndef PLATFORM_WEB
  flags |= FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI;
#endif
  SetConfigFlags(flags);

  InitWindow(960, 540, "Clay Counter");

#ifndef PLATFORM_WEB
  ToggleBorderlessWindowed();
#endif

  SetTargetFPS(60);

  uint64_t   clay_memory_size = Clay_MinMemorySize();
  Clay_Arena arena            = Clay_CreateArenaWithCapacityAndMemory(
      clay_memory_size, malloc(clay_memory_size));
  Clay_Initialize(
      arena,
      (Clay_Dimensions){(float)GetScreenWidth(), (float)GetScreenHeight()},
      (Clay_ErrorHandler){.errorHandlerFunction = handle_clay_error,
                          .userData             = NULL});
  Clay_SetMeasureTextFunction(measure_clay_text, NULL);

#ifdef PLATFORM_WEB
  emscripten_set_main_loop(frame, 0, 1);
#else
  while (!WindowShouldClose()) frame();

  if (render_target.id != 0) UnloadRenderTexture(render_target);
  CloseWindow();
#endif

  return 0;
}
