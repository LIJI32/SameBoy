# Make hacks
.INTERMEDIATE:

# Set target, configuration, version and destination folders

PLATFORM := $(shell uname -s)
ifneq ($(findstring MINGW,$(PLATFORM)),)
PLATFORM := windows32
USE_WINDRES := true
endif

ifneq ($(findstring MSYS,$(PLATFORM)),)
PLATFORM := windows32
endif

ifeq ($(PLATFORM),windows32)
_ := $(shell chcp 65001)
EXESUFFIX:=.exe
NATIVE_CC = clang -IWindows -Wno-deprecated-declarations --target=x86_64-pc-windows
SDL_AUDIO_DRIVERS ?= xaudio2 sdl
else
EXESUFFIX:=
NATIVE_CC := cc
SDL_AUDIO_DRIVERS ?= sdl
endif

PB12_COMPRESS := build/pb12$(EXESUFFIX)

ifeq ($(PLATFORM),Darwin)
DEFAULT := cocoa
ENABLE_OPENAL ?= 1
else
DEFAULT := sdl
endif

NULL := /dev/null
ifeq ($(PLATFORM),windows32)
NULL := NUL
endif

ifneq ($(shell which xdg-open 2> $(NULL))$(FREEDESKTOP),)
# Running on an FreeDesktop environment, configure for (optional) installation
DESTDIR ?= 
PREFIX ?= /usr/local
DATA_DIR ?= $(PREFIX)/share/sameboy/
FREEDESKTOP ?= true
endif

default: $(DEFAULT)

ifeq ($(MAKECMDGOALS),)
MAKECMDGOALS := $(DEFAULT)
endif

ifneq ($(DISABLE_TIMEKEEPING),)
CFLAGS += -DGB_DISABLE_TIMEKEEPING
CPPP_FLAGS += -DGB_DISABLE_TIMEKEEPING
else
CPPP_FLAGS += -UGB_DISABLE_TIMEKEEPING
endif

ifneq ($(DISABLE_REWIND),)
CFLAGS += -DGB_DISABLE_REWIND
CPPP_FLAGS += -DGB_DISABLE_REWIND
CORE_FILTER += Core/rewind.c
else
CPPP_FLAGS += -UGB_DISABLE_REWIND
endif

ifneq ($(DISABLE_DEBUGGER),)
CFLAGS += -DGB_DISABLE_DEBUGGER
CPPP_FLAGS += -DGB_DISABLE_DEBUGGER
CORE_FILTER += Core/debugger.c Core/sm83_disassembler.c Core/symbol_hash.c
else
CPPP_FLAGS += -UGB_DISABLE_DEBUGGER
endif

ifneq ($(DISABLE_CHEATS),)
CFLAGS += -DGB_DISABLE_CHEATS
CPPP_FLAGS += -DGB_DISABLE_CHEATS
CORE_FILTER += Core/cheats.c
else
CPPP_FLAGS += -UGB_DISABLE_CHEATS
endif

ifneq ($(CORE_FILTER)$(DISABLE_TIMEKEEPING),)
ifneq ($(MAKECMDGOALS),lib)
$(error SameBoy features can only be disabled when compiling the 'lib' target)
endif
endif

CPPP_FLAGS += -UGB_INTERNAL


include version.mk
COPYRIGHT_YEAR := $(shell grep -oE "20[2-9][0-9]" LICENSE)
export VERSION
CONF ?= debug

BIN := build/bin
OBJ := build/obj
INC := build/include/sameboy
LIBDIR := build/lib

BOOTROMS_DIR ?= $(BIN)/BootROMs

ifdef DATA_DIR
CFLAGS += -DDATA_DIR="\"$(DATA_DIR)\""
endif

# Set tools

# Use clang if it's available.
ifeq ($(origin CC),default)
ifneq (, $(shell which clang 2> $(NULL)))
CC := clang 
endif
endif

# Find libraries with pkg-config if available.
ifneq (, $(shell which pkg-config 2> $(NULL)))
# But not on macOS, it's annoying
ifneq ($(PLATFORM),Darwin)
PKG_CONFIG := pkg-config
endif
endif

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

ifeq ($(CONF),fat_release)
override CONF := release
FAT_FLAGS += -arch x86_64 -arch arm64
endif

IOS_MIN := 11.0

