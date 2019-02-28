// Use the best hash function for our hashes repartition
// See https://troydhanson.github.io/uthash/userguide.html#hash_functions
#define HASH_FUNCTION HASH_OAT
#include "uthash.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ftw.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include "wide_gb.h"

// Constants
#define WIDE_GB_DEBUG false
#define WGB_SCROLL_WRAP_AROUND_THRESHOLD 10
#define WGB_SCENE_CHANGE_THRESHOLD 14
#define WGB_YOUNG_SCENE_DELAY 2
#define WGB_SCENE_DELETED 0

// Debug macros
#if WIDE_GB_DEBUG
#define WGB_DEBUG_LOG(msg, ...) fprintf(stderr, ("wgb: " msg "\n"), __VA_ARGS__)
#else
#define WGB_DEBUG_LOG(msg, ...)
#endif

// Math macros
#define MAX(a,b) ((a) > (b) ? a : b)
#define MIN(a,b) ((a) < (b) ? a : b)
#define FLOOR_DIV(a, b) (((a) < 0) ? ((((a) + 1) / (b)) - 1) : ((a) / (b)))

// Forward declarations
WGB_tile *WGB_create_tile(wide_gb *wgb, WGB_scene *scene, WGB_tile_position position);
WGB_scene *WGB_create_scene(wide_gb *wgb);
WGB_exact_hash WGB_frame_hash(wide_gb *wgb, uint8_t *rgb_pixels);
WGB_perceptual_hash WGB_added_difference_hash(wide_gb *wgb, uint8_t *rgb_pixels);
int hamming_distance(WGB_perceptual_hash x, WGB_perceptual_hash y);
bool WGB_tile_position_equal_to(WGB_tile_position position1, WGB_tile_position position2);
WGB_tile_position WGB_tile_position_from_screen_point(wide_gb *wgb, WGB_Point screen_point);
WGB_Point WGB_tile_point_from_screen_point(wide_gb *wgb, WGB_Point screen_point, WGB_tile_position target_tile);
void WGB_tile_write_to_file(WGB_tile *tile, char *tile_path, WGB_rgb_decode_callback_t rgb_decode);
void WGB_load_tile_from_file(WGB_tile *tile, char *path, WGB_rgb_encode_callback_t rgb_encode);
void WGB_store_frame_hash(wide_gb *wgb, WGB_exact_hash hash, int scene_id, WGB_Point scene_scroll);
int WGB_IO_rmdir(const char *path);
void WGB_IO_write_PPM(char *filename, int width, int height, uint8_t *pixels);

/*---------------- Initializers --------------------------------------*/

