#ifndef wide_gb_h
#define wide_gb_h

#include <SDL2/SDL.h>
#include  <stdbool.h>

#define WIDE_GB_ENABLED true
#define WIDE_GB_DEBUG false

// A tile is a recorded framebuffer the size of the screen.
typedef struct {
    SDL_Point position;
    uint32_t *pixel_buffer;
} wgb_tile;

#define WGB_MAX_TILES 512

typedef struct {
    SDL_Point logical_pos;
    SDL_Point hardware_pos;
    wgb_tile tiles[WGB_MAX_TILES];
    int tiles_count;
} wide_gb;

// Return a new initialized wide_gb struct
wide_gb wgb_init();

// Callback when the hardware scroll registers are updated
void wgb_update_hardware_scroll(wide_gb *wgb, int scx, int scy);

// Set a specific pixel on a given tile.
// The tile is created if it doesn't exist yet.
void wgb_write_tile_pixel(wide_gb *wgb, SDL_Point tile_pos, SDL_Point pixel_pos, uint32_t pixel);

// Return the current logical scroll position, taking into account:
// - screen wrapping
// - padding (todo)
SDL_Point wgb_get_logical_scroll(wide_gb *wgb);

// Enumerate tiles
int wgb_tiles_count(wide_gb *wgb);
int wgb_index_of_tile(wide_gb *wgb, wgb_tile *tile);
wgb_tile* wgb_tile_at_index(wide_gb *wgb, int index);

// Find a tile
wgb_tile* wgb_tile_at_point(wide_gb *wgb, SDL_Point point);

// Free tiles and memory used by the struct.
// The struct cannot be used again after this.
void wgb_destroy(wide_gb *wgb);

#endif
