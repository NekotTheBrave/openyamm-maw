// Clustered forward light grid (engine/render/LightGrid.h; docs/LIGHTING_AND_MATERIALS_ARCHITECTURE.md, section 6).
// The CPU bins every light of the frame into view tiles x logarithmic depth slices; a fragment loops over only its
// own cell's lights. One point-sampled float texture, 64 texels wide, in row bands: light data (7 texels per light),
// then one texel per cell, then the index list. Read with texelFetch (GLSL 1.50, ESSL 3.00, SPIR-V).
SAMPLER2D(s_lightGrid, 11);
// [0]: tiles x, tiles y, depth slices, first cell row; [1]: near distance, log(far / near), cell cap,
// mode (0 = no grid: the surface's own point lights; 1 = full lighting; 2 = model highlights only);
// [2]: first index row.
uniform vec4 u_lightGrid[3];

#define LIGHT_GRID_MAX_CELL_LIGHTS 32
#define LIGHT_GRID_TEXTURE_WIDTH 64
#define LIGHT_GRID_TEXELS_PER_LIGHT 7
#define LIGHT_GRID_MAX_REACH 16

float lightGridMode()
{
    return u_lightGrid[1].w;
}

vec4 lightGridTexel(int texel, float firstRow)
{
    int row = texel / LIGHT_GRID_TEXTURE_WIDTH;
    return texelFetch(s_lightGrid, ivec2(texel - row * LIGHT_GRID_TEXTURE_WIDTH, int(firstRow) + row), 0);
}

// The (offset, count) of the cell a world-space point falls in, from its clip position under the view's u_viewProj
// (the same rule the CPU bins with: tile from NDC x/y, slice from clip w).
vec2 lightGridCellRange(vec3 worldPosition)
{
    vec4 clip = mul(u_viewProj, vec4(worldPosition, 1.0));
    float depth = max(clip.w, u_lightGrid[1].x);
    vec2 ndc = clip.xy / max(clip.w, 0.0001);
    float tileX = clamp(floor((ndc.x * 0.5 + 0.5) * u_lightGrid[0].x), 0.0, u_lightGrid[0].x - 1.0);
    float tileY = clamp(floor((ndc.y * 0.5 + 0.5) * u_lightGrid[0].y), 0.0, u_lightGrid[0].y - 1.0);
    float slice = clamp(floor(log(depth / u_lightGrid[1].x) / u_lightGrid[1].y * u_lightGrid[0].z), 0.0,
        u_lightGrid[0].z - 1.0);
    int cell = int(slice * u_lightGrid[0].y * u_lightGrid[0].x + tileY * u_lightGrid[0].x + tileX);
    return lightGridTexel(cell, u_lightGrid[0].w).xy;
}

int lightGridLightIndex(int entry)
{
    int texel = entry / 4;
    vec4 entries = lightGridTexel(texel, u_lightGrid[2].x);
    int component = entry - texel * 4;
    float value = component == 0 ? entries.x : (component == 1 ? entries.y : (component == 2 ? entries.z : entries.w));
    return int(value + 0.5);
}

// Position and radius (xyz, w); colour and intensity (rgb, a).
void lightGridLight(int index, out vec4 positionRadius, out vec4 colorIntensity)
{
    positionRadius = lightGridTexel(index * LIGHT_GRID_TEXELS_PER_LIGHT, 0.0);
    colorIntensity = lightGridTexel(index * LIGHT_GRID_TEXELS_PER_LIGHT + 1, 0.0);
}

// x: the light's indoor sector (-1: none); y: 1 for a dynamic light (torch, spell, effect), 0 for a static one;
// z: how many sectors it reaches (0: everywhere).
vec3 lightGridLightSource(int index)
{
    return lightGridTexel(index * LIGHT_GRID_TEXELS_PER_LIGHT + 2, 0.0).xyz;
}

// Whether a light reaches a fragment in sector a or b (indoor leak control: a light lights only the rooms it reaches
// through portals). A fragment without a sector (negative) and a light reaching everywhere always match.
bool lightGridReaches(int index, vec3 source, vec2 sectors)
{
    int count = int(source.z + 0.5);
    if (count == 0 || (sectors.x < -0.5 && sectors.y < -0.5))
    {
        return true;
    }
    // Four sectors per texel; entries past the count do not match.
    for (int group = 0; group < LIGHT_GRID_MAX_REACH / 4; ++group)
    {
        if (group * 4 >= count)
        {
            break;
        }
        vec4 texel = lightGridTexel(index * LIGHT_GRID_TEXELS_PER_LIGHT + 3 + group, 0.0);
        vec4 valid = step(vec4(0.5, 1.5, 2.5, 3.5) + float(group * 4), vec4_splat(source.z));
        vec4 match = max(step(abs(texel - sectors.x), vec4_splat(0.5)), step(abs(texel - sectors.y), vec4_splat(0.5)));
        if (dot(match, valid) > 0.5)
        {
            return true;
        }
    }
    return false;
}

// The (1 - d^2/r^2)^2 falloff of every point light, matching the static bakes.
float lightGridAttenuation(vec4 positionRadius, vec3 worldPosition)
{
    vec3 toLight = positionRadius.xyz - worldPosition;
    float radius = max(positionRadius.w, 1.0);
    float attenuation = 1.0 - clamp(dot(toLight, toLight) / (radius * radius), 0.0, 1.0);
    return attenuation * attenuation;
}

// Undirected light from the dynamic lights of the point's cell (outdoor terrain and bmodels: their static lights are
// baked or not lit by points at all), with no surface angle, as their per-draw light lists lit them.
vec3 lightGridDynamicLighting(vec3 worldPosition)
{
    vec3 lighting = vec3_splat(0.0);
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
        // Dynamic lights are binned first, so the first static one ends them.
        if (lightGridLightSource(index).y < 0.5)
        {
            break;
        }
        vec4 positionRadius;
        vec4 colorIntensity;
        lightGridLight(index, positionRadius, colorIntensity);
        lighting += colorIntensity.rgb * (colorIntensity.a * lightGridAttenuation(positionRadius, worldPosition));
    }
    return lighting;
}
