$input v_texcoord0, v_worldPosition, v_texcoord1, v_screenspace, v_flowInfo, v_color0

#include "common.sh"
#include "world_clip.sh"
#include "light_grid.sh"

SAMPLER2D(s_texColor, 0);

uniform vec4 u_indoorLightPositions[12];
uniform vec4 u_indoorLightColors[12];
uniform vec4 u_indoorLightParams;
// x = 1 when the face takes its dynamic lights from the light grid, y/z = its sector and back sector (-1: none).
uniform vec4 u_indoorFaceGrid;
uniform vec4 u_secretPulseParams;
uniform vec4 u_indoorSkyParams;
uniform vec4 u_indoorSkyProjectionParams;

// The dynamic lights' response to the face's angle. Faces turned toward the light keep their old (angle-free) light;
// grazing faces lose only a little (a torch-lit frame is at most about 5 % darker than without N.L), and the
// response fades out just before the face turns away, so the far side of a wall takes none. Static lights are in the
// vertex bake.
float indoorFaceFacing(vec3 normal, vec3 toLight)
{
    float facing = clamp(dot(normal, toLight) * inversesqrt(max(dot(toLight, toLight), 0.0001)), 0.0, 1.0);
    return smoothstep(0.0, 0.08, facing) * (0.86 + 0.14 * sqrt(facing));
}

vec3 getIndoorLighting(vec3 worldPosition, vec3 vertexLighting)
{
    vec3 lighting = u_indoorLightParams.yzw + vertexLighting * 2.0;
    // BLV faces are flat: the normal from the position derivatives, turned toward the camera.
    vec3 normal = cross(dFdx(worldPosition), dFdy(worldPosition));
    normal *= inversesqrt(max(dot(normal, normal), 0.000001));
    if (dot(normal, mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz - worldPosition) < 0.0)
    {
        normal = -normal;
    }

    if (u_indoorFaceGrid.x > 0.5)
    {
        // The view's light grid: the dynamic lights that reach the face's sectors through portals.
        vec2 range = lightGridCellRange(worldPosition);
        int first = int(range.x + 0.5);
        int count = int(range.y + 0.5);
        for (int i = 0; i < LIGHT_GRID_MAX_CELL_LIGHTS; ++i)
        {
            if (i >= count)
            {
                break;
            }
            int index = lightGridLightIndex(first + i);
            vec3 source = lightGridLightSource(index);
            // Dynamic lights are binned first, so the first static one ends them.
            if (source.y < 0.5)
            {
                break;
            }
            if (!lightGridReaches(index, source, u_indoorFaceGrid.yz))
            {
                continue;
            }
            vec4 positionRadius;
            vec4 colorIntensity;
            lightGridLight(index, positionRadius, colorIntensity);
            lighting += colorIntensity.rgb * (colorIntensity.a * lightGridAttenuation(positionRadius, worldPosition)
                * indoorFaceFacing(normal, positionRadius.xyz - worldPosition));
        }
    }
    else
    {
        // The draw's own list (water reflections, blood splats).
        for (int i = 0; i < 12; ++i)
        {
            if (float(i) >= u_indoorLightParams.x)
            {
                break;
            }
            lighting += u_indoorLightColors[i].rgb * (u_indoorLightColors[i].w
                * lightGridAttenuation(u_indoorLightPositions[i], worldPosition)
                * indoorFaceFacing(normal, u_indoorLightPositions[i].xyz - worldPosition));
        }
    }

    return clamp(lighting, vec3(0.0, 0.0, 0.0), vec3(2.0, 2.0, 2.0));
}

void main()
{
    clipWorldPosition(v_worldPosition);
    vec2 texcoord = v_texcoord0;

    if (v_flowInfo.w < -1.5)
    {
        float screenY = u_viewRect.w - gl_FragCoord.y;
        float xDistance = (u_indoorSkyProjectionParams.x - gl_FragCoord.x) * u_indoorSkyProjectionParams.z;
        float yDistance = (u_indoorSkyProjectionParams.y - screenY) * u_indoorSkyProjectionParams.z;
        float cosYaw = cos(u_indoorSkyParams.z);
        float sinYaw = sin(u_indoorSkyParams.z);
        float oePitch = -u_indoorSkyParams.w;
        float cosPitch = cos(oePitch);
        float sinPitch = sin(oePitch);
        float skyLeft = (-sinYaw * xDistance) + (cosYaw * sinPitch * yDistance) + (cosYaw * cosPitch);
        float skyFront = (cosYaw * xDistance) + (sinYaw * sinPitch * yDistance) + (sinYaw * cosPitch);
        float v18x = -sin((-oePitch) + u_indoorSkyProjectionParams.w);
        float v18z = -cos(oePitch + u_indoorSkyProjectionParams.w);
        float topProjection = min(v18x + v18z * yDistance, -0.0000001);
        float skyDepth = -512.0 / topProjection;
        float scrollPixels = (1000.0 / 64.0) * 0.25 * u_secretPulseParams.y;
        texcoord.xy = vec2(
            (scrollPixels + skyLeft * skyDepth / 16.0) * v_flowInfo.x,
            (scrollPixels + skyFront * skyDepth / 16.0) * v_flowInfo.y);
    }
    else
    {
        texcoord.xy += v_flowInfo.xy * u_secretPulseParams.y;

        if (v_flowInfo.w < -0.5)
        {
            texcoord.xy += u_indoorSkyParams.xy;
        }
    }

    if (v_flowInfo.z > 0.5 || v_flowInfo.w > 0.5)
    {
        float pongPhase = sin(mod(u_secretPulseParams.y, 8.0) * 6.2831853 / 8.0);
        float swirlPhase = mod(u_secretPulseParams.y, 5.0) * 6.2831853 / 5.0;
        float ripplePhase = mod(u_secretPulseParams.y, 2.0) * 6.2831853 / 2.0;

        vec2 delta = vec2(0.0, 0.0);
        delta.x += pongPhase * 0.01 * sin(texcoord.x * 6.2831853);
        delta.y += pongPhase * 0.01 * sin(texcoord.y * 6.2831853);
        delta.x += 0.01 * sin(swirlPhase + texcoord.y * 6.2831853);
        delta.y += 0.01 * cos(swirlPhase + texcoord.x * 6.2831853);
        delta.x -= 0.005 * cos(ripplePhase + (texcoord.y + delta.y) * 6.2831853 * 24.0);
        texcoord += delta;
    }

    vec4 textureColor = texture2D(s_texColor, texcoord);

    if (textureColor.a <= 0.001)
    {
        discard;
    }

    vec3 color = textureColor.rgb * getIndoorLighting(v_worldPosition, v_color0.rgb);

    if (v_texcoord1.x > 0.5 && u_secretPulseParams.x > 0.5)
    {
        float pulse = 0.5 + 0.5 * sin(u_secretPulseParams.y * 4.0);
        vec3 secretTint = color * vec3(1.0, pulse, pulse);
        color = secretTint;
    }

    gl_FragColor = vec4(color, textureColor.a);
}