WGB_tile WGB_tile_init(WGB_tile_position position)
{
    WGB_tile new = {
        .position = position,
        .pixel_buffer = calloc(WIDE_GB_TILE_WIDTH * WIDE_GB_TILE_HEIGHT, sizeof(uint32_t))
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

WGB_scene WGB_scene_init(int scene_id)
{
    WGB_scene new = { 0 };
    new.id = scene_id;
    new.created_at = time(NULL);
    return new;
}

void WGB_scene_destroy(WGB_scene *scene)
{
    for (int i = 0; i < scene->tiles_count; i++) {
        WGB_tile_destroy(&scene->tiles[i]);
    }
    scene->tiles_count = 0;
}

wide_gb WGB_init_from_path(const char *save_path, WGB_rgb_encode_callback_t rgb_encode)
{
    wide_gb wgb = { 0 };
    int max_scene_id = 0;

    // For each item in save_path…
    int save_path_len = strlen(save_path);
    struct dirent* save_dirent;
    DIR* save_dir = opendir(save_path);
    while (save_dir != NULL && (save_dirent = readdir(save_dir)) != NULL) {
        WGB_DEBUG_LOG("Scanning subdir '%s'", save_dirent->d_name);

        // Skip items that do not begin with "scene_"
        const char *scene_prefix = "scene_";
        if (strncmp(scene_prefix, save_dirent->d_name, strlen(scene_prefix)) != 0) {
            continue;
        }

        // Extract scene id
        int scene_id;
        sscanf(save_dirent->d_name, "scene_%i", &scene_id);

        // Create a new scene
        WGB_scene *scene = WGB_create_scene(&wgb);
        scene->id = scene_id;
        max_scene_id = MAX(scene->id, max_scene_id);

        // For each item in the scene directory…
        char scene_path[save_path_len + 30];
        sprintf(scene_path, "%s/%s", save_path, save_dirent->d_name);
        DIR* scene_dir = opendir(scene_path);
        struct dirent* scene_dirent;
        while (scene_dir != NULL && (scene_dirent = readdir(scene_dir)) != NULL) {
            WGB_DEBUG_LOG("Scanning subdir '%s'", scene_dirent->d_name);

            // Skip items that do not begin with "tile_"
            const char *tile_prefix = "tile_";
            if (strncmp(tile_prefix, scene_dirent->d_name, strlen(tile_prefix)) != 0) {
                continue;
            }

            // Extract tile position
            WGB_tile_position position;
            sscanf(scene_dirent->d_name, "tile_%i_%i", &position.horizontal, &position.vertical);

            // Create a new tile for the scene
            WGB_tile *tile = WGB_create_tile(&wgb, scene, position);

            // Restore the tile pixel buffer from the file
            char tile_path[save_path_len + 50];
            sprintf(tile_path, "%s/%s", scene_path, scene_dirent->d_name);
            WGB_load_tile_from_file(tile, tile_path, rgb_encode);
            tile->dirty = true;
        }

        // Open `scene_%i/scene_frames.csv`
        char frames_path[save_path_len + 50];
        sprintf(frames_path, "%s/scene_frames.csv", scene_path);
        FILE *frames_file = fopen(frames_path, "r");

        char line[255];
        fgets(line, 255, frames_file); // skip CSV header

        // For each line in scene_frames.csv
        while (fgets(line, 1024, frames_file)) {

            // Parse the line
            WGB_exact_hash frame_hash;
            int scene_id;
            WGB_Point scene_scroll;
            sscanf(line, "%llu,%i,%i,%i", &frame_hash, &scene_id, &scene_scroll.x, &scene_scroll.y);

            // Create a new scene frame
            WGB_store_frame_hash(&wgb, frame_hash, scene_id, scene_scroll);
        }
        fclose(frames_file);

        scene_dir != NULL && closedir(scene_dir);
    }
    save_dir != NULL && closedir(save_dir);

    // Ensure scene ids created for now won't overlap with restored scenes
    wgb.next_scene_id = max_scene_id + 1;

    // Create a new blank active scene
    wgb.active_scene = WGB_create_scene(&wgb);

    // Return
    return wgb;
}

void WGB_save_to_path(wide_gb *wgb, const char *save_path, WGB_rgb_decode_callback_t rgb_decode)
{
    // Create temp directory
    int path_len = strlen(save_path);
    char tmp_dir[path_len + 5];
    sprintf(tmp_dir, "%s.tmp", save_path);
    mkdir(tmp_dir, 0774);

    // For each scene…
    for (int i = 0; i < wgb->scenes_count; i++) {
        WGB_scene scene = wgb->scenes[i];
        if (scene.id == WGB_SCENE_DELETED) {
            continue;
        }

        // Create a `#{tmp_dir}/scene_#{id}` directory
        char scene_dir[path_len + 30];
        sprintf(scene_dir, "%s/scene_%i", tmp_dir, scene.id);
        mkdir(scene_dir, 0774);

        // For each tile
        for (int j = 0; j < scene.tiles_count; j++) {
            WGB_tile tile = scene.tiles[j];

            // Write a `tile_#{x}_#{y}.ppm` file
            char tile_path[path_len + 50];
            sprintf(tile_path, "%s/tile_%i_%i.ppm", scene_dir, tile.position.horizontal, tile.position.vertical);
            WGB_tile_write_to_file(&tile, tile_path, rgb_decode);
        }

        // Create a scene_frames.csv file
        char frames_path[path_len + 30];
        sprintf(frames_path, "%s/scene_frames.csv", scene_dir);
        FILE *frames_file = fopen(frames_path, "w");
        fprintf(frames_file, "frame_hash,scene_id,scene_scroll_x,scene_scroll_y\n");

        // For each scene_frame…
        WGB_scene_frame *scene_frame, *tmp;
        HASH_ITER(hh, wgb->scene_frames, scene_frame, tmp) {
            // If the scene_frame belongs to the current scene…
            if (scene_frame->scene_id == scene.id) {
                // Write a line in scene_frames.csv
                fprintf(frames_file, "%llu,%i,%i,%i\n", scene_frame->frame_hash, scene_frame->scene_id, scene_frame->scene_scroll.x, scene_frame->scene_scroll.y);
            }
        }
        fclose(frames_file);
    }

    // Delete any previous save_path, and rename the tmp dir to the save_path
    WGB_IO_rmdir(save_path);
    rename(tmp_dir, save_path);
}

void WGB_destroy(wide_gb *wgb)
{
    for (int i = 0; i < wgb->scenes_count; i++) {
        if (wgb->scenes[i].id != WGB_SCENE_DELETED) {
            WGB_scene_destroy(&wgb->scenes[i]);
        }
    }
    wgb->scenes_count = 0;
}

/*---------------- Managing tiles -------------------------------------*/

size_t WGB_tiles_count(wide_gb *wgb)
{
    return wgb->active_scene->tiles_count;
}

WGB_tile *WGB_tile_at_position(wide_gb *wgb, WGB_tile_position position_to_find)
{
    size_t tiles_count = WGB_tiles_count(wgb);
    for (int i = 0; i < tiles_count; i++) {
        WGB_tile *tile = WGB_tile_at_index(wgb, i);
        if (WGB_tile_position_equal_to(tile->position, position_to_find)) {
            return tile;
        }
    }
    return NULL;
}

WGB_tile* WGB_tile_at_index(wide_gb *wgb, int index)
{
    return &(wgb->active_scene->tiles[index]);
}

WGB_tile *WGB_create_tile(wide_gb *wgb, WGB_scene *scene, WGB_tile_position position)
{
    WGB_DEBUG_LOG("Create tile on scene %i at { %i, %i }", scene->id, position.horizontal, position.vertical);

    scene->tiles[scene->tiles_count] = WGB_tile_init(position);
    scene->tiles_count += 1;

   return &(scene->tiles[scene->tiles_count - 1]);
}

void WGB_tile_write_to_file(WGB_tile *tile, char *path, WGB_rgb_decode_callback_t rgb_decode)
{
    // Convert the opaque pixels values to triplets of RGB values
    uint8_t rgb_pixels[160 * 144 * 3];
    uint8_t r, g, b;
    for (int i = 0; i < 160 * 144 * 3; i += 3) {
        uint32_t pixel = tile->pixel_buffer[i / 3];
        rgb_decode(pixel, &r, &g, &b);
        rgb_pixels[i + 0] = r;
        rgb_pixels[i + 1] = g;
        rgb_pixels[i + 2] = b;
    }

    // Write RGB buffer to file
    WGB_IO_write_PPM(path, 160, 144, rgb_pixels);
}

void WGB_load_tile_from_file(WGB_tile *tile, char *path, WGB_rgb_encode_callback_t rgb_encode)
{
    // Load RGB buffer from file
    uint8_t rgb_pixels[160 * 144 * 3];
    FILE *ppm_file = fopen(path, "r");

    // Check image dimensions
    int ppm_width, ppm_height;
    while (fgetc(ppm_file) != '\n') ; // skip header
    fscanf(ppm_file, "%d %d\n", &ppm_width, &ppm_height);
    if (ppm_width != 160 || ppm_height != 144) {
        fprintf(stderr, "wgb: failed to load file at '%s': invalid dimensions (width: %i, height: %i)\n", path, ppm_width, ppm_height);
        return;
    }

    // Read pixel data
    while (fgetc(ppm_file) != '\n'); // skip pixel format
    if (fread(rgb_pixels, 3, 160 * 144, ppm_file) != 160 * 144) {
        fprintf(stderr, "wgb: failed to load file at '%s': file is too short\n", path);
        return;
    }

    fclose(ppm_file);

    // Convert the RGB format to opaque pixels values, and store it to the tile
    for (int i = 0; i < 160 * 144; i += 1) {
        int rgb_index = i * 3;
        tile->pixel_buffer[i] = rgb_encode(
            rgb_pixels[rgb_index + 0],
            rgb_pixels[rgb_index + 1],
            rgb_pixels[rgb_index + 2]
        );
    }
}

/*---------------- Managing scenes -------------------------------------*/

WGB_scene *WGB_create_scene(wide_gb *wgb)
{
    WGB_DEBUG_LOG("Create scene %i", wgb->next_scene_id);

    wgb->scenes[wgb->scenes_count] = WGB_scene_init(wgb->next_scene_id);
    wgb->scenes_count += 1;
    wgb->next_scene_id += 1;

    return &(wgb->scenes[wgb->scenes_count - 1]);
}

WGB_scene *WGB_find_scene_by_id(wide_gb *wgb, int scene_id)
{
    for (int i = 0; i < wgb->scenes_count; i++) {
        if (wgb->scenes[i].id == scene_id) {
            return &wgb->scenes[i];
        }
    }
    return NULL;
}

WGB_scene_frame *WGB_find_scene_frame_for_hash(wide_gb *wgb, WGB_exact_hash hash)
{
    WGB_scene_frame *scene_frame;
    HASH_FIND_INT(wgb->scene_frames, &hash, scene_frame);
    return scene_frame;
}

void WGB_make_scene_active(wide_gb *wgb, WGB_scene *scene)
{
    wgb->active_scene = scene;
}

bool WGB_has_scene_changed(wide_gb *wgb, WGB_perceptual_hash frame_perceptual_hash, WGB_perceptual_hash previous_perceptual_hash)
{
    // When the scroll position jumps to the origin, this can either signal:
    //
    // - an actual scene change;
    // - the game reseting the viewport (e.g. Pokemon Gold/Silver when opening a dialog or a menu).
    //
    // In either case, triggering a scene change starts the scene matching sequence, which allow WGB
    // to recognize the frame and map the new hardware scroll value to the logical scroll value.
    bool is_hardware_scroll_at_origin = (wgb->hardware_scroll.x == 0 && wgb->hardware_scroll.y == 0);
    WGB_Point scroll_distance = { abs(wgb->scroll_delta.x), abs(wgb->scroll_delta.y) };
    bool scroll_jumped = MAX(scroll_distance.x, scroll_distance.y) > WGB_SCROLL_WRAP_AROUND_THRESHOLD;
    if (is_hardware_scroll_at_origin && scroll_jumped) {
        WGB_DEBUG_LOG("WideGB scene changed (scroll jumped to origin by %i, %i)\n\n", scroll_distance.x, scroll_distance.y);
        return true;
    }

    int distance = hamming_distance(frame_perceptual_hash, previous_perceptual_hash);
    // if (distance > 0) {
    //     WGB_DEBUG_LOG("Perceptual distance from previous frame: %i", distance);
    // }

    // A great distance between two perceptual hashes signals a scene change.
    if (distance >= WGB_SCENE_CHANGE_THRESHOLD) {
        WGB_DEBUG_LOG("WideGB scene changed (distance = %i)\n\n", distance);
        return true;
    }

    // Transitionning from or to a screen with a uniform color (i.e. no edges, i.e. p_hash == 0)
    // signals a scene change.
    if ((frame_perceptual_hash == 0 || previous_perceptual_hash == 0) && distance != 0) {
        WGB_DEBUG_LOG("WideGB scene changed (transitionned from or to a uniform color ; distance = %i)\n\n", distance);
        return true;
    }

    return false;
}


void WGB_delete_scene(wide_gb *wgb, WGB_scene *scene)
{
    WGB_DEBUG_LOG("Delete scene %i", scene->id);

    // Remove the frames belonging to this scene
    WGB_scene_frame *current_scene_frame, *tmp;
    HASH_ITER(hh, wgb->scene_frames, current_scene_frame, tmp) {
        if (current_scene_frame->scene_id == scene->id) {
            HASH_DEL(wgb->scene_frames, current_scene_frame);
            free(current_scene_frame);
        }
    }

    // Mark the scene as deleted
    scene->id = WGB_SCENE_DELETED;

    // Destroy the scene itself
    WGB_scene_destroy(scene);
}

void WGB_restore_scene_for_frame(wide_gb *wgb, WGB_scene_frame *scene_frame)
{
    WGB_DEBUG_LOG("Found a matching scene: restore scene %i.", scene_frame->scene_id);

    // Find the scene matching the frame
    WGB_scene *matched_scene = WGB_find_scene_by_id(wgb, scene_frame->scene_id);
    if (matched_scene == NULL) {
        fprintf(stderr, "wgb: Error while restoring a saved scene for scene %i: matching scene not found.\n", scene_frame->scene_id);
        return;
    }

    // Mark all tiles of the new scene as dirty
    matched_scene->scroll = scene_frame->scene_scroll;
    for (int i = 0; i < matched_scene->tiles_count; i++) {
        matched_scene->tiles[i].dirty = true;
    }

    // Restore the scene
    WGB_scene *previous_scene = wgb->active_scene;
    WGB_make_scene_active(wgb, matched_scene);

    // Now that we know this frame belonged to a specific scene,
    // destroy the temporary scene that was created meanwhile.
    WGB_delete_scene(wgb, previous_scene);
}

void WGB_store_frame_hash(wide_gb *wgb, WGB_exact_hash hash, int scene_id, WGB_Point scene_scroll)
{
    // Attempt to find an existing scene_frame for this frame
    WGB_scene_frame *scene_frame;
    HASH_FIND_INT(wgb->scene_frames, &hash, scene_frame);
    if (scene_frame == NULL) {
        // No scene_frame for this frame has been stored yet: create and insert it
        scene_frame = malloc(sizeof(WGB_scene_frame));
        scene_frame->frame_hash = hash;
        HASH_ADD_INT(wgb->scene_frames, frame_hash, scene_frame);
    }

    // Update the stored scene_frame with all informations (except the hash key)
    scene_frame->scene_id = scene_id;
    scene_frame->scene_scroll = scene_scroll;
}

double WGB_is_scene_young(WGB_scene *scene)
{
    return difftime(time(NULL), scene->created_at) < WGB_YOUNG_SCENE_DELAY;
}

WGB_tile* WGB_write_tile_pixel(wide_gb *wgb, WGB_Point pixel_pos, uint32_t pixel)
{
    // Retrieve the tile for this pixel
    WGB_tile_position tile_pos = WGB_tile_position_from_screen_point(wgb, pixel_pos);
    WGB_tile *tile = WGB_tile_at_position(wgb, tile_pos);

    // Create the tile if it doesn't exist
    if (tile == NULL) {
        tile = WGB_create_tile(wgb, wgb->active_scene, tile_pos);
    }

    // Convert the pixel position from screen-space to tile-space
    WGB_Point pixel_destination = WGB_tile_point_from_screen_point(wgb, pixel_pos, tile_pos);

    tile->pixel_buffer[pixel_destination.x + pixel_destination.y * 160] = pixel;

    return tile;
}

/*---------------- Updates from hardware ------------------------------*/

void WGB_update_hardware_values(wide_gb *wgb, int scx, int scy, int wx, int wy, bool is_window_enabled)
{
    //
    // Update hardware scroll registers
    //

    WGB_Point previous_hardware_scroll = wgb->hardware_scroll;
    WGB_Point new_hardware_scroll = { scx, scy };

    // Compute difference with the previous scroll position
    WGB_Point delta = {
        .x = new_hardware_scroll.x - previous_hardware_scroll.x,
        .y = new_hardware_scroll.y - previous_hardware_scroll.y
    };

    // Apply heuristic to tell if the background position wrapped into the other side
    const int gb_background_size = 256;
    const int threshold = gb_background_size - WGB_SCROLL_WRAP_AROUND_THRESHOLD;
    // 255 -> 0 | delta.x is negative: we are going right
    // 0 -> 255 | delta.x is positive: we are going left
    if (abs(delta.x) > threshold) {
        if (delta.x < 0) delta.x += gb_background_size; // going right
        else             delta.x -= gb_background_size; // going left
    }
    // 255 -> 0 | delta.y is negative: we are going down
    // 0 -> 255 | delta.y is positive: we are going up
    if (abs(delta.y) > threshold) {
        if (delta.y < 0) delta.y += gb_background_size; // going down
        else             delta.y -= gb_background_size; // going up
    }

    // Update the new scroll positions
    wgb->hardware_scroll = new_hardware_scroll;
    wgb->scroll_delta = delta;
    wgb->active_scene->scroll.x += delta.x;
    wgb->active_scene->scroll.y += delta.y;

    //
    // Update window position
    //

    wgb->window_enabled = is_window_enabled;
    wgb->window_rect = (WGB_Rect) {
        .x = MIN(wx, 160),
        .y = MIN(wy, 144),
        .w = MAX(0, 160 - wx),
        .h = MAX(0, 144 - wy)
    };
}

void WGB_update_screen(wide_gb *wgb, uint32_t *pixels, WGB_rgb_decode_callback_t rgb_decode)
{
    //
    // Generate frame hashes
    //

    // Decode the RGB components of the pixels
    uint8_t rgb_pixels[160 * 144 * 3];
    for (size_t pixels_i = 0, rgb_i = 0; pixels_i < 160 * 144; pixels_i += 1, rgb_i += 3) {
        rgb_decode(pixels[pixels_i],
            &rgb_pixels[rgb_i + 0],
            &rgb_pixels[rgb_i + 1],
            &rgb_pixels[rgb_i + 2]);
    }

    // Compute frame hashes from RGB values
    WGB_exact_hash hash = WGB_frame_hash(wgb, rgb_pixels);
    WGB_perceptual_hash p_hash = WGB_added_difference_hash(wgb, rgb_pixels);

    // Save new hash values
    WGB_exact_hash previous_exact_hash = wgb->frame_hash;
    WGB_perceptual_hash previous_perceptual_hash = wgb->frame_perceptual_hash;
    wgb->frame_hash = hash;
    wgb->frame_perceptual_hash = p_hash;

    //
    // If we detect a scene transition, create a new scene
    //

    if (WGB_has_scene_changed(wgb, wgb->frame_perceptual_hash, previous_perceptual_hash)) {
        WGB_scene *previous_scene = wgb->active_scene;
        WGB_scene *new_scene = WGB_create_scene(wgb);
        WGB_make_scene_active(wgb, new_scene);

        // Short-lived scenes often contain garbage during screen transitions.
        // Delete it, so that we won't match it by error later.
        if (WGB_is_scene_young(previous_scene)) {
            WGB_delete_scene(wgb, previous_scene);
        }
    }

    //
    // If the scene is young, attempt to match the current frame with an existing scene
    //

    if (WGB_is_scene_young(wgb->active_scene)) {
        WGB_scene_frame *existing_frame = WGB_find_scene_frame_for_hash(wgb, hash);
        if (existing_frame && existing_frame->scene_id != wgb->active_scene->id) {
            WGB_restore_scene_for_frame(wgb, existing_frame);
        }
    }

    // Store the frame hash for the active scene
    WGB_store_frame_hash(wgb, hash, wgb->active_scene->id, wgb->active_scene->scroll);

    //
    // Write frame pixels to the relevant tiles
    //

    bool frame_changed = (wgb->frame_hash != previous_exact_hash);
    if (frame_changed) {

        // TODO: optimize hot loop, and avoid to write the tile all the time
        // Generate corner tiles
        // for each 4 tiles…
        // write pixels on the tile
        // For each frame pixel…
        for (int pixel_y = 0; pixel_y < 144; pixel_y++) {
            for (int pixel_x = 0; pixel_x < 160; pixel_x++) {
                WGB_Point pixel_pos = { pixel_x, pixel_y };
                // Skip pixels in window
                if (wgb->window_enabled && WGB_rect_contains_point(wgb->window_rect, pixel_pos)) {
                    continue;
                }
                // Read the frame pixel
                uint32_t pixel = pixels[pixel_x + pixel_y * 160];
                // Write the pixel to the relevant tile
                WGB_tile *tile = WGB_write_tile_pixel(wgb, pixel_pos, pixel);
                tile->dirty = true;
            }
        }
    }
}

/*---------------------- Laying out tiles --------------------------------*/

bool WGB_is_tile_visible(wide_gb *wgb, WGB_tile *tile, WGB_Rect viewport)
{
    WGB_Rect tile_rect = WGB_rect_for_tile(wgb, tile);
    return WGB_rect_intersects_rect(tile_rect, viewport);
}

WGB_Rect WGB_rect_for_tile(wide_gb *wgb, WGB_tile *tile)
{
    return (WGB_Rect) {
        .x = tile->position.horizontal * 160 - wgb->active_scene->scroll.x,
        .y = tile->position.vertical   * 144 - wgb->active_scene->scroll.y,
        .w = 160,
        .h = 144
    };
}

/*---------------------- Laying out screen -------------------------------*/

void WGB_get_screen_layout(wide_gb *wgb, WGB_Rect *bg_rect1, WGB_Rect *bg_rect2, WGB_Rect *wnd_rect)
{
    WGB_Rect window_rect = wgb->window_enabled ? wgb->window_rect : (WGB_Rect){ 160, 144, 0, 0 };

    bg_rect1->x = 0;
    bg_rect1->y = 0;
    bg_rect1->w = 160;
    bg_rect1->h = window_rect.y;

    bg_rect2->x = 0;
    bg_rect2->y = window_rect.y;
    bg_rect2->w = window_rect.x;
    bg_rect2->h = 144 - window_rect.y;

    wnd_rect->x = window_rect.x;
    wnd_rect->y = window_rect.y;
    wnd_rect->w = window_rect.w;
    wnd_rect->h = window_rect.h;
}

bool WGB_is_window_covering_screen(wide_gb *wgb, unsigned int tolered_pixels)
{
    if (wgb->window_enabled) {
        return (wgb->window_rect.x <= tolered_pixels) && (wgb->window_rect.y <= tolered_pixels);
    } else {
        return false;
    }
}

/*---------------- Frame hashing helpers -------------------------------*/

// Compute an exact hash of a frame. This is used to identify if a frame belongs
// to an already stored scene.
//
// As the window content is often not relevant to know if a given screen
// is the same than another, pixels in the window area are excluded from the hash.
//
// `rgb_pixels` must be a 160 * 144 * 3 array, where each consecutive triplet
// store the r, g and b components for a pixel.
WGB_exact_hash WGB_frame_hash(wide_gb *wgb, uint8_t *rgb_pixels)
{
    // The window is often used as a HUD – and we don't want things like the
    // number of remaning lives to influence wether a frame is matched or
    // not.
    //
    // If the window is enabled, and partially covering the screen, ignore
    // window pixels in the frame hash.
    bool ignore_pixels_in_window = wgb->window_enabled && !WGB_is_window_covering_screen(wgb, 0);

    WGB_exact_hash hash = 0;
    for (int y = 0; y < 144; y++) {
        for (int x = 0; x < 160; x++) {
            if (ignore_pixels_in_window && WGB_rect_contains_point(wgb->window_rect, (WGB_Point) { x, y })) {
                continue;
            }
            size_t rgb_i = (x + y * 160) * 3;
            int pixels_sum = rgb_pixels[rgb_i] + rgb_pixels[rgb_i + 1] + rgb_pixels[rgb_i + 2];
            hash = (hash + 324723947 + pixels_sum * 2) ^ 93485734985;
        }
    }

    return hash;
}

void DEBUG_write_grayscale_PPM(int width, int height, uint8_t *pixels) {
    char filename[255];
    static int filename_increment = 0;
    filename_increment++;
    sprintf(filename, "/Users/kemenaran/Desktop/debug/%i.ppm", filename_increment);
    FILE *fp = fopen(filename, "wb"); /* b - binary mode */
    fprintf(fp, "P6\n%d %d\n255\n", width, height);

    uint8_t color[3];
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            uint8_t grayscale = pixels[i + j * width];
            color[0] = grayscale; // red
            color[1] = grayscale; // green
            color[2] = grayscale; // blue
            fwrite(color, 1, 3, fp);
      }
  }
  fclose(fp);
}

