#ifndef wide_gb_h
#define wide_gb_h

#include <stdbool.h>
#include "uthash.h"

// This file implements an engine for recording and displaying
// extended scenes on a canvas in a Game Boy emulator, in a
// library-agnostic way.
//
// It provides:
//  - basic data types (equal to the SDL geometric types if the SDL is included)
//  - conversion of coordinates between screen-space and logical-scroll space
//  - screen-updating logic
//
// How it works
// ============
//
// When the Game Boy background scrolls or wraps around, the engine keeps track
// of the logical scroll position.
//
// When the console screen is updated, pixels are recorded on screen-sized tiles,
// which are then laid out continuousely using the logical scroll position.
//
// The engine exposes the tiles, which the UI client uses to display the recorded
// screens to the user.
//
// How to use
// ==========
//
// To implement WideGB in your own emulator, the basic steps are:
//
// 1. Create a global wide_gb struct using `WGB_init`
// 2. On V-blank, notify WideGB of the updates
//    using `WGB_update_hardware_values` and `WGB_update_screen`
// 3. Render the visible tiles over the canvas, using:
//    - `WGB_tiles_count` and `WGB_tile_at_index` to enumerate the tiles,
//    - `WGB_is_tile_visible` to cull tiles not visible on screen,
//    - `tile->dirty` to tell whether the tile pixel buffer has been updated,
//    - `WGB_rect_for_tile` to draw the tile using your frontend drawing library ;
// 4. Render the console screen over the tiles


// TODO:
// - Performances
//   - don't query tiles for each written pixel
//   - optimize hashes
// - Improve the scene transitions detection algorithm:
//   - Currently it glitches on zelda pause menu
//   - Ensures it doesn't break Pokemon Red/Blue (which has some worst-case transitions)
// - Use dynamic memory allocation for scenes (will reduce stack size and hardcoded limits)
// - Implement our own hash table?

/*---------------- Data definitions --------------------------------------*/

#define WIDE_GB_MAX_TILES 512
#define WIDE_GB_MAX_SCENES 512
#define WIDE_GB_TILE_WIDTH 160
#define WIDE_GB_TILE_HEIGHT 144

#ifdef SDL_INIT_EVERYTHING
#define WGB_Rect SDL_Rect
#define WGB_Point SDL_Point
#else
typedef struct { int x, y, w, h; } WGB_Rect;
typedef struct { int x, y; } WGB_Point;
#endif


// The position of a screen-wide tile, as a number of screens relative
// to the origin.
typedef struct {
    int horizontal;
    int vertical;
} WGB_tile_position;

// A tile is a recorded framebuffer the size of the screen.
typedef struct {
    WGB_tile_position position;
    uint32_t *pixel_buffer;
    bool dirty;
} WGB_tile;

// A scene is a group of connected tiles.
typedef struct {
    int id;
    WGB_Point scroll;
    WGB_tile tiles[WIDE_GB_MAX_TILES];
    size_t tiles_count;
    time_t created_at;
} WGB_scene;

typedef uint64_t WGB_exact_hash;
typedef uint64_t WGB_perceptual_hash;

// A scene frame connects the hash of a specific frame to the scene it belongs to.
typedef struct {
    int scene_id;
    WGB_Point scene_scroll;
    WGB_exact_hash frame_hash;
    UT_hash_handle hh;
} WGB_scene_frame;

// Main WideGB struct.
// Initialize with WGB_init().
typedef struct {
    WGB_Point hardware_scroll;
    WGB_Point scroll_delta;
    WGB_Rect window_rect;
    bool window_enabled;
    WGB_exact_hash frame_hash;
    WGB_perceptual_hash frame_perceptual_hash;
    WGB_scene *active_scene;
    int next_scene_id;
    WGB_scene scenes[WIDE_GB_MAX_SCENES];
    size_t scenes_count;
    WGB_scene_frame *scene_frames; // a <frame_hash, WGB_scene_frame> map
} wide_gb;

// A pointer to a function that takes an opaque uint32 value and decode it into RGB components
typedef void (*WGB_rgb_decode_callback_t)(uint32_t encoded, uint8_t *r, uint8_t *g, uint8_t *b);

// A pointer to a function that takes RGB components and encode it into the pixel format expected by the texture handler
typedef uint32_t (*WGB_rgb_encode_callback_t)(uint8_t r, uint8_t g, uint8_t b);

