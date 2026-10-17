$input v_texcoord0
#include "common.sh"
#include "ambient_occlusion.sh"
SAMPLER2D(s_aoLinearDepth, 1);
// View-ray slope per NDC unit (xy) and projection offset (zw).
uniform vec4 u_aoRay;

// Half-resolution linear distance avoids a matrix per tap and keeps wide taps cache-friendly.
vec3 aoLinearPosition(vec2 uv)
{
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y *= u_aoDepthParams.z;
    float distance = texture2D(s_aoLinearDepth, uv).r;
    return vec3((ndc * u_aoRay.xy + u_aoRay.zw) * distance, -distance);
}

void main()
{
    vec2 uv = v_texcoord0;
    vec3 position = aoLinearPosition(uv);
    float distance = -position.z;
    if (distance > u_aoParams.w)
    {
        gl_FragColor = vec4(1.0, min(distance, 60000.0), 0.0, 1.0);
        return;
    }
    vec2 dx = vec2(u_aoTexel.z, 0.0);
    vec2 dy = vec2(0.0, u_aoTexel.w);
    vec3 left = aoLinearPosition(uv - dx);
    vec3 right = aoLinearPosition(uv + dx);
    vec3 down = aoLinearPosition(uv - dy);
    vec3 up = aoLinearPosition(uv + dy);
    // Choose the neighbour on the same surface at silhouette discontinuities.
    vec3 tangentX = abs(left.z - position.z) < abs(right.z - position.z) ? position - left : right - position;
    vec3 tangentY = abs(down.z - position.z) < abs(up.z - position.z) ? position - down : up - position;
    vec3 normal = cross(tangentX, tangentY);
    normal /= sqrt(max(dot(normal, normal), 0.000001));
    normal *= dot(normal, -position) < 0.0 ? -1.0 : 1.0;
    // Nine rotations within the 45 degrees between directions, in a 3x3 pattern: the 3x3 depth-aware filter after this
    // pass averages each pixel over all nine, and neighbouring pixels repeat each other's tap offsets.
    vec2 cell = mod(floor(uv / u_aoTexel.zw), 3.0);
    float rotation = (cell.x + 3.0 * cell.y) * (0.78539816 / 9.0);
    vec2 ray = vec2(cos(rotation), sin(rotation));
    vec2 radiusUv = 0.5 * u_aoParams.x / (u_aoRay.xy * max(distance, 1.0));
    radiusUv = min(radiusUv, vec2(0.12, 0.12));
    float occlusion = 0.0;
    for (int direction = 0; direction < 8; ++direction)
    {
        if (direction > 0)
        {
            // The next direction, 45 degrees on.
            ray = vec2(ray.x - ray.y, ray.x + ray.y) * 0.70710678;
        }
        for (int step = 0; step < 2; ++step)
        {
            vec2 sampleUv = uv + ray * radiusUv * (0.4 + float(step) * 0.5);
            if (any(lessThan(sampleUv, vec2(0.0, 0.0))) || any(greaterThan(sampleUv, vec2(1.0, 1.0))))
            {
                continue;
            }
            vec3 delta = aoLinearPosition(sampleUv) - position;
            float lengthSquared = dot(delta, delta);
            float falloff = max(0.0, 1.0 - lengthSquared / (u_aoParams.x * u_aoParams.x));
            float facing = max(0.0, dot(normal, delta) / sqrt(max(lengthSquared, 0.01)) - u_aoParams.y);
            occlusion += facing * falloff * falloff;
        }
    }
    gl_FragColor = vec4(1.0 - min(0.65, occlusion * 0.25), distance, 0.0, 1.0);
}
