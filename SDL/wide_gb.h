#ifndef wide_gb_h
#define wide_gb_h

#include <SDL2/SDL.h>
#include  <stdbool.h>

#define WIDE_GB_ENABLED true

#define WIDE_GB_DEBUG false
#define WIDE_GB_MAX_TILES 512

typedef struct wide_gb wide_gb;
typedef struct WGB_tile WGB_tile;

/*---------------- Utils -------------------------------------------------*/

// The position of a screen-wide tile, as a number of screens relative
// to the origin.
typedef struct {
    int horizontal;
    int vertical;
} WGB_tile_position;

bool WGB_tile_position_equal_to(WGB_tile_position position1, WGB_tile_position position2);

SDL_Point WGB_tile_point_from_screen_point(wide_gb *wgb, SDL_Point screen_point, WGB_tile_position target_tile);
WGB_tile_position WGB_tile_position_from_screen_point(wide_gb *wgb, SDL_Point screen_point);

SDL_Point WGB_offset_point(SDL_Point point, SDL_Point offset);
SDL_Rect WGB_offset_rect(SDL_Rect rect, SDL_Point offset);
SDL_Rect WGB_scale_rect(SDL_Rect rect, double dx, double dy);
bool WGB_rect_contains_point(SDL_Rect rect, SDL_Point point);

/*---------------- Data definitions --------------------------------------*/

// A tile is a recorded framebuffer the size of the screen.
struct WGB_tile {
    WGB_tile_position position;
    uint32_t *pixel_buffer;
    bool dirty;
};

// Main WideGB struct.
// Initialize with WGB_init().
struct wide_gb {
    SDL_Point logical_pos;
    SDL_Point hardware_pos;
    SDL_Rect window_rect;
    bool window_enabled;
    WGB_tile tiles[WIDE_GB_MAX_TILES];
    size_t tiles_count;
};

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
void WGB_write_screen(wide_gb *wgb, uint32_t *pixels);

/*---------------- Retrieving informations for rendering -----------------*/

// Return the current logical scroll position
SDL_Point WGB_get_logical_scroll(wide_gb *wgb);
// Return the rectangle of the current window
SDL_Rect WGB_get_window_rect(wide_gb *wgb);

// Enumerate tiles
int WGB_tiles_count(wide_gb *wgb);
int WGB_index_of_tile(wide_gb *wgb, WGB_tile *tile);
WGB_tile* WGB_tile_at_index(wide_gb *wgb, int index);
SDL_Rect WGB_rect_for_tile(wide_gb *wgb, WGB_tile *tile);

// Find a tile
WGB_tile* WGB_tile_at_point(wide_gb *wgb, SDL_Point screen_point);

/*---------------- Cleanup ----------------------------------------------*/

// Free tiles and memory used by the struct.
// The struct cannot be used again after this.
void WGB_destroy(wide_gb *wgb);

#endif
