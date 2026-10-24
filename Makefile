# Makefile
CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
CFLAGS   ?= -O2 -Wall -Wextra
LDFLAGS  ?=

FLTK_CFLAGS := $(shell fltk-config --cxxflags)
FLTK_LDFLAGS:= $(shell fltk-config --ldflags)

CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null || echo "-I/usr/include")
CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null || echo "-lcurl")

CXXFLAGS += $(FLTK_CFLAGS) $(CURL_CFLAGS) -Isrc -I. -Isrc
CFLAGS   += $(CURL_CFLAGS) -Isrc -I. -Isrc
LDFLAGS  += $(FLTK_LDFLAGS) -lfltk_images $(CURL_LIBS) -lpthread -ldl -lm

CXX_SRCS := src/app.cpp src/themes.cpp src/views.cpp src/ui.cpp src/player.cpp src/backend.cpp src/parse.cpp src/data.cpp
C_SRCS   := src/engine.c src/ws.c src/stream.c src/hash.c src/cJSON.c

CXX_OBJS := $(CXX_SRCS:src/%.cpp=build/%.o)
C_OBJS   := $(C_SRCS:src/%.c=build/%.o)
OBJS     := $(CXX_OBJS) $(C_OBJS)

TARGET   := build/ytmusic-fltk

.PHONY: all clean install run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

install: all
	install -d $(DESTDIR)/usr/local/bin
	install -d $(DESTDIR)/usr/local/share/ytmusic-fltk
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/ytmusic-fltk
	cp -r plugins js $(DESTDIR)/usr/local/share/ytmusic-fltk/

run: all
	YTMUSIC_FLTK_SHARE=$(CURDIR) ./$(TARGET)

clean:
	rm -rf build
