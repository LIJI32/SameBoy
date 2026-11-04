/* Shader implementation by ChatGPT 5.2 & Aleksander Gajewski <aleksandergajewski@gmail.com> */

// Tweak these first
#define TILE_SCALE        8.0
#define VARIANTS          6.0
#define SEED              0.0

#define JITTER_LO         0.95
#define JITTER_HI         1.05

#define GRAIN_AMPL        0.05          // slightly reduced so bevel reads
#define BEVEL_STRENGTH    0.05          // BOOST to make it obvious (try 0.12)
#define AUTO_BRIGHTNESS   1.0

STATIC float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

STATIC float hash12s(vec2 p, float seed, float salt)
{
    return hash12(p + vec2(seed * 17.0 + salt * 131.0, seed * 29.0 + salt * 71.0));
}

STATIC float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Nearest sample in input texture (prevents blur)
STATIC vec4 sample_nearest_input(sampler2D image, vec2 input_pix, vec2 input_resolution)
{
    vec2 pix = floor(input_pix) + vec2(0.5);
    return texture(image, pix / input_resolution);
}

// tile_uv: [0..1) inside the micro-tile
// returns multiplicative factor ~[0.6..1.4] depending on settings
STATIC float micro_bevel(vec2 tile_uv, vec2 src_pix, float variant_id)
{
    // grain per micro-cell
    vec2 gcoord = src_pix * 37.0 + variant_id * 101.0 + floor(tile_uv * TILE_SCALE * 8.0);
    float g = (hash12s(gcoord, SEED, 1.0) * 2.0 - 1.0) * GRAIN_AMPL;

    // integer-ish coordinate inside the tile: [0..scale-1]
    vec2 q  = tile_uv * TILE_SCALE;
    vec2 qi = floor(q);

    // edge masks
    float left   = (qi.x < 0.5) ? 1.0 : 0.0;
    float right  = (qi.x > (TILE_SCALE - 1.5)) ? 1.0 : 0.0;
    float bottom = (qi.y < 0.5) ? 1.0 : 0.0;
    float top    = (qi.y > (TILE_SCALE - 1.5)) ? 1.0 : 0.0;

    // bevel gradient: top/left bright, bottom/right dark
    // stronger “face” gradient so it reads even on flat colors
    float face = 0.0;
    face += (1.0 - tile_uv.x) * 0.6; // left brighter
    face += (tile_uv.y)       * 0.6; // top brighter (if Y is flipped, this swaps)

    // border emphasis
    float border = (left + top) - 1.25 * (right + bottom);

    float bevel = (face + border) * BEVEL_STRENGTH;

    // combine, clamp
    return clamp(1.0 + g + bevel, 0.0, 2.0);
}

STATIC vec4 scale(sampler2D image, vec2 position, vec2 input_resolution, vec2 output_resolution)
{
    // Work in OUTPUT pixel space (this is the big fix)
    vec2 out_pix = position * output_resolution;

    // Snap to pixel center to avoid shimmering
    out_pix = floor(out_pix) + vec2(0.5);

    // Map output pixel -> input pixel
    // For a pure integer upscale, out_pix / TILE_SCALE corresponds to input pixel space
    vec2 in_pix_f = out_pix / TILE_SCALE;
    vec2 src_pix  = floor(in_pix_f);

    // Local coordinate within the TILE_SCALE x TILE_SCALE block
    vec2 tile_uv = fract(in_pix_f); // [0..1) inside the micro tile

    // Sample source pixel (nearest, no filtering)
    vec4 src = sample_nearest_input(image, src_pix, input_resolution);
    if (src.a <= 0.0) return vec4(0.0);

    // Deterministic variant + jitter per source pixel
    float vpick = hash12s(src_pix, SEED, 10.0);
    float variant_id = floor(vpick * VARIANTS + 1e-6);

    float jpick = hash12s(src_pix, SEED, 20.0);
    float jitter = lerp(JITTER_LO, JITTER_HI, jpick);

    float t = micro_bevel(tile_uv, src_pix, variant_id);

    // auto brightness (approx; keep 1 unless you need it)
    float brightness_comp = 1.0; // keep simple; add back if desired

    vec3 rgb = src.rgb * (t * jitter * brightness_comp);

    return vec4(clamp(rgb, 0.0, 1.0), src.a);
}
