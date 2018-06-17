# Make hacks
.INTERMEDIATE:

# Set target, configuration, version and destination folders

PLATFORM := $(shell uname -s)
ifneq ($(findstring MINGW,$(PLATFORM)),)
PLATFORM := windows32
USE_WINDRES := true
endif

ifeq ($(PLATFORM),Darwin)
DEFAULT := cocoa
else
DEFAULT := sdl
endif

default: $(DEFAULT)

ifeq ($(MAKECMDGOALS),)
MAKECMDGOALS := $(DEFAULT)
endif

VERSION := 0.10.1
export VERSION
CONF ?= debug

BIN := build/bin
OBJ := build/obj
BOOTROMS_DIR ?= $(BIN)/BootROMs

# Set tools

CC := clang
ifeq ($(PLATFORM),windows32)
# To force use of the Unix version instead of the Windows version
MKDIR := $(shell which mkdir)
else
MKDIR := mkdir
endif

ifeq ($(CONF),native_release)
override CONF := release
LDFLAGS += -march=native -mtune=native
CFLAGS += -march=native -mtune=native
endif

# Set compilation and linkage flags based on target, platform and configuration

CFLAGS += -Werror -Wall -std=gnu11 -D_GNU_SOURCE -DVERSION="$(VERSION)" -I. -D_USE_MATH_DEFINES
SDL_LDFLAGS := -lSDL2 -lGL
ifeq ($(PLATFORM),windows32)
CFLAGS += -IWindows
LDFLAGS += -lmsvcrt -lSDL2main -Wl,/MANIFESTFILE:NUL
SDL_LDFLAGS := -lSDL2 -lopengl32
else
LDFLAGS += -lc -lm
endif

ifeq ($(PLATFORM),Darwin)
SYSROOT := $(shell xcodebuild -sdk macosx -version Path 2> /dev/null)
CFLAGS += -F/Library/Frameworks
OCFLAGS += -x objective-c -fobjc-arc -Wno-deprecated-declarations -isysroot $(SYSROOT) -mmacosx-version-min=10.9
LDFLAGS += -framework AppKit -framework PreferencePanes -framework Carbon -framework QuartzCore -weak_framework Metal -weak_framework MetalKit
SDL_LDFLAGS := -F/Library/Frameworks -framework SDL2 -framework OpenGL
endif
CFLAGS += -Wno-deprecated-declarations
ifeq ($(PLATFORM),windows32)
CFLAGS += -Wno-deprecated-declarations # Seems like Microsoft deprecated every single LIBC function
LDFLAGS += -Wl,/NODEFAULTLIB:libcmt.lib
endif

ifeq ($(CONF),debug)
CFLAGS += -g
else ifeq ($(CONF), release)
CFLAGS += -O3 -DNDEBUG
ifneq ($(PLATFORM),windows32)
LDFLAGS += -flto
CFLAGS += -flto
endif
else
$(error Invalid value for CONF: $(CONF). Use "debug", "release" or "native_release")
endif

# Define our targets

ifeq ($(PLATFORM),windows32)
SDL_TARGET := $(BIN)/SDL/sameboy.exe $(BIN)/SDL/sameboy_debugger.exe $(BIN)/SDL/SDL2.dll
TESTER_TARGET := $(BIN)/tester/sameboy_tester.exe
else
SDL_TARGET := $(BIN)/SDL/sameboy
TESTER_TARGET := $(BIN)/tester/sameboy_tester
endif

cocoa: $(BIN)/SameBoy.app
quicklook: $(BIN)/SameBoy.qlgenerator
sdl: $(SDL_TARGET) $(BIN)/SDL/dmg_boot.bin $(BIN)/SDL/cgb_boot.bin $(BIN)/SDL/agb_boot.bin $(BIN)/SDL/LICENSE $(BIN)/SDL/registers.sym $(BIN)/SDL/background.bmp $(BIN)/SDL/Shaders
bootroms: $(BIN)/BootROMs/agb_boot.bin $(BIN)/BootROMs/cgb_boot.bin $(BIN)/BootROMs/dmg_boot.bin
tester: $(TESTER_TARGET) $(BIN)/tester/dmg_boot.bin $(BIN)/tester/cgb_boot.bin $(BIN)/tester/agb_boot.bin
all: cocoa sdl tester libretro

