@@Version

// Keep the horizontal shader's public controls in lockstep with world_pbr.frag.
@@Uniform(float VIEW_DISTANCE);
@@Uniform(float GLOBAL_TIME);
@@Uniform(float PIXEL_SIZE);
@@Uniform(float FAR_GRID_SIZE);
@@Uniform(vec3 PLAYER_POSITION);
@@Uniform(vec3 LIGHT_POSITION);
@@Uniform(float LIQUID_EYE_SURFACE_Z);
@@Uniform(vec3 LIQUID_EXTINCTION);
@@Uniform(vec3 LIQUID_TINT);
@@Uniform(int MPP_VIRTUAL_CAMERA);
@@Uniform(float LIGHT_ATTENUATION_RADIUS);
@@Uniform(float LIGHT_ATTENUATION_FALLOFF);
@@Uniform(float MATERIAL_SCALE);
@@Uniform(int SECONDARY_MATERIAL_INDEX);
@@Uniform(int USE_SECONDARY_MATERIAL);

// Per batch. The emboss relief resolves from the surface's Emboss preset,
// not a global render option, so it arrives with the batch exactly as the
// Technique index and its parameters do. EMBOSS_PATTERN is EmbossPattern:
// 0 none, 1 square, 2 hexagon, 3 running bond, 4 modular opus, 5 Voronoi.
@@Uniform(int MATERIAL_INDEX);
@@Uniform(float MATERIAL_PARAMS[8]);
@@Uniform(int EMBOSS_PATTERN);
@@Uniform(float EMBOSS_RADIUS);
@@Uniform(float EMBOSS_DEPTH);
@@Uniform(float EMBOSS_DEPTH_VARIATION);
@@Uniform(float EMBOSS_RUNNING_BOND_WIDTH);
@@Uniform(float EMBOSS_RUNNING_BOND_OFFSET);
@@Uniform(float EMBOSS_VORONOI_ROUNDING);
@@Uniform(int WALL_NORMAL_MAP_ENABLED);
@@Uniform(float WALL_NORMAL_MAP_STRENGTH);
@@Uniform(float WALL_NORMAL_MAP_ASPECT_RATIO);
## Texture
@@Texture(sampler2D TEX1);
##
@@Texture(sampler2DShadow SHADOW_MAP);
@@Texture(samplerCubeShadow POINT_SHADOW_MAP);

layout(std140, binding = 2) uniform ShadowFrame
{
    mat4 LIGHT_VIEW_PROJECTION;
    vec4 MAP_TEXEL_SIZE_AND_RADIUS;
    vec4 BIAS_AND_ENABLED;
    vec4 POINT_POSITION_AND_RANGE;
    vec4 SHADOW_TYPE_AND_LIGHT_INDEX;
};

layout(std140, binding = 3) uniform CameraFrame
{
    mat4 VIEW_MATRIX;
    mat4 PROJECTION_MATRIX;
    mat4 INVERSE_PROJECTION_MATRIX;
    vec4 VIEWPORT_SIZE;
    vec4 NEAR_FAR_TIME;
};

vec2 encodeOctahedralNormal(vec3 normal)
{
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    vec2 oct = normal.xy;
    if (normal.z < 0.0)
        oct = (1.0 - abs(oct.yx)) * sign(oct.xy);
    return oct * 0.5 + 0.5;
}

const float PI = 3.14159265359;

vec2 snapToGrid(vec2 p, float gridSize)
{
    return round(p / gridSize) * gridSize;
}

vec2 quantizeByPlayerDistance(vec2 p, float fragmentDistance)
{
    float distanceRatio = clamp(
        fragmentDistance / max(@Uniform(VIEW_DISTANCE), 0.001), 0.0, 1.0);
    const float levelCount = 4.0;
    float level = smoothstep(0.12, 0.92, distanceRatio) * levelCount;
    float baseGridSize = max(@Uniform(PIXEL_SIZE), 0.00001);
    float farGridSize = max(@Uniform(FAR_GRID_SIZE), baseGridSize);
    float gridRatio = farGridSize / baseGridSize;
    float lowerLevel = floor(level);
    float upperLevel = min(lowerLevel + 1.0, levelCount);
    float lowerGridSize = baseGridSize *
                          pow(gridRatio, lowerLevel / levelCount);
    float upperGridSize = baseGridSize *
                          pow(gridRatio, upperLevel / levelCount);

    // Cross-fade adjacent grid levels with a quintic curve. Both ends
    // have zero first and second derivatives, avoiding visible LOD pops while
    // keeping both endpoints grid-quantized.
    float transition = fract(level);
    transition = transition * transition * transition *
                 (transition * (transition * 6.0 - 15.0) + 10.0);
    return mix(
        snapToGrid(p, lowerGridSize),
        snapToGrid(p, upperGridSize), transition);
}

struct Material
{
    vec3 albedo;
    float metallic;
    float roughness;
    vec3 normal;
};

float hash(vec2 p)
{
    p = fract(p * vec2(0.3183099, 0.3678794) + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * (p.x + p.y));
}

float noise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 local = fract(p);
    local = local * local * (3.0 - 2.0 * local);
    float a = hash(cell);
    float b = hash(cell + vec2(1.0, 0.0));
    float c = hash(cell + vec2(0.0, 1.0));
    float d = hash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, local.x), mix(c, d, local.x), local.y);
}

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    mat2 rotation = mat2(0.80, -0.60, 0.60, 0.80);
    for (int octave = 0; octave < 5; ++octave)
    {
        value += amplitude * noise(p);
        p = rotation * p * 2.03 + vec2(13.1, 7.7);
        amplitude *= 0.5;
    }
    return value;
}

float voronoi(vec2 p)
{
    vec2 cell = floor(p);
    vec2 local = fract(p);
    float nearest = 10.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 offset = vec2(float(x), float(y));
            vec2 feature = offset + vec2(
                hash(cell + offset + vec2(17.0, 3.0)),
                hash(cell + offset + vec2(5.0, 19.0)));
            nearest = min(nearest, length(feature - local));
        }
    }
    return nearest;
}

vec3 spectralPalette(float phase)
{
    return 0.52 + 0.48 * cos(6.2831853 *
        (phase + vec3(0.0, 0.33, 0.67)));
}

float frostedGlassField(vec2 p)
{
    float facets = voronoi(p * 8.5);
    float scratches = noise(vec2(p.x * 26.0, p.y * 3.0));
    return (1.0 - smoothstep(0.05, 0.34, facets)) * 0.65 +
           scratches * 0.35;
}

// brick_height is a parameter, not a hardcoded local, so it reaches this
// consistently with material2d's own type-31 copy of the same layout math;
// materialField's type-31 branch (this function's only caller) is updated
// to match.
float brickReliefField(vec2 p, float brickHeight)
{
    float brickWidth = 1.0;
    float mortarWidth = 0.05;
    float row = floor(p.y / brickHeight);
    float rowOffset = mod(row, 2.0) * brickWidth * 0.5;
    vec2 local = vec2(
        mod(p.x - rowOffset, brickWidth), mod(p.y, brickHeight));
    vec2 distanceToEdge = min(
        local, vec2(brickWidth, brickHeight) - local);
    return 1.0 - smoothstep(
        0.0, mortarWidth, min(distanceToEdge.x, distanceToEdge.y));
}

float circuitReliefField(vec2 p)
{
    vec2 grid = p * 9.0;
    vec2 cell = floor(grid);
    vec2 local = fract(grid) - 0.5;
    float trace = 1.0 - smoothstep(
        0.045, 0.10, min(abs(local.x), abs(local.y)));
    float traceActive = step(0.4, hash(cell));
    float pad = 1.0 - smoothstep(0.11, 0.17, length(local));
    float padActive = step(0.82, hash(cell + vec2(5.0, 2.0)));
    return max(trace * traceActive, pad * padActive);
}

// fine_trace_mix is a parameter, not a hardcoded local, so it reaches this
// consistently with materialField's type-32 caller - its only caller.
float circuitBoardField(vec2 p, float fineTraceMix)
{
    return clamp(max(
        circuitReliefField(p),
        circuitReliefField(p * 2.2 + vec2(11.0, 4.0)) * fineTraceMix),
        0.0, 1.0);
}

float naturalRockField(vec2 p)
{
    float fractured = fbm(p * 0.82);
    float grains = noise(p * 7.5);
    float cells = voronoi(p * 2.6);
    return fractured * 0.58 + grains * 0.20 +
           (1.0 - smoothstep(0.10, 0.42, cells)) * 0.22;
}

