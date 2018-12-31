#ifndef wide_gb_h
#define wide_gb_h

#include <SDL2/SDL.h>
#include  <stdbool.h>

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
// 3. Render the tiles over the canvas
//    using `WGB_tiles_count` and `WGB_tile_at_index` to enumerate the tiles
// 4. Render the console screen over the tiles

#define WIDE_GB_ENABLED true

#define WIDE_GB_DEBUG false
#define WIDE_GB_MAX_TILES 512

/*---------------- Utils -------------------------------------------------*/

SDL_Point WGB_offset_point(SDL_Point point, SDL_Point offset);
SDL_Rect WGB_offset_rect(SDL_Rect rect, SDL_Point offset);
SDL_Rect WGB_scale_rect(SDL_Rect rect, double dx, double dy);
bool WGB_rect_contains_point(SDL_Rect rect, SDL_Point point);

/*---------------- Data definitions --------------------------------------*/

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

// Main WideGB struct.
// Initialize with WGB_init().
typedef struct {
    SDL_Point logical_pos;
    SDL_Point hardware_pos;
    SDL_Rect window_rect;
    bool window_enabled;
    WGB_tile tiles[WIDE_GB_MAX_TILES];
    size_t tiles_count;
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
//
// The updated tiles are marked as `dirty`.
void WGB_update_screen(wide_gb *wgb, uint32_t *pixels);

/*---------------- Retrieving informations for rendering -----------------*/

// Return the rectangle of the current Game Boy window
SDL_Rect WGB_get_window_rect(wide_gb *wgb);

// Enumerate tiles
int WGB_tiles_count(wide_gb *wgb);
WGB_tile* WGB_tile_at_index(wide_gb *wgb, int index);

// Layout tiles
SDL_Rect WGB_rect_for_tile(wide_gb *wgb, WGB_tile *tile);

/*---------------- Cleanup ----------------------------------------------*/

// Free tiles and memory used by the struct.
// The struct cannot be used again after this.
void WGB_destroy(wide_gb *wgb);

#endif
