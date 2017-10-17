#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <stdarg.h>

#define AUDIO_FREQUENCY 44100

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define snprintf _snprintf
#endif

#include <Core/gb.h>
#include "libretro.h"

#ifdef _WIN32
   char slash = '\\';
#else
   char slash = '/';
#endif

#define VIDEO_WIDTH 160
#define VIDEO_HEIGHT 144
#define VIDEO_PIXELS VIDEO_WIDTH * VIDEO_HEIGHT 

char battery_save_path[512]; 
char symbols_path[512];

static uint32_t *frame_buf;
static struct retro_log_callback logging;
static retro_log_printf_t log_cb;

static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

signed short soundbuf[1024*2];

char retro_system_directory[4096];
char retro_save_directory[4096];
char retro_game_path[4096];
int RLOOP=1;

GB_gameboy_t gb;
extern const unsigned char dmg_boot[], cgb_boot[];
extern const unsigned dmg_boot_length, cgb_boot_length;

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   va_list va;
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}

static void replace_extension(const char *src, size_t length, char *dest, const char *ext)
{
    memcpy(dest, src, length);
    dest[length] = 0;

    /* Remove extension */
    for (size_t i = length; i--;) {
        if (dest[i] == '/') break;
        if (dest[i] == '.') {
            dest[i] = 0;
            break;
        }
    }

    /* Add new extension */
    strcat(dest, ext);
}

static struct retro_rumble_interface rumble;

static void GB_update_keys_status(GB_gameboy_t *gb)
{

	input_poll_cb();

	GB_set_key_state(gb, GB_KEY_RIGHT,input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT));
	GB_set_key_state(gb, GB_KEY_LEFT, input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT));
	GB_set_key_state(gb, GB_KEY_UP,input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP) );
	GB_set_key_state(gb, GB_KEY_DOWN,input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN));
	GB_set_key_state(gb, GB_KEY_A,input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) );
	GB_set_key_state(gb, GB_KEY_B,input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) );
	GB_set_key_state(gb, GB_KEY_SELECT,input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT));
   GB_set_key_state(gb, GB_KEY_START,input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) );
   
   if (gb->rumble_state)
      rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, 65535);
   else
      rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, 0);

}


static void audio_callback(void *gb)
{
   GB_apu_copy_buffer(gb, (GB_sample_t *) soundbuf, (float)AUDIO_FREQUENCY / 59.72);
   audio_batch_cb(soundbuf, (float)AUDIO_FREQUENCY / 59.72);
}


static void vblank(GB_gameboy_t *gb)
{
   GB_update_keys_status(gb);
   GB_set_pixels_output(gb, frame_buf);
   audio_callback(gb);
}

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return r<<16|g<<8|b;
}

#ifndef DISABLE_DEBUGGER
static void debugger_interrupt(int ignore)
{
    /* ^C twice to exit */
    if (GB_debugger_is_stopped(&gb))
        exit(0);
    GB_debugger_break(&gb);
}
#endif

static retro_environment_t environ_cb;

void retro_init(void)
{
   frame_buf = (uint32_t*)malloc(VIDEO_PIXELS * sizeof(uint32_t));
   const char *dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
      snprintf(retro_system_directory, sizeof(retro_system_directory), "%s", dir);
   else
      snprintf(retro_system_directory, sizeof(retro_system_directory), "%s", ".");

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
      snprintf(retro_save_directory, sizeof(retro_save_directory), "%s", dir);
   else
      snprintf(retro_save_directory, sizeof(retro_save_directory), "%s", ".");
}

void retro_deinit(void)
{
   free(frame_buf);
   frame_buf = NULL;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   log_cb(RETRO_LOG_INFO, "Plugging device %u into port %u.\n", device, port);
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "SameBoy";
   info->library_version  = "0.9";
   info->need_fullpath    = true;
   info->valid_extensions = "gb|gbc";
}