// Marble (type 0) and Stone (type 1) read three of their eight/three
// MATERIAL_PARAMS here - warpScale, veinsScale, veinsFineScale for Marble;
// mediumScale for Stone - kept as direct @Uniform(MATERIAL_PARAMS[i]) reads
// rather than a struct threaded through this signature, since materialField
// is shared by all 37 material types and every other branch must stay
// exactly as it was. See MarbleParams/StoneParams in world_pbr.frag for the
// full parameter set and what each slot means; fbmScale/baseScale and
// Marble's remaining four are applied by material2d below instead, at the
// two places this compressed encoding still has a type-local bind point for
// them. Every material's base_scale (slot 0, or slot 7 for Marble's
// fbm_scale) is bound in material2d's p-scale override; most materials'
// second parameter is bound too, at whichever of materialField's or
// material2d's own type branch has an isolated local constant for it - see
// the comment at each bind site for which shader constant it replaced. Ten
// materials' second parameter (Obsidian, Rusted iron, Galvanized steel,
// Brushed metal, Patinated copper, Damascene steel, Leather, Flesh, Coral,
// Arcane crystal) and Marble's remaining four have no such point without
// restructuring code every other material type shares, so - like
// light_warm_mix/vein_mix/cloudiness/fine_detail_scale above - they stay
// unbound here and only take effect in the 3D pass.
float materialField(vec2 p, int type)
{
    if (type == 0)
    {
        float warpScale = @Uniform(MATERIAL_PARAMS[0]);
        float veinsScale = @Uniform(MATERIAL_PARAMS[1]);
        float veinsFineScale = @Uniform(MATERIAL_PARAMS[2]);
        vec2 warp = vec2(
            noise(p * 0.65 + vec2(7.1, 1.7)),
            noise(p * 0.65 + vec2(2.8, 9.2))) - 0.5;
        vec2 q = p + warp * warpScale;
        float turbulence = fbm(q * 1.15) - 0.5;
        float broad = abs(sin(q.x * veinsScale + q.y * 0.8 + turbulence * 7.0));
        float fine = abs(sin(q.x * veinsFineScale + q.y * 2.1 + fbm(q * 2.4) * 5.0));
        return max(1.0 - smoothstep(0.06, 0.30, broad),
                   (1.0 - smoothstep(0.025, 0.13, fine)) * 0.45);
    }
    if (type == 1)
    {
        float mediumScale = @Uniform(MATERIAL_PARAMS[1]);
        return fbm(p * 0.65) * 0.45 + noise(p * mediumScale) * 0.20 +
               (1.0 - smoothstep(0.12, 0.48, voronoi(p * 2.2))) * 0.35;
    }
    if (type == 2)
    {
        float layers = sin(p.y * 8.0 + p.x * 0.7 + fbm(p * 0.8) * 3.2) * 0.5 + 0.5;
        float fracture = 1.0 - smoothstep(0.015, 0.09, abs(noise(p * 3.0) - 0.5));
        return layers * 0.32 + fracture * 0.68;
    }
    if (type == 3)
        return (sin(p.y * 5.5 + fbm(p * 0.45) * 4.0) * 0.5 + 0.5) * 0.62 +
               noise(p * 18.0) * 0.38;
    if (type == 4)
        return fbm(p * 0.75) * 0.58 -
               (1.0 - smoothstep(0.08, 0.28, voronoi(p * 5.0))) * 0.42;
    if (type == 5)
        return noise(p * 12.0) * 0.25 -
               (1.0 - smoothstep(0.10, 0.32, voronoi(p * 3.8))) * 0.75;
    if (type == 6)
    {
        vec2 q = p + (vec2(noise(p * 0.55 + vec2(4.0, 7.0)),
                              noise(p * 0.55 + vec2(8.0, 2.0))) - 0.5) * 1.8;
        return sin(q.x * 3.7 + q.y * 2.4) * 0.5 + 0.5;
    }
    if (type == 7)
        return voronoi(p * 2.4) * 0.78 +
               (sin(dot(p, normalize(vec2(1.0, -0.35))) * 13.0) * 0.5 + 0.5) * 0.22;
    if (type == 8)
    {
        // vein_scale (MATERIAL_PARAMS[1]) - same 4.5 default as oreTexture.
        float veinScale = @Uniform(MATERIAL_PARAMS[1]);
        float warp = fbm(p * 0.62) - 0.5;
        float vein = abs(sin(p.x * veinScale - p.y * 0.8 + warp * 8.0));
        return fbm(p * 1.6) * 0.42 + (1.0 - smoothstep(0.06, 0.24, vein)) * 0.58;
    }
    if (type == 9)
        return fbm(p * 1.15) * 0.58 -
               (1.0 - smoothstep(0.08, 0.25, voronoi(p * 6.0))) * 0.42;
    if (type == 10)
        return voronoi(p * 3.6) + noise(p * 9.0) * 0.12;
    if (type == 11)
        return noise(vec2(p.x * 1.2, p.y * 35.0)) * 0.68 +
               noise(vec2(p.x * 2.0, p.y * 90.0)) * 0.32;
    if (type == 12)
        // dent_scale (MATERIAL_PARAMS[1]) - same 4.2 default as
        // hammeredMetalTexture.
        return smoothstep(0.08, 0.72, voronoi(p * @Uniform(MATERIAL_PARAMS[1])));
    if (type == 13)
        return fbm(p * 0.82) * 0.72 + noise(vec2(p.x * 2.0, p.y * 0.35)) * 0.28;
    if (type == 14)
        return sin(p.x * 10.0 + p.y * 2.1 + (fbm(p * 0.65) - 0.5) * 9.0) * 0.5 + 0.5;
    if (type == 15)
        return sin(dot(p, normalize(vec2(0.8, -0.55))) * 4.0 + fbm(p * 0.45) * 3.0) * 0.5 + 0.5;
    if (type == 16)
    {
        // ring_scale (MATERIAL_PARAMS[1]) - same 18.0 default as woodTexture.
        float ringScale = @Uniform(MATERIAL_PARAMS[1]);
        float rings = sin(length(p) * ringScale + fbm(p * 0.55) * 4.5) * 0.5 + 0.5;
        return rings * 0.68 + noise(vec2(p.x * 4.0, p.y * 0.32)) * 0.32;
    }
    if (type == 17)
    {
        // ridge_scale (MATERIAL_PARAMS[1]) - same 7.0 default as barkTexture.
        float ridgeScale = @Uniform(MATERIAL_PARAMS[1]);
        float ridges = abs(sin(p.x * ridgeScale + p.y * 5.0 + fbm(p * 0.48) * 3.0));
        float cracks = 1.0 - smoothstep(0.025, 0.14,
            abs(noise(vec2(p.x * 2.2, p.y * 0.35)) - 0.5));
        return ridges * 0.58 - cracks * 0.72;
    }
    if (type == 18)
        // pore_scale (MATERIAL_PARAMS[1]) - same 7.0 default as boneTexture.
        return fbm(p * 0.82) * 0.70 -
               (1.0 - smoothstep(0.07, 0.23, voronoi(p * @Uniform(MATERIAL_PARAMS[1])))) * 0.30;
    if (type == 19)
    {
        float cells = voronoi(p * 5.5);
        float wrinkles = sin(p.x * 3.0 + p.y * 2.0 + fbm(p * 0.8) * 6.0) * 0.5 + 0.5;
        float pores = 1.0 - smoothstep(0.035, 0.13, voronoi(p * 15.0));
        return cells * 0.36 + wrinkles * 0.38 - pores * 0.26;
    }
    if (type == 20)
    {
        float vein = abs(sin(p.x * 3.6 + p.y * 2.3 + fbm(p * 0.5) * 7.0));
        return fbm(p * 0.72) * 0.82 - (1.0 - smoothstep(0.035, 0.16, vein)) * 0.18;
    }
    if (type == 21)
        // plate_scale (MATERIAL_PARAMS[1]) - same 3.2 default as
        // chitinTexture.
        return voronoi(p * @Uniform(MATERIAL_PARAMS[1])) * 0.55 +
               (sin(dot(p, normalize(vec2(0.7, 0.65))) * 9.0 + fbm(p * 0.65) * 3.0) * 0.5 + 0.5) * 0.45;
    if (type == 22)
        return fbm(p * 1.1) * 0.48 -
               (1.0 - smoothstep(0.10, 0.31, voronoi(p * 5.2))) * 0.72;
    if (type == 23)
        return voronoi(p * 2.8) * 0.72 +
               (sin(dot(p, normalize(vec2(0.6, -0.28))) * 15.0) * 0.5 + 0.5) * 0.28;
    if (type == 24)
    {
        // energy_scale (MATERIAL_PARAMS[1]) - same 4.2 default as
        // energyStoneTexture.
        float energyScale = @Uniform(MATERIAL_PARAMS[1]);
        float seam = abs(sin(p.x * energyScale + p.y * 2.0 + (fbm(p * 0.55) - 0.5) * 8.0));
        return fbm(p * 1.45) * 0.62 - (1.0 - smoothstep(0.035, 0.18, seam)) * 0.72;
    }
    if (type == 25)
        // cell_scale (MATERIAL_PARAMS[1]) - same 3.5 default as
        // alienTissueTexture.
        return voronoi(p * @Uniform(MATERIAL_PARAMS[1])) * 0.52 +
               abs(sin(p.x * 3.0 - p.y * 2.7 + fbm(p * 0.7) * 5.0)) * 0.48;
    if (type == 26)
    {
        // rune_scale (MATERIAL_PARAMS[1]) - same 1.7 default as
        // magicalMetalTexture. material2d's own type-26 branch recomputes
        // this same grid for its albedo blend and reads the same slot.
        float runeScale = @Uniform(MATERIAL_PARAMS[1]);
        vec2 grid = abs(fract(p * runeScale) - 0.5);
        float rune = 1.0 - smoothstep(0.035, 0.10, min(grid.x, grid.y));
        float warp = fbm(p * 0.65) - 0.5;
        float flow = sin(p.x * 10.0 + p.y * 2.1 + warp * 9.0) * 0.5 + 0.5;
        return flow * 0.72 - rune * 0.28;
    }
    if (type == 27)
        return fbm(p * 0.72) * 0.72 + fbm(p * 2.8) * 0.28;
    if (type == 28)
        // scan_scale (MATERIAL_PARAMS[1]) - same 35.0 default as
        // holographicTexture.
        return (sin(p.y * @Uniform(MATERIAL_PARAMS[1]) + @Uniform(GLOBAL_TIME) * 3.0) * 0.5 + 0.5) * 0.28 + noise(p * 9.0) * 0.12;
    if (type == 30)
        return frostedGlassField(p);
    if (type == 31)
        // brick_height (MATERIAL_PARAMS[1]) - same 0.42 default as
        // brickTexture.
        return brickReliefField(p, @Uniform(MATERIAL_PARAMS[1]));
    if (type == 32)
        // fine_trace_mix (MATERIAL_PARAMS[1]) - same 0.55 default as
        // circuitBoardTexture.
        return circuitBoardField(p, @Uniform(MATERIAL_PARAMS[1]));
    if (type == 33)
    {
        float warp = fbm(p * 0.48) - 0.5;
        float bands = sin(dot(p, normalize(vec2(0.82, -0.52))) * 8.0 +
                          warp * 7.0) * 0.5 + 0.5;
        return bands * 0.72 + noise(p * 9.0) * 0.28;
    }
    if (type == 34)
        return naturalRockField(p);
    if (type == 35)
        return naturalRockField(p) * 0.82 + fbm(p * 2.1) * 0.18;
    if (type == 36)
        return naturalRockField(p) * 0.78 + fbm(p * 0.42) * 0.22;

    float spread = fbm(p * 0.58 + vec2(@Uniform(GLOBAL_TIME) * 0.025));
    float tendril = abs(sin(p.x * 3.4 + p.y * 2.6 + spread * 9.0));
    return spread * 0.46 + voronoi(p * 3.8) * 0.22 -
           (1.0 - smoothstep(0.03, 0.17, tendril)) * 0.48;
}

