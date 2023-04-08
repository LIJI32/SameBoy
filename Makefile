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
NATIVE_CC = clang -IWindows -Wno-deprecated-declarations --target=i386-pc-windows
SDL_AUDIO_DRIVERS ?= xaudio2 xaudio2_7 sdl
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

ifneq ($(shell which xdg-open)$(FREEDESKTOP),)
# Running on an FreeDesktop environment, configure for (optional) installation
DESTDIR ?= 
PREFIX ?= /usr/local
DATA_DIR ?= $(PREFIX)/share/sameboy/
FREEDESKTOP ?= true
endif

ENABLE_MINIZIP ?= 1

default: $(DEFAULT)

ifeq ($(MAKECMDGOALS),)
MAKECMDGOALS := $(DEFAULT)
endif

include version.mk
COPYRIGHT_YEAR := $(shell grep -oE "20[2-9][0-9]" LICENSE)
export VERSION
CONF ?= debug

BIN := build/bin
OBJ := build/obj
BOOTROMS_DIR ?= $(BIN)/BootROMs

ifdef DATA_DIR
CFLAGS += -DDATA_DIR="\"$(DATA_DIR)\""
endif

# Set tools

# Use clang if it's available.
ifeq ($(origin CC),default)
ifneq (, $(shell which clang))
CC := clang 
endif
endif

# Find libraries with pkg-config if available.
ifneq (, $(shell which pkg-config))
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



# Set compilation and linkage flags based on target, platform and configuration

OPEN_DIALOG = OpenDialog/gtk.c
NULL := /dev/null

ifeq ($(PLATFORM),windows32)
OPEN_DIALOG = OpenDialog/windows.c
NULL := NUL
endif

ifeq ($(PLATFORM),Darwin)
OPEN_DIALOG = OpenDialog/cocoa.m
endif

# These must come before the -Wno- flags
WARNINGS += -Werror -Wall -Wno-unknown-warning -Wno-unknown-warning-option -Wno-missing-braces
WARNINGS += -Wno-nonnull -Wno-unused-result -Wno-strict-aliasing -Wno-multichar -Wno-int-in-bool-context -Wno-format-truncation

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

ifneq ($(ENABLE_MINIZIP),0)
MINIZIP_CPPFLAGS := -DGB_HAS_MINIZIP
ifeq (,$(PKG_CONFIG))
MINIZIP_LDFLAGS := -lminizip
else
MINIZIP_LDFLAGS := $(shell $(PKG_CONFIG) --libs minizip)
endif
endif

ifeq ($(PLATFORM),windows32)
CFLAGS += -IWindows -Drandom=rand --target=i386-pc-windows
LDFLAGS += -lmsvcrt -lcomdlg32 -luser32 -lshell32 -lole32 -lSDL2main -Wl,/MANIFESTFILE:NUL --target=i386-pc-windows
SDL_LDFLAGS := -lSDL2
GL_LDFLAGS := -lopengl32
else
LDFLAGS += -lc -lm -ldl
endif

ifeq ($(MAKECMDGOALS),_ios)
OBJ := build/obj-ios
SYSROOT := $(shell xcodebuild -sdk iphoneos -version Path 2> $(NULL))
ifeq ($(SYSROOT),)
$(error Could not find an iOS SDK)
endif
CFLAGS += -arch arm64 -miphoneos-version-min=11.0 -isysroot $(SYSROOT) -IAppleCommon
LDFLAGS += -arch arm64
OCFLAGS += -x objective-c -fobjc-arc -Wno-deprecated-declarations -isysroot $(SYSROOT)
LDFLAGS += -miphoneos-version-min=11.0  -isysroot $(SYSROOT)
REREGISTER_LDFLAGS := $(LDFLAGS) -lobjc -framework CoreServices -framework Foundation
LDFLAGS += -lobjc -framework UIKit -framework Foundation -framework CoreGraphics -framework Metal -framework MetalKit -framework AudioToolbox -framework AVFoundation -framework QuartzCore -framework CoreMotion -framework CoreVideo -framework CoreMedia -framework CoreImage -weak_framework CoreHaptics 
CODESIGN := codesign -fs -
else
ifeq ($(PLATFORM),Darwin)
SYSROOT := $(shell xcodebuild -sdk macosx -version Path 2> $(NULL))
ifeq ($(SYSROOT),)
SYSROOT := /Library/Developer/CommandLineTools/SDKs/$(shell ls /Library/Developer/CommandLineTools/SDKs/ | grep 10 | tail -n 1)
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

