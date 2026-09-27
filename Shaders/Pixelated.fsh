STATIC vec4 scale(sampler2D image, vec2 position, vec2 input_resolution, vec2 output_resolution)
{
    vec2 scale_factor = output_resolution / input_resolution;
    vec2 pixel = position * input_resolution - vec2(0.5, 0.5);
    
    vec4 q11 = texture(image, (floor(pixel) + 0.5) / input_resolution);
    vec4 q12 = texture(image, (vec2(floor(pixel.x), ceil(pixel.y)) + 0.5) / input_resolution);
    vec4 q21 = texture(image, (vec2(ceil(pixel.x), floor(pixel.y)) + 0.5) / input_resolution);
    vec4 q22 = texture(image, (ceil(pixel) + 0.5) / input_resolution);
    
    vec2 t = fract(pixel);
    vec2 w = clamp((t - 0.5) * scale_factor + 0.5, 0.0, 1.0);
    
    vec4 r1 = mix(q11, q21, w.x);
    vec4 r2 = mix(q12, q22, w.x);
    
    return mix(r1, r2, w.y);
}