vec3 perturbHorizontalNormal(
    vec2 p, vec3 normal, int type, float field, float strength)
{
    const float epsilon = 0.024;
    vec2 gradient = vec2(
        materialField(p + vec2(epsilon, 0.0), type) - field,
        materialField(p + vec2(0.0, epsilon), type) - field) / epsilon;
    float facing = normal.y < 0.0 ? -1.0 : 1.0;
    return normalize(normal - vec3(gradient.x, 0.0, gradient.y) *
                     strength * facing);
}

// Horizontal port of "Procedural Wood texture" by dean_the_coder:
// https://www.shadertoy.com/view/mdy3R1
// Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported.
float wood2Sum(vec2 v) { return dot(v, vec2(1.0)); }
float wood2Hash31(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 333.3456);
    return fract(wood2Sum(p.xy) * p.z);
}
float wood2Hash21(vec2 p) { return wood2Hash31(p.xyx); }
float wood2Noise31(vec3 p)
{
    const vec3 s = vec3(7.0, 157.0, 113.0);
    vec3 cell = floor(p);
    p = fract(p);
    p = p * p * (3.0 - 2.0 * p);
    vec4 h = vec4(0.0, s.yz, wood2Sum(s.yz)) + dot(cell, s);
    h = mix(fract(sin(h) * 43758.545),
            fract(sin(h + s.x) * 43758.545), p.x);
    h.xy = mix(h.xz, h.yw, p.y);
    return mix(h.x, h.y, p.z);
}
float wood2Fbm(vec3 p, int octaves, float roughness)
{
    float sum = 0.0;
    float amplitude = 1.0;
    float total = 0.0;
    roughness = clamp(roughness, 0.0, 1.0);
    for (int octave = 0; octave < octaves; ++octave)
    {
        sum += amplitude * wood2Noise31(p);
        total += amplitude;
        amplitude *= roughness;
        p *= 2.0;
    }
    return sum / total;
}
vec3 wood2RandomPosition(float seed)
{
    vec4 s = vec4(seed, 0.0, 1.0, 2.0);
    return vec3(wood2Hash21(s.xy), wood2Hash21(s.xz),
                wood2Hash21(s.xw)) * 100.0 + 100.0;
}
float wood2DistortedFbm(vec3 p)
{
    p += (vec3(wood2Noise31(p + wood2RandomPosition(0.0)),
               wood2Noise31(p + wood2RandomPosition(1.0)),
               wood2Noise31(p + wood2RandomPosition(2.0))) * 2.0 - 1.0) *
         1.12;
    return wood2Fbm(p, 8, 0.5);
}
float wood2MusgraveFbm(
    vec3 p, float octaves, float dimension, float lacunarity)
{
    float sum = 0.0;
    float amplitude = 1.0;
    float multiplier = pow(lacunarity, -dimension);
    for (float octave = 0.0; octave < octaves; octave += 1.0)
    {
        sum += (wood2Noise31(p) * 2.0 - 1.0) * amplitude;
        amplitude *= multiplier;
        p *= lacunarity;
    }
    return sum;
}
vec3 wood2WaveFbmX(vec3 p)
{
    float wave = p.x * 20.0;
    wave += 0.4 * wood2Fbm(p * 3.0, 3, 3.0);
    return vec3(sin(wave) * 0.5 + 0.5, p.yz);
}
float wood2Remap01(float value, float low, float high)
{
    return clamp((value - low) / (high - low), 0.0, 1.0);
}
vec3 wood2Colour(vec3 p)
{
    float firstNoise = wood2DistortedFbm(p * vec3(7.8, 1.17, 1.17));
    firstNoise = mix(firstNoise, 1.0, 0.2);
    float wood = mix(
        wood2MusgraveFbm(vec3(firstNoise * 4.6), 8.0, 0.0, 2.5),
        firstNoise, 0.85);
    float dirt = 1.0 - wood2MusgraveFbm(
        wood2WaveFbmX(p * vec3(0.01, 0.15, 0.15)),
        15.0, 0.26, 2.4) * 0.4;
    float grain = 1.0 - smoothstep(
        0.2, 1.0,
        wood2MusgraveFbm(p * vec3(500.0, 6.0, 1.0),
                         2.0, 2.0, 2.5)) * 0.2;
    wood *= dirt * grain;
    return mix(
        mix(vec3(0.03, 0.012, 0.003), vec3(0.25, 0.11, 0.04),
            wood2Remap01(wood, 0.19, 0.56)),
        vec3(0.52, 0.32, 0.19), wood2Remap01(wood, 0.56, 1.0));
}
Material wood2Material2d(vec2 worldPos, vec3 normal)
{
    Material material;
    vec3 p = vec3(worldPos.x, 0.0, worldPos.y) *
             @Uniform(MATERIAL_PARAMS[0]);
    material.albedo = wood2Colour(p);
    material.metallic = 0.0;
    material.roughness = 0.56;
    material.normal = normalize(normal);
    return material;
}

Material plainGreyMaterial2d(vec3 normal)
{
    Material material;
    material.albedo = vec3(0.5, 0.5, 0.5);
    material.metallic = 0.0;
    material.roughness = 0.0;
    material.normal = normalize(normal);
    return material;
}

Material waterMaterial2d(vec3 normal)
{
    Material material;
    material.albedo = vec3(0.1, 0.35, 0.6);
    material.metallic = 0.0;
    material.roughness = 0.0;
    material.normal = normalize(normal);
    return material;
}

