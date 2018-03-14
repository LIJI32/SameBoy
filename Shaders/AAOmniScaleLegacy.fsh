
float quickDistance(vec4 a, vec4 b)
{
    return abs(a.x - b.x) + abs(a.y - b.y) + abs(a.z - b.z);
}

vec4 omniScale(sampler2D image, vec2 texCoord)
{
    vec2 pixel = texCoord * textureDimensions - vec2(0.5, 0.5);

    vec4 q11 = texture(image, (floor(pixel) + 0.5) / textureDimensions);
    vec4 q12 = texture(image, (vec2(floor(pixel.x), ceil(pixel.y)) + 0.5) / textureDimensions);
    vec4 q21 = texture(image, (vec2(ceil(pixel.x), floor(pixel.y)) + 0.5) / textureDimensions);
    vec4 q22 = texture(image, (ceil(pixel) + 0.5) / textureDimensions);

    vec2 pos = fract(pixel);

    /* Special handling for diaonals */
    bool hasDownDiagonal = false;
    bool hasUpDiagonal = false;
    if (q12 == q21 && q11 != q22) hasUpDiagonal = true;
    else if (q12 != q21 && q11 == q22) hasDownDiagonal = true;
    else if (q12 == q21 && q11 == q22) {
        if (q11 == q12) return q11;
        int diagonalBias = 0;
        for (float y = -1.0; y < 3.0; y++) {
            for (float x = -1.0; x < 3.0; x++) {
                vec4 color = texture(image, (pixel + vec2(x, y)) / textureDimensions);
                if (color == q11) diagonalBias++;
                if (color == q12) diagonalBias--;
            }
        }
        if (diagonalBias <= 0) {
            hasDownDiagonal = true;
        }
        if (diagonalBias >= 0) {
            hasUpDiagonal = true;
        }
    }

    if (hasUpDiagonal || hasDownDiagonal) {
        vec4 downDiagonalResult, upDiagonalResult;

        if (hasUpDiagonal) {
            float diagonalPos = pos.x + pos.y;

            if (diagonalPos < 0.5) {
                upDiagonalResult = q11;
            }
            else if (diagonalPos > 1.5) {
                upDiagonalResult = q22;
            }
            else {
                upDiagonalResult = q12;
            }
        }

        if (hasDownDiagonal) {
            float diagonalPos = 1.0 - pos.x + pos.y;

            if (diagonalPos < 0.5) {
                downDiagonalResult = q21;
            }
            else if (diagonalPos > 1.5) {
                downDiagonalResult = q12;
            }
            else {
                downDiagonalResult = q11;
            }
        }

        if (!hasUpDiagonal) return downDiagonalResult;
        if (!hasDownDiagonal) return upDiagonalResult;
        return mix(downDiagonalResult, upDiagonalResult, 0.5);
    }

    vec4 r1 = mix(q11, q21, fract(pos.x));
    vec4 r2 = mix(q12, q22, fract(pos.x));

    vec4 unqunatized = mix(r1, r2, fract(pos.y));

    float q11d = quickDistance(unqunatized, q11);
    float q21d = quickDistance(unqunatized, q21);
    float q12d = quickDistance(unqunatized, q12);
    float q22d = quickDistance(unqunatized, q22);

    float best = min(q11d,
                     min(q21d,
                         min(q12d,
                             q22d)));

    if (q11d == best) {
        return q11;
    }

    if (q21d == best) {
        return q21;
    }
    
    if (q12d == best) {
        return q12;
    }
    
    return q22;
}

vec4 scale(sampler2D image)
{
    vec2 texCoord = vec2(gl_FragCoord.x, uResolution.y - gl_FragCoord.y) / uResolution;
    vec2 pixel = vec2(1.0, 1.0) / uResolution;
    // 4-pixel super sampling

    vec4 q11 = omniScale(image, texCoord + pixel * vec2(-0.25, -0.25));
    vec4 q21 = omniScale(image, texCoord + pixel * vec2(+0.25, -0.25));
    vec4 q12 = omniScale(image, texCoord + pixel * vec2(-0.25, +0.25));
    vec4 q22 = omniScale(image, texCoord + pixel * vec2(+0.25, +0.25));

    return (q11 + q21 + q12 + q22) / 4.0;
}