ifeq ($(CONF),debug)
CFLAGS += -g
else ifeq ($(CONF), release)
CFLAGS += -O3 -DNDEBUG
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
_ios: $(BIN)/SameBoy-iOS.app $(OBJ)/reregister
ios-ipa: $(BIN)/SameBoy-iOS.ipa
ios-deb: $(BIN)/SameBoy-iOS.deb
all: cocoa sdl tester libretro

# Get a list of our source files and their respective object file targets

CORE_SOURCES := $(shell ls Core/*.c)
SDL_SOURCES := $(shell ls SDL/*.c) $(OPEN_DIALOG) $(patsubst %,SDL/audio/%.c,$(SDL_AUDIO_DRIVERS))
TESTER_SOURCES := $(shell ls Tester/*.c)
IOS_SOURCES := $(filter-out iOS/reregister.m, $(shell ls iOS/*.m)) $(shell ls AppleCommon/*.m)
COCOA_SOURCES := $(shell ls Cocoa/*.m) $(shell ls HexFiend/*.m) $(shell ls JoyKit/*.m) $(shell ls AppleCommon/*.m)
QUICKLOOK_SOURCES := $(shell ls QuickLook/*.m) $(shell ls QuickLook/*.c)

ifeq ($(PLATFORM),windows32)
CORE_SOURCES += $(shell ls Windows/*.c)
endif

CORE_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(CORE_SOURCES))
COCOA_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(COCOA_SOURCES))
IOS_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(IOS_SOURCES))
QUICKLOOK_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(QUICKLOOK_SOURCES))
SDL_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(SDL_SOURCES))
TESTER_OBJECTS := $(patsubst %,$(OBJ)/%.o,$(TESTER_SOURCES))

# Automatic dependency generation

ifneq ($(filter-out ios ios-ipa ios-dev clean bootroms libretro %.bin, $(MAKECMDGOALS)),)
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
	$(CC) $(CFLAGS) $(FAT_FLAGS) $(MINIZIP_CPPFLAGS) -DGB_INTERNAL -c $< -o $@

$(OBJ)/SDL/%.c.o: SDL/%.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FAT_FLAGS) $(SDL_CFLAGS) $(GL_CFLAGS) -c $< -o $@

$(OBJ)/OpenDialog/%.c.o: OpenDialog/%.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FAT_FLAGS) $(SDL_CFLAGS) $(GL_CFLAGS) -c $< -o $@


$(OBJ)/%.c.o: %.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FAT_FLAGS) -c $< -o $@
	
# HexFiend requires more flags
$(OBJ)/HexFiend/%.m.o: HexFiend/%.m
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FAT_FLAGS) $(OCFLAGS) -c $< -o $@ -fno-objc-arc -include HexFiend/HexFiend_2_Framework_Prefix.pch
	
$(OBJ)/%.m.o: %.m
	-@$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) $(FAT_FLAGS) $(OCFLAGS) -c $< -o $@
    
# iOS Port

$(BIN)/SameBoy-iOS.app: $(BIN)/SameBoy-iOS.app/SameBoy \
                        $(shell ls iOS/*.png) \
                        iOS/License.html \
                        iOS/Info.plist \
                        $(BIN)/SameBoy-iOS.app/dmg_boot.bin \
                        $(BIN)/SameBoy-iOS.app/mgb_boot.bin \
                        $(BIN)/SameBoy-iOS.app/cgb0_boot.bin \
                        $(BIN)/SameBoy-iOS.app/cgb_boot.bin \
                        $(BIN)/SameBoy-iOS.app/agb_boot.bin \
                        $(BIN)/SameBoy-iOS.app/sgb_boot.bin \
                        $(BIN)/SameBoy-iOS.app/sgb2_boot.bin \
                        Shaders
	$(MKDIR) -p $(BIN)/SameBoy-iOS.app
	cp iOS/*.png $(BIN)/SameBoy-iOS.app
	sed "s/@VERSION/$(VERSION)/;s/@COPYRIGHT_YEAR/$(COPYRIGHT_YEAR)/" < iOS/Info.plist > $(BIN)/SameBoy-iOS.app/Info.plist
	sed "s/@COPYRIGHT_YEAR/$(COPYRIGHT_YEAR)/" < iOS/License.html > $(BIN)/SameBoy-iOS.app/License.html
	$(MKDIR) -p $(BIN)/SameBoy-iOS.app/Shaders
	cp Shaders/*.fsh Shaders/*.metal $(BIN)/SameBoy-iOS.app/Shaders
	$(CODESIGN) $@

$(BIN)/SameBoy-iOS.app/SameBoy: $(CORE_OBJECTS) $(IOS_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(MINIZIP_LDFLAGS)
ifeq ($(CONF), release)
	$(STRIP) $@
endif

$(OBJ)/reregister: iOS/reregister.m
	$(CC) $< -o $@ $(REREGISTER_LDFLAGS) $(CFLAGS)

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
                    $(patsubst %.xib,%.nib,$(addprefix $(BIN)/SameBoy.app/Contents/Resources/Base.lproj/,$(shell cd Cocoa;ls *.xib))) \
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
	$(CC) $^ -o $@ $(LDFLAGS) $(FAT_FLAGS) $(MINIZIP_LDFLAGS) -framework OpenGL -framework AudioToolbox -framework AudioUnit -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework IOKit -framework PreferencePanes -framework Carbon -framework QuartzCore -framework Security -framework WebKit -weak_framework Metal -weak_framework MetalKit
ifeq ($(CONF), release)
	$(STRIP) $@
endif

$(BIN)/SameBoy.app/Contents/Resources/Base.lproj/%.nib: Cocoa/%.xib
	ibtool --compile $@ $^ 2>&1 | cat -
	
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
	$(CC) $^ -o $@ $(LDFLAGS) $(FAT_FLAGS) $(MINIZIP_LDFLAGS) -Wl,-exported_symbols_list,QuickLook/exports.sym -bundle -framework Cocoa -framework Quicklook
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
	$(CC) $^ -o $@ $(LDFLAGS) $(FAT_FLAGS) $(SDL_LDFLAGS) $(GL_LDFLAGS) $(MINIZIP_LDFLAGS)
ifeq ($(CONF), release)
	$(STRIP) $@
	$(CODESIGN) $@
endif

# Windows version builds two, one with a conole and one without it
$(BIN)/SDL/sameboy.exe: $(CORE_OBJECTS) $(SDL_OBJECTS) $(OBJ)/Windows/resources.o
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS) $(GL_LDFLAGS) $(MINIZIP_LDFLAGS) -Wl,/subsystem:windows

$(BIN)/SDL/sameboy_debugger.exe: $(CORE_OBJECTS) $(SDL_OBJECTS) $(OBJ)/Windows/resources.o
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS) $(GL_LDFLAGS) $(MINIZIP_LDFLAGS) -Wl,/subsystem:console

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

# We must provide SDL2.dll with the Windows port.
$(BIN)/SDL/SDL2.dll:
	@$(eval MATCH := $(shell where $$LIB:SDL2.dll))
	cp "$(MATCH)" $@

# Tester

$(BIN)/tester/sameboy_tester: $(CORE_OBJECTS) $(TESTER_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(MINIZIP_LDFLAGS)
ifeq ($(CONF), release)
	$(STRIP) $@
	$(CODESIGN) $@
endif

$(BIN)/tester/sameboy_tester.exe: $(CORE_OBJECTS) $(SDL_OBJECTS)
	-@$(MKDIR) -p $(dir $@)
	$(CC) $^ -o $@ $(LDFLAGS) $(MINIZIP_LDFLAGS) -Wl,/subsystem:console

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
    
$(BIN)/SDL/Palettes: Misc/Palettes
	-@$(MKDIR) -p $@
	cp -rf Misc/Palettes/*.sbp $@

# Boot ROMs

$(OBJ)/%.2bpp: %.png
	-@$(MKDIR) -p $(dir $@)
	rgbgfx $(if $(filter $(shell echo 'print __RGBDS_MAJOR__ || (!__RGBDS_MAJOR__ && __RGBDS_MINOR__ > 5)' | rgbasm -), $$0), -h -u, -Z -u -c embedded) -o $@ $<

$(OBJ)/BootROMs/SameBoyLogo.pb12: $(OBJ)/BootROMs/SameBoyLogo.2bpp $(PB12_COMPRESS)
	-@$(MKDIR) -p $(dir $@)
	$(realpath $(PB12_COMPRESS)) < $< > $@
	
$(PB12_COMPRESS): BootROMs/pb12.c
	-@$(MKDIR) -p $(dir $@)
	$(NATIVE_CC) -std=c99 -Wall -Werror $< -o $@

$(BIN)/BootROMs/cgb0_boot.bin: BootROMs/cgb_boot.asm
$(BIN)/BootROMs/agb_boot.bin: BootROMs/cgb_boot.asm
$(BIN)/BootROMs/cgb_boot_fast.bin: BootROMs/cgb_boot.asm
$(BIN)/BootROMs/sgb2_boot: BootROMs/sgb_boot.asm

$(BIN)/BootROMs/%.bin: BootROMs/%.asm $(OBJ)/BootROMs/SameBoyLogo.pb12
	-@$(MKDIR) -p $(dir $@)
	rgbasm -i $(OBJ)/BootROMs/ -i BootROMs/ -o $@.tmp $<
	rgblink -o $@.tmp2 $@.tmp
	dd if=$@.tmp2 of=$@ count=1 bs=$(if $(findstring dmg,$@)$(findstring sgb,$@)$(findstring mgb,$@),256,2304) 2> $(NULL)
	@rm $@.tmp $@.tmp2

# Libretro Core (uses its own build system)
libretro:
	CFLAGS="$(WARNINGS)" $(MAKE) -C libretro BOOTROMS_DIR=$(abspath $(BOOTROMS_DIR))

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
    
$(BIN)/SameBoy-iOS.ipa: ios
	$(MKDIR) -p $(OBJ)/Payload
	cp -rf $(BIN)/SameBoy-iOS.app $(OBJ)/Payload/SameBoy-iOS.app
	(cd $(OBJ) && zip $(abspath $@) -r Payload)
	rm -rf $(OBJ)/Payload

    
$(BIN)/SameBoy-iOS.deb: $(OBJ)/debian-binary $(OBJ)/control.tar.gz $(OBJ)/data.tar.gz
	-@$(MKDIR) -p $(dir $@)
	(cd $(OBJ) && ar cr $(abspath $@) $(notdir $^))
	
$(OBJ)/data.tar.gz: ios iOS/jailbreak.entitlements
	$(MKDIR) -p $(OBJ)/Applications
	cp -rf $(BIN)/SameBoy-iOS.app $(OBJ)/Applications/SameBoy-iOS.app
	cp build/obj-ios/reregister iOS/reregister.entitlements $(OBJ)/Applications/SameBoy-iOS.app
	codesign -fs - --entitlements iOS/jailbreak.entitlements $(OBJ)/Applications/SameBoy-iOS.app
	(cd $(OBJ) && tar -czf $(abspath $@) ./Applications)
	rm -rf $(OBJ)/Applications
	
$(OBJ)/control.tar.gz: iOS/deb-postinst iOS/deb-control
	-@$(MKDIR) -p $(dir $@)
	sed "s/@VERSION/$(VERSION)/" < iOS/deb-control > $(OBJ)/control
	ln iOS/deb-postinst $(OBJ)/postinst
	(cd $(OBJ) && tar -czf $(abspath $@) ./control ./postinst)
	rm $(OBJ)/control $(OBJ)/postinst
	
$(OBJ)/debian-binary:
	-@$(MKDIR) -p $(dir $@)
	echo 2.0 > $@
	
# Clean
clean:
	rm -rf build

.PHONY: libretro tester cocoa ios _ios
