#ifndef wide_gb_h
#define wide_gb_h

#include <SDL2/SDL.h>
#include <Misc/uthash.h>
#include <stdbool.h>

// This file implements an engine for recording and displaying
// extended scenes on a canvas in a Game Boy emulator, in a
// library-agnostic way (except for geometric SDL data-types).
//
// It provides:
//  - basic data types
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
//    using `WGB_update_hardware_scroll` and `WGB_update_screen`
// 3. Render the visible tiles over the canvas, using:
//    - `WGB_tiles_count` and `WGB_tile_at_index` to enumerate the tiles,
//    - `WGB_is_tile_visible` to cull tiles not visible on screen,
//    - `tile->is_dirty` to tell whether the tile pixel buffer has been updated,
//    - `WGB_rect_for_tile` to draw the tile using your frontend drawing library ;
// 4. Render the console screen over the tiles


// TODO:
// - Save/restore scenes on the disk
// - OpenGL / Cocoa implementation
// - Implement own hash table?
// - Better dynamic arrays?

/*---------------- Data definitions --------------------------------------*/

#define WIDE_GB_MAX_TILES 512
#define WIDE_GB_MAX_SCENES 512

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

typedef struct {
    int id;
    SDL_Point scroll;
    WGB_tile tiles[WIDE_GB_MAX_TILES];
    size_t tiles_count;
    time_t created_at;
} WGB_scene;

typedef uint64_t WGB_exact_hash;
typedef uint64_t WGB_perceptual_hash;

typedef struct {
    int scene_id;
    SDL_Point scene_scroll;
    WGB_exact_hash frame_hash;
    UT_hash_handle hh;
} WGB_scene_frame;

// Main WideGB struct.
// Initialize with WGB_init().
typedef struct {
    SDL_Point hardware_scroll;
    SDL_Rect window_rect;
    bool window_enabled;
    WGB_perceptual_hash frame_perceptual_hash;
    WGB_scene *active_scene;
    WGB_scene scenes[WIDE_GB_MAX_SCENES];
    size_t scenes_count;
    WGB_scene_frame *scene_frames; // a <frame_hash, WGB_scene_frame> map
    int find_existing_scene_countdown;
} wide_gb;

/*---------------- Initializing ------------------------------------------*/

// Return a new initialized wide_gb struct
wide_gb WGB_init();

/*---------------- Updating from hardware --------------------------------*/

// Notify WGB of the new hardware scroll registers values.
// Typically called at vblank.
void WGB_update_hardware_scroll(wide_gb *wgb, int scx, int scy);

// Notify WGB of the new Game Boy Window status and position.
// Typically called at vblank.
//
// This is used to avoid writing the Window area to the tiles
// (as the window area is most often overlapped UI).
void WGB_update_window_position(wide_gb *wgb, bool is_window_enabled, int wx, int wy);

// Write the screen content to the relevant tiles.
// Typically called at vblank.
//
// This function uses the logical scroll position and window position
// to write pixels on the correct tiles – so `WGB_update_hardware_scroll`
// must be called before to set the current scroll position.
//
// Inputs:
//   - pixels: an array of 160*144 uint32 opaque values, written as-this to the relevant tiles.
//   - hash: an exact hash of the pixels. If any of the pixels changes, the hash should be different.
//   - p_hash: a perceptual hash of the pixels. If the p_hash is too different from the previous one,
//             WGB creates a new scene.
//
// On return, the updated tiles are marked as `dirty`.
void WGB_update_screen(wide_gb *wgb, uint32_t *pixels, WGB_exact_hash hash, WGB_perceptual_hash p_hash);

/*---------------- Retrieving informations for rendering -----------------*/

// Enumerate tiles
size_t WGB_tiles_count(wide_gb *wgb);
WGB_tile* WGB_tile_at_index(wide_gb *wgb, int index);

// Layout tiles

// Returns true if a tile is visible in the current viewport.
// Viewport is in screen-space (i.e. like { -160, -160, 480, 432 }
// for a window twice as large as the console screen).
bool WGB_is_tile_visible(wide_gb *wgb, WGB_tile *tile, SDL_Rect viewport);
// Returns the rect of the tile in screen-space
SDL_Rect WGB_rect_for_tile(wide_gb *wgb, WGB_tile *tile);

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
void WGB_get_screen_layout(wide_gb *wgb, SDL_Rect *bg_rect1, SDL_Rect *bg_rect2, SDL_Rect *wnd_rect);
// Return true if the window is enabled and entirely covering the screen.
// Some games slighly shake the window at times (e.g. Pokémon):
// you can use `tolered_pixels` to return `true` even if the window is not
// entirerly overlapping the screen.
bool WGB_is_window_covering_screen(wide_gb *wgb, uint tolered_pixels);

/*---------------- Geometry helpers --------------------------------------*/

SDL_Point WGB_offset_point(SDL_Point point, SDL_Point offset);
SDL_Rect WGB_offset_rect(SDL_Rect rect, int dx, int dy);
SDL_Rect WGB_scale_rect(SDL_Rect rect, double dx, double dy);
bool WGB_rect_contains_point(SDL_Rect rect, SDL_Point point);
bool WGB_rect_intersects_rect(SDL_Rect rect1, SDL_Rect rect2);

/*---------------- Frame hashing helpers --------------------------------*/

// Compute an exact hash of a frame. This is used to identify if a frame belongs
// to an already stored scene.
//
// As the window content is often not relevant to know if a given screen
// is the same than another, pixels in the window area are excluded from the hash.
//
// `rgb_pixels` must be a 160 * 144 * 3 array, where each consecutive triplet
// store the r, g and b values for a pixel.
WGB_exact_hash WGB_frame_hash(wide_gb *wgb, uint8_t *rgb_pixels);

// Compute a perceptual hash of a frame using the "added difference hash" algorithm.
//
// This algorith detects how many blocks are brighter than the adjascent block.
// The resulting hash is not very sensitive to translations (great for allowing scrolling),
// but quite sensitive to luminance changes (great for detecting fade transitions).
//
// `rgb_pixels` must be a 160 * 144 * 3 array, where each consecutive triplet
// store the r, g and b values for a pixel.
WGB_perceptual_hash WGB_added_difference_hash(wide_gb *wgb, uint8_t *rgb_pixels);

/*---------------- Cleanup ----------------------------------------------*/

// Free tiles and memory used by the struct.
// The struct cannot be used again after this.
void WGB_destroy(wide_gb *wgb);

#endif