Material material2d(vec2 worldPos, vec3 normal, vec3 viewDir, int type)
{
    if (type == 37)
        return wood2Material2d(worldPos, normal);
    if (type == 38)
        return plainGreyMaterial2d(normal);
    if (type == 40)
        return waterMaterial2d(normal);

    Material material;
    float scales[37] = float[37](
        0.72, 0.82, 0.72, 0.58, 0.66, 0.85, 0.70, 0.62, 0.72, 0.72,
        0.66, 0.82, 0.72, 0.66, 0.72, 0.62, 0.54, 0.62, 0.68, 0.76,
        0.60, 0.67, 0.66, 0.64, 0.68, 0.67, 0.72, 0.53, 0.75, 0.65,
        1.40, 0.95, 1.15, 0.68, 0.74, 0.72, 0.74);
    float strengths[37] = float[37](
        0.11, 0.055, 0.045, 0.07, 0.065, 0.075, 0.018, 0.075, 0.06,
        0.065, 0.035, 0.012, 0.085, 0.04, 0.018, 0.014, 0.038, 0.095,
        0.045, 0.035, 0.022, 0.048, 0.085, 0.095, 0.065, 0.055, 0.026,
        0.032, 0.008, 0.072, 0.045, 0.045, 0.020, 0.052, 0.075, 0.068,
        0.050);
    vec2 p = worldPos * scales[type];
    // Marble's fbm_scale and Stone's base_scale (MATERIAL_PARAMS[7] and [0])
    // override this shared per-type table for exactly the one type each
    // belongs to; scales[0] and scales[1] are those parameters' own defaults,
    // so nothing changes here until a Primitive's material is actually edited.
    if (type == 0) {
        // Marble alone keeps its scale in slot 7 (fbm_scale) - every other
        // material's base_scale is slot 0.
        p = worldPos * @Uniform(MATERIAL_PARAMS[7]);
    } else {
        p = worldPos * @Uniform(MATERIAL_PARAMS[0]);
    }
    float field = materialField(p, type);
    float detail = noise(p * 7.0);
    float cells = voronoi(p * 3.5);
    float broad = fbm(p * 0.75);

    material.albedo = vec3(0.5);
    material.metallic = 0.0;
    material.roughness = 0.6;

    if (type == 0) {
        vec3 stone = mix(vec3(0.72, 0.76, 0.81), vec3(0.93, 0.89, 0.82), broad);
        material.albedo = mix(stone * mix(0.91, 1.06, detail), vec3(0.06, 0.04, 0.04), field);
        material.roughness = clamp(mix(0.32, 0.18, field) + (detail - 0.5) * 0.10, 0.12, 0.48);
    } else if (type == 1) {
        // stone_mix (MATERIAL_PARAMS[2]) - same 0.28 default as graniteTexture
        // in world_pbr.frag, and the same role: how strongly mica flecks read
        // as metallic.
        float stoneMix = @Uniform(MATERIAL_PARAMS[2]);
        float quartz = smoothstep(0.68, 0.88, detail);
        material.albedo = mix(vec3(0.16, 0.15, 0.15), vec3(0.72, 0.70, 0.66), quartz);
        material.metallic = smoothstep(0.90, 0.98, noise(p * 13.0)) * stoneMix;
        material.roughness = 0.58 - quartz * 0.16;
    } else if (type == 2) {
        // Slate's rust_mix has no local bind point here the way it does in
        // world_pbr.frag's slateTexture - this compressed branch has no
        // separate rust blend at all, only the shared "field" - so it stays
        // unbound in this shader, same as four of Marble's parameters above.
        material.albedo = mix(vec3(0.075, 0.095, 0.115), vec3(0.18, 0.21, 0.22), field);
        material.roughness = clamp(0.42 + field * 0.18, 0.34, 0.68);
    } else if (type == 3) {
        // Sandstone's grain_scale would have to change the shared `detail`
        // variable other types also read, so it stays unbound here too.
        material.albedo = mix(vec3(0.70, 0.43, 0.22), vec3(0.43, 0.16, 0.075), field) * mix(0.86, 1.08, detail);
        material.roughness = clamp(0.72 + (detail - 0.5) * 0.18, 0.58, 0.9);
    } else if (type == 4) {
        // pore_mix (MATERIAL_PARAMS[1]) - same 0.38 default as
        // limestoneTexture in world_pbr.frag.
        float poreMix = @Uniform(MATERIAL_PARAMS[1]);
        float pores = 1.0 - smoothstep(0.08, 0.28, voronoi(p * 5.0));
        material.albedo = mix(vec3(0.48, 0.45, 0.35), vec3(0.82, 0.79, 0.66), broad) * (1.0 - pores * poreMix);
        material.roughness = 0.62 + pores * 0.25;
    } else if (type == 5) {
        // vesicle_mix (MATERIAL_PARAMS[1]) - same 0.72 default as
        // basaltTexture.
        float vesicleMix = @Uniform(MATERIAL_PARAMS[1]);
        float pores = 1.0 - smoothstep(0.10, 0.32, voronoi(p * 3.8));
        material.albedo = mix(vec3(0.025), vec3(0.13), detail) * (1.0 - pores * vesicleMix);
        material.metallic = 0.03; material.roughness = 0.58 + pores * 0.30;
    } else if (type == 6) {
        material.albedo = mix(vec3(0.006, 0.008, 0.012), vec3(0.055, 0.025, 0.075), pow(field, 4.0));
        material.roughness = 0.075 + smoothstep(0.84, 0.96, detail) * 0.24;
    } else if (type == 7) {
        // amethyst_mix (MATERIAL_PARAMS[1]) - same 0.72 default as
        // quartzTexture.
        material.albedo = mix(vec3(0.72, 0.82, 0.88), vec3(0.34, 0.14, 0.52), broad * @Uniform(MATERIAL_PARAMS[1]));
        material.roughness = clamp(0.11 + cells * 0.20, 0.08, 0.34);
    } else if (type == 8) {
        float metal = smoothstep(0.48, 0.70, field);
        material.albedo = mix(vec3(0.08), vec3(0.62, 0.48, 0.20), metal);
        material.metallic = metal; material.roughness = mix(0.68, 0.20, metal);
    } else if (type == 9) {
        float rust = smoothstep(0.42, 0.72, broad);
        material.albedo = mix(vec3(0.22, 0.23, 0.24), vec3(0.58, 0.19, 0.035), rust);
        material.metallic = 0.92 * (1.0 - rust); material.roughness = mix(0.28, 0.88, rust);
    } else if (type == 10) {
        material.albedo = mix(vec3(0.42, 0.45, 0.47), vec3(0.74, 0.77, 0.78), clamp(field, 0.0, 1.0));
        material.metallic = 0.94; material.roughness = 0.25 + clamp(field, 0.0, 1.0) * 0.20;
    } else if (type == 11) {
        material.albedo = mix(vec3(0.42), vec3(0.70), field); material.metallic = 0.96; material.roughness = 0.20 + field * 0.30;
    } else if (type == 12) {
        material.albedo = mix(vec3(0.24), vec3(0.56), field); material.metallic = 0.92; material.roughness = 0.30 + (1.0 - field) * 0.22;
    } else if (type == 13) {
        float patina = smoothstep(0.43, 0.67, field);
        material.albedo = mix(vec3(0.72, 0.27, 0.09), vec3(0.08, 0.38, 0.29), patina);
        material.metallic = mix(0.98, 0.03, patina); material.roughness = mix(0.20, 0.72, patina);
    } else if (type == 14) {
        material.albedo = mix(vec3(0.12), vec3(0.65), field); material.metallic = 0.95; material.roughness = mix(0.34, 0.17, field);
    } else if (type == 15) {
        // oxide_mix (MATERIAL_PARAMS[1]) - same 0.72 default as
        // heatTreatedMetalTexture.
        vec3 oxide = mix(vec3(0.78, 0.38, 0.08), vec3(0.035, 0.16, 0.48), field);
        material.albedo = mix(vec3(0.40), oxide, @Uniform(MATERIAL_PARAMS[1])); material.metallic = 0.90; material.roughness = 0.19 + detail * 0.13;
    } else if (type == 16) {
        material.albedo = mix(vec3(0.16, 0.055, 0.018), vec3(0.58, 0.29, 0.095), field) * mix(0.76, 1.12, detail);
        material.roughness = 0.48 + detail * 0.18;
    } else if (type == 17) {
        material.albedo = mix(vec3(0.055, 0.020, 0.008), vec3(0.30, 0.12, 0.035), smoothstep(0.2, 0.78, field)); material.roughness = 0.82;
    } else if (type == 18) {
        material.albedo = mix(vec3(0.52, 0.43, 0.27), vec3(0.91, 0.84, 0.65), broad); material.roughness = 0.38 + (1.0 - field) * 0.25;
    } else if (type == 19) {
        material.albedo = mix(vec3(0.09, 0.022, 0.012), vec3(0.47, 0.20, 0.08), cells); material.roughness = 0.50 + cells * 0.14;
    } else if (type == 20) {
        material.albedo = mix(vec3(0.24, 0.025, 0.035), vec3(0.69, 0.24, 0.20), broad); material.roughness = 0.42 + (1.0 - broad) * 0.14;
    } else if (type == 21) {
        material.albedo = mix(vec3(0.025, 0.018, 0.035), vec3(0.08, 0.22, 0.21), field); material.metallic = 0.08; material.roughness = 0.16 + (1.0 - field) * 0.30;
    } else if (type == 22) {
        material.albedo = mix(vec3(0.35, 0.055, 0.045), vec3(0.92, 0.38, 0.24), broad); material.roughness = 0.65 + (1.0 - field) * 0.20;
    } else if (type == 23) {
        material.albedo = mix(vec3(0.035, 0.10, 0.24), spectralPalette(broad + @Uniform(GLOBAL_TIME) * 0.025), 1.0 - smoothstep(0.08, 0.36, cells));
        material.metallic = 0.16; material.roughness = 0.07 + cells * 0.18;
    } else if (type == 24) {
        float energy = smoothstep(-0.35, 0.05, -field);
        material.albedo = mix(vec3(0.018, 0.022, 0.030), vec3(0.025, 0.32, 0.72), energy); material.metallic = 0.04; material.roughness = mix(0.78, 0.18, energy);
    } else if (type == 25) {
        float pulse = sin(@Uniform(GLOBAL_TIME) * 2.2 + broad * 8.0) * 0.5 + 0.5;
        material.albedo = mix(vec3(0.055, 0.012, 0.075), vec3(0.18, 0.52, 0.16), cells); material.albedo = mix(material.albedo, vec3(0.62, 0.08, 0.31), pulse * 0.28); material.roughness = 0.28 + cells * 0.24;
    } else if (type == 26) {
        // rune_scale (MATERIAL_PARAMS[1]) - same slot materialField's own
        // type-26 branch reads; kept in sync rather than duplicated as a
        // second independent constant.
        vec2 grid = abs(fract(p * @Uniform(MATERIAL_PARAMS[1])) - 0.5); float rune = 1.0 - smoothstep(0.035, 0.10, min(grid.x, grid.y));
        material.albedo = mix(vec3(0.08, 0.045, 0.16), vec3(0.58, 0.44, 0.82), field); material.albedo = mix(material.albedo, vec3(0.04, 0.75, 0.92), rune); material.metallic = mix(0.96, 0.35, rune); material.roughness = mix(0.20, 0.11, rune);
    } else if (type == 27) {
        // density_threshold (MATERIAL_PARAMS[1]) - same 0.30 default as
        // cloudSolidTexture.
        float density = smoothstep(@Uniform(MATERIAL_PARAMS[1]), 0.78, broad); material.albedo = mix(vec3(0.18, 0.28, 0.46), vec3(0.92, 0.96, 1.0), density); material.roughness = 0.82 - density * 0.26;
    } else if (type == 28) {
        float fresnel = pow(1.0 - max(dot(normalize(normal), viewDir), 0.0), 2.2);
        material.albedo = mix(vec3(0.025, 0.12, 0.18), spectralPalette(fresnel * 0.72 + field * 0.18 + @Uniform(GLOBAL_TIME) * 0.035), 0.55 + fresnel * 0.4); material.metallic = 0.48; material.roughness = 0.10 + field * 0.12;
    } else if (type == 29) {
        // spread_threshold (MATERIAL_PARAMS[1]) - same 0.36 default as
        // corruptionTexture.
        float spread = smoothstep(@Uniform(MATERIAL_PARAMS[1]), 0.68, broad); material.albedo = mix(vec3(0.025, 0.022, 0.020), vec3(0.20, 0.008, 0.24), spread); material.metallic = spread * 0.12; material.roughness = mix(0.82, 0.30, spread);
    } else if (type == 30) {
        // frost_threshold (MATERIAL_PARAMS[1]) - same 0.22 default as
        // frostedGlassTexture.
        float frostDensity = smoothstep(@Uniform(MATERIAL_PARAMS[1]), 0.82, field);
        material.albedo = vec3(0.85, 0.91, 0.94) *
                          mix(0.78, 0.98, frostDensity);
        material.roughness = clamp(
            mix(0.34, 0.80, frostDensity), 0.28, 0.86);
    } else if (type == 31) {
        float brickWidth = 1.0;
        float brickHeight = @Uniform(MATERIAL_PARAMS[1]);
        float row = floor(p.y / brickHeight);
        float rowOffset = mod(row, 2.0) * brickWidth * 0.5;
        vec2 cell = vec2(floor((p.x - rowOffset) / brickWidth), row);
        float shade = hash(cell + vec2(4.0, 9.0));
        float weather = noise(p * 3.3 + vec2(shade * 11.0));
        vec3 fired = mix(
            vec3(0.36, 0.12, 0.075), vec3(0.66, 0.30, 0.16), shade);
        fired *= mix(0.80, 1.12, weather);
        vec3 mortar = mix(
            vec3(0.55, 0.53, 0.49), vec3(0.68, 0.66, 0.62), weather);
        material.albedo = mix(fired, mortar, field);
        material.roughness = clamp(
            mix(0.58, 0.90, field) + (weather - 0.5) * 0.08,
            0.55, 0.94);
    } else if (type == 32) {
        vec2 padGrid = p * 9.0;
        vec2 padCell = floor(padGrid);
        float padActive = step(0.82, hash(padCell + vec2(5.0, 2.0)));
        float padLocal = 1.0 - smoothstep(
            0.11, 0.17, length(fract(padGrid) - 0.5));
        float isPad = padActive * padLocal;
        float fleck = step(0.985, noise(p * 42.0));
        vec3 solderMask = vec3(0.035, 0.16, 0.075) +
            vec3(0.62, 0.62, 0.58) * fleck * 0.35;
        vec3 copper = mix(
            vec3(0.55, 0.32, 0.09), vec3(0.85, 0.72, 0.35), isPad);
        material.albedo = mix(solderMask, copper, field);
        material.metallic = field * 0.9;
        material.roughness = clamp(mix(0.55, 0.16, field), 0.14, 0.6);
    } else if (type == 33) {
        // garnet_scale (MATERIAL_PARAMS[1]) - same 13.0 default as
        // bandedGneissTexture.
        float garnetScale = @Uniform(MATERIAL_PARAMS[1]);
        float warp = fbm(p * 0.48) - 0.5;
        float band = sin(dot(p, normalize(vec2(0.82, -0.52))) * 8.0 +
                         warp * 7.0) * 0.5 + 0.5;
        float garnet = smoothstep(
            0.91, 0.975, noise(p * garnetScale + vec2(7.0)));
        material.albedo = mix(
            vec3(0.075, 0.080, 0.085), vec3(0.66, 0.61, 0.54),
            smoothstep(0.30, 0.70, band));
        material.albedo = mix(
            material.albedo, vec3(0.30, 0.045, 0.055), garnet);
        material.metallic = 0.02;
        material.roughness = clamp(
            0.48 + (noise(p * 8.0) - 0.5) * 0.16, 0.36, 0.62);
    } else if (type == 34) {
        // mineral_scale (MATERIAL_PARAMS[1]) - same 7.5 default as
        // rockTexture.
        float mineral = noise(p * @Uniform(MATERIAL_PARAMS[1]));
        material.albedo = mix(
            vec3(0.16, 0.15, 0.135), vec3(0.43, 0.41, 0.37),
            fbm(p * 0.55));
        material.albedo *= mix(0.82, 1.10, mineral);
        material.roughness = clamp(
            0.68 + (mineral - 0.5) * 0.16, 0.58, 0.82);
    } else if (type == 35) {
        // moss_scale (MATERIAL_PARAMS[1]) - same 16.0 default as
        // mossyRockTexture.
        float moisture = fbm(p * 0.82 + vec2(4.0, 9.0));
        float upward = max(normalize(normal).y, 0.0);
        float moss = smoothstep(0.43, 0.68, moisture) *
                     (0.42 + upward * 0.58);
        float fineMoss = noise(p * @Uniform(MATERIAL_PARAMS[1]));
        vec3 stone = mix(
            vec3(0.13, 0.13, 0.115), vec3(0.38, 0.37, 0.32),
            fbm(p * 0.55));
        vec3 mossColour = mix(
            vec3(0.055, 0.105, 0.025), vec3(0.25, 0.34, 0.07),
            fineMoss);
        material.albedo = mix(stone, mossColour, moss);
        material.roughness = mix(0.72, 0.92, moss);
    } else {
        // wetness_threshold (MATERIAL_PARAMS[1]) - same 0.24 default as
        // wetRockTexture.
        float wetness = smoothstep(
            @Uniform(MATERIAL_PARAMS[1]), 0.76, fbm(p * 0.46 + vec2(12.0, 3.0)));
        vec3 dryStone = mix(
            vec3(0.14, 0.14, 0.135), vec3(0.39, 0.38, 0.35),
            fbm(p * 0.62));
        material.albedo = dryStone * mix(0.72, 0.36, wetness);
        material.roughness = mix(0.52, 0.075, wetness);
    }

    material.roughness = clamp(material.roughness, 0.04, 1.0);
    material.normal = perturbHorizontalNormal(
        p, normal, type, field, strengths[type]);
    return material;
}