void retro_get_system_av_info(struct retro_system_av_info *info)
{
   struct retro_game_geometry geom = { VIDEO_WIDTH, VIDEO_HEIGHT,VIDEO_WIDTH, VIDEO_HEIGHT ,160.0 / 144.0 };
   struct retro_system_timing timing = { 59.72, 44100.0 };

   info->geometry = geom;
   info->timing   = timing;

}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
      log_cb = logging.log;
   else
      log_cb = fallback_log;

   static const struct retro_controller_description controllers[] = {
      { "Nintendo Gameboy", RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0) },
   };

   static const struct retro_controller_info ports[] = {
      { controllers, 1 },
      { NULL, 0 },
   };
   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_reset(void)
{
   GB_reset(&gb);
}

static void check_variables(void)
{
   struct retro_variable var = {0};

   var.key = "sameboy_color_correction_mode";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && GB_is_cgb(&gb))
   {
      if (strcmp(var.value, "off") == 0)
         GB_set_color_correction_mode(&gb, GB_COLOR_CORRECTION_DISABLED);
      else if (strcmp(var.value, "correct curves") == 0)
         GB_set_color_correction_mode(&gb, GB_COLOR_CORRECTION_CORRECT_CURVES);
      else if (strcmp(var.value, "emulate hardware") == 0)
         GB_set_color_correction_mode(&gb, GB_COLOR_CORRECTION_EMULATE_HARDWARE);
      else if (strcmp(var.value, "preserve brightness") == 0)
         GB_set_color_correction_mode(&gb, GB_COLOR_CORRECTION_PRESERVE_BRIGHTNESS);
   }

   var.key = "sameboy_high_pass_filter_mode";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "off") == 0)
         GB_set_highpass_filter_mode(&gb, GB_HIGHPASS_OFF);
      else if (strcmp(var.value, "accurate") == 0)
         GB_set_highpass_filter_mode(&gb, GB_HIGHPASS_ACCURATE);
      else if (strcmp(var.value, "remove dc offset") == 0)
         GB_set_highpass_filter_mode(&gb, GB_HIGHPASS_REMOVE_DC_OFFSET);
   }
}

void retro_run(void)
{
   static int frames;
   size_t samples;

   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();
   samples = GB_apu_get_current_buffer_length(&gb);
   if (!(frames < (samples / 35112)))
   {
      GB_run_frame(&gb);
      frames ++;
   }
   else
      frames = 0;

   video_cb(frame_buf, VIDEO_WIDTH, VIDEO_HEIGHT, 0);
}

bool retro_load_game(const struct retro_game_info *info)
{
   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 0 },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
      return false;
   }

   snprintf(retro_game_path, sizeof(retro_game_path), "%s", info->path);

   char buf[256];
   int err = 0;
   if (!strstr(info->path, "gbc"))
   {
      GB_init(&gb);
      snprintf(buf, sizeof(buf), "%s%cdmg_boot.bin", retro_system_directory, slash);
      log_cb(RETRO_LOG_INFO, "Loading boot image: %s\n", buf);
      err = GB_load_boot_rom(&gb, buf);

      if (err) {
         GB_load_boot_rom_from_buffer(&gb, dmg_boot, dmg_boot_length);
         err = 0;
      }
   }
   else 
   {
      GB_init_cgb(&gb);
      snprintf(buf, sizeof(buf), "%s%ccgb_boot.bin", retro_system_directory, slash);
      log_cb(RETRO_LOG_INFO, "Loading boot image: %s\n", buf);
      err = GB_load_boot_rom(&gb, buf);

      if (err) {
         GB_load_boot_rom_from_buffer(&gb, cgb_boot, cgb_boot_length);
         err = 0;
      }
   }
   if (err) 
      log_cb(RETRO_LOG_INFO, "Failed to load boot ROM %s %d\n", buf, err);
   (void)info;

   if (GB_load_rom(&gb,info->path)) {
        perror("Failed to load ROM");
        exit(1);
   }

   GB_set_vblank_callback(&gb, (GB_vblank_callback_t) vblank);
   GB_set_user_data(&gb, (void*)NULL);
   GB_set_pixels_output(&gb,(unsigned int*)frame_buf);
   GB_set_rgb_encode_callback(&gb, rgb_encode);

   size_t path_length = strlen(retro_game_path);

#ifndef DISABLE_DEBUGGER
   {
      char TMPC[512];
      sprintf(TMPC,"%s/registers.sym",retro_system_directory);
      GB_debugger_load_symbol_file(&gb, TMPC);
   }