IOS_PNGS := $(shell ls iOS/*.png)
# Support out-of-PATH RGBDS
RGBASM  := $(RGBDS)rgbasm
RGBLINK := $(RGBDS)rgblink
RGBGFX  := $(RGBDS)rgbgfx

# RGBASM 0.7+ deprecate and remove `-h`
RGBASM_FLAGS := $(if $(filter $(shell echo 'println __RGBDS_MAJOR__ || (!__RGBDS_MAJOR__ && __RGBDS_MINOR__ > 6)' | $(RGBASM) -), $$0), -h,) --include $(OBJ)/BootROMs/ --include BootROMs/
# RGBGFX 0.6+ replace `-h` with `-Z`, and need `-c embedded`
RGBGFX_FLAGS := $(if $(filter $(shell echo 'println __RGBDS_MAJOR__ || (!__RGBDS_MAJOR__ && __RGBDS_MINOR__ > 5)' | $(RGBASM) -), $$0), -h -u, -Z -u -c embedded)


# Set compilation and linkage flags based on target, platform and configuration

OPEN_DIALOG = OpenDialog/gtk.c

ifeq ($(PLATFORM),windows32)
OPEN_DIALOG = OpenDialog/windows.c
endif

ifeq ($(PLATFORM),Darwin)
OPEN_DIALOG = OpenDialog/cocoa.m
endif

# These must come before the -Wno- flags
WARNINGS += -Werror -Wall -Wno-unknown-warning -Wno-unknown-warning-option -Wno-missing-braces
WARNINGS += -Wno-nonnull -Wno-unused-result -Wno-multichar -Wno-int-in-bool-context -Wno-format-truncation

# Only add this flag if the compiler supports it
ifeq ($(shell $(CC) -x c -c $(NULL) -o $(NULL) -Werror -Wpartial-availability 2> $(NULL); echo $$?),0)
WARNINGS += -Wpartial-availability
endif

# GCC's implementation of this warning has false positives, so we skip it
ifneq ($(shell $(CC) --version 2>&1 | grep "gcc"), )
WARNINGS += -Wno-maybe-uninitialized
endif

CFLAGS += $(WARNINGS)

CFLAGS += -std=gnu11 -D_GNU_SOURCE -DGB_VERSION='"$(VERSION)"' -DGB_COPYRIGHT_YEAR='"$(COPYRIGHT_YEAR)"' -I. -D_USE_MATH_DEFINES
ifneq (,$(UPDATE_SUPPORT))
CFLAGS += -DUPDATE_SUPPORT
endif

ifeq (,$(PKG_CONFIG))
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LDFLAGS := $(shell sdl2-config --libs) -lpthread

# We cannot detect the presence of OpenAL dev headers,
# so we must do this manually
ifeq ($(ENABLE_OPENAL),1)
SDL_CFLAGS += -DENABLE_OPENAL
ifeq ($(PLATFORM),Darwin)
SDL_LDFLAGS += -framework OpenAL
else
SDL_LDFLAGS += -lopenal
endif
SDL_AUDIO_DRIVERS += openal
endif
else
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2) -lpthread

# Allow OpenAL to be disabled even if the development libraries are available
ifneq ($(ENABLE_OPENAL),0)
ifeq ($(shell $(PKG_CONFIG) --exists openal && echo 0),0)
SDL_CFLAGS += $(shell $(PKG_CONFIG) --cflags openal) -DENABLE_OPENAL
SDL_LDFLAGS += $(shell $(PKG_CONFIG) --libs openal)
SDL_AUDIO_DRIVERS += openal
endif
endif
endif

ifeq (,$(PKG_CONFIG))
GL_LDFLAGS := -lGL
else
GL_CFLAGS := $(shell $(PKG_CONFIG) --cflags gl)
GL_LDFLAGS := $(shell $(PKG_CONFIG) --libs gl || echo -lGL)
endif

ifeq ($(PLATFORM),windows32)
CFLAGS += -IWindows -Drandom=rand --target=x86_64-pc-windows
LDFLAGS += -lmsvcrt -lcomdlg32 -luser32 -lshell32 -lole32 -lSDL2main -Wl,/MANIFESTFILE:NUL --target=x86_64-pc-windows
SDL_LDFLAGS := -lSDL2
GL_LDFLAGS := -lopengl32
ifneq ($(REDIST_XAUDIO),)
CFLAGS += -DREDIST_XAUDIO
LDFLAGS += -lxaudio2_9redist
sdl: $(BIN)/SDL/xaudio2_9redist.dll
endif
else
LDFLAGS += -lc -lm -ldl
endif

ifeq ($(MAKECMDGOALS),_ios)
OBJ := build/obj-ios
SYSROOT := $(shell xcodebuild -sdk iphoneos -version Path 2> $(NULL))
ifeq ($(SYSROOT),)
$(error Could not find an iOS SDK)
endif
CFLAGS += -arch arm64 -miphoneos-version-min=$(IOS_MIN) -isysroot $(SYSROOT) -IAppleCommon -DGB_DISABLE_DEBUGGER
CORE_FILTER += Core/debugger.c Core/sm83_disassembler.c Core/symbol_hash.c
LDFLAGS += -arch arm64
OCFLAGS += -x objective-c -fobjc-arc -Wno-deprecated-declarations -isysroot $(SYSROOT)
LDFLAGS += -miphoneos-version-min=$(IOS_MIN)  -isysroot $(SYSROOT)
IOS_INSTALLER_LDFLAGS := $(LDFLAGS) -lobjc -framework CoreServices -framework Foundation
LDFLAGS += -lobjc -framework UIKit -framework Foundation -framework CoreGraphics -framework Metal -framework MetalKit -framework AudioToolbox -framework AVFoundation -framework QuartzCore -framework CoreMotion -framework CoreVideo -framework CoreMedia -framework CoreImage -framework UserNotifications -framework GameController -weak_framework CoreHaptics -framework MobileCoreServices
CODESIGN := codesign -fs -
else
ifeq ($(PLATFORM),Darwin)
SYSROOT := $(shell xcodebuild -sdk macosx -version Path 2> $(NULL))
ifeq ($(SYSROOT),)
SYSROOT := /Library/Developer/CommandLineTools/SDKs/$(shell ls /Library/Developer/CommandLineTools/SDKs/ | grep "[0-9]\." | tail -n 1)
endif
ifeq ($(SYSROOT),/Library/Developer/CommandLineTools/SDKs/)
$(error Could not find a macOS SDK)
endif

CFLAGS += -F/Library/Frameworks -mmacosx-version-min=10.9 -isysroot $(SYSROOT) -IAppleCommon
OCFLAGS += -x objective-c -fobjc-arc -Wno-deprecated-declarations -isysroot $(SYSROOT)
LDFLAGS += -framework AppKit -mmacosx-version-min=10.9 -isysroot $(SYSROOT)
GL_LDFLAGS := -framework OpenGL
endif
CFLAGS += -Wno-deprecated-declarations
ifeq ($(PLATFORM),windows32)
CFLAGS += -Wno-deprecated-declarations # Seems like Microsoft deprecated every single LIBC function
LDFLAGS += -Wl,/NODEFAULTLIB:libcmt.lib
endif
endif

LIBFLAGS := -nostdlib -Wl,-r
ifneq ($(PLATFORM),Darwin)
LIBFLAGS += -no-pie
endif

ifeq ($(CONF),debug)
CFLAGS += -g
else ifeq ($(CONF), release)
CFLAGS += -O3 -ffast-math -DNDEBUG
# The frontend code is not time-critical, prefer reducing the size for less memory use and better cache utilization 
ifeq ($(shell $(CC) -x c -c $(NULL) -o $(NULL) -Werror -Oz 2> $(NULL); echo $$?),0)
FRONTEND_CFLAGS += -Oz
else
FRONTEND_CFLAGS += -Os
endif

# Don't use function outlining. I breaks Obj-C ARC optimizations and Apple never bothered to fix it. It also hardly has any effect on file size.
ifeq ($(shell $(CC) -x c -c $(NULL) -o $(NULL) -Werror -mno-outline 2> $(NULL); echo $$?),0)
FRONTEND_CFLAGS += -mno-outline
LDFLAGS += -mno-outline
endif

STRIP := strip
CODESIGN := true
ifeq ($(PLATFORM),Darwin)
LDFLAGS += -Wl,-exported_symbols_list,$(NULL)
STRIP := strip -x
CODESIGN := codesign -fs -
endif
ifeq ($(PLATFORM),windows32)
LDFLAGS +=  -fuse-ld=lld
endif
LDFLAGS += -flto
CFLAGS += -flto
LDFLAGS += -Wno-lto-type-mismatch # For GCC's LTO

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
sdl: $(SDL_TARGET) $(BIN)/SDL/dmg_boot.bin $(BIN)/SDL/mgb_boot.bin $(BIN)/SDL/cgb0_boot.bin $(BIN)/SDL/cgb_boot.bin $(BIN)/SDL/agb_boot.bin $(BIN)/SDL/sgb_boot.bin $(BIN)/SDL/sgb2_boot.bin $(BIN)/SDL/LICENSE $(BIN)/SDL/registers.sym $(BIN)/SDL/background.bmp $(BIN)/SDL/Shaders $(BIN)/SDL/Palettes
bootroms: $(BIN)/BootROMs/agb_boot.bin $(BIN)/BootROMs/cgb_boot.bin $(BIN)/BootROMs/cgb0_boot.bin $(BIN)/BootROMs/dmg_boot.bin $(BIN)/BootROMs/mgb_boot.bin $(BIN)/BootROMs/sgb_boot.bin $(BIN)/BootROMs/sgb2_boot.bin
tester: $(TESTER_TARGET) $(BIN)/tester/dmg_boot.bin $(BIN)/tester/cgb_boot.bin $(BIN)/tester/agb_boot.bin $(BIN)/tester/sgb_boot.bin $(BIN)/tester/sgb2_boot.bin
_ios: $(BIN)/SameBoy-iOS.app $(OBJ)/installer
ios-ipa: $(BIN)/SameBoy-iOS.ipa
ios-deb: $(BIN)/SameBoy-iOS.deb
ifeq ($(PLATFORM),windows32)
lib: lib-unsupported
else
lib: $(LIBDIR)/libsameboy.o $(LIBDIR)/libsameboy.a
endif
all: sdl tester libretro lib
ifeq ($(PLATFORM),Darwin)
all: cocoa ios-ipa ios-deb
endif

# Get a list of our source files and their respective object file targets

CORE_SOURCES := $(filter-out $(CORE_FILTER),$(shell ls Core/*.c))
CORE_HEADERS := $(shell ls Core/*.h)
SDL_SOURCES := $(shell ls SDL/*.c) $(OPEN_DIALOG) $(patsubst %,SDL/audio/%.c,$(SDL_AUDIO_DRIVERS))
TESTER_SOURCES := $(shell ls Tester/*.c)
IOS_SOURCES := $(filter-out iOS/installer.m, $(shell ls iOS/*.m)) $(shell ls AppleCommon/*.m)
COCOA_SOURCES := $(shell ls Cocoa/*.m) $(shell ls HexFiend/*.m) $(shell ls JoyKit/*.m) $(shell ls AppleCommon/*.m)
QUICKLOOK_SOURCES := $(shell ls QuickLook/*.m) $(shell ls QuickLook/*.c)

ifeq ($(PLATFORM),windows32)
CORE_SOURCES += $(shell ls Windows/*.c)
endif

CORE_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(CORE_SOURCES))
PUBLIC_HEADERS := $(patsubst Core/%,$(INC)/%,$(CORE_HEADERS))
COCOA_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(COCOA_SOURCES))
IOS_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(IOS_SOURCES))
QUICKLOOK_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(QUICKLOOK_SOURCES))
SDL_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(SDL_SOURCES))
TESTER_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(TESTER_SOURCES))

lib: $(PUBLIC_HEADERS)

# Automatic dependency generation

ifneq ($(filter-out ios ios-ipa ios-deb clean bootroms libretro %.bin, $(MAKECMDGOALS)),)
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
ifneq ($(filter $(MAKECMDGOALS),_ios),)
-include $(IOS_OBJECTS:.o=.dep)
endif
endif

$(OBJ)/SDL/%.dep: SDL/%
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(GL_CFLAGS) -MT $(OBJ)/$^.o -M $^ -c -o $@
	
$(OBJ)/OpenDialog/%.dep: OpenDialog/%
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(GL_CFLAGS) -MT $(OBJ)/$^.o -M $^ -c -o $@

$(OBJ)/%.dep: %
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -MT $(OBJ)/$^.o -M $^ -c -o $@

# Compilation rules

$(OBJ)/Core/%.c.o: Core/%.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FAT_FLAGS) -DGB_INTERNAL -c $< -o $@

$(OBJ)/SDL/%.c.o: SDL/%.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FRONTEND_CFLAGS) $(FAT_FLAGS) $(SDL_CFLAGS) $(GL_CFLAGS) -c $< -o $@

$(OBJ)/OpenDialog/%.c.o: OpenDialog/%.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FRONTEND_CFLAGS) $(FAT_FLAGS) $(SDL_CFLAGS) $(GL_CFLAGS) -c $< -o $@


$(OBJ)/%.c.o: %.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FRONTEND_CFLAGS) $(FAT_FLAGS) -c $< -o $@
	
# HexFiend requires more flags
$(OBJ)/HexFiend/%.m.o: HexFiend/%.m
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FRONTEND_CFLAGS) $(FAT_FLAGS) $(OCFLAGS) -c $< -o $@ -fno-objc-arc -include HexFiend/HexFiend_2_Framework_Prefix.pch
	
$(OBJ)/%.m.o: %.m
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FRONTEND_CFLAGS) $(FAT_FLAGS) $(OCFLAGS) -c $< -o $@
    
# iOS Port

$(BIN)/SameBoy-iOS.app: $(BIN)/SameBoy-iOS.app/SameBoy \
                        $(IOS_PNGS) \
                        iOS/License.html \
                        iOS/Info.plist \
                        $(BIN)/SameBoy-iOS.app/dmg_boot.bin \
                        $(BIN)/SameBoy-iOS.app/mgb_boot.bin \
                        $(BIN)/SameBoy-iOS.app/cgb0_boot.bin \
                        $(BIN)/SameBoy-iOS.app/cgb_boot.bin \
                        $(BIN)/SameBoy-iOS.app/agb_boot.bin \
                        $(BIN)/SameBoy-iOS.app/sgb_boot.bin \
                        $(BIN)/SameBoy-iOS.app/sgb2_boot.bin \
						$(BIN)/SameBoy-iOS.app/LaunchScreen.storyboardc \
                        Shaders
	$(MKDIR) -p $(BIN)/SameBoy-iOS.app
	cp $(IOS_PNGS) $(BIN)/SameBoy-iOS.app
	sed "s/@VERSION/$(VERSION)/;s/@COPYRIGHT_YEAR/$(COPYRIGHT_YEAR)/" < iOS/Info.plist > $(BIN)/SameBoy-iOS.app/Info.plist
	sed "s/@COPYRIGHT_YEAR/$(COPYRIGHT_YEAR)/" < iOS/License.html > $(BIN)/SameBoy-iOS.app/License.html
	$(MKDIR) -p $(BIN)/SameBoy-iOS.app/Shaders
	cp Shaders/*.fsh Shaders/*.metal $(BIN)/SameBoy-iOS.app/Shaders
	$(CODESIGN) $@

$(BIN)/SameBoy-iOS.app/SameBoy: $(CORE_OBJECTS) $(IOS_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS)
ifeq ($(CONF), release)
	$(STRIP) $@
endif

$(OBJ)/installer: iOS/installer.m
	$(CC) $< -o $@ $(IOS_INSTALLER_LDFLAGS) $(CFLAGS)

# Cocoa Port

$(BIN)/SameBoy.app: $(BIN)/SameBoy.app/Contents/MacOS/SameBoy \
                    $(shell ls Cocoa/*.icns Cocoa/*.png) \
                    Cocoa/License.html \
                    Cocoa/Info.plist \
                    Misc/registers.sym \
                    $(BIN)/SameBoy.app/Contents/Resources/dmg_boot.bin \
                    $(BIN)/SameBoy.app/Contents/Resources/mgb_boot.bin \
                    $(BIN)/SameBoy.app/Contents/Resources/cgb0_boot.bin \
                    $(BIN)/SameBoy.app/Contents/Resources/cgb_boot.bin \
                    $(BIN)/SameBoy.app/Contents/Resources/agb_boot.bin \
                    $(BIN)/SameBoy.app/Contents/Resources/sgb_boot.bin \
                    $(BIN)/SameBoy.app/Contents/Resources/sgb2_boot.bin \
                    $(patsubst %.xib,%.nib,$(addprefix $(BIN)/SameBoy.app/Contents/Resources/,$(shell cd Cocoa;ls *.xib))) \
                    $(BIN)/SameBoy.qlgenerator \
                    Shaders
	$(MKDIR) -p $(BIN)/SameBoy.app/Contents/Resources
	cp Cocoa/*.icns Cocoa/*.png Misc/registers.sym $(BIN)/SameBoy.app/Contents/Resources/
	sed "s/@VERSION/$(VERSION)/;s/@COPYRIGHT_YEAR/$(COPYRIGHT_YEAR)/" < Cocoa/Info.plist > $(BIN)/SameBoy.app/Contents/Info.plist
	sed "s/@COPYRIGHT_YEAR/$(COPYRIGHT_YEAR)/" < Cocoa/License.html > $(BIN)/SameBoy.app/Contents/Resources/Credits.html
	$(MKDIR) -p $(BIN)/SameBoy.app/Contents/Resources/Shaders
	cp Shaders/*.fsh Shaders/*.metal $(BIN)/SameBoy.app/Contents/Resources/Shaders
	$(MKDIR) -p $(BIN)/SameBoy.app/Contents/Library/QuickLook/
	cp -rf $(BIN)/SameBoy.qlgenerator $(BIN)/SameBoy.app/Contents/Library/QuickLook/
ifeq ($(CONF), release)
	$(CODESIGN) $@
endif

$(BIN)/SameBoy.app/Contents/MacOS/SameBoy: $(CORE_OBJECTS) $(COCOA_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(FAT_FLAGS) -framework OpenGL -framework AudioToolbox -framework AudioUnit -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework IOKit -framework PreferencePanes -framework Carbon -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit
ifeq ($(CONF), release)
	$(STRIP) $@
endif

$(BIN)/SameBoy.app/Contents/Resources/%.nib: Cocoa/%.xib
	ibtool --target-device mac --minimum-deployment-target 10.9 --compile $@ $^ 2>&1 | cat -
	
$(BIN)/SameBoy-iOS.app/%.storyboardc: iOS/%.storyboard
	ibtool --target-device iphone --target-device ipad --minimum-deployment-target $(IOS_MIN) --compile $@ $^ 2>&1 | cat -

# Quick Look generator

$(BIN)/SameBoy.qlgenerator: $(BIN)/SameBoy.qlgenerator/Contents/MacOS/SameBoyQL \
                            $(shell ls QuickLook/*.png) \
                            QuickLook/Info.plist \
                            $(BIN)/SameBoy.qlgenerator/Contents/Resources/cgb_boot_fast.bin
	$(MKDIR) -p $(BIN)/SameBoy.qlgenerator/Contents/Resources
	cp QuickLook/*.png $(BIN)/SameBoy.qlgenerator/Contents/Resources/
	sed "s/@VERSION/$(VERSION)/;s/@COPYRIGHT_YEAR/$(COPYRIGHT_YEAR)/" < QuickLook/Info.plist > $(BIN)/SameBoy.qlgenerator/Contents/Info.plist
ifeq ($(CONF), release)
	$(CODESIGN) $@
endif

# Currently, SameBoy.app includes two "copies" of each Core .o file once in the app itself and
# once in the QL Generator. It should probably become a dylib instead.
$(BIN)/SameBoy.qlgenerator/Contents/MacOS/SameBoyQL: $(CORE_OBJECTS) $(QUICKLOOK_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(FAT_FLAGS) -Wl,-exported_symbols_list,QuickLook/exports.sym -bundle -framework Cocoa -framework Quicklook
ifeq ($(CONF), release)
	$(STRIP) $@
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
	$(CC) $^ -o $@ $(LDFLAGS) $(FAT_FLAGS) $(SDL_LDFLAGS) $(GL_LDFLAGS)
ifeq ($(CONF), release)
	$(STRIP) $@
	$(CODESIGN) $@
endif

# Windows version builds two, one with a conole and one without it
$(BIN)/SDL/sameboy.exe: $(CORE_OBJECTS) $(SDL_OBJECTS) $(OBJ)/Windows/resources.o
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS) $(GL_LDFLAGS) -Wl,/subsystem:windows

$(BIN)/SDL/sameboy_debugger.exe: $(CORE_OBJECTS) $(SDL_OBJECTS) $(OBJ)/Windows/resources.o
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS) $(GL_LDFLAGS) -Wl,/subsystem:console

ifneq ($(USE_WINDRES),)
$(OBJ)/%.o: %.rc
	-@$(MKDIR) -p $(dir $@)
	windres --preprocessor cpp -DVERSION=\"$(VERSION)\" -DCOPYRIGHT_YEAR=\"$(COPYRIGHT_YEAR)\" $^ $@
else
$(OBJ)/%.res: %.rc
	-@$(MKDIR) -p $(dir $@)
	rc /fo $@ /dVERSION=\"$(VERSION)\" /dCOPYRIGHT_YEAR=\"$(COPYRIGHT_YEAR)\" $^ 

%.o: %.res
	cvtres /OUT:"$@" $^
endif

# Copy required DLL files for the Windows port
$(BIN)/SDL/%.dll:
	-@$(MKDIR) -p $(dir $@)
	@$(eval MATCH := $(shell where $$LIB:$(notdir $@)))
	cp "$(MATCH)" $@

# Tester

$(BIN)/tester/sameboy_tester: $(CORE_OBJECTS) $(TESTER_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS)
ifeq ($(CONF), release)
	$(STRIP) $@
	$(CODESIGN) $@
endif

$(BIN)/tester/sameboy_tester.exe: $(CORE_OBJECTS) $(SDL_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) -Wl,/subsystem:console

$(BIN)/SDL/%.bin: $(BOOTROMS_DIR)/%.bin
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(BIN)/tester/%.bin: $(BOOTROMS_DIR)/%.bin
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(BIN)/SameBoy.app/Contents/Resources/%.bin: $(BOOTROMS_DIR)/%.bin
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(BIN)/SameBoy-iOS.app/%.bin: $(BOOTROMS_DIR)/%.bin
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(BIN)/SDL/LICENSE: LICENSE
	-@$(MKDIR) -p $(dir $@)
	grep -v "^  " $^ > $@

$(BIN)/SDL/registers.sym: Misc/registers.sym
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(BIN)/SDL/background.bmp: SDL/background.bmp
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(BIN)/SDL/Shaders: Shaders
	-@$(MKDIR) -p $@
	cp -rf Shaders/*.fsh $@
    
$(BIN)/SDL/Palettes: Misc/Palettes
	-@$(MKDIR) -p $@
	cp -rf Misc/Palettes/*.sbp $@

# Boot ROMs

$(OBJ)/%.2bpp: %.png
	-@$(MKDIR) -p $(dir $@)
	$(RGBGFX) $(RGBGFX_FLAGS) -o $@ $<

$(OBJ)/BootROMs/SameBoyLogo.pb12: $(OBJ)/BootROMs/SameBoyLogo.2bpp $(PB12_COMPRESS)
	-@$(MKDIR) -p $(dir $@)
	"$(realpath $(PB12_COMPRESS))" < $< > $@
	
$(PB12_COMPRESS): BootROMs/pb12.c
	-@$(MKDIR) -p $(dir $@)
	$(NATIVE_CC) -std=c99 -Wall -Werror $< -o $@

$(BIN)/BootROMs/cgb0_boot.bin: BootROMs/cgb_boot.asm
$(BIN)/BootROMs/agb_boot.bin: BootROMs/cgb_boot.asm
$(BIN)/BootROMs/cgb_boot_fast.bin: BootROMs/cgb_boot.asm
$(BIN)/BootROMs/sgb2_boot.bin: BootROMs/sgb_boot.asm

$(BIN)/BootROMs/%.bin: BootROMs/%.asm $(OBJ)/BootROMs/SameBoyLogo.pb12
	-@$(MKDIR) -p $(dir $@)
	$(RGBASM) $(RGBASM_FLAGS) -o $@.tmp $<
	$(RGBLINK) -x -o $@ $@.tmp
	@rm $@.tmp

# Libretro Core (uses its own build system)
libretro:
	CFLAGS="$(WARNINGS)" $(MAKE) -C libretro BOOTROMS_DIR=$(abspath $(BOOTROMS_DIR)) BIN=$(abspath $(BIN))

# install for Linux/FreeDesktop/etc.
# Does not install mimetype icons because FreeDesktop is cursed abomination with no right to exist.
# If you somehow find a reasonable way to make associate an icon with an extension in this dumpster
# fire of a desktop environment, open an issue or a pull request
ifneq ($(FREEDESKTOP),)
ICON_NAMES := apps/sameboy mimetypes/x-gameboy-rom mimetypes/x-gameboy-color-rom
ICON_SIZES := 16x16 32x32 64x64 128x128 256x256 512x512
ICONS := $(foreach name,$(ICON_NAMES), $(foreach size,$(ICON_SIZES),$(DESTDIR)$(PREFIX)/share/icons/hicolor/$(size)/$(name).png))
install: sdl $(DESTDIR)$(PREFIX)/share/mime/packages/sameboy.xml $(ICONS) FreeDesktop/sameboy.desktop
	-@$(MKDIR) -p $(dir $(DESTDIR)$(PREFIX))
	mkdir -p $(DESTDIR)$(DATA_DIR)/ $(DESTDIR)$(PREFIX)/bin/
	cp -rf $(BIN)/SDL/* $(DESTDIR)$(DATA_DIR)/
	mv $(DESTDIR)$(DATA_DIR)/sameboy $(DESTDIR)$(PREFIX)/bin/sameboy
ifeq ($(DESTDIR),)
	-update-mime-database -n $(PREFIX)/share/mime
	-xdg-desktop-menu install --novendor --mode system FreeDesktop/sameboy.desktop
	-xdg-icon-resource forceupdate --mode system
	-xdg-desktop-menu forceupdate --mode system
ifneq ($(SUDO_USER),)
	-su $(SUDO_USER) -c "xdg-desktop-menu forceupdate --mode system"
endif
else
	-@$(MKDIR) -p $(DESTDIR)$(PREFIX)/share/applications/
	cp FreeDesktop/sameboy.desktop $(DESTDIR)$(PREFIX)/share/applications/sameboy.desktop
endif

$(DESTDIR)$(PREFIX)/share/icons/hicolor/%/apps/sameboy.png: FreeDesktop/AppIcon/%.png
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(DESTDIR)$(PREFIX)/share/icons/hicolor/%/mimetypes/x-gameboy-rom.png: FreeDesktop/Cartridge/%.png
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(DESTDIR)$(PREFIX)/share/icons/hicolor/%/mimetypes/x-gameboy-color-rom.png: FreeDesktop/ColorCartridge/%.png
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@

$(DESTDIR)$(PREFIX)/share/mime/packages/sameboy.xml: FreeDesktop/sameboy.xml
	-@$(MKDIR) -p $(dir $@)
	cp -f $^ $@
endif

ios:
	@$(MAKE) _ios
    
$(BIN)/SameBoy-iOS.ipa: ios iOS/sideload.entitlements
	$(MKDIR) -p $(OBJ)/Payload
	cp -rf $(BIN)/SameBoy-iOS.app $(OBJ)/Payload/SameBoy-iOS.app
	codesign -fs - --entitlements iOS/sideload.entitlements $(OBJ)/Payload/SameBoy-iOS.app
	(cd $(OBJ) && zip -q $(abspath $@) -r Payload)
	rm -rf $(OBJ)/Payload

    
$(BIN)/SameBoy-iOS.deb: $(OBJ)/debian-binary $(OBJ)/control.tar.gz $(OBJ)/data.tar.gz
	-@$(MKDIR) -p $(dir $@)
	(cd $(OBJ) && ar cr $(abspath $@) $(notdir $^))
	
$(OBJ)/data.tar.gz: ios iOS/jailbreak.entitlements iOS/installer.entitlements
	$(MKDIR) -p $(OBJ)/private/var/containers/
	cp -rf $(BIN)/SameBoy-iOS.app $(OBJ)/private/var/containers/SameBoy-iOS.app
	cp build/obj-ios/installer $(OBJ)/private/var/containers/SameBoy-iOS.app
	codesign -fs - --entitlements iOS/installer.entitlements $(OBJ)/private/var/containers/SameBoy-iOS.app/installer
	codesign -fs - --entitlements iOS/jailbreak.entitlements $(OBJ)/private/var/containers/SameBoy-iOS.app
	(cd $(OBJ) && tar -czf $(abspath $@) --format ustar --uid 501 --gid 501 --numeric-owner ./private)
	rm -rf $(OBJ)/private/
	
$(OBJ)/control.tar.gz: iOS/deb-postinst iOS/deb-prerm iOS/deb-control
	-@$(MKDIR) -p $(dir $@)
	sed "s/@VERSION/$(VERSION)/" < iOS/deb-control > $(OBJ)/control
	ln iOS/deb-postinst $(OBJ)/postinst
	ln iOS/deb-prerm $(OBJ)/prerm
	(cd $(OBJ) && tar -czf $(abspath $@) --format ustar --uid 501 --gid 501 --numeric-owner ./control ./postinst ./prerm)
	rm $(OBJ)/control $(OBJ)/postinst $(OBJ)/prerm
	
$(OBJ)/debian-binary:
	-@$(MKDIR) -p $(dir $@)
	echo 2.0 > $@
    
$(LIBDIR)/libsameboy.o: $(CORE_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	@# This is a somewhat simple hack to force Clang and GCC to build a native object file out of one or many LTO objects
	echo "static const char __attribute__((used)) x=0;"| $(CC) $(filter-out -flto,$(CFLAGS)) -c -x c - -o $(OBJ)/lto_hack.o
	@# And this is a somewhat complicated hack to invoke the correct LTO-enabled LD command in a mostly cross-platform nature
	$(CC) $(FAT_FLAGS) $(CFLAGS) $(LIBFLAGS) $^ $(OBJ)/lto_hack.o -o $@
	-@rm $(OBJ)/lto_hack.o
    
$(LIBDIR)/libsameboy.a: $(LIBDIR)/libsameboy.o
	-@$(MKDIR) -p $(dir $@)
	-@rm -f $@
	ar -crs $@ $^
	
$(INC)/%.h: Core/%.h
	-@$(MKDIR) -p $(dir $@)
	-@# CPPP doesn't like multibyte characters, so we replace the single quote character before processing so it doesn't complain
	sed "s/'/@SINGLE_QUOTE@/g" $^ | cppp $(CPPP_FLAGS) | sed "s/@SINGLE_QUOTE@/'/g" > $@

lib-unsupported:
	@echo Due to limitations of lld-link, compiling SameBoy as a library on Windows is not supported.
	@false
	
# Clean
clean:
	rm -rf build

.PHONY: libretro tester cocoa ios _ios ios-ipa ios-deb liblib-unsupported bootroms