vec3 supernaturalEmission(vec2 worldPos, int materialIndex)
{
    vec2 p = worldPos * 0.68;
    float pulse = sin(@Uniform(GLOBAL_TIME) * 2.4) * 0.5 + 0.5;
    if (materialIndex == 23) {
        float core = 1.0 - smoothstep(0.07, 0.28, voronoi(p * 2.8));
        return spectralPalette(fbm(p * 0.4) + @Uniform(GLOBAL_TIME) * 0.03) * core * (0.45 + pulse * 0.35);
    }
    if (materialIndex == 24)
        return vec3(0.01, 0.42, 1.25) * smoothstep(-0.35, 0.05, -materialField(p, 24)) * (0.65 + pulse * 0.55);
    if (materialIndex == 25)
        return vec3(0.55, 0.03, 0.34) * (1.0 - smoothstep(0.08, 0.30, voronoi(p * 3.5))) * pulse * 0.75;
    if (materialIndex == 26) {
        vec2 grid = abs(fract(p * 1.7) - 0.5);
        float rune = 1.0 - smoothstep(0.035, 0.10, min(grid.x, grid.y));
        return vec3(0.02, 0.62, 1.0) * rune * (0.5 + pulse * 0.5);
    }
    if (materialIndex == 28)
        return spectralPalette(p.y * 0.12 + @Uniform(GLOBAL_TIME) * 0.04) * 0.12;
    if (materialIndex == 29)
        return vec3(0.72, 0.01, 0.46) * smoothstep(-0.16, 0.04, -materialField(p, 29)) * (0.35 + pulse * 0.65);
    return vec3(0.0);
}

