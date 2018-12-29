#ifndef wide_gb_h
#define wide_gb_h

#include <SDL2/SDL.h>
#include  <stdbool.h>

#define WIDE_GB_ENABLED true

#define WIDE_GB_DEBUG false
#define WIDE_GB_MAX_TILES 512

/*---------------- Utils -------------------------------------------------*/

// The position of a screen-wide tile, as a number of screens relative
// to the origin.
typedef struct {
    int horizontal;
    int vertical;
} WGB_tile_position;

bool WGB_tile_position_equal_to(WGB_tile_position position1, WGB_tile_position position2);

/*---------------- Data definitions --------------------------------------*/

// A tile is a recorded framebuffer the size of the screen.
typedef struct {
    WGB_tile_position position;
    uint32_t *pixel_buffer;
} WGB_tile;

// Main WideGB struct.
// Initialize with WGB_init().
typedef struct {
    SDL_Point logical_pos;
    SDL_Point hardware_pos;
    WGB_tile tiles[WIDE_GB_MAX_TILES];
    int tiles_count;
} wide_gb;

/*---------------- Initializing ------------------------------------------*/

// Return a new initialized wide_gb struct
wide_gb WGB_init();

/*---------------- Updating from hardware --------------------------------*/

// Callback when the hardware scroll registers are updated
void WGB_update_hardware_scroll(wide_gb *wgb, int scx, int scy);

// Set a specific pixel on a given tile.
// The tile is created if it doesn't exist yet.
void WGB_write_tile_pixel(wide_gb *wgb, WGB_tile_position tile_pos, SDL_Point pixel_pos, uint32_t pixel);

/*---------------- Retrieving informations for rendering -----------------*/

// Return the current logical scroll position, taking into account:
// - screen wrapping
// - padding (todo)
SDL_Point WGB_get_logical_scroll(wide_gb *wgb);

// Enumerate tiles
int WGB_tiles_count(wide_gb *wgb);
int WGB_index_of_tile(wide_gb *wgb, WGB_tile *tile);
WGB_tile* WGB_tile_at_index(wide_gb *wgb, int index);

// Find a tile
WGB_tile* WGB_tile_at_point(wide_gb *wgb, SDL_Point point);

/*---------------- Cleanup ----------------------------------------------*/

// Free tiles and memory used by the struct.
// The struct cannot be used again after this.
void WGB_destroy(wide_gb *wgb);

#endif
