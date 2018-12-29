#include <SDL2/SDL.h>
#include <stdlib.h>
#include <math.h>
#include "wide_gb.h"

#define BACKGROUND_SIZE 256

WGB_tile WGB_tile_init(SDL_Point position)
{
    WGB_tile new = {
        .position = position,
        .pixel_buffer = calloc(160 * 144, sizeof(uint32_t))
    };
    return new;
}

void WGB_tile_destroy(WGB_tile *tile)
{
    if (tile->pixel_buffer) {
        free(tile->pixel_buffer);
        tile->pixel_buffer = NULL;
    }
}

wide_gb WGB_init()
{
    wide_gb new = {
        .logical_pos  = { 0, 0 },
        .hardware_pos = { 0, 0 }
    };
    return new;
}

void WGB_destroy(wide_gb *wgb)
{
    for (int i = 0; i < wgb->tiles_count; i++) {
        WGB_tile_destroy(&wgb->tiles[i]);
    }
}

int WGB_tiles_count(wide_gb *wgb)
{
    return wgb->tiles_count;
}

int WGB_index_of_tile(wide_gb *wgb, WGB_tile *tile)
{
    for (int i = 0; i < wgb->tiles_count; i++) {
        if (&(wgb->tiles[i]) == tile) {
            return i;
        }
    }
    return -1;
}

WGB_tile *WGB_tile_at_position(wide_gb *wgb, SDL_Point position_to_find)
{
    for (int i = 0; i < wgb->tiles_count; i++) {
        WGB_tile *tile = &(wgb->tiles[i]);
        if (tile->position.x == position_to_find.x && tile->position.y == position_to_find.y) {
            return tile;
        }
    }
    return NULL;
}

WGB_tile* WGB_tile_at_index(wide_gb *wgb, int index)
{
    return &(wgb->tiles[index]);
}

WGB_tile* WGB_tile_at_point(wide_gb *wgb, SDL_Point point)
{
    SDL_Point position_to_find = {
        floorf(point.x / 160.0),
        floorf(point.y / 144.0)
    };
    // fprintf(stderr, "wgb: search for tile at { %i, %i }\n", position_to_find.x, position_to_find.y);
    return WGB_tile_at_position(wgb, position_to_find);
}

WGB_tile *WGB_create_tile(wide_gb *wgb, SDL_Point tile_pos)
{
    fprintf(stderr, "wgb: create tile at { %i, %i } (tiles count: %i)\n", tile_pos.x, tile_pos.y, wgb->tiles_count);
    wgb->tiles[wgb->tiles_count] = WGB_tile_init(tile_pos);
    wgb->tiles_count += 1;
    return &(wgb->tiles[wgb->tiles_count - 1]);
}

void WGB_update_hardware_scroll(wide_gb *wide_gb, int scx, int scy)
{
    SDL_Point new_hardware_pos = { scx, scy };

    // Compute difference with the previous scroll position
    SDL_Point delta;
    delta.x = new_hardware_pos.x - wide_gb->hardware_pos.x;
    delta.y = new_hardware_pos.y - wide_gb->hardware_pos.y;

    // Apply heuristic to tell if the background position wrapped into the other side
    const int fuzz = 10;
    const int threshold = BACKGROUND_SIZE - fuzz;
    // 255 -> 0 | delta.x is negative: we are going right
    // 0 -> 255 | delta.x is positive: we are going left
    if (abs(delta.x) > threshold) {
        if (delta.x < 0) delta.x += BACKGROUND_SIZE; // going right
        else             delta.x -= BACKGROUND_SIZE; // going left
    }
    // 255 -> 0 | delta.y is negative: we are going down
    // 0 -> 255 | delta.y is positive: we are going up
    if (abs(delta.y) > threshold) {
        if (delta.y < 0) delta.y += BACKGROUND_SIZE; // going down
        else             delta.y -= BACKGROUND_SIZE; // going up
    }

    // Update the new positions
    wide_gb->hardware_pos = new_hardware_pos;
    wide_gb->logical_pos.x += delta.x;
    wide_gb->logical_pos.y += delta.y;
}

void WGB_write_tile_pixel(wide_gb *wgb, SDL_Point tile_pos, SDL_Point pixel_pos, uint32_t pixel)
{
    // if (pixel_pos.x % 50 == 0 && pixel_pos.y % 50 == 0) {
    //     fprintf(stderr, "Write pixel { %i, %i } to tile at { %i, %i }\n", pixel_pos.x, pixel_pos.y, tile_pos.x, tile_pos.y);
    // }

    WGB_tile *tile = WGB_tile_at_position(wgb, tile_pos);
    if (tile == NULL) {
        tile = WGB_create_tile(wgb, tile_pos);
    }

    // Convert the pixel position from screen-space to tile-space
    SDL_Point pixel_destination = {
        .x = (wgb->logical_pos.x - tile->position.x * 160) + pixel_pos.x,
        .y = (wgb->logical_pos.y - tile->position.y * 144) + pixel_pos.y
    };

    tile->pixel_buffer[pixel_destination.x + pixel_destination.y * 160] = pixel;
}

// Return the current logical scroll position, taking into account:
// - screen wrapping
// - padding (todo)
SDL_Point WGB_get_logical_scroll(wide_gb *wide_gb)
{
    return wide_gb->logical_pos;
}
