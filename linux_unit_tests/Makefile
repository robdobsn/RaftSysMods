# RaftSysMods host-side tests: SysMod logic that doesn't need ESP32 hardware (e.g. BLEManager's connection arbiter)
#
# Built against RaftCore for its utilities: by default the RaftCore checkout next to this one (../../RaftCore, as in
# a Raft development folder); RAFTCORE=<path> for another.
#
#   make check      build and run the tests (exit status non-zero if any fail)

CC = g++
RAFTCORE ?= ../../RaftCore
CORE = $(RAFTCORE)/components/core
SYSMODS = ../components

CFLAGS = -Wall -Wextra -std=c++20 -g -DRAFT_CORE

INCLUDES = -I. -I$(SYSMODS)/BLEManager \
  -I$(CORE)/ArduinoUtils -I$(CORE)/Utils -I$(CORE)/RaftJson

SOURCES = main.cpp test_ble_conn_arbiter.cpp

OBJDIR = obj
OBJECTS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(notdir $(SOURCES)))

OUTPUT = linux_unit_tests

.PHONY: all check clean

all: $(OUTPUT)

check: $(OUTPUT)
	./$(OUTPUT)

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OUTPUT): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(OUTPUT) $(OBJECTS)

clean:
	rm -rf $(OUTPUT) $(OBJDIR)