float embossTileDepthOffset(float depth, vec2 tileId)
{
    float factor = clamp(
        @Uniform(EMBOSS_DEPTH_VARIATION), 0.0, 1.0);
    return -max(depth, 0.0) * factor * hash(tileId);
}

vec2 nearestHexagonCenter(vec2 position, float radius)
{
    float safeRadius = max(radius, 0.001);
    float q = (0.57735026919 * position.x - position.y / 3.0) / safeRadius;
    float r = (2.0 * position.y / 3.0) / safeRadius;
    vec3 cube = vec3(q, -q - r, r);
    vec3 roundedCube = round(cube);
    vec3 error = abs(roundedCube - cube);
    if (error.x > error.y && error.x > error.z) roundedCube.x = -roundedCube.y - roundedCube.z;
    else if (error.y > error.z) roundedCube.y = -roundedCube.x - roundedCube.z;
    else roundedCube.z = -roundedCube.x - roundedCube.y;
    return safeRadius * vec2(1.73205080757 * (roundedCube.x + roundedCube.z * 0.5), 1.5 * roundedCube.z);
}

// Groove width is fixed in world units, so changing tile size expands only
// each tile's flat area rather than also softening its edges.
float tileGrooveHeight(float distanceToEdge, float depth)
{
    const float grooveWidth = 1.0;
    float groove = 1.0 - smoothstep(0.0, grooveWidth, distanceToEdge);
    return -max(depth, 0.0) * groove;
}

vec2 modularOpusLatticeCoordinates(vec2 p, float largeTileSize)
{
    float smallTileSize = largeTileSize * 0.5;
    float determinant = largeTileSize * largeTileSize +
        smallTileSize * smallTileSize;
    return vec2(
        (largeTileSize * p.x + smallTileSize * p.y) / determinant,
        (-smallTileSize * p.x + largeTileSize * p.y) / determinant);
}

vec2 modularOpusLargeTileOrigin(vec2 lattice, float largeTileSize)
{
    float smallTileSize = largeTileSize * 0.5;
    return lattice.x * vec2(largeTileSize, smallTileSize) +
        lattice.y * vec2(-smallTileSize, largeTileSize);
}

float distanceToSquareBoundary(vec2 p, vec2 origin, float tileSize)
{
    vec2 fromBox = abs(p - (origin + vec2(tileSize * 0.5))) -
        vec2(tileSize * 0.5);
    float signedDistance = length(max(fromBox, vec2(0.0))) +
        min(max(fromBox.x, fromBox.y), 0.0);
    return abs(signedDistance);
}

bool modularOpusPositionIsInLargeTile(vec2 p, float largeTileSize)
{
    float size = max(largeTileSize, 0.001);
    vec2 base = floor(modularOpusLatticeCoordinates(p, size));
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 origin = modularOpusLargeTileOrigin(
                base + vec2(float(x), float(y)), size);
            vec2 local = p - origin;
            if (local.x >= 0.0 && local.y >= 0.0 &&
                local.x <= size && local.y <= size)
                return true;
        }
    }
    return false;
}

float modularOpusTileHeight(vec2 p, float largeTileSize, float depth)
{
    float size = max(largeTileSize, 0.001);
    vec2 base = floor(modularOpusLatticeCoordinates(p, size));
    float smallTileSize = size * 0.5;
    float distanceToEdge = size;
    vec2 tileId = base;
    bool foundTile = false;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 lattice = base + vec2(float(x), float(y));
            vec2 origin = modularOpusLargeTileOrigin(lattice, size);
            distanceToEdge = min(
                distanceToEdge,
                distanceToSquareBoundary(p, origin, size));
            vec2 largeLocal = p - origin;
            if (!foundTile && largeLocal.x >= 0.0 &&
                largeLocal.y >= 0.0 && largeLocal.x <= size &&
                largeLocal.y <= size) {
                tileId = lattice;
                foundTile = true;
            }
            vec2 smallLocal = p - (origin + vec2(size, 0.0));
            if (!foundTile && smallLocal.x >= 0.0 &&
                smallLocal.y >= 0.0 &&
                smallLocal.x <= smallTileSize &&
                smallLocal.y <= smallTileSize) {
                tileId = lattice + vec2(43.0, 17.0);
                foundTile = true;
            }
        }
    }
    return embossTileDepthOffset(depth, tileId) +
        tileGrooveHeight(distanceToEdge, depth);
}

vec2 voronoiFeaturePoint(vec2 cell)
{
    return cell + vec2(
        hash(cell + vec2(17.0, 3.0)),
        hash(cell + vec2(5.0, 19.0)));
}

float roundedEdgeMinimum(float a, float b, float rounding)
{
    if (rounding <= 0.000001)
        return min(a, b);
    float blend = max(rounding - abs(a - b), 0.0) / rounding;
    return min(a, b) - blend * blend * rounding * 0.25;
}

float voronoiTileHeight(vec2 p, float cellSize, float depth)
{
    float size = max(cellSize, 0.001);
    vec2 scaledPosition = p / size;
    vec2 base = floor(scaledPosition);
    vec2 nearestSite = vec2(0.0);
    vec2 nearestCell = base;
    float nearestDistanceSquared = 100.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 site = voronoiFeaturePoint(
                base + vec2(float(x), float(y)));
            float distanceSquared = dot(
                site - scaledPosition, site - scaledPosition);
            if (distanceSquared < nearestDistanceSquared) {
                nearestDistanceSquared = distanceSquared;
                nearestSite = site;
                nearestCell = base + vec2(float(x), float(y));
            }
        }
    }

    float distanceToEdge = 10.0;
    float rounding = clamp(
        @Uniform(EMBOSS_VORONOI_ROUNDING), 0.0, 1.0) * 0.25;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 site = voronoiFeaturePoint(
                base + vec2(float(x), float(y)));
            vec2 betweenSites = site - nearestSite;
            float siteSeparationSquared = dot(betweenSites, betweenSites);
            if (siteSeparationSquared > 0.000001) {
                vec2 midpoint = (nearestSite + site) * 0.5;
                float candidateDistance = dot(
                    midpoint - scaledPosition,
                    betweenSites / sqrt(siteSeparationSquared));
                distanceToEdge = roundedEdgeMinimum(
                    distanceToEdge, candidateDistance, rounding);
            }
        }
    }
    return embossTileDepthOffset(depth, nearestCell) +
        tileGrooveHeight(
            max(distanceToEdge, 0.0) * size, depth);
}

