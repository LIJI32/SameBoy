STATIC vec4 scale2x(sampler2D image, vec2 position, vec2 input_resolution, vec2 output_resolution)
{
    // texel arrangement
    // A B C
    // D E F
    // G H I
    vec4 B = texture_relative(image, position, vec2(  0,  1));
    vec4 D = texture_relative(image, position, vec2( -1,  0));
    vec4 E = texture_relative(image, position, vec2(  0,  0));
    vec4 F = texture_relative(image, position, vec2(  1,  0));
    vec4 H = texture_relative(image, position, vec2(  0, -1));
    vec2 p = position * input_resolution;
    // p = the position within a pixel [0...1]
    p = fract(p);
    if (p.x > .5) {
        if (p.y > .5) {
            // Top Right
            return equal(B, F) && inequal(B, D) && inequal(F, H) ? F : E;
        } else {
            // Bottom Right
            return equal(H, F) && inequal(D, H) && inequal(B, F) ? F : E;
        }
    } else {
        if (p.y > .5) {
            // Top Left
            return equal(D, B) && inequal(B, F) && inequal(D, H) ? D : E;
        } else {
            // Bottom Left
            return equal(D, H) && inequal(D, B) && inequal(H, F) ? D : E;
        }
    }
}

STATIC vec4 scale2x_wrapper(sampler2D t, vec2 pos, vec2 offset, vec2 input_resolution, vec2 output_resolution)
{
    vec2 origin = (floor(pos * input_resolution * 2.0)) + vec2(0.5, 0.5);
    return scale2x(t, (origin + offset) / input_resolution / 2.0, input_resolution, output_resolution);
}

STATIC vec4 scale(sampler2D image, vec2 position, vec2 input_resolution, vec2 output_resolution)
{
    // texel arrangement
    // A B C
    // D E F
    // G H I
    vec4 B = scale2x_wrapper(image, position, vec2(  0,  1), input_resolution, output_resolution);
    vec4 D = scale2x_wrapper(image, position, vec2( -1,  0), input_resolution, output_resolution);
    vec4 E = scale2x_wrapper(image, position, vec2(  0,  0), input_resolution, output_resolution);
    vec4 F = scale2x_wrapper(image, position, vec2(  1,  0), input_resolution, output_resolution);
    vec4 H = scale2x_wrapper(image, position, vec2(  0, -1), input_resolution, output_resolution);
    
    vec2 p = position * input_resolution * 2.;
    // p = the position within a pixel [0...1]
    p = fract(p);
    if (p.x > .5) {
        if (p.y > .5) {
            // Top Right
            return equal(B, F) && inequal(B, D) && inequal(F, H) ? F : E;
        } else {
            // Bottom Right
            return equal(H, F) && inequal(D, H) && inequal(B, F) ? F : E;
        }
    } else {
        if (p.y > .5) {
            // Top Left
            return equal(D, B) && inequal(B, F) && inequal(D, H) ? D : E;
        } else {
            // Bottom Left
            return equal(D, H) && inequal(D, B) && inequal(H, F) ? D : E;
        }
    }
}
