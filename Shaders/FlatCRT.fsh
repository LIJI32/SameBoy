#define COLOR_LOW 0.45
#define COLOR_HIGH 1.0
#define VERTICAL_BORDER_DEPTH 0.6
#define SCANLINE_DEPTH 0.55

STATIC vec4 scale(sampler2D image, vec2 position, vec2 input_resolution, vec2 output_resolution)
{
    /* Setting up common vars */
    vec2 pos = fract(position * input_resolution);
    vec2 sub_pos = pos * 6.0;

    vec4 center = texture_relative(image, position, vec2(0, 0));
    vec4 left = texture_relative(image, position, vec2(-1, 0));
    vec4 right = texture_relative(image, position, vec2(1, 0));
    
    /* Vertical blurring */
    if (sub_pos.y < 1.0) {
        center = mix(center, texture_relative(image, position, vec2( 0, -1)), 0.5 - sub_pos.y / 2.0);
        left =   mix(left,   texture_relative(image, position, vec2(-1, -1)), 0.5 - sub_pos.y / 2.0);
        right =  mix(right,  texture_relative(image, position, vec2( 1, -1)), 0.5 - sub_pos.y / 2.0);
    }
    else if (sub_pos.y > 5.0) {
        center = mix(center, texture_relative(image, position, vec2( 0, 1)), (sub_pos.y - 5.0) / 2.0);
        left =   mix(left,   texture_relative(image, position, vec2(-1, 1)), (sub_pos.y - 5.0) / 2.0);
        right =  mix(right,  texture_relative(image, position, vec2( 1, 1)), (sub_pos.y - 5.0) / 2.0);
    }
    
    /* Scanlines */
    float scanline_multiplier;
    if (pos.y < 0.5) {
        scanline_multiplier = (pos.y * 2.0) * SCANLINE_DEPTH + (1.0 - SCANLINE_DEPTH);
    }
    else  {
        scanline_multiplier = ((1.0 - pos.y) * 2.0) * SCANLINE_DEPTH + (1.0 - SCANLINE_DEPTH);
    }
    
    center *= scanline_multiplier;
    left *= scanline_multiplier;
    right *= scanline_multiplier;

    /* Vertical separator for shadow masks */
    bool odd = bool(int((position * input_resolution).x) & 1);
    if (odd) {
        pos.y += 0.5;
        pos.y = fract(pos.y);
    }
    
    if (pos.y < 1.0 / 3.0) {
        float gradient_position = pos.y * 3.0;
        center *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
        left *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
        right *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
    }
    else if (pos.y > 2.0 / 3.0) {
        float gradient_position = (1.0 - pos.y) * 3.0;
        center *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
        left *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
        right *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
    }

    /* Blur the edges of the separators of adjacent columns */
    if (sub_pos.x < 1.0 || sub_pos.x > 5.0) {
        pos.y += 0.5;
        pos.y = fract(pos.y);
        
        if (pos.y < 1.0 / 3.0) {
            float gradient_position = pos.y * 3.0;
            if (pos.x < 0.5) {
                gradient_position = 1.0 - (1.0 - gradient_position) * (1.0 - (pos.x) * 6.0);
            }
            else {
                gradient_position = 1.0 - (1.0 - gradient_position) * (1.0 - (1.0 - pos.x) * 6.0);
            }
            center *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
            left *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
            right *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
        }
        else if (pos.y > 2.0 / 3.0) {
            float gradient_position = (1.0 - pos.y) * 3.0;
            if (pos.x < 0.5) {
                gradient_position = 1.0 - (1.0 - gradient_position) * (1.0 - (pos.x) * 6.0);
            }
            else {
                gradient_position = 1.0 - (1.0 - gradient_position) * (1.0 - (1.0 - pos.x) * 6.0);
            }
            center *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
            left *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
            right *= gradient_position * VERTICAL_BORDER_DEPTH + (1.0 - VERTICAL_BORDER_DEPTH);
        }
    }

    
    /* Subpixel blurring, like LCD filter*/
    
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
    
    /* Anti alias the curve */
    vec2 pixel_position = position * output_resolution;
    if (pixel_position.x < 1.0) {
        ret *= pixel_position.x;
    }
    else if (pixel_position.x > output_resolution.x - 1.0) {
        ret *= output_resolution.x - pixel_position.x;
    }
    if (pixel_position.y < 1.0) {
        ret *= pixel_position.y;
    }
    else if (pixel_position.y > output_resolution.y - 1.0) {
        ret *= output_resolution.y - pixel_position.y;
    }
    
    return ret;
}