float embossPatternHeight(
    vec2 p, float radius, float depth, int pattern,
    float runningBondWidthPercent, float runningBondOffsetPercent)
{
    float safeRadius = max(radius, 0.001);
    if (pattern == 1) {
        vec2 local = abs(mod(p + safeRadius, safeRadius * 2.0) - safeRadius);
        vec2 tileId = floor(
            (p + vec2(safeRadius)) / (safeRadius * 2.0));
        return embossTileDepthOffset(depth, tileId) +
            tileGrooveHeight(
                safeRadius - max(local.x, local.y), depth);
    }
    if (pattern == 2) {
        vec2 local = p - nearestHexagonCenter(p, safeRadius);
        float fromCenter = max(abs(local.x), max(abs(0.5 * local.x + 0.8660254 * local.y), abs(-0.5 * local.x + 0.8660254 * local.y)));
        return embossTileDepthOffset(
                   depth, (p - local) / safeRadius) +
            tileGrooveHeight(
                0.8660254 * safeRadius - fromCenter, depth);
    }
    if (pattern == 3) {
        float widthFactor = clamp(
            runningBondWidthPercent * 0.01, 0.01, 1.0);
        float tileWidth = safeRadius * widthFactor;
        float row = floor(p.y / tileWidth);
        float rowOffset = mod(row, 2.0) * safeRadius * clamp(
            runningBondOffsetPercent * 0.01, 0.0, 1.0);
        float column = floor((p.x - rowOffset) / safeRadius);
        vec2 local = vec2(
            mod(p.x - rowOffset, safeRadius), mod(p.y, tileWidth));
        vec2 distanceToEdges = min(
            local, vec2(safeRadius, tileWidth) - local);
        return embossTileDepthOffset(depth, vec2(column, row)) +
            tileGrooveHeight(
                min(distanceToEdges.x, distanceToEdges.y), depth);
    }
    if (pattern == 4)
        return modularOpusTileHeight(p, radius, depth);
    if (pattern == 5)
        return voronoiTileHeight(p, radius, depth);
    return 0.0;
}

bool gridTileUsesSecondaryMaterial(vec2 p, float radius)
{
    float tileSize = max(radius, 0.001) * 2.0;
    vec2 cell = floor((p + vec2(radius)) / tileSize);
    return mod(cell.x + cell.y, 2.0) > 0.5;
}

bool hexagonTileUsesSecondaryMaterial(vec2 p, float radius)
{
    float safeRadius = max(radius, 0.001);
    float q = (0.57735026919 * p.x - p.y / 3.0) / safeRadius;
    float r = (2.0 * p.y / 3.0) / safeRadius;
    vec3 cube = vec3(q, -q - r, r);
    vec3 roundedCube = round(cube);
    vec3 error = abs(roundedCube - cube);
    if (error.x > error.y && error.x > error.z)
        roundedCube.x = -roundedCube.y - roundedCube.z;
    else if (error.y > error.z)
        roundedCube.y = -roundedCube.x - roundedCube.z;
    else
        roundedCube.z = -roundedCube.x - roundedCube.y;
    return mod(roundedCube.x - roundedCube.z, 3.0) > 0.5;
}

bool modularOpusTileUsesSecondaryMaterial(vec2 p, float largeTileSize)
{
    return !modularOpusPositionIsInLargeTile(p, largeTileSize);
}

int floorMaterialIndex(vec3 worldPos, int primaryMaterialIndex)
{
    int secondaryMaterialIndex = @Uniform(SECONDARY_MATERIAL_INDEX);
    if (@Uniform(USE_SECONDARY_MATERIAL) == 0 ||
        secondaryMaterialIndex < 0 ||
        secondaryMaterialIndex == primaryMaterialIndex)
        return primaryMaterialIndex;
    vec2 p = worldPos.xz;
    float radius = @Uniform(EMBOSS_RADIUS);
    int pattern = @Uniform(EMBOSS_PATTERN);
    if (pattern == 1 && gridTileUsesSecondaryMaterial(p, radius))
        return secondaryMaterialIndex;
    if (pattern == 2 && hexagonTileUsesSecondaryMaterial(p, radius))
        return secondaryMaterialIndex;
    if (pattern == 4 &&
        modularOpusTileUsesSecondaryMaterial(p, radius))
        return secondaryMaterialIndex;
    return primaryMaterialIndex;
}

// The plane a surface's relief is laid out in: the ground plane for anything
// roughly horizontal, and the wall's own across/up axes for anything else.
// Embossing used to be a floors-only effect and could assume world xz; now
// that it belongs to the material, it has to work on whatever the material is
// applied to, and a wall tiled through its ground-plane projection would read
// as vertical streaks rather than tiles.
void embossSurfaceAxes(vec3 normal, out vec3 axisU, out vec3 axisV)
{
    if (abs(normal.y) > 0.5)
    {
        axisU = vec3(1.0, 0.0, 0.0);
        axisV = vec3(0.0, 0.0, 1.0);
    }
    else
    {
        axisU = normalize(cross(vec3(0.0, 1.0, 0.0), normal));
        axisV = vec3(0.0, 1.0, 0.0);
    }
}

vec3 embossSurface(
    vec3 normal, vec3 worldPos, float radius, float depth, int pattern,
    float runningBondWidth, float runningBondOffset)
{
    vec3 surfaceNormal = normalize(normal);
    vec3 axisU;
    vec3 axisV;
    embossSurfaceAxes(surfaceNormal, axisU, axisV);

    // Keep the finite-difference distance in world units too: scaling it with
    // the tile size blurred the fixed-width grooves on large tiles.
    const float e = 0.02;
    vec2 p = vec2(dot(worldPos, axisU), dot(worldPos, axisV));
    vec2 gradient = vec2(
        embossPatternHeight(p + vec2(e, 0.0), radius, depth, pattern, runningBondWidth, runningBondOffset) - embossPatternHeight(p - vec2(e, 0.0), radius, depth, pattern, runningBondWidth, runningBondOffset),
        embossPatternHeight(p + vec2(0.0, e), radius, depth, pattern, runningBondWidth, runningBondOffset) - embossPatternHeight(p - vec2(0.0, e), radius, depth, pattern, runningBondWidth, runningBondOffset)) / (2.0 * e);
    return normalize(surfaceNormal - axisU * gradient.x - axisV * gradient.y);
}

float distributionGGX(vec3 normal, vec3 halfway, float roughness)
{
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;
    float nDotH = max(dot(normal, halfway), 0.0);
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.000001);
}

float geometrySchlickGGX(float n, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return n / max(n * (1.0 - k) + k, 0.000001);
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 evaluatePbrLight(Material m, vec3 viewDir, vec3 lightDir, vec3 radiance)
{
    vec3 halfway = normalize(viewDir + lightDir);
    float nDotV = max(dot(m.normal, viewDir), 0.0);
    float nDotL = max(dot(m.normal, lightDir), 0.0);
    float distribution = distributionGGX(m.normal, halfway, m.roughness);
    float geometry = geometrySchlickGGX(nDotV, m.roughness) * geometrySchlickGGX(nDotL, m.roughness);
    vec3 f0 = mix(vec3(0.04), m.albedo, m.metallic);
    vec3 fresnel = fresnelSchlick(max(dot(halfway, viewDir), 0.0), f0);
    vec3 specular = distribution * geometry * fresnel / max(4.0 * nDotV * nDotL, 0.0001);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - m.metallic);
    return (diffuseWeight * m.albedo / PI + specular) * radiance * nDotL;
}

float playerTorchVisibility(vec3 worldPosition, vec3 normal, vec3 lightDirection)
{
    if (BIAS_AND_ENABLED.z < 0.5 || SHADOW_TYPE_AND_LIGHT_INDEX.x < 0.5)
        return 1.0;
    vec3 lightToFragment = worldPosition - POINT_POSITION_AND_RANGE.xyz;
    float range = POINT_POSITION_AND_RANGE.w;
    float distanceToLight = length(lightToFragment);
    if (distanceToLight >= range) return 1.0;
    float bias = BIAS_AND_ENABLED.x + BIAS_AND_ENABLED.y *
        (1.0 - max(dot(normal, lightDirection), 0.0));
    float compareDepth = distanceToLight / range - bias;
    float visibility;
    if (MAP_TEXEL_SIZE_AND_RADIUS.w < 0.5)
    {
        visibility = texture(@Texture(POINT_SHADOW_MAP),
            vec4(lightToFragment, compareDepth));
    }
    else
    {
        vec3 direction = lightToFragment / max(distanceToLight, 0.00001);
        vec3 reference = abs(direction.z) < 0.999
            ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
        vec3 tangent = normalize(cross(reference, direction));
        vec3 bitangent = cross(direction, tangent);
        float radius = 2.0 * MAP_TEXEL_SIZE_AND_RADIUS.x *
            MAP_TEXEL_SIZE_AND_RADIUS.z;
        visibility = 0.0;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
            {
                vec3 tapDirection = normalize(direction +
                    tangent * (float(x) * radius) +
                    bitangent * (float(y) * radius));
                visibility += texture(@Texture(POINT_SHADOW_MAP),
                    vec4(tapDirection, compareDepth));
            }
        visibility /= 9.0;
    }
    float fade = clamp((distanceToLight / range - BIAS_AND_ENABLED.w) /
        max(1.0 - BIAS_AND_ENABLED.w, 0.00001), 0.0, 1.0);
    fade = fade * fade * (3.0 - 2.0 * fade);
    return mix(visibility, 1.0, fade);
}

float playerTorchAttenuation(float lightDistance)
{
    float radius = max(@Uniform(LIGHT_ATTENUATION_RADIUS), 0.0);
    if (radius <= 0.0 || lightDistance >= radius)
        return 0.0;

    float falloff = clamp(
        @Uniform(LIGHT_ATTENUATION_FALLOFF), 0.0, radius);
    float edgeAttenuation = falloff > 0.0
        ? 1.0 - smoothstep(radius - falloff, radius, lightDistance)
        : 1.0;
    float physicalAttenuation =
        1.0 / (1.0 + lightDistance * 0.04 +
               lightDistance * lightDistance * 0.0015);
    return physicalAttenuation * edgeAttenuation;
}

