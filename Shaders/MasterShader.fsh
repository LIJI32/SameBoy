#version 150
uniform sampler2D image;
uniform sampler2D previous_image;
uniform bool mix_previous;

uniform vec2 output_resolution;
uniform vec2 origin;
const vec2 input_resolution = vec2(160, 144);

#define equal(x, y) ((x) == (y))
#define inequal(x, y) ((x) != (y))
#define STATIC

out vec4 frag_color;

#line 1
{filter}

void main()
{
    vec2 position = gl_FragCoord.xy - origin;
    position /= output_resolution;
    position.y = 1 - position.y;
    
    if (mix_previous) {
        frag_color = mix(scale(image, position, input_resolution, output_resolution),
                         scale(previous_image, position, input_resolution, output_resolution), 0.5);
    }
    else {
        frag_color = scale(image, position, input_resolution, output_resolution);
    }
}
