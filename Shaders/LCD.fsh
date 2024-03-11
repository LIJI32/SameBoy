#define COLOR_LOW 0.6
#define COLOR_HIGH 1.0
#define SCANLINE_DEPTH 0.2

STATIC vec4 scale(sampler2D image, vec2 position, vec2 input_resolution, vec2 output_resolution)
{
    vec2 pos = fract(position * input_resolution);
    vec2 sub_pos = pos * 6.0;
    
    vec4 center = texture_relative(image, position, vec2(0, 0));
    vec4 left = texture_relative(image, position, vec2(-1, 0));
    vec4 right = texture_relative(image, position, vec2(1, 0));
    
    if (sub_pos.y < 1.0) {
        center = mix(center, texture_relative(image, position, vec2( 0, -1)), 0.5 - sub_pos.y / 2.0);
        left =   mix(left,   texture_relative(image, position, vec2(-1, -1)), 0.5 - sub_pos.y / 2.0);
        right =  mix(right,  texture_relative(image, position, vec2( 1, -1)), 0.5 - sub_pos.y / 2.0);
        center *= sub_pos.y * SCANLINE_DEPTH + (1.0 - SCANLINE_DEPTH);
        left *= sub_pos.y * SCANLINE_DEPTH + (1.0 - SCANLINE_DEPTH);
        right *= sub_pos.y * SCANLINE_DEPTH + (1.0 - SCANLINE_DEPTH);
    }
    else if (sub_pos.y > 5.0) {
        center = mix(center, texture_relative(image, position, vec2( 0, 1)), (sub_pos.y - 5.0) / 2.0);
        left =   mix(left,   texture_relative(image, position, vec2(-1, 1)), (sub_pos.y - 5.0) / 2.0);
        right =  mix(right,  texture_relative(image, position, vec2( 1, 1)), (sub_pos.y - 5.0) / 2.0);
        center *= (6.0 - sub_pos.y) * SCANLINE_DEPTH + (1.0 - SCANLINE_DEPTH);
        left *= (6.0 - sub_pos.y) * SCANLINE_DEPTH + (1.0 - SCANLINE_DEPTH);
        right *= (6.0 - sub_pos.y) * SCANLINE_DEPTH + (1.0 - SCANLINE_DEPTH);
    }
    
    
    vec4 midleft = mix(left, center, 0.5);
    vec4 midright = mix(right, center, 0.5);
    
    vec4 ret;
    if (sub_pos.x < 1.0) {
        ret = mix(vec4(COLOR_HIGH * center.r, COLOR_LOW * center.g, COLOR_HIGH * left.b, 1),
                  vec4(COLOR_HIGH * center.r, COLOR_LOW * center.g, COLOR_LOW  * left.b, 1),
                  sub_pos.x);
    }
    else if (sub_pos.x < 2.0) {
        ret = mix(vec4(COLOR_HIGH * center.r, COLOR_LOW  * center.g, COLOR_LOW * left.b, 1),
                  vec4(COLOR_HIGH * center.r, COLOR_HIGH * center.g, COLOR_LOW * midleft.b, 1),
                  sub_pos.x - 1.0);
    }
    else if (sub_pos.x < 3.0) {
        ret = mix(vec4(COLOR_HIGH * center.r  , COLOR_HIGH * center.g, COLOR_LOW * midleft.b, 1),
                  vec4(COLOR_LOW  * midright.r, COLOR_HIGH * center.g, COLOR_LOW * center.b, 1),
                  sub_pos.x - 2.0);
    }
    else if (sub_pos.x < 4.0) {
        ret = mix(vec4(COLOR_LOW * midright.r, COLOR_HIGH * center.g , COLOR_LOW  * center.b, 1),
                  vec4(COLOR_LOW * right.r   , COLOR_HIGH  * center.g, COLOR_HIGH * center.b, 1),
                  sub_pos.x - 3.0);
    }
    else if (sub_pos.x < 5.0) {
        ret = mix(vec4(COLOR_LOW * right.r, COLOR_HIGH * center.g  , COLOR_HIGH * center.b, 1),
                  vec4(COLOR_LOW * right.r, COLOR_LOW  * midright.g, COLOR_HIGH * center.b, 1),
                  sub_pos.x - 4.0);
    }
    else {
        ret = mix(vec4(COLOR_LOW  * right.r, COLOR_LOW * midright.g, COLOR_HIGH * center.b, 1),
                  vec4(COLOR_HIGH * right.r, COLOR_LOW * right.g  ,  COLOR_HIGH * center.b, 1),
                  sub_pos.x - 5.0);
    }
    
    return ret;
}