# Get a list of our source files and their respective object file targets

CORE_SOURCES := $(shell ls Core/*.c)
SDL_SOURCES := $(shell ls SDL/*.c)
TESTER_SOURCES := $(shell ls Tester/*.c)

ifeq ($(PLATFORM),Darwin)
COCOA_SOURCES := $(shell ls Cocoa/*.m) $(shell ls HexFiend/*.m)
QUICKLOOK_SOURCES := $(shell ls QuickLook/*.m) $(shell ls QuickLook/*.c)
endif

ifeq ($(PLATFORM),windows32)
CORE_SOURCES += $(shell ls Windows/*.c)
endif

CORE_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(CORE_SOURCES))
COCOA_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(COCOA_SOURCES))
QUICKLOOK_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(QUICKLOOK_SOURCES))
SDL_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(SDL_SOURCES))
TESTER_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(TESTER_SOURCES))

# Automatic dependency generation

ifneq ($(filter-out clean bootroms libretro %.bin, $(MAKECMDGOALS)),)
-include $(CORE_OBJECTS:.o=.dep)
ifneq ($(filter $(MAKECMDGOALS),sdl),)
-include $(SDL_OBJECTS:.o=.dep)
endif
ifneq ($(filter $(MAKECMDGOALS),tester),)
-include $(TESTER_OBJECTS:.o=.dep)
endif
ifneq ($(filter $(MAKECMDGOALS),cocoa),)
-include $(COCOA_OBJECTS:.o=.dep)
endif
endif

$(OBJ)/%.dep: %
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -MT $(OBJ)/$^.o -M $^ -c -o $@

# Compilation rules

$(OBJ)/Core/%.c.o: Core/%.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -DGB_INTERNAL -c $< -o $@

$(OBJ)/%.c.o: %.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	
# HexFiend requires more flags
$(OBJ)/HexFiend/%.m.o: HexFiend/%.m
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(OCFLAGS) -c $< -o $@ -fno-objc-arc -include HexFiend/HexFiend_2_Framework_Prefix.pch
	
$(OBJ)/%.m.o: %.m
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(OCFLAGS) -c $< -o $@

# Cocoa Port

$(BIN)/SameBoy.app: $(BIN)/SameBoy.app/Contents/MacOS/SameBoy \
                    $(shell ls Cocoa/*.icns) \
                    Cocoa/License.html \
                    Cocoa/Info.plist \
                    Misc/registers.sym \
                    $(BIN)/SameBoy.app/Contents/Resources/dmg_boot.bin \
                    $(BIN)/SameBoy.app/Contents/Resources/cgb_boot.bin \
                    $(BIN)/SameBoy.app/Contents/Resources/agb_boot.bin \
                    $(patsubst %.xib,%.nib,$(addprefix $(BIN)/SameBoy.app/Contents/Resources/Base.lproj/,$(shell cd Cocoa;ls *.xib))) \
                    $(BIN)/SameBoy.qlgenerator \
                    Shaders
	$(MKDIR) -p $(BIN)/SameBoy.app/Contents/Resources
	cp Cocoa/*.icns Misc/registers.sym $(BIN)/SameBoy.app/Contents/Resources/
	sed s/@VERSION/$(VERSION)/ < Cocoa/Info.plist > $(BIN)/SameBoy.app/Contents/Info.plist
	cp Cocoa/License.html $(BIN)/SameBoy.app/Contents/Resources/Credits.html
	$(MKDIR) -p $(BIN)/SameBoy.app/Contents/Resources/Shaders
	cp Shaders/*.fsh Shaders/*.metal $(BIN)/SameBoy.app/Contents/Resources/Shaders
	$(MKDIR) -p $(BIN)/SameBoy.app/Contents/Library/QuickLook/
	cp -rf $(BIN)/SameBoy.qlgenerator $(BIN)/SameBoy.app/Contents/Library/QuickLook/

$(BIN)/SameBoy.app/Contents/MacOS/SameBoy: $(CORE_OBJECTS) $(COCOA_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) -framework OpenGL -framework AudioUnit -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework IOKit
ifeq ($(CONF), release)
	strip $@
endif

$(BIN)/SameBoy.app/Contents/Resources/Base.lproj/%.nib: Cocoa/%.xib
	ibtool --compile $@ $^
	
# Quick Look generator

$(BIN)/SameBoy.qlgenerator: $(BIN)/SameBoy.qlgenerator/Contents/MacOS/SameBoyQL \
                            $(shell ls QuickLook/*.png) \
                            QuickLook/Info.plist \
                            $(BIN)/SameBoy.qlgenerator/Contents/Resources/cgb_boot_fast.bin
	$(MKDIR) -p $(BIN)/SameBoy.qlgenerator/Contents/Resources
	cp QuickLook/*.png $(BIN)/SameBoy.qlgenerator/Contents/Resources/
	sed s/@VERSION/$(VERSION)/ < QuickLook/Info.plist > $(BIN)/SameBoy.qlgenerator/Contents/Info.plist

# Currently, SameBoy.app includes two "copies" of each Core .o file once in the app itself and
# once in the QL Generator. It should probably become a dylib instead.
$(BIN)/SameBoy.qlgenerator/Contents/MacOS/SameBoyQL: $(CORE_OBJECTS) $(QUICKLOOK_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) -bundle -framework Cocoa -framework Quicklook
ifeq ($(CONF), release)
	strip -u -r -s QuickLook/exports.sym $@
endif

# cgb_boot_fast.bin is not a standard boot ROM, we don't expect it to exist in the user-provided
# boot ROM directory.
$(BIN)/SameBoy.qlgenerator/Contents/Resources/cgb_boot_fast.bin: $(BIN)/BootROMs/cgb_boot_fast.bin
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@
	
# SDL Port

# Unix versions build only one binary
$(BIN)/SDL/sameboy: $(CORE_OBJECTS) $(SDL_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS)
ifeq ($(CONF), release)
	strip $@
endif

# Windows version builds two, one with a conole and one without it
$(BIN)/SDL/sameboy.exe: $(CORE_OBJECTS) $(SDL_OBJECTS) $(OBJ)/Windows/resources.o
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS) -Wl,/subsystem:windows

$(BIN)/SDL/sameboy_debugger.exe: $(CORE_OBJECTS) $(SDL_OBJECTS) $(OBJ)/Windows/resources.o
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS) -Wl,/subsystem:console

ifneq ($(USE_WINDRES),)
$(OBJ)/%.o: %.rc
	-@$(MKDIR) -p $(dir $@)
	windres --preprocessor cpp -DVERSION=\"$(VERSION)\" $^ $@
else
$(OBJ)/%.res: %.rc
	-@$(MKDIR) -p $(dir $@)
	rc /fo $@ /dVERSION=\"$(VERSION)\" $^ 

%.o: %.res
	cvtres /OUT:"$@" $^
endif

# We must provide SDL2.dll with the Windows port.
$(BIN)/SDL/SDL2.dll:
	@$(eval MATCH := $(shell where $$LIB:SDL2.dll))
	cp "$(MATCH)" $@

# Tester

$(BIN)/tester/sameboy_tester: $(CORE_OBJECTS) $(TESTER_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS)
ifeq ($(CONF), release)
	strip $@
endif

$(BIN)/tester/sameboy_tester.exe: $(CORE_OBJECTS) $(SDL_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) -Wl,/subsystem:console

$(BIN)/SDL/%.bin $(BIN)/tester/%.bin: $(BOOTROMS_DIR)/%.bin
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@
	
$(BIN)/SameBoy.app/Contents/Resources/%.bin: $(BOOTROMS_DIR)/%.bin
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@
	
$(BIN)/SDL/LICENSE: LICENSE
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(BIN)/SDL/registers.sym: Misc/registers.sym
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@
	
$(BIN)/SDL/background.bmp: SDL/background.bmp
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(BIN)/SDL/Shaders: Shaders
	-@$(MKDIR) -p $@
	cp -rf Shaders/*.fsh $@

# Boot ROMs

$(BIN)/BootROMs/%.bin: BootROMs/%.asm
	-@$(MKDIR) -p $(dir $@)
	cd BootROMs && rgbasm -o ../$@.tmp ../$<
	rgblink -o $@.tmp2 $@.tmp
	head -c $(if $(findstring dmg,$@), 256, 2304) $@.tmp2 > $@
	@rm $@.tmp $@.tmp2

# Libretro Core (uses its own build system)
libretro:
	$(MAKE) -C libretro
	
# Clean
clean:
	rm -rf build

.PHONY: libretro