#endif
 
   replace_extension(retro_game_path, path_length, symbols_path, ".sym");

#ifndef DISABLE_DEBUGGER
   GB_debugger_load_symbol_file(&gb, symbols_path);
#endif

   GB_set_sample_rate(&gb, AUDIO_FREQUENCY);

   struct retro_memory_descriptor descs[7];
   size_t size;
   uint16_t bank;

   memset(descs, 0, sizeof(descs));

   descs[0].ptr   = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_IE, &size, &bank);
   descs[0].start = 0xFFFF;
   descs[0].len   = 1;

   descs[1].ptr   = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_HRAM, &size, &bank);
   descs[1].start = 0xFF80;
   descs[1].len   = 0x0080;
   
   descs[2].ptr   = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_RAM, &size, &bank);
   descs[2].start = 0xC000;
   descs[2].len   = 0x2000;
      
   descs[3].ptr   = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_CART_RAM, &size, &bank);
   descs[3].start = 0xA000;
   descs[3].len   = 0x2000;
      
   descs[4].ptr   = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_VRAM, &size, &bank);
   descs[4].start = 0x8000;
   descs[4].len   = 0x2000;
      
   descs[5].ptr   = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_ROM, &size, &bank);
   descs[5].start = 0x0000;
   descs[5].len   = 0x4000;
   descs[5].flags = RETRO_MEMDESC_CONST;

   descs[6].ptr   = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_OAM, &size, &bank);
   descs[6].start = 0xFE00;
   descs[6].len   = 0x00A0;
      
   struct retro_memory_map mmaps;
   mmaps.descriptors = descs;
   mmaps.num_descriptors = sizeof(descs) / sizeof(descs[0]);
   environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmaps);

   bool yes = true;
   environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &yes);

   if (environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble))
      log_cb(RETRO_LOG_INFO, "Rumble environment supported.\n");
   else
      log_cb(RETRO_LOG_INFO, "Rumble environment not supported.\n");

   static struct retro_variable vars_cgb[] = {
      { "sameboy_color_correction_mode", "Color Correction; off|correct curves|emulate hardware|preserve brightness" },
      { "sameboy_high_pass_filter_mode", "High Pass Filter; off|accurate|remove dc offset" },
      { NULL }
   };

   static struct retro_variable vars_dmg[] = {
      { "sameboy_high_pass_filter_mode", "High Pass Filter; off|accurate|remove dc offset" },
      { NULL }
   };

   if (GB_is_cgb(&gb))
      environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, vars_cgb);
   else
      environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, vars_dmg);
   check_variables();

   return true;
}

void retro_unload_game(void)
{
   GB_free(&gb);
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   return false;
}

size_t retro_serialize_size(void)
{
   return GB_get_save_state_size(&gb);
}

bool retro_serialize(void *data, size_t size)
{
   GB_save_state_to_buffer(&gb, (uint8_t*) data);
   if (data)
      return true;
   else
      return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   if (GB_load_state_from_buffer(&gb, (uint8_t*) data, size) == 0)
      return true;
   else
      return false;
}

void *retro_get_memory_data(unsigned type)
{
   void* data;
   switch(type)
   {
      case RETRO_MEMORY_SAVE_RAM:
         if (gb.cartridge_type->has_battery && gb.mbc_ram_size != 0)
            data = gb.mbc_ram;
         else
            data = NULL;
         break;
      case RETRO_MEMORY_RTC:
         if(gb.cartridge_type->has_battery)
            data = &gb.rtc_real;
         else
            data = NULL;
         break;
     default:
         data = NULL;
       break;
   }
   
   return data;
}

size_t retro_get_memory_size(unsigned type)
{
   size_t size;
   switch(type)
   {
      case RETRO_MEMORY_SAVE_RAM:
         if (gb.cartridge_type->has_battery && gb.mbc_ram_size != 0)
            size = gb.mbc_ram_size;
         else
            size = 0;
         break;
      case RETRO_MEMORY_RTC:
         if(gb.cartridge_type->has_battery)
            size = sizeof (gb.rtc_real);
         else
            size =  0;
         break;
     default:
        size = 0;
       break;
   }
   
   return size;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