struct PbrLighting
{
    vec3 direct;
    vec3 ambient;
};

PbrLighting shadePbr(Material m, vec3 viewDir, vec3 worldPos, vec3 lightPos)
{
    vec3 toLight = lightPos - worldPos;
    float distance = max(length(toLight), 0.0001);
    vec3 lightDirection = toLight / distance;
    float attenuation = playerTorchAttenuation(distance);
    vec3 direct = evaluatePbrLight(m, viewDir, lightDirection, vec3(14.0) * attenuation);
    direct *= playerTorchVisibility(worldPos, m.normal, lightDirection);
    vec3 ambient = vec3(0.12);
    vec3 f0 = mix(vec3(0.04), m.albedo, m.metallic);
    vec3 fresnel = fresnelSchlick(max(dot(m.normal, viewDir), 0.0), f0);
    PbrLighting lighting;
    lighting.direct = direct;
    lighting.ambient =
        (vec3(1.0) - fresnel) * (1.0 - m.metallic) * m.albedo * ambient +
        fresnel * mix(vec3(0.10), ambient, 1.0 - m.roughness);
    return lighting;
}

// Shared with world_pbr.frag. Wall normal maps currently have no horizontal
// authoring owner, so every 2D horizontal batch binds the enable flag to zero;
// retaining the full implementation keeps both PBR programs contract-compatible.
vec3 applyWallNormalMap(vec3 geometricNormal)
{
    vec3 surfaceNormal = normalize(geometricNormal);
    if (@Uniform(WALL_NORMAL_MAP_ENABLED) == 0)
        return surfaceNormal;

    vec2 normalMapUv = @In(TEXCOORDS);
    normalMapUv.y *= @Uniform(WALL_NORMAL_MAP_ASPECT_RATIO);
    vec3 sampled = texture(
        @Texture(TEX1), normalMapUv).rgb * 2.0 - 1.0;
    float strength = max(@Uniform(WALL_NORMAL_MAP_STRENGTH), 0.0);
    if (strength == 0.0)
        sampled = vec3(0.0, 0.0, 1.0);
    else
        sampled = normalize(vec3(sampled.xy * strength, sampled.z));
    vec3 tangent = normalize(cross(surfaceNormal, vec3(0.0, 1.0, 0.0)));
    return normalize(
        tangent * sampled.x + vec3(0.0, 1.0, 0.0) * sampled.y +
        surfaceNormal * sampled.z);
}

// This is a GLSL transliteration of core::CalculateLiquidPathLength, which is
// normative and covered by liquid_path_length_tests.cpp. Change both
// expressions in the same commit. Keep this helper identical in
// world_pbr.frag and world_pbr_2d.frag so walls and horizontals cannot drift.
vec3 applyLiquidAbsorption(
    vec3 direct, vec3 ambient, vec3 eyePosition, vec3 pointPosition,
    float eyeSurfaceHeight, float pointSurfaceHeight, out vec3 outTransmittance)
{
    // A Planar reflected-scene pass uses a mirrored virtual eye. Applying the
    // ordinary camera-to-surface path to that eye would invent absorption through
    // Liquid that the reflected radiance never traversed.
    if (@Uniform(MPP_VIRTUAL_CAMERA) != 0)
    {
        outTransmittance = vec3(1.0);
        return direct + ambient;
    }

    bool eyeIsWet = eyeSurfaceHeight > eyePosition.y;
    bool pointIsWet = pointSurfaceHeight > pointPosition.y;
    float surfaceHeight = eyeIsWet ? eyeSurfaceHeight : pointSurfaceHeight;

    float low = min(eyePosition.y, pointPosition.y);
    float high = max(eyePosition.y, pointPosition.y);
    float verticalSpan = high - low;
    float submergedFraction = verticalSpan > 0.001
        ? clamp((min(high, surfaceHeight) - low) / verticalSpan, 0.0, 1.0)
        : (0.5 * (low + high) <= surfaceHeight ? 1.0 : 0.0);
    vec3 delta = pointPosition - eyePosition;
    float chordLength = sqrt(
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);

    float pathLength = pointIsWet
        ? chordLength * submergedFraction
        : max(surfaceHeight - eyePosition.y, 0.0);
    vec3 transmittance = exp(-@Uniform(LIQUID_EXTINCTION) * pathLength);
    outTransmittance = transmittance;
    // The head-mounted Player Torch's outbound path nearly coincides with the
    // point-to-eye path. Pre-attenuating its direct contribution once, then
    // attenuating all radiance over the view path, gives direct the round trip
    // while ambient (and emission grouped with it) travels that path once.
    vec3 colour = ambient + direct * transmittance;
    return colour * transmittance +
        (vec3(1.0) - transmittance) * @Uniform(LIQUID_TINT);
}

void main()
{
    float liquidSurfaceHeight = @In(LIQUID_SURFACE_HEIGHT);

    // Fade every contribution to black at the world-view boundary. This is
    // intentionally separate from the Torch's configurable direct-light
    // attenuation: ambient and emissive terms must disappear there too.
    float fragmentDistance = length(
        @Uniform(LIGHT_POSITION) - @In(FRAGPOSITION));
    float fadeToBlack = pow(clamp(
        1.0 - fragmentDistance / @Uniform(VIEW_DISTANCE), 0.0, 1.0), 1.7);

    vec3 worldPos = @In(FRAGPOSITION);
    vec3 viewDir = normalize(@ViewPos - worldPos);
    vec3 shadingNormal = normalize(@In(FRAGNORMAL));
    // Every horizontal surface here is single-sided geometry rendered without
    // backface culling (liquid surfaces are seen from below when a player is
    // submerged). Without this, dot(normal, viewDir) goes negative and clamps
    // to 0 in shadePbr's Fresnel term, which fresnelSchlick reads as grazing
    // incidence (cosTheta = 0) and returns ~1 - drowning the albedo term in
    // flat grey ambient regardless of the material's actual colour.
    if (dot(shadingNormal, viewDir) < 0.0) {
        shadingNormal = -shadingNormal;
    }
    vec3 normal = applyWallNormalMap(shadingNormal);
    float playerDistance = length(
        @Uniform(PLAYER_POSITION) - worldPos);
    vec2 texturePosition = quantizeByPlayerDistance(
        worldPos.xz / @Uniform(MATERIAL_SCALE), playerDistance);
    int materialIndex = floorMaterialIndex(
        worldPos, clamp(@Uniform(MATERIAL_INDEX), 0, 40));
    materialIndex = clamp(materialIndex, 0, 40);
    Material material = material2d(
        texturePosition, normal, viewDir, materialIndex);
    // Whatever this material embosses, on whatever surface it was
    // applied to - floor, ceiling or wall.
    if (@Uniform(EMBOSS_PATTERN) != 0)
        material.normal = embossSurface(
            material.normal, worldPos, @Uniform(EMBOSS_RADIUS),
            @Uniform(EMBOSS_DEPTH), @Uniform(EMBOSS_PATTERN),
            @Uniform(EMBOSS_RUNNING_BOND_WIDTH),
            @Uniform(EMBOSS_RUNNING_BOND_OFFSET));
    shadingNormal = material.normal;
    PbrLighting lighting = shadePbr(
        material, viewDir, worldPos, @Uniform(LIGHT_POSITION));
    vec3 ambientAndEmission = lighting.ambient +
        supernaturalEmission(texturePosition, materialIndex);
    // Absorption attenuates all radiance in linear space. It therefore follows
    // emission but precedes tone mapping, gamma, and the stylistic distance
    // fade applied at output. The Player Torch's direct term takes its nearly
    // coincident outbound and view paths; ambient and emission take only the
    // view path.
    vec3 outTransmittance;
    vec3 value = applyLiquidAbsorption(
        lighting.direct, ambientAndEmission, @ViewPos, @In(FRAGPOSITION),
        @Uniform(LIQUID_EYE_SURFACE_Z), liquidSurfaceHeight, outTransmittance);
    value = value / (value + vec3(1.0));
    value = pow(value, vec3(1.0 / 2.2));

    // Preserve vertex alpha for a blended receiver. Visibility was applied to
    // the direct term above, before this final opacity is composited.
    @Out(vec4 COLOUR) = vec4(value * fadeToBlack, @In(COLOUR).a);
    @Out(vec4 BLOOM_MASK) = vec4(0.0);
    @Out(vec2 SHADING_NORMAL) = encodeOctahedralNormal(
        normalize(mat3(VIEW_MATRIX) * shadingNormal));
    @Out(float LIQUID_RETENTION) = dot(outTransmittance, vec3(1.0 / 3.0));
##
}