// Compute a perceptual hash of a frame using the "added difference hash" algorithm.
//
// This algorith detects how many blocks are brighter than the adjascent block.
// The resulting hash is not very sensitive to translations (great for allowing scrolling),
// but quite sensitive to luminance changes (great for detecting fade transitions).
//
// `rgb_pixels` must be a 160 * 144 * 3 array, where each consecutive triplet
// store the r, g and b components for a pixel.
WGB_perceptual_hash WGB_added_difference_hash(wide_gb *wgb, uint8_t *rgb_pixels)
{
    const int block_width = 160 / 8;
    const int block_height = 144 / 8;

    // 1. Grayscale

    // For each pixel
    uint8_t r, g, b;
    uint8_t grayscaled_pixels[160 * 144];
    for (size_t rgb_i = 0, grayscaled_i = 0; rgb_i < 160 * 144 * 3; rgb_i += 3, grayscaled_i += 1) {
        r = rgb_pixels[rgb_i + 0];
        g = rgb_pixels[rgb_i + 1];
        b = rgb_pixels[rgb_i + 2];
        // Convert to grayscale
        grayscaled_pixels[grayscaled_i] = 0.212671f * r + 0.715160f * g + 0.072169f * b;
    }

    // DEBUG_write_grayscale_PPM(160, 144, grayscaled_pixels);

    // 2. Downscale to 8 * 8 blocks, using a Triangle filter

    uint8_t grayscale[8*8];
    const float block_center_x = block_width / 2;
    const float block_center_y = block_height / 2;
    const float max_dist = sqrtf(powf(block_width, 2) + powf(block_height, 2)) / 2;
    // For each block
    for (int block_y = 0; block_y < 8; block_y++) {
        for (int block_x = 0; block_x < 8; block_x++) {
            float sum_of_coefs = 0;
            float block_contributions = 0;
            int block_top_x = block_x * block_width;
            int block_top_y = block_y * block_height;
            // For each pixel in the block
            for (int pixel_y = 0; pixel_y < block_height; pixel_y++) {
                for (int pixel_x = 0; pixel_x < block_width; pixel_x++) {
                    // Extract pixel luminance
                    int pixel_index = ((block_top_x + pixel_x) + (block_top_y + pixel_y) * 160);
                    float luminance = grayscaled_pixels[pixel_index];
                    // Compute pixel contribution using a Triangle filter
                    // (pixels at the center point contribute `luminance * 1.0`, pixels farther contribute less)
                    float pixel_center_x = pixel_x + 0.5;
                    float pixel_center_y = pixel_y + 0.5;
                    float distance_from_center = sqrtf(powf(block_center_x - pixel_center_x, 2) + powf(block_center_y - pixel_center_y, 2));
                    float coef = -(distance_from_center / max_dist) + 1;
                    float contrib = coef * luminance;
                    // Add pixel contribution to the block value
                    block_contributions += contrib;
                    sum_of_coefs += coef;
                }
            }
            // Normalize the contributions (so that the sum of each coeficient is exacly 1.0)
            float normalized_block_contribution = block_contributions / sum_of_coefs;
            // Write final block value to the downsampled grayscale picture
            grayscale[block_x + block_y * 8] = normalized_block_contribution;
        }
    }

    // DEBUG_write_grayscale_PPM(8, 8, grayscale);

    // 2. Count the number of blocks brighter than the block on the top-left

    int sum = 0;
    for (int x = 1; x < 8; x++) {
        for (int y = 1; y < 8; y++) {
            uint8_t block_luminance = grayscale[x + y * 8];
            uint8_t diagonal_block_luminance = grayscale[(x - 1) + (y - 1) * 8];
            if (block_luminance > diagonal_block_luminance) {
                sum += 1;
            }
        }
    }

    // 3. Encode the sum as a hash that can be compared using a hamming distance

    // The first N bits of the hash are set to 1, and the others to 0.
    WGB_perceptual_hash hash = 0;
    for (int i = 0; i < sum; i++) {
        hash |= 1ULL << i;
    }

    return hash;
}

