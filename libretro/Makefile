STATIC_LINKING := 0
AR             := ar

ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   platform = osx
else ifneq ($(findstring win,$(shell uname -a)),)
   platform = win
endif
endif

# system platform
system_platform = unix
ifeq ($(shell uname -a),)
	EXE_EXT = .exe
	system_platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
	system_platform = osx
	arch = intel
ifeq ($(shell uname -p),powerpc)
	arch = ppc
endif
else ifneq ($(findstring MINGW,$(shell uname -a)),)
	system_platform = win
endif

ifeq ($(platform), win)
	INCFLAGS += -I Windows
endif

CORE_DIR     += ..

TARGET_NAME  = sameboy
LIBM		    = -lm

ifeq ($(ARCHFLAGS),)
ifeq ($(archs),ppc)
   ARCHFLAGS = -arch ppc -arch ppc64
else
   ARCHFLAGS = -arch i386 -arch x86_64
endif
endif

ifeq ($(platform), osx)
ifndef ($(NOUNIVERSAL))
   CXXFLAGS += $(ARCHFLAGS)
   LFLAGS += $(ARCHFLAGS)
endif
endif

ifeq ($(STATIC_LINKING), 1)
EXT := a
endif

ifeq ($(platform), unix)
    EXT ?= so
   TARGET := $(TARGET_NAME)_libretro.$(EXT)
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/libretro/link.T -Wl,--no-undefined
else ifeq ($(platform), linux-portable)
   TARGET := $(TARGET_NAME)_libretro.$(EXT)
   fpic := -fPIC -nostdlib
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/libretro/link.T
	LIBM :=
else ifneq (,$(findstring osx,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
else ifneq (,$(findstring ios,$(platform)))
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
	fpic := -fPIC
	SHARED := -dynamiclib

ifeq ($(IOSSDK),)
   IOSSDK := $(shell xcodebuild -version -sdk iphoneos Path)
endif

	DEFINES := -DIOS
ifeq ($(platform),ios-arm64)
   CC = cc -arch armv64 -isysroot $(IOSSDK)
else   
	CC = cc -arch armv7 -isysroot $(IOSSDK)
endif
ifeq ($(platform),$(filter $(platform),ios9 ios-arm64))
CC     += -miphoneos-version-min=8.0
CXXFLAGS += -miphoneos-version-min=8.0
else
CC     += -miphoneos-version-min=5.0
CXXFLAGS += -miphoneos-version-min=5.0
endif
else ifneq (,$(findstring qnx,$(platform)))
	TARGET := $(TARGET_NAME)_libretro_qnx.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/libretro/link.T -Wl,--no-undefined
else ifeq ($(platform), emscripten)
   TARGET := $(TARGET_NAME)_libretro_emscripten.bc
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/libretro/link.T -Wl,--no-undefined
else ifeq ($(platform), vita)
   TARGET := $(TARGET_NAME)_vita.a
   CC = arm-vita-eabi-gcc
   AR = arm-vita-eabi-ar
   CFLAGS += -Wl,-q -Wall -O3 -fno-short-enums -fno-optimize-sibling-calls
	STATIC_LINKING = 1
else
   CC = gcc
   TARGET := $(TARGET_NAME)_libretro.dll
   SHARED := -shared -static-libgcc -static-libstdc++ -s -Wl,--version-script=$(CORE_DIR)/libretro/link.T -Wl,--no-undefined
endif

TARGET := $(CORE_DIR)/$(TARGET)

# To force use of the Unix version instead of the Windows version
MKDIR := $(shell which mkdir)

LDFLAGS += $(LIBM)

ifeq ($(DEBUG), 1)
   CXXFLAGS += -O0 -g
else
   CXXFLAGS += -O2 -DNDEBUG
endif

include Makefile.common

OBJECTS := $(patsubst %.c,$(CORE_DIR)/build/obj/%_libretro.c.o,$(SOURCES_C))

CFLAGS   += -Wall -D__LIBRETRO__ $(fpic) $(INCFLAGS) -std=gnu11 -D_GNU_SOURCE -D_USE_MATH_DEFINES
CXXFLAGS += -Wall -D__LIBRETRO__ $(fpic)

all: $(TARGET)

$(CORE_DIR)/libretro/%_boot.c: $(CORE_DIR)/BootROMs/prebuilt/%_boot.bin
	echo "/* AUTO-GENERATED */" > $@
	echo "const unsigned char $(notdir $(@:%.c=%))[] = {" >> $@
	hexdump -v -e '/1 "0x%02x, "' $< >> $@
	echo "};" >> $@
	echo "const unsigned $(notdir $(@:%.c=%))_length = sizeof($(notdir $(@:%.c=%)));" >> $@

$(CORE_DIR)/build/bin/BootROMs/%_boot.bin:
	$(MAKE) -C $(CORE_DIR) $(patsubst $(CORE_DIR)/%,%,$@)

$(TARGET): $(OBJECTS)
	-@$(MKDIR) -p $(dir $@)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CC) $(fpic) $(SHARED) $(INCFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)
endif

$(CORE_DIR)/build/obj/%_libretro.c.o: %.c
	-@$(MKDIR) -p $(dir $@)
	$(CC) -c -o $@ $< $(CFLAGS) $(fpic) -DGB_INTERNAL

%.o: %.c
	$(CC) $(CFLAGS) $(fpic) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean

