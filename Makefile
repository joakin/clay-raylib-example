APP := clay_counter
SRC := src/main.c

CC := clang
EMCC := emcc

RAYLIB_VERSION := 5.5
RAYLIB_ARCHIVE := vendor/raylib-$(RAYLIB_VERSION).tar.gz
RAYLIB_DIR := vendor/raylib-$(RAYLIB_VERSION)
RAYLIB_SRC := $(RAYLIB_DIR)/src
RAYLIB_STAMP := $(RAYLIB_DIR)/.unpacked
RAYLIB_WEB_LIB := build/web/libraylib.a

EM_CACHE ?= $(CURDIR)/build/emscripten-cache

CFLAGS := -std=c11 -Wall -Wextra -pedantic -Iinclude
NATIVE_CFLAGS := $(shell pkg-config --cflags raylib)
NATIVE_LIBS := $(shell pkg-config --libs raylib) -lm

WEB_CFLAGS := -Os -Wall -Iinclude -I$(RAYLIB_SRC) -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES2
WEB_LDFLAGS := -sUSE_GLFW=3 -sASSERTIONS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0 --shell-file web/shell.html -lm

.PHONY: all native web clean run serve compile-flags.txt

all: native web

native: compile_flags.txt build/native/$(APP)

web: build/web/index.html

run: native
	./build/native/$(APP)

serve: web
	emrun --no_browser --port 8080 build/web

compile-flags.txt:
	printf '%s\n' $(CFLAGS) $(NATIVE_CFLAGS) > compile_flags.txt

compile_flags.txt: Makefile
	printf '%s\n' $(CFLAGS) $(NATIVE_CFLAGS) > $@

build/native/$(APP): $(SRC) include/clay.h | build/native
	$(CC) $(CFLAGS) $(NATIVE_CFLAGS) $(SRC) -o $@ $(NATIVE_LIBS)

build/web/index.html: $(SRC) include/clay.h web/shell.html $(RAYLIB_WEB_LIB) | build/web
	EM_CACHE=$(EM_CACHE) $(EMCC) $(WEB_CFLAGS) $(SRC) $(RAYLIB_WEB_LIB) -o $@ $(WEB_LDFLAGS)

$(RAYLIB_WEB_LIB): $(RAYLIB_STAMP) | build/web
	EM_CACHE=$(EM_CACHE) $(MAKE) -C $(RAYLIB_SRC) PLATFORM=PLATFORM_WEB GRAPHICS=GRAPHICS_API_OPENGL_ES2 RAYLIB_LIBTYPE=STATIC
	cp $(RAYLIB_SRC)/libraylib.a $@

$(RAYLIB_STAMP): $(RAYLIB_ARCHIVE) | vendor
	tar -xzf $(RAYLIB_ARCHIVE) -C vendor
	touch $@

$(RAYLIB_ARCHIVE): | vendor
	curl -L https://github.com/raysan5/raylib/archive/refs/tags/$(RAYLIB_VERSION).tar.gz -o $@

build/native build/web vendor:
	mkdir -p $@

clean:
	rm -rf build