int hamming_distance(WGB_perceptual_hash x, WGB_perceptual_hash y) {
    WGB_perceptual_hash z  = x ^ y;
    int d = 0;
    for (; z > 0; z >>= 1) {
        d += z & 1;
    }
    return d;
}

/*---------------- File utils --------------------------------------*/

int WGB_IO_unlink_callback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    int rv = remove(fpath);
    if (rv) {
        perror(fpath);
    }
    return rv;
}

int WGB_IO_rmdir(const char *path)
{
    return nftw(path, WGB_IO_unlink_callback, 64, FTW_DEPTH | FTW_PHYS);
}

void WGB_IO_write_PPM(char *filename, int width, int height, uint8_t *pixels) {
    FILE *file = fopen(filename, "wb");
    fprintf(file, "P6\n%d %d\n255\n", width, height);

    for (int i = 0; i < width * height * 3; i += 3) {
        // Write 3 uint8 values (R, G and B), starting from pixels[i] to pixels[i + 2]
        fwrite((uint8_t *)&pixels[i], 1, 3, file);
    }

    fclose(file);
}

/*---------------- Geometry utils --------------------------------------*/

WGB_Point WGB_offset_point(WGB_Point point, WGB_Point offset)
{
    point.x += offset.x;
    point.y += offset.y;
    return point;
}

