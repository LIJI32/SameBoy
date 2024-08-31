#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "retro_inline.h"

/*
 ********************************
 * VERSION: 2.0
 ********************************
 *
 * - 2.0: Add support for core options v2 interface
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
 */

/* RETRO_LANGUAGE_ENGLISH */

/* Default language:
 * - All other languages must include the same keys and values
 * - Will be used as a fallback in the event that frontend language
 *   is not available
 * - Will be used as a fallback for any missing entries in
 *   frontend language definition */

struct retro_core_option_v2_category option_cats_us[] = {
    {
        "system",
        "System",
        "Configure base hardware selection."
    },
    {
        "video",
        "Video",
        "Configure display parameters: palette selection, colour correction, screen border."
    },
    {
        "audio",
        "Audio",
        "Configure audio emulation: highpass filter, electrical interference."
    },
    {
        "input",
        "Input",
        "Configure input parameters: rumble support."
    },
    { NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {

    /* Core options used in single cart mode */

    {
        "sameboy_model",
        "System - Emulated Model (Requires Restart)",
        "Emulated Model (Requires Restart)",
        "Chooses which system model the content should be started on. Certain games may activate special in-game features when ran on specific models. This option requires a content restart in order to take effect.",
        NULL,
        "system",
        {
            { "Auto",                 "Auto Detect DMG/CGB"          },
            { "Auto (SGB)",           "Auto Detect DMG/SGB/CGB"      },
            { "Game Boy",             "Game Boy (DMG-CPU B)"         },
            { "Game Boy Pocket",      "Game Boy Pocket/Light"        },
            { "Game Boy Color 0",     "Game Boy Color (CPU CGB 0)"   },
            { "Game Boy Color A",     "Game Boy Color (CPU CGB A)"   },
            { "Game Boy Color B",     "Game Boy Color (CPU CGB B)"   },
            { "Game Boy Color C",     "Game Boy Color (CPU CGB C)"   },
            { "Game Boy Color D",     "Game Boy Color (CPU CGB D)"   },
            { "Game Boy Color",       "Game Boy Color (CPU CGB E)"   },
            { "Game Boy Advance",     "Game Boy Advance (CPU AGB A)" },
            { "Game Boy Player",      "Game Boy Player (CPU AGB A)"  },
            { "Super Game Boy",       "Super Game Boy NTSC"          },
            { "Super Game Boy PAL",   NULL },
            { "Super Game Boy 2",     NULL },
            { NULL, NULL },
        },
        "Auto"
    },
    {
        "sameboy_auto_sgb_model",
        "System - Auto Detected SGB Model (Requires Restart)",
        "Auto Detected SGB Model (Requires Restart)",
        "Specifies which model of Super Game Boy hardware to emulate when SGB content is automatically detected. This option requires a content restart in order to take effect.",
        NULL,
        "system",
        {
            { "Super Game Boy",       "Super Game Boy NTSC" },
            { "Super Game Boy PAL",   NULL },
            { "Super Game Boy 2",     NULL },
            { NULL, NULL },
        },
        "Super Game Boy"
    },
    {
        "sameboy_rtc",
        "System - Real Time Clock Emulation",
        "Real Time Clock Emulation",
        "Specifies how the emulation of the real-time clock feature used in certain Game Boy and Game Boy Color games should function.",
        NULL,
        "system",
        {
            { "sync to system clock", "Sync to System Clock" },
            { "accurate",             "Accurate"             },
            { NULL, NULL },
        },
        "sync to system clock"
    },
    {
        "sameboy_mono_palette",
        "Video - GB Mono Palette",
        "GB Mono Palette",
        "Selects the color palette that should be used when playing Game Boy games.",
        NULL,
        "video",
        {
            { "greyscale", "Greyscale"               },
            { "lime",      "Lime (Game Boy)"         },
            { "olive",     "Olive (Game Boy Pocket)" },
            { "teal",      "Teal (Game Boy Light)"   },
            { NULL, NULL },
        },
        "greyscale"
    },
    {
        "sameboy_color_correction_mode",
        "Video - GBC Color Correction",
        "GBC Color Correction",
        "Defines which type of color correction should be applied when playing Game Boy Color games.",
        NULL,
        "video",
        {
            { "emulate hardware",    "Modern – Balanced"            },
            { "accurate",            "Modern – Accurate"            },
            { "preserve brightness", "Modern – Boost Contrast"      },
            { "reduce contrast",     "Reduce Contrast"              },
            { "correct curves",      "Correct Color Curves"         },
            { "harsh reality",       "Harsh Reality (Low Contrast)" },
            { "off",                 "Disabled"                     },
            { NULL, NULL },
        },
        "emulate hardware"
    },
    {
        "sameboy_light_temperature",
        "Video - Ambient Light Temperature",
        "Ambient Light Temperature",
        "Simulates an ambient light's effect on non-backlit Game Boy screens, by setting a user-controlled color temperature. This option has no effect if the content is run on an original Game Boy (DMG) emulated model.",
        NULL,
        "video",
        {
            { "1.0",   "1000K (Warmest)"       },
            { "0.9",   "1550K" },
            { "0.8",   "2100K" },
            { "0.7",   "2650K" },
            { "0.6",   "3200K" },
            { "0.5",   "3750K" },
            { "0.4",   "4300K" },
            { "0.3",   "4850K" },
            { "0.2",   "5400K" },
            { "0.1",   "5950K" },
            { "0",     "6500K (Neutral White)" },
            { "-0.1",  "7050K" },
            { "-0.2",  "7600K" },
            { "-0.3",  "8150K" },
            { "-0.4",  "8700K" },
            { "-0.5",  "9250K" },
            { "-0.6",  "9800K" },
            { "-0.7", "10350K" },
            { "-0.8", "10900K" },
            { "-0.9", "11450K" },
            { "-1.0", "12000K (Coolest)"       },
            { NULL, NULL },
        },
        "0"
    },
    {
        "sameboy_border",
        "Video - Display Border",
        "Display Border",
        "Defines when to display an on-screen border around the content.",
        NULL,
        "video",
        {
            { "always",              "Always"                  },
            { "Super Game Boy only", "Only for Super Game Boy" },
            { "never",               "Disabled"                },
            { NULL, NULL },
        },
        "Super Game Boy only"
    },
    {
        "sameboy_high_pass_filter_mode",
        "Audio - Highpass Filter",
        "Highpass Filter",
        "Applies a filter to the audio output, removing certain pop sounds caused by the DC Offset. If disabled, the sound will be rendered as output by the Game Boy APU, but popping effects will be heard when the emulator is paused or resumed. 'Accurate' will apply a global filter, masking popping sounds while also reducing lower frequencies. 'Preserve Waveform' applies the filter only to the DC Offset.",
        NULL,
        "audio",
        {
            { "accurate",         "Accurate"              },
            { "remove dc offset", "Preserve Waveform"     },
            { "off",              "Disabled"              },
            { NULL, NULL },
        },
        "accurate"
    },
    {
        "sameboy_audio_interference",
        "Audio - Interference Volume",
        "Interference Volume",
        "Controls the volume of the buzzing effect caused by the electrical interference between the Game Boy SoC and the system speakers.",
        NULL,
        "audio",
        {
            { "0",   "0%"   },
            { "5",   "5%"   },
            { "10",  "10%"  },
            { "15",  "15%"  },
            { "20",  "20%"  },
            { "25",  "25%"  },
            { "30",  "30%"  },
            { "35",  "35%"  },
            { "40",  "40%"  },
            { "45",  "45%"  },
            { "50",  "50%"  },
            { "55",  "55%"  },
            { "60",  "60%"  },
            { "65",  "65%"  },
            { "70",  "70%"  },
            { "75",  "75%"  },
            { "80",  "80%"  },
            { "85",  "85%"  },
            { "90",  "90%"  },
            { "95",  "95%"  },
            { "100", "100%" },
            { NULL, NULL },
        },
        "0"
    },
    {
        "sameboy_rumble",
        "Input - Rumble Mode",
        "Rumble Mode",
        "Defines which type of content should trigger rumble effects.",
        NULL,
        "input",
        {
            { "all games",            "Always"                        },
            { "rumble-enabled games", "Only for rumble-enabled games" },
            { "never",                "Never"                      },
            { NULL, NULL },
        },
        "rumble-enabled games"
    },

    /* Core options used in dual cart mode */

    {
        "sameboy_link",
        "System - Link Cable Emulation",
        "Link Cable Emulation",
        "Enables the emulation of the link cable, allowing communication and exchange of data between two Game Boy systems.",
        NULL,
        "system",
        {
            { "enabled",  "Enabled" },
            { "disabled", "Disabled" },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        "sameboy_screen_layout",
        "System - Screen Layout",
        "Screen Layout",
        "When emulating two systems at once, this option defines the respective position of the two screens.",
        NULL,
        "system",
        {
            { "top-down",   "Top-Down"   },
            { "left-right", "Left-Right" },
            { NULL, NULL },
        },
        "top-down"
    },
    {
        "sameboy_audio_output",
        "System - Audio Output",
        "Audio Output",
        "Selects which of the two emulated Game Boy systems should output audio.",
        NULL,
        "system",
        {
            { "Game Boy #1", NULL },
            { "Game Boy #2", NULL },
            { NULL, NULL },
        },
        "Game Boy #1"
    },
    {
        "sameboy_model_1",
        "System - Emulated Model for Game Boy #1 (Requires Restart)",
        "Emulated Model for Game Boy #1 (Requires Restart)",
        "Chooses which system model the content should be started on for Game Boy #1. Certain games may activate special in-game features when ran on specific models. This option requires a content restart in order to take effect.",
        NULL,
        "system",
        {
            { "Auto",                 "Auto Detect DMG/CGB"          },
            { "Auto (SGB)",           "Auto Detect DMG/SGB/CGB"      },
            { "Game Boy",             "Game Boy (DMG-CPU B)"         },
            { "Game Boy Pocket",      "Game Boy Pocket/Light"        },
            { "Game Boy Color 0",     "Game Boy Color (CPU CGB 0)"   },
            { "Game Boy Color A",     "Game Boy Color (CPU CGB A)"   },
            { "Game Boy Color B",     "Game Boy Color (CPU CGB B)"   },
            { "Game Boy Color C",     "Game Boy Color (CPU CGB C)"   },
            { "Game Boy Color D",     "Game Boy Color (CPU CGB D)"   },
            { "Game Boy Color",       "Game Boy Color (CPU CGB E)"   },
            { "Game Boy Advance",     "Game Boy Advance (CPU AGB A)" },
            { "Game Boy Player",      "Game Boy Player (CPU AGB A)"  },
            { "Super Game Boy",       "Super Game Boy NTSC"          },
            { "Super Game Boy PAL",   NULL },
            { "Super Game Boy 2",     NULL },
            { NULL, NULL },
        },
        "Auto"
    },
    {
        "sameboy_auto_sgb_model_1",
        "System - Auto Detected SGB Model for Game Boy #1 (Requires Restart)",
        "Auto Detected SGB Model for Game Boy #1 (Requires Restart)",
        "Specifies which model of Super Game Boy hardware to emulate when SGB content is automatically detected for Game Boy #1. This option requires a content restart in order to take effect.",
        NULL,
        "system",
        {
            { "Super Game Boy",       "Super Game Boy NTSC" },
            { "Super Game Boy PAL",   NULL },
            { "Super Game Boy 2",     NULL },
            { NULL, NULL },
        },
        "Super Game Boy"
    },
    {
        "sameboy_model_2",
        "System - Emulated Model for Game Boy #2 (Requires Restart)",
        "Emulated Model for Game Boy #2 (Requires Restart)",
        "Chooses which system model the content should be started on for Game Boy #2. Certain games may activate special in-game features when ran on specific models. This option requires a content restart in order to take effect.",
        NULL,
        "system",
        {
            { "Auto",                 "Auto Detect DMG/CGB"          },
            { "Auto (SGB)",           "Auto Detect DMG/SGB/CGB"      },
            { "Game Boy",             "Game Boy (DMG-CPU B)"         },
            { "Game Boy Pocket",      "Game Boy Pocket/Light"        },
            { "Game Boy Color 0",     "Game Boy Color (CPU CGB 0)"   },
            { "Game Boy Color A",     "Game Boy Color (CPU CGB A)"   },
            { "Game Boy Color B",     "Game Boy Color (CPU CGB B)"   },
            { "Game Boy Color C",     "Game Boy Color (CPU CGB C)"   },
            { "Game Boy Color D",     "Game Boy Color (CPU CGB D)"   },
            { "Game Boy Color",       "Game Boy Color (CPU CGB E)"   },
            { "Game Boy Advance",     "Game Boy Advance (CPU AGB A)" },
            { "Game Boy Player",      "Game Boy Player (CPU AGB A)"  },
            { "Super Game Boy",       "Super Game Boy NTSC"          },
            { "Super Game Boy PAL",   NULL },
            { "Super Game Boy 2",     NULL },
            { NULL, NULL },
        },
        "Auto"
    },
    {
        "sameboy_auto_sgb_model_2",
        "System - Auto Detected SGB Model for Game Boy #2 (Requires Restart)",
        "Auto Detected SGB Model for Game Boy #2 (Requires Restart)",
        "Specifies which model of Super Game Boy hardware to emulate when SGB content is automatically detected for Game Boy #2. This option requires a content restart in order to take effect.",
        NULL,
        "system",
        {
            { "Super Game Boy",       "Super Game Boy NTSC" },
            { "Super Game Boy PAL",   NULL },
            { "Super Game Boy 2",     NULL },
            { NULL, NULL },
        },
        "Super Game Boy"
    },
    {
        "sameboy_mono_palette_1",
        "Video - GB Mono Palette for Game Boy #1",
        "GB Mono Palette for Game Boy #1",
        "Selects the color palette that should be used when playing Game Boy games on Game Boy #1.",
        NULL,
        "video",
        {
            { "greyscale", "Greyscale"               },
            { "lime",      "Lime (Game Boy)"         },
            { "olive",     "Olive (Game Boy Pocket)" },
            { "teal",      "Teal (Game Boy Light)"   },
            { NULL, NULL },
        },
        "greyscale"
    },
    {
        "sameboy_mono_palette_2",
        "Video - GB Mono Palette for Game Boy #2",
        "GB Mono Palette for Game Boy #2",
        "Selects the color palette that should be used when playing Game Boy games on Game Boy #2.",
        NULL,
        "video",
        {
            { "greyscale", "Greyscale"               },
            { "lime",      "Lime (Game Boy)"         },
            { "olive",     "Olive (Game Boy Pocket)" },
            { "teal",      "Teal (Game Boy Light)"   },
            { NULL, NULL },
        },
        "greyscale"
    },
    {
        "sameboy_color_correction_mode_1",
        "Video - GBC Color Correction for Game Boy #1",
        "GBC Color Correction for Game Boy #1",
        "Defines which type of color correction should be applied when playing Game Boy Color games on Game Boy #1.",
        NULL,
        "video",
        {
            { "emulate hardware",    "Modern – Balanced"            },
            { "accurate",            "Modern – Accurate"            },
            { "preserve brightness", "Modern – Boost Contrast"      },
            { "reduce contrast",     "Reduce Contrast"              },
            { "correct curves",      "Correct Color Curves"         },
            { "harsh reality",       "Harsh Reality (Low Contrast)" },
            { "off",                 "Disabled"                     },
            { NULL, NULL },
        },
        "emulate hardware"
    },
    {
        "sameboy_color_correction_mode_2",
        "Video - GBC Color Correction for Game Boy #2",
        "GBC Color Correction for Game Boy #2",
        "Defines which type of color correction should be applied when playing Game Boy Color games on Game Boy #2.",
        NULL,
        "video",
        {
            { "emulate hardware",    "Modern – Balanced"            },
            { "accurate",            "Modern – Accurate"            },
            { "preserve brightness", "Modern – Boost Contrast"      },
            { "reduce contrast",     "Reduce Contrast"              },
            { "correct curves",      "Correct Color Curves"         },
            { "harsh reality",       "Harsh Reality (Low Contrast)" },
            { "off",                 "Disabled"                     },
            { NULL, NULL },
        },
        "emulate hardware"
    },
    {
        "sameboy_light_temperature_1",
        "Video - Ambient Light Temperature for Game Boy #1",
        "Ambient Light Temperature for Game Boy #1",
        "Simulates an ambient light's effect on non-backlit Game Boy screens, by setting a user-controlled color temperature applied to the screen of Game Boy #1. This option has no effect if the content is run on an original Game Boy (DMG) emulated model.",
        NULL,
        "video",
        {
            { "1.0",   "1000K (Warmest)"       },
            { "0.9",   "1550K" },
            { "0.8",   "2100K" },
            { "0.7",   "2650K" },
            { "0.6",   "3200K" },
            { "0.5",   "3750K" },
            { "0.4",   "4300K" },
            { "0.3",   "4850K" },
            { "0.2",   "5400K" },
            { "0.1",   "5950K" },
            { "0",     "6500K (Neutral White)" },
            { "-0.1",  "7050K" },
            { "-0.2",  "7600K" },
            { "-0.3",  "8150K" },
            { "-0.4",  "8700K" },
            { "-0.5",  "9250K" },
            { "-0.6",  "9800K" },
            { "-0.7", "10350K" },
            { "-0.8", "10900K" },
            { "-0.9", "11450K" },
            { "-1.0", "12000K (Coolest)"       },
            { NULL, NULL },
        },
        "0"
    },
    {
        "sameboy_light_temperature_2",
        "Video - Ambient Light Temperature for Game Boy #2",
        "Ambient Light Temperature for Game Boy #2",
        "Simulates an ambient light's effect on non-backlit Game Boy screens, by setting a user-controlled color temperature applied to the screen of Game Boy #2. This option has no effect if the content is run on an original Game Boy (DMG) emulated model.",
        NULL,
        "video",
        {
            { "1.0",   "1000K (Warmest)"       },
            { "0.9",   "1550K" },
            { "0.8",   "2100K" },
            { "0.7",   "2650K" },
            { "0.6",   "3200K" },
            { "0.5",   "3750K" },
            { "0.4",   "4300K" },
            { "0.3",   "4850K" },
            { "0.2",   "5400K" },
            { "0.1",   "5950K" },
            { "0",     "6500K (Neutral White)" },
            { "-0.1",  "7050K" },
            { "-0.2",  "7600K" },
            { "-0.3",  "8150K" },
            { "-0.4",  "8700K" },
            { "-0.5",  "9250K" },
            { "-0.6",  "9800K" },
            { "-0.7", "10350K" },
            { "-0.8", "10900K" },
            { "-0.9", "11450K" },
            { "-1.0", "12000K (Coolest)"       },
            { NULL, NULL },
        },
        "0"
    },
    {
        "sameboy_high_pass_filter_mode_1",
        "Audio - Highpass Filter for Game Boy #1",
        "Highpass Filter for Game Boy #1",
        "Applies a filter to the audio output for Game Boy #1, removing certain pop sounds caused by the DC Offset. If disabled, the sound will be rendered as output by the Game Boy APU, but popping effects will be heard when the emulator is paused or resumed. 'Accurate' will apply a global filter, masking popping sounds while also reducing lower frequencies. 'Preserve Waveform' applies the filter only to the DC Offset.",
        NULL,
        "audio",
        {
            { "accurate",         "Accurate"              },
            { "remove dc offset", "Preserve Waveform"     },
            { "off",              "disabled"              },
            { NULL, NULL },
        },
        "accurate"
    },
    {
        "sameboy_high_pass_filter_mode_2",
        "Audio - Highpass Filter for Game Boy #2",
        "Highpass Filter for Game Boy #2",
        "Applies a filter to the audio output for Game Boy #2, removing certain pop sounds caused by the DC Offset. If disabled, the sound will be rendered as output by the Game Boy APU, but popping effects will be heard when the emulator is paused or resumed. 'Accurate' will apply a global filter, masking popping sounds while also reducing lower frequencies. 'Preserve Waveform' applies the filter only to the DC Offset.",
        NULL,
        "audio",
        {
            { "accurate",         "Accurate"              },
            { "remove dc offset", "Preserve Waveform"     },
            { "off",              "disabled"              },
            { NULL, NULL },
        },
        "accurate"
    },
    {
        "sameboy_audio_interference_1",
        "Audio - Interference Volume for Game Boy #1",
        "Interference Volume for Game Boy #1",
        "Controls the volume of the buzzing effect caused by the electrical interference between the Game Boy SoC and the system speakers on Game Boy #1.",
        NULL,
        "audio",
        {
            { "0",   "0%"   },
            { "5",   "5%"   },
            { "10",  "10%"  },
            { "15",  "15%"  },
            { "20",  "20%"  },
            { "25",  "25%"  },
            { "30",  "30%"  },
            { "35",  "35%"  },
            { "40",  "40%"  },
            { "45",  "45%"  },
            { "50",  "50%"  },
            { "55",  "55%"  },
            { "60",  "60%"  },
            { "65",  "65%"  },
            { "70",  "70%"  },
            { "75",  "75%"  },
            { "80",  "80%"  },
            { "85",  "85%"  },
            { "90",  "90%"  },
            { "95",  "95%"  },
            { "100", "100%" },
            { NULL, NULL },
        },
        "0"
    },
    {
        "sameboy_audio_interference_2",
        "Audio - Interference Volume for Game Boy #2",
        "Interference Volume for Game Boy #2",
        "Controls the volume of the buzzing effect caused by the electrical interference between the Game Boy SoC and the system speakers on Game Boy #2.",
        NULL,
        "audio",
        {
            { "0",   "0%"   },
            { "5",   "5%"   },
            { "10",  "10%"  },
            { "15",  "15%"  },
            { "20",  "20%"  },
            { "25",  "25%"  },
            { "30",  "30%"  },
            { "35",  "35%"  },
            { "40",  "40%"  },
            { "45",  "45%"  },
            { "50",  "50%"  },
            { "55",  "55%"  },
            { "60",  "60%"  },
            { "65",  "65%"  },
            { "70",  "70%"  },
            { "75",  "75%"  },
            { "80",  "80%"  },
            { "85",  "85%"  },
            { "90",  "90%"  },
            { "95",  "95%"  },
            { "100", "100%" },
            { NULL, NULL },
        },
        "0"
    },
    {
        "sameboy_rumble_1",
        "Input - Rumble Mode for Game Boy #1",
        "Rumble Mode for Game Boy #1",
        "Defines which type of content should trigger rumble effects when played on Game Boy #1.",
        NULL,
        "input",
        {
            { "all games",            "Always"                        },
            { "rumble-enabled games", "Only for rumble-enabled games" },
            { "never",                "Never"                         },
            { NULL, NULL },
        },
        "rumble-enabled games"
    },
    {
        "sameboy_rumble_2",
        "Input - Rumble Mode for Game Boy #2",
        "Rumble Mode for Game Boy #2",
        "Defines which type of content should trigger rumble effects when played on Game Boy #2.",
        NULL,
        "input",
        {
            { "all games",            "Always"                        },
            { "rumble-enabled games", "Only for rumble-enabled games" },
            { "never",                "Never"                         },
            { NULL, NULL },
        },
        "rumble-enabled games"
    },

    { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/*
 ********************************
 * Language Mapping
 ********************************
 */

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_options_v2 *options_intl[RETRO_LANGUAGE_LAST] = {
    &options_us,    /* RETRO_LANGUAGE_ENGLISH */
    NULL,           /* RETRO_LANGUAGE_JAPANESE */
    NULL,           /* RETRO_LANGUAGE_FRENCH */
    NULL,           /* RETRO_LANGUAGE_SPANISH */
    NULL,           /* RETRO_LANGUAGE_GERMAN */
    NULL,           /* RETRO_LANGUAGE_ITALIAN */
    NULL,           /* RETRO_LANGUAGE_DUTCH */
    NULL,           /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
    NULL,           /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
    NULL,           /* RETRO_LANGUAGE_RUSSIAN */
    NULL,           /* RETRO_LANGUAGE_KOREAN */
    NULL,           /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
    NULL,           /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
    NULL,           /* RETRO_LANGUAGE_ESPERANTO */
    NULL,           /* RETRO_LANGUAGE_POLISH */
    NULL,           /* RETRO_LANGUAGE_VIETNAMESE */
    NULL,           /* RETRO_LANGUAGE_ARABIC */
    NULL,           /* RETRO_LANGUAGE_GREEK */
    NULL,           /* RETRO_LANGUAGE_TURKISH */
    NULL,           /* RETRO_LANGUAGE_SLOVAK */
    NULL,           /* RETRO_LANGUAGE_PERSIAN */
    NULL,           /* RETRO_LANGUAGE_HEBREW */
    NULL,           /* RETRO_LANGUAGE_ASTURIAN */
    NULL,           /* RETRO_LANGUAGE_FINNISH */
};
#endif

/*
 ********************************
 * Functions
 ********************************
 */

/* Handles configuration/setting of core options.
 * Should be called as early as possible - ideally inside
 * retro_set_environment(), and no later than retro_load_game()
 * > We place the function body in the header to avoid the
 *   necessity of adding more .c files (i.e. want this to
 *   be as painless as possible for core devs)
 */

static INLINE void libretro_set_core_options(retro_environment_t environ_cb,
    bool *categories_supported)
{
    unsigned version  = 0;
#ifndef HAVE_NO_LANGEXTRA
    unsigned language = 0;
#endif

    if (!environ_cb || !categories_supported) return;

    *categories_supported = false;

    if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version)) version = 0;

    if (version >= 2) {
#ifndef HAVE_NO_LANGEXTRA
        struct retro_core_options_v2_intl core_options_intl;

        core_options_intl.us    = &options_us;
        core_options_intl.local = NULL;

        if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
            (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
            core_options_intl.local = options_intl[language];

        *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
                                           &core_options_intl);
#else
        *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
                                           &options_us);
#endif
    }
    else {
        size_t i, j;
        size_t option_index              = 0;
        size_t num_options               = 0;
        struct retro_core_option_definition
              *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
        size_t num_options_intl          = 0;
        struct retro_core_option_v2_definition
              *option_defs_intl          = NULL;
        struct retro_core_option_definition
              *option_v1_defs_intl       = NULL;
        struct retro_core_options_intl
              core_options_v1_intl;
#endif
        struct retro_variable *variables = NULL;
        char **values_buf                = NULL;

        /* Determine total number of options */
        while (true) {
            if (option_defs_us[num_options].key) num_options++;
            else break;
        }

        if (version >= 1) {
            /* Allocate US array */
            option_v1_defs_us = (struct retro_core_option_definition *) calloc(num_options + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_us array */
            for (i = 0; i < num_options; i++) {
                struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
                struct retro_core_option_value *option_values         = option_def_us->values;
                struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
                struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

                option_v1_def_us->key           = option_def_us->key;
                option_v1_def_us->desc          = option_def_us->desc;
                option_v1_def_us->info          = option_def_us->info;
                option_v1_def_us->default_value = option_def_us->default_value;

                /* Values must be copied individually... */
                while (option_values->value) {
                    option_v1_values->value = option_values->value;
                    option_v1_values->label = option_values->label;

                    option_values++;
                    option_v1_values++;
                }
            }

#ifndef HAVE_NO_LANGEXTRA
            if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
                (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
                options_intl[language])
                option_defs_intl = options_intl[language]->definitions;

            if (option_defs_intl) {
                /* Determine number of intl options */
                while (true) {
                    if (option_defs_intl[num_options_intl].key) num_options_intl++;
                    else break;
                }

                /* Allocate intl array */
                option_v1_defs_intl = (struct retro_core_option_definition *)
                      calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

                /* Copy parameters from option_defs_intl array */
                for (i = 0; i < num_options_intl; i++) {
                    struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
                    struct retro_core_option_value *option_values           = option_def_intl->values;
                    struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
                    struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

                    option_v1_def_intl->key           = option_def_intl->key;
                    option_v1_def_intl->desc          = option_def_intl->desc;
                    option_v1_def_intl->info          = option_def_intl->info;
                    option_v1_def_intl->default_value = option_def_intl->default_value;

                    /* Values must be copied individually... */
                    while (option_values->value) {
                        option_v1_values->value = option_values->value;
                        option_v1_values->label = option_values->label;

                        option_values++;
                        option_v1_values++;
                    }
                }
            }

            core_options_v1_intl.us    = option_v1_defs_us;
            core_options_v1_intl.local = option_v1_defs_intl;

            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
        }
        else {
            /* Allocate arrays */
            variables  = (struct retro_variable *)calloc(num_options + 1,
                 sizeof(struct retro_variable));
            values_buf = (char **)calloc(num_options, sizeof(char *));

            if (!variables || !values_buf) goto error;

            /* Copy parameters from option_defs_us array */
            for (i = 0; i < num_options; i++) {
                const char *key                        = option_defs_us[i].key;
                const char *desc                       = option_defs_us[i].desc;
                const char *default_value              = option_defs_us[i].default_value;
                struct retro_core_option_value *values = option_defs_us[i].values;
                size_t buf_len                         = 3;
                size_t default_index                   = 0;

                values_buf[i] = NULL;

                if (desc) {
                    size_t num_values = 0;

                    /* Determine number of values */
                    while (true) {
                        if (values[num_values].value) {
                            /* Check if this is the default value */
                            if (default_value) {
                                if (strcmp(values[num_values].value, default_value) == 0) default_index = num_values;

                                buf_len += strlen(values[num_values].value);
                                num_values++;
                            }
                        }
                        else break;
					}

                    /* Build values string */
                    if (num_values > 0) {
                        buf_len += num_values - 1;
                        buf_len += strlen(desc);

                        values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                        if (!values_buf[i]) goto error;

                        strcpy(values_buf[i], desc);
                        strcat(values_buf[i], "; ");

                        /* Default value goes first */
                        strcat(values_buf[i], values[default_index].value);

                        /* Add remaining values */
                        for (j = 0; j < num_values; j++) {
                            if (j != default_index) {
                                strcat(values_buf[i], "|");
                                strcat(values_buf[i], values[j].value);
                            }
                        }
                    }
                }

                variables[option_index].key   = key;
                variables[option_index].value = values_buf[i];
                option_index++;
            }

            /* Set variables */
            environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
        }

error:
        /* Clean up */

        if (option_v1_defs_us) {
            free(option_v1_defs_us);
            option_v1_defs_us = NULL;
        }

#ifndef HAVE_NO_LANGEXTRA
        if (option_v1_defs_intl) {
            free(option_v1_defs_intl);
            option_v1_defs_intl = NULL;
        }
#endif

        if (values_buf) {
            for (i = 0; i < num_options; i++) {
                if (values_buf[i]) {
                    free(values_buf[i]);
                    values_buf[i] = NULL;
                }
            }

            free(values_buf);
            values_buf = NULL;
        }

        if (variables) {
            free(variables);
            variables = NULL;
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif
