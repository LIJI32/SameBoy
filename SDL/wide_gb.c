#include <SDL2/SDL.h>
#include <stdlib.h>
#include <math.h>
#include "wide_gb.h"

#define BACKGROUND_SIZE 256

/*---------------- Utils -------------------------------------------------*/

SDL_Point WGB_offset_point(SDL_Point point, SDL_Point offset)
{
    point.x += offset.x;
    point.y += offset.y;
    return point;
}

SDL_Rect WGB_offset_rect(SDL_Rect rect, SDL_Point offset)
{
    rect.x += offset.x;
    rect.y += offset.y;
    return rect;
}

SDL_Rect WGB_scale_rect(SDL_Rect rect, double dx, double dy)
{
    rect.x *= dx;
    rect.y *= dy;
    rect.w *= dx;
    rect.h *= dy;
    return rect;
}

bool WGB_tile_position_equal_to(WGB_tile_position position1, WGB_tile_position position2)
{
    return (position1.horizontal == position2.horizontal &&
            position1.vertical == position2.vertical);
}

WGB_tile_position WGB_tile_position_from_screen_point(wide_gb *wgb, SDL_Point screen_point)
{
    return (WGB_tile_position){
        .horizontal = floorf((wgb->logical_pos.x + screen_point.x) / 160.0),
        .vertical   = floorf((wgb->logical_pos.y + screen_point.y) / 144.0)
    };
}

SDL_Point WGB_tile_point_from_screen_point(wide_gb *wgb, SDL_Point screen_point, WGB_tile_position target_tile)
{
    SDL_Point tile_origin = {
        .x = wgb->logical_pos.x - target_tile.horizontal * 160,
        .y = wgb->logical_pos.y - target_tile.vertical   * 144
    };
    return WGB_offset_point(tile_origin, screen_point);
}

SDL_Rect WGB_rect_for_tile(wide_gb *wgb, WGB_tile *tile)
{
    return (SDL_Rect) {
        .x = tile->position.horizontal * 160 - wgb->logical_pos.x,
        .y = tile->position.vertical   * 144 - wgb->logical_pos.y,
        .w = 160,
        .h = 144
    };
}

/*---------------- Initializers --------------------------------------*/

WGB_tile WGB_tile_init(WGB_tile_position position)
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

WGB_tile *WGB_tile_at_position(wide_gb *wgb, WGB_tile_position position_to_find)
{
    for (int i = 0; i < wgb->tiles_count; i++) {
        WGB_tile *tile = &(wgb->tiles[i]);
        if (WGB_tile_position_equal_to(tile->position, position_to_find)) {
            return tile;
        }
    }
    return NULL;
}

WGB_tile* WGB_tile_at_index(wide_gb *wgb, int index)
{
    return &(wgb->tiles[index]);
}

WGB_tile* WGB_tile_at_point(wide_gb *wgb, SDL_Point screen_point)
{
    WGB_tile_position position_to_find = WGB_tile_position_from_screen_point(wgb, screen_point);
    return WGB_tile_at_position(wgb, position_to_find);
}

WGB_tile *WGB_create_tile(wide_gb *wgb, WGB_tile_position position)
{
    fprintf(stderr, "wgb: create tile at { %i, %i } (tiles count: %i)\n", position.horizontal, position.vertical, wgb->tiles_count);
    wgb->tiles[wgb->tiles_count] = WGB_tile_init(position);
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

void WGB_write_tile_pixel(wide_gb *wgb, SDL_Point pixel_pos, uint32_t pixel)
{
    // if (pixel_pos.x % 50 == 0 && pixel_pos.y % 50 == 0) {
    //     fprintf(stderr, "Write pixel { %i, %i } to tile at { %i, %i }\n", pixel_pos.x, pixel_pos.y, tile_pos.x, tile_pos.y);
    // }

    // Retrieve the tile for this pixel
    WGB_tile_position tile_pos = WGB_tile_position_from_screen_point(wgb, pixel_pos);
    WGB_tile *tile = WGB_tile_at_position(wgb, tile_pos);
    if (tile == NULL) {
        tile = WGB_create_tile(wgb, tile_pos);
    }

    // Convert the pixel position from screen-space to tile-space
    SDL_Point pixel_destination = WGB_tile_point_from_screen_point(wgb, pixel_pos, tile_pos);

    tile->pixel_buffer[pixel_destination.x + pixel_destination.y * 160] = pixel;
}

// Return the current logical scroll position, taking into account:
// - screen wrapping
// - padding (todo)
SDL_Point WGB_get_logical_scroll(wide_gb *wide_gb)
{
    return wide_gb->logical_pos;
}