WGB_Rect WGB_offset_rect(WGB_Rect rect, int dx, int dy)
{
    rect.x += dx;
    rect.y += dy;
    return rect;
}

WGB_Rect WGB_scale_rect(WGB_Rect rect, double dx, double dy)
{
    rect.x *= dx;
    rect.y *= dy;
    rect.w *= dx;
    rect.h *= dy;
    return rect;
}

bool WGB_rect_contains_point(WGB_Rect rect, WGB_Point point)
{
    return (rect.x <= point.x
        && point.x <= rect.x + rect.w
        && rect.y <= point.y
        && point.y <= rect.y + rect.h);
}

bool WGB_rect_intersects_rect(WGB_Rect rect1, WGB_Rect rect2)
{
  if (rect2.x < rect1.x + rect1.w && rect1.x < rect2.x + rect2.w && rect2.y < rect1.y + rect1.h)
    return rect1.y < rect2.y + rect2.h;
  else
    return false;
}

bool WGB_tile_position_equal_to(WGB_tile_position position1, WGB_tile_position position2)
{
    return (position1.horizontal == position2.horizontal &&
            position1.vertical == position2.vertical);
}

WGB_tile_position WGB_tile_position_from_screen_point(wide_gb *wgb, WGB_Point screen_point)
{
    return (WGB_tile_position) {
        .horizontal = FLOOR_DIV((wgb->active_scene->scroll.x + screen_point.x), 160),
        .vertical   = FLOOR_DIV((wgb->active_scene->scroll.y + screen_point.y), 144)
    };
}

WGB_Point WGB_tile_point_from_screen_point(wide_gb *wgb, WGB_Point screen_point, WGB_tile_position target_tile)
{
    WGB_Point tile_origin = {
        .x = wgb->active_scene->scroll.x - target_tile.horizontal * 160,
        .y = wgb->active_scene->scroll.y - target_tile.vertical   * 144
    };
    return WGB_offset_point(tile_origin, screen_point);
}