/*---------------- Initializing ------------------------------------------*/

// Return a initialized wide_gb struct with the content of previously
// saved data.
// If the path does not contain valid save data, a new wide_gb struct is returned.
wide_gb WGB_init_from_path(const char *save_path, WGB_rgb_encode_callback_t rgb_encode);

// Save WideGB data to the given path.
// if the path already exists, it will be overwritten.
void WGB_save_to_path(wide_gb *wgb, const char *save_path, WGB_rgb_decode_callback_t rgb_decode);

/*---------------- Updating from hardware --------------------------------*/

// Notify WGB of the new hardware registers values.
// Typically called at vblank.
//
// Inputs:
//   - scx: the SCX (ScrollX) Game Boy register value
//   - scy: the SCY (ScrollY) Game Boy register value
//   - wx: the WX (WindowX) Game Boy register value
//   - wy: the WY (WindowY) Game Boy register value
//   - is_window_enabled: the Game Boy register flag indicating that the Window is enabled (see LCDC)
void WGB_update_hardware_values(wide_gb *wgb, int scx, int scy, int wx, int wy, bool is_window_enabled);

// Write the screen content to the relevant tiles.
// Typically called at vblank.
//
// This function uses the logical scroll position and window position
// to write pixels on the correct tiles – so `WGB_update_hardware_values`
// must be called before.
//
// Inputs:
//   - pixels: a array of 160 * 144 values, which can be decoded to RGB components
//             using the `rgb_decode` function.
//   - rgb_decode: a callback for decoding the pixels to RGB components
//
// On return, the updated tiles are marked as `dirty`.
void WGB_update_screen(wide_gb *wgb, uint32_t *pixels, WGB_rgb_decode_callback_t rgb_decode);

/*---------------- Retrieving informations for rendering -----------------*/

// Enumerate tiles
size_t WGB_tiles_count(wide_gb *wgb);
WGB_tile* WGB_tile_at_index(wide_gb *wgb, int index);

// Layout tiles

// Returns true if a tile is visible in the current viewport.
// Viewport is in screen-space (i.e. like { -160, -160, 480, 432 }
// for a window twice as large as the console screen).
bool WGB_is_tile_visible(wide_gb *wgb, WGB_tile *tile, WGB_Rect viewport);

// Returns the rect of the tile in screen-space
WGB_Rect WGB_rect_for_tile(wide_gb *wgb, WGB_tile *tile);

// Layout screen (optional)

// These helpers can help you to draw the background-part and the window-part
// of the screen separately.
//
// The Background may be partially overlapped by the Game Boy window.
// If we want to draw the background and window separately, we potentially
// need two rects for the background, and one for the window.
//
// +-----------------+
// |                 |
// |   part1         |
// |                 |
// |.......+---------|
// | part2 |  window |
// +-----------------+

// Return the different screen areas:
//  - first part of the background area,
//  - second part of the backgroud area,
//  - area overlapped by window (if the Game Boy window is enabled)
// Depending on how the window is positionned,
// some of these areas may have a width or a height of 0.
void WGB_get_screen_layout(wide_gb *wgb, WGB_Rect *bg_rect1, WGB_Rect *bg_rect2, WGB_Rect *wnd_rect);

// Return true if the window is enabled and entirely covering the screen.
// Some games slighly shake the window at times (e.g. Pokémon):
// you can use `tolered_pixels` to return `true` even if the window is not
// entirerly overlapping the screen.

bool WGB_is_window_covering_screen(wide_gb *wgb, unsigned int tolered_pixels);

/*---------------- Geometry helpers --------------------------------------*/

WGB_Point WGB_offset_point(WGB_Point point, WGB_Point offset);
WGB_Rect WGB_offset_rect(WGB_Rect rect, int dx, int dy);
WGB_Rect WGB_scale_rect(WGB_Rect rect, double dx, double dy);
bool WGB_rect_contains_point(WGB_Rect rect, WGB_Point point);
bool WGB_rect_intersects_rect(WGB_Rect rect1, WGB_Rect rect2);

/*---------------- Cleanup ----------------------------------------------*/

// Free tiles and memory used by the struct.
// The struct cannot be used again after this.
void WGB_destroy(wide_gb *wgb);

#endif
