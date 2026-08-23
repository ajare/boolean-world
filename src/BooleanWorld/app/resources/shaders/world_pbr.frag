@@Version

// Global
@@Uniform(float VIEW_DISTANCE);
@@Uniform(float GLOBAL_TIME);
@@Uniform(float PIXEL_SIZE);
@@Uniform(vec3 PLAYER_POSITION);
@@Uniform(float MATERIAL_SCALE);

// Per batch
@@Uniform(int MATERIAL_INDEX);
@@Uniform(float MATERIAL_PARAMS[8]);
## Texture
@@Texture(sampler2D TEX1);
##

const float PI = 3.14159265359;

vec3 snapToGrid(vec3 p, float gridSize)
{
    return round(p / gridSize) * gridSize;
}

struct Material
{
    vec3  albedo;
    float metallic;
    float roughness;
    vec3  normal;
};

float hash(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 p)
{
    vec3 cell = floor(p);
    vec3 local = fract(p);
    local = local * local * (3.0 - 2.0 * local);

    float x00 = mix(hash(cell), hash(cell + vec3(1.0, 0.0, 0.0)), local.x);
    float x10 = mix(hash(cell + vec3(0.0, 1.0, 0.0)),
                    hash(cell + vec3(1.0, 1.0, 0.0)), local.x);
    float x01 = mix(hash(cell + vec3(0.0, 0.0, 1.0)),
                    hash(cell + vec3(1.0, 0.0, 1.0)), local.x);
    float x11 = mix(hash(cell + vec3(0.0, 1.0, 1.0)),
                    hash(cell + vec3(1.0, 1.0, 1.0)), local.x);

    return mix(mix(x00, x10, local.y), mix(x01, x11, local.y), local.z);
}

float fbm(vec3 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    mat3 rotation = mat3(
         0.00,  0.80,  0.60,
        -0.80,  0.36, -0.48,
        -0.60, -0.48,  0.64);

    for (int octave = 0; octave < 5; ++octave)
    {
        value += amplitude * noise(p);
        p = rotation * p * 2.03 + vec3(13.1, 7.7, 3.4);
        amplitude *= 0.5;
    }
    return value;
}

// A continuous three-dimensional marble field. Because it is evaluated from
// world position rather than UVs, veins continue naturally across floors,
// walls and ceilings without seams or planar stretching.
float marbleField(vec3 p)
{
    vec3 warp = vec3(
        noise(p * 0.65 + vec3(7.1, 1.7, 4.3)),
        noise(p * 0.65 + vec3(2.8, 9.2, 5.6)),
        noise(p * 0.65 + vec3(5.4, 3.1, 8.7))) - 0.5;
    vec3 q = p + warp * 1.35;

    float turbulence = fbm(q * 1.15) - 0.5;
    float broadVein = abs(sin(q.x * 5.0 + q.y * 1.15 + q.z * 0.8 +
                              turbulence * 7.0));
    float fineVein = abs(sin(q.x * 12.0 - q.y * 1.7 + q.z * 2.1 +
                             fbm(q * 2.4) * 5.0));

    float broadMask = 1.0 - smoothstep(0.06, 0.30, broadVein);
    float fineMask = (1.0 - smoothstep(0.025, 0.13, fineVein)) * 0.45;
    return clamp(max(broadMask, fineMask), 0.0, 1.0);
}

Material marbleTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.72;
    float veins = marbleField(p);
    float cloud = fbm(p * 0.42);
    float grain = noise(p * 7.0);

    vec3 coolStone = vec3(0.72, 0.76, 0.81);
    vec3 warmStone = vec3(0.93, 0.89, 0.82);
    vec3 stone = mix(coolStone, warmStone, smoothstep(0.2, 0.85, cloud));
    stone *= mix(0.91, 1.06, grain);

    vec3 veinColour = mix(vec3(0.025, 0.030, 0.040),
                          vec3(0.20, 0.13, 0.10), cloud * 0.35);
    material.albedo = mix(stone, veinColour, veins);
    material.metallic = 0.0;
    material.roughness = clamp(mix(0.32, 0.18, veins) +
                               (grain - 0.5) * 0.10, 0.12, 0.48);

    // Derive a small-scale normal from the same scalar field. Projecting its
    // gradient onto the geometric tangent plane keeps the perturbation valid
    // for every surface orientation without requiring tangents or UVs.
    const float epsilon = 0.025;
    vec3 gradient = vec3(
        marbleField(p + vec3(epsilon, 0.0, 0.0)) - veins,
        marbleField(p + vec3(0.0, epsilon, 0.0)) - veins,
        marbleField(p + vec3(0.0, 0.0, epsilon)) - veins) / epsilon;
    vec3 geometricNormal = normalize(normal);
    vec3 surfaceGradient = gradient - geometricNormal * dot(gradient, geometricNormal);
    material.normal = normalize(geometricNormal - surfaceGradient * 0.11);

    return material;
}

// Approximate distance to the nearest cell feature. This gives crystalline
// inclusions, pores and fracture cells without any UV-dependent projection.
float geologyVoronoi(vec3 p)
{
    vec3 cell = floor(p);
    vec3 local = fract(p);
    float nearest = 10.0;

    for (int z = -1; z <= 1; ++z)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                vec3 offset = vec3(float(x), float(y), float(z));
                vec3 feature = offset + vec3(
                    hash(cell + offset + vec3(17.0, 3.0, 11.0)),
                    hash(cell + offset + vec3(5.0, 19.0, 7.0)),
                    hash(cell + offset + vec3(13.0, 23.0, 2.0)));
                nearest = min(nearest, length(feature - local));
            }
        }
    }
    return nearest;
}

// Scalar surface fields shared by the geology materials below. The type is a
// compile-time constant at each call site in practice, allowing drivers to
// discard all unrelated branches after specializing the material switch.
float geologyField(vec3 p, int type)
{
    if (type == 0) // Granite: interlocked mineral grains.
    {
        float coarse = geologyVoronoi(p * 2.2);
        return fbm(p * 0.65) * 0.45 + noise(p * 8.0) * 0.20 +
               (1.0 - smoothstep(0.12, 0.48, coarse)) * 0.35;
    }
    if (type == 1) // Slate: compressed layers and narrow fractures.
    {
        float warpedLayer = p.y * 8.0 + p.x * 0.7 + fbm(p * 0.8) * 3.2;
        float layers = sin(warpedLayer) * 0.5 + 0.5;
        float fracture = 1.0 - smoothstep(
            0.015, 0.09, abs(noise(p * 3.0) - 0.5));
        return layers * 0.32 + fracture * 0.68;
    }
    if (type == 2) // Sandstone: sediment bands over granular erosion.
    {
        float strata = sin(p.y * 5.5 + fbm(p * 0.45) * 4.0) * 0.5 + 0.5;
        float grains = noise(p * 18.0);
        return strata * 0.62 + grains * 0.38;
    }
    if (type == 3) // Limestone: soft deposits broken by dissolved pits.
    {
        float deposits = fbm(p * 0.75);
        float pores = 1.0 - smoothstep(0.08, 0.28, geologyVoronoi(p * 5.0));
        return deposits * 0.58 - pores * 0.42;
    }
    if (type == 4) // Basalt: fine volcanic grain and vesicles.
    {
        float grain = noise(p * 12.0);
        float vesicles = 1.0 - smoothstep(
            0.10, 0.32, geologyVoronoi(p * 3.8));
        return grain * 0.25 - vesicles * 0.75;
    }
    if (type == 5) // Obsidian: glassy, warped cooling-flow bands.
    {
        vec3 warp = vec3(
            noise(p * 0.55 + vec3(4.0, 1.0, 7.0)),
            noise(p * 0.55 + vec3(8.0, 5.0, 2.0)),
            noise(p * 0.55 + vec3(3.0, 9.0, 6.0))) - 0.5;
        vec3 q = p + warp * 1.8;
        return sin(q.x * 3.7 + q.y * 1.2 + q.z * 2.4) * 0.5 + 0.5;
    }
    if (type == 6) // Quartz: faceted crystal cells and growth bands.
    {
        float cells = geologyVoronoi(p * 2.4);
        float growth = sin(dot(p, normalize(vec3(1.0, 0.7, -0.35))) * 13.0);
        return cells * 0.78 + (growth * 0.5 + 0.5) * 0.22;
    }

    // Ore: host rock crossed by folded metallic deposits.
    float warp = fbm(p * 0.62) - 0.5;
    float vein = abs(sin(p.x * 4.5 + p.y * 1.1 - p.z * 0.8 + warp * 8.0));
    float veinMask = 1.0 - smoothstep(0.06, 0.24, vein);
    return fbm(p * 1.6) * 0.42 + veinMask * 0.58;
}

vec3 geologyNormal(vec3 p, vec3 normal, int type, float field,
                   float strength)
{
    const float epsilon = 0.025;
    vec3 gradient = vec3(
        geologyField(p + vec3(epsilon, 0.0, 0.0), type) - field,
        geologyField(p + vec3(0.0, epsilon, 0.0), type) - field,
        geologyField(p + vec3(0.0, 0.0, epsilon), type) - field) / epsilon;
    vec3 geometricNormal = normalize(normal);
    gradient -= geometricNormal * dot(gradient, geometricNormal);
    return normalize(geometricNormal - gradient * strength);
}

Material graniteTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.82;
    float surface = geologyField(p, 0);
    float quartz = smoothstep(0.68, 0.88, noise(p * 7.3 + vec3(2.0)));
    float feldspar = smoothstep(0.52, 0.78, noise(p * 4.7 + vec3(11.0)));
    float mica = smoothstep(0.88, 0.97, noise(p * 13.0 + vec3(23.0)));
    vec3 colour = mix(vec3(0.16, 0.15, 0.15), vec3(0.48, 0.42, 0.38), feldspar);
    colour = mix(colour, vec3(0.72, 0.70, 0.66), quartz);
    material.albedo = mix(colour, vec3(0.035), mica);
    material.metallic = mica * 0.28;
    material.roughness = clamp(0.58 - quartz * 0.16 - mica * 0.20, 0.24, 0.68);
    material.normal = geologyNormal(p, normal, 0, surface, 0.055);
    return material;
}

Material slateTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.72;
    float surface = geologyField(p, 1);
    float layer = sin(p.y * 8.0 + p.x * 0.7 + fbm(p * 0.8) * 3.2) * 0.5 + 0.5;
    float rust = smoothstep(0.73, 0.93, fbm(p * 1.9 + vec3(8.0)));
    material.albedo = mix(vec3(0.075, 0.095, 0.115),
                          vec3(0.18, 0.21, 0.22), layer);
    material.albedo = mix(material.albedo, vec3(0.30, 0.13, 0.055), rust * 0.35);
    material.metallic = 0.0;
    material.roughness = clamp(0.42 + layer * 0.18, 0.34, 0.68);
    material.normal = geologyNormal(p, normal, 1, surface, 0.045);
    return material;
}

Material sandstoneTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.58;
    float surface = geologyField(p, 2);
    float band = sin(p.y * 5.5 + fbm(p * 0.45) * 4.0) * 0.5 + 0.5;
    float grain = noise(p * 18.0);
    vec3 pale = vec3(0.70, 0.43, 0.22);
    vec3 red = vec3(0.43, 0.16, 0.075);
    material.albedo = mix(pale, red, smoothstep(0.25, 0.8, band));
    material.albedo *= mix(0.86, 1.08, grain);
    material.metallic = 0.0;
    material.roughness = clamp(0.72 + (grain - 0.5) * 0.18, 0.58, 0.9);
    material.normal = geologyNormal(p, normal, 2, surface, 0.07);
    return material;
}

Material limestoneTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.66;
    float surface = geologyField(p, 3);
    float deposits = fbm(p * 0.75);
    float pores = 1.0 - smoothstep(0.08, 0.28, geologyVoronoi(p * 5.0));
    material.albedo = mix(vec3(0.48, 0.45, 0.35),
                          vec3(0.82, 0.79, 0.66), deposits);
    material.albedo *= 1.0 - pores * 0.38;
    material.metallic = 0.0;
    material.roughness = clamp(0.62 + pores * 0.25, 0.52, 0.9);
    material.normal = geologyNormal(p, normal, 3, surface, 0.065);
    return material;
}

Material basaltTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.85;
    float surface = geologyField(p, 4);
    float grain = noise(p * 12.0);
    float vesicles = 1.0 - smoothstep(0.10, 0.32, geologyVoronoi(p * 3.8));
    material.albedo = mix(vec3(0.025, 0.027, 0.030),
                          vec3(0.12, 0.13, 0.14), grain);
    material.albedo *= 1.0 - vesicles * 0.72;
    material.metallic = 0.03;
    material.roughness = clamp(0.58 + vesicles * 0.30, 0.48, 0.92);
    material.normal = geologyNormal(p, normal, 4, surface, 0.075);
    return material;
}

Material obsidianTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.70;
    float surface = geologyField(p, 5);
    float sheen = pow(surface, 4.0);
    float inclusions = smoothstep(0.84, 0.96, noise(p * 9.0));
    material.albedo = mix(vec3(0.006, 0.008, 0.012),
                          vec3(0.055, 0.025, 0.075), sheen);
    material.albedo = mix(material.albedo, vec3(0.17, 0.09, 0.05), inclusions * 0.3);
    material.metallic = 0.0;
    material.roughness = clamp(0.075 + inclusions * 0.24 + sheen * 0.035, 0.055, 0.34);
    material.normal = geologyNormal(p, normal, 5, surface, 0.018);
    return material;
}

Material quartzTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.62;
    float surface = geologyField(p, 6);
    float cells = geologyVoronoi(p * 2.4);
    float amethyst = smoothstep(
        0.48, 0.88, fbm(p * 0.48 + vec3(17.0)));
    float edge = 1.0 - smoothstep(0.10, 0.30, cells);
    material.albedo = mix(vec3(0.72, 0.82, 0.88),
                          vec3(0.34, 0.14, 0.52), amethyst * 0.72);
    material.albedo = mix(material.albedo, vec3(0.92, 0.98, 1.0), edge * 0.55);
    material.metallic = 0.0;
    material.roughness = clamp(0.11 + cells * 0.20, 0.08, 0.34);
    material.normal = geologyNormal(p, normal, 6, surface, 0.075);
    return material;
}

Material oreTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.72;
    float surface = geologyField(p, 7);
    float warp = fbm(p * 0.62) - 0.5;
    float vein = abs(sin(p.x * 4.5 + p.y * 1.1 - p.z * 0.8 + warp * 8.0));
    float metal = 1.0 - smoothstep(0.06, 0.24, vein);
    float oxidation = smoothstep(
        0.62, 0.88, noise(p * 3.7 + vec3(31.0))) * metal;
    vec3 host = mix(vec3(0.055, 0.06, 0.065), vec3(0.20, 0.18, 0.15), fbm(p * 1.6));
    vec3 ore = mix(vec3(0.48, 0.45, 0.38), vec3(0.73, 0.43, 0.12), oxidation);
    material.albedo = mix(host, ore, metal);
    material.metallic = metal * (1.0 - oxidation * 0.65);
    material.roughness = clamp(mix(0.68, 0.20, metal) + oxidation * 0.25, 0.16, 0.78);
    material.normal = geologyNormal(p, normal, 7, surface, 0.06);
    return material;
}

float metalField(vec3 p, int type)
{
    if (type == 0) // Rusted iron: corrosion scale and pitting.
    {
        float corrosion = fbm(p * 1.15);
        float pits = 1.0 - smoothstep(0.08, 0.25, geologyVoronoi(p * 6.0));
        return corrosion * 0.58 - pits * 0.42;
    }
    if (type == 1) // Galvanized steel: zinc spangle crystals.
    {
        float crystals = geologyVoronoi(p * 3.6);
        return crystals + noise(p * 9.0) * 0.12;
    }
    if (type == 2) // Brushed metal: long directional micro-scratches.
    {
        float longScratch = noise(vec3(p.x * 1.2, p.y * 35.0, p.z * 35.0));
        float fineScratch = noise(vec3(p.x * 2.0, p.y * 90.0, p.z * 90.0));
        return longScratch * 0.68 + fineScratch * 0.32;
    }
    if (type == 3) // Hammered metal: overlapping shallow impact bowls.
    {
        float dents = geologyVoronoi(p * 4.2);
        return smoothstep(0.08, 0.72, dents);
    }
    if (type == 4) // Copper patina: broad oxidation blooms and drips.
    {
        float bloom = fbm(p * 0.82);
        float drips = noise(vec3(p.x * 2.0, p.y * 0.35, p.z * 2.0));
        return bloom * 0.72 + drips * 0.28;
    }
    if (type == 5) // Damascene steel: folded, domain-warped layers.
    {
        float warp = fbm(p * 0.65) - 0.5;
        return sin(p.x * 10.0 + p.y * 1.3 + p.z * 2.1 + warp * 9.0) *
                   0.5 +
               0.5;
    }

    // Heat-treated steel: broad thermal bands over polished metal.
    float warp = fbm(p * 0.45) * 3.0;
    return sin(dot(p, normalize(vec3(0.8, 0.25, -0.55))) * 4.0 + warp) *
               0.5 +
           0.5;
}

vec3 metalNormal(vec3 p, vec3 normal, int type, float field, float strength)
{
    const float epsilon = 0.02;
    vec3 gradient = vec3(
        metalField(p + vec3(epsilon, 0.0, 0.0), type) - field,
        metalField(p + vec3(0.0, epsilon, 0.0), type) - field,
        metalField(p + vec3(0.0, 0.0, epsilon), type) - field) / epsilon;
    vec3 geometricNormal = normalize(normal);
    gradient -= geometricNormal * dot(gradient, geometricNormal);
    return normalize(geometricNormal - gradient * strength);
}

Material rustedIronTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.72;
    float surface = metalField(p, 0);
    float corrosion = smoothstep(0.42, 0.72, fbm(p * 1.15));
    float pits = 1.0 - smoothstep(0.08, 0.25, geologyVoronoi(p * 6.0));
    vec3 iron = vec3(0.22, 0.23, 0.24);
    vec3 rust = mix(vec3(0.18, 0.045, 0.012),
                    vec3(0.58, 0.19, 0.035), noise(p * 4.5));
    float rustMask = clamp(corrosion + pits * 0.45, 0.0, 1.0);
    material.albedo = mix(iron, rust, rustMask);
    material.metallic = mix(0.92, 0.0, rustMask);
    material.roughness = mix(0.28, 0.88, rustMask);
    material.normal = metalNormal(p, normal, 0, surface, 0.065);
    return material;
}

Material galvanizedSteelTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.66;
    float surface = metalField(p, 1);
    float crystals = geologyVoronoi(p * 3.6);
    float facet = clamp(crystals * 1.45, 0.0, 1.0);
    material.albedo = mix(vec3(0.42, 0.45, 0.47),
                          vec3(0.74, 0.77, 0.78), facet);
    material.metallic = 0.94;
    material.roughness = clamp(0.25 + facet * 0.20, 0.22, 0.48);
    material.normal = metalNormal(p, normal, 1, surface, 0.035);
    return material;
}

Material brushedMetalTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.82;
    float surface = metalField(p, 2);
    float scratch = smoothstep(0.58, 0.86, surface);
    material.albedo = mix(vec3(0.42, 0.44, 0.46),
                          vec3(0.68, 0.70, 0.72), surface);
    material.albedo *= 1.0 - scratch * 0.18;
    material.metallic = 0.96;
    material.roughness = clamp(0.20 + scratch * 0.30, 0.18, 0.52);
    material.normal = metalNormal(p, normal, 2, surface, 0.012);
    return material;
}

Material hammeredMetalTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.72;
    float surface = metalField(p, 3);
    float dents = geologyVoronoi(p * 4.2);
    material.albedo = mix(vec3(0.24, 0.25, 0.27),
                          vec3(0.52, 0.55, 0.58), smoothstep(0.1, 0.7, dents));
    material.metallic = 0.92;
    material.roughness = clamp(0.30 + (1.0 - surface) * 0.22, 0.28, 0.58);
    material.normal = metalNormal(p, normal, 3, surface, 0.085);
    return material;
}

Material patinatedCopperTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.66;
    float surface = metalField(p, 4);
    float patina = smoothstep(0.43, 0.67, surface);
    float exposed = smoothstep(0.66, 0.82, noise(p * 2.8 + vec3(13.0)));
    patina *= 1.0 - exposed;
    vec3 copper = vec3(0.72, 0.27, 0.09);
    vec3 verdigris = mix(vec3(0.025, 0.20, 0.15),
                         vec3(0.12, 0.48, 0.39), fbm(p * 1.6));
    material.albedo = mix(copper, verdigris, patina);
    material.metallic = mix(0.98, 0.03, patina);
    material.roughness = mix(0.20, 0.72, patina);
    material.normal = metalNormal(p, normal, 4, surface, 0.04);
    return material;
}

Material damasceneSteelTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.72;
    float surface = metalField(p, 5);
    float layers = smoothstep(0.32, 0.68, surface);
    material.albedo = mix(vec3(0.12, 0.13, 0.15),
                          vec3(0.62, 0.65, 0.68), layers);
    material.metallic = 0.95;
    material.roughness = mix(0.34, 0.17, layers);
    material.normal = metalNormal(p, normal, 5, surface, 0.018);
    return material;
}

Material heatTreatedMetalTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.62;
    float surface = metalField(p, 6);
    float band = clamp(surface, 0.0, 1.0);
    vec3 straw = vec3(0.78, 0.38, 0.08);
    vec3 violet = vec3(0.28, 0.06, 0.42);
    vec3 blue = vec3(0.035, 0.16, 0.48);
    vec3 oxideColour = mix(straw, violet, smoothstep(0.18, 0.58, band));
    oxideColour = mix(oxideColour, blue, smoothstep(0.55, 0.88, band));
    material.albedo = mix(vec3(0.38, 0.40, 0.42), oxideColour, 0.72);
    material.metallic = 0.90;
    material.roughness = clamp(0.19 + noise(p * 6.0) * 0.13, 0.18, 0.34);
    material.normal = metalNormal(p, normal, 6, surface, 0.014);
    return material;
}

float organicField(vec3 p, int type)
{
    if (type == 0) // Wood: cylindrical growth rings and longitudinal grain.
    {
        float radius = length(p.xz);
        float rings = sin(radius * 18.0 + fbm(p * 0.55) * 4.5) * 0.5 + 0.5;
        float grain = noise(vec3(p.x * 4.0, p.y * 0.32, p.z * 4.0));
        return rings * 0.68 + grain * 0.32;
    }
    if (type == 1) // Bark: vertical ridges split by deep cracks.
    {
        float warp = fbm(p * 0.48) * 3.0;
        float ridges = abs(sin(p.x * 7.0 + p.z * 5.0 + warp));
        float cracks = 1.0 - smoothstep(
            0.025, 0.14, abs(noise(vec3(p.x * 2.2, p.y * 0.35, p.z * 2.2)) - 0.5));
        return ridges * 0.58 - cracks * 0.72;
    }
    if (type == 2) // Bone: laminated mineral structure and fine pores.
    {
        float layers = fbm(p * 0.82);
        float pores = 1.0 - smoothstep(0.07, 0.23, geologyVoronoi(p * 7.0));
        return layers * 0.70 - pores * 0.30;
    }
    if (type == 3) // Leather: compressed cells, wrinkles and pores.
    {
        float cells = geologyVoronoi(p * 5.5);
        float wrinkles = sin(p.x * 3.0 + p.z * 2.0 + fbm(p * 0.8) * 6.0) * 0.5 + 0.5;
        float pores = 1.0 - smoothstep(0.035, 0.13, geologyVoronoi(p * 15.0));
        return cells * 0.36 + wrinkles * 0.38 - pores * 0.26;
    }
    if (type == 4) // Flesh: soft tissue variation and recessed veins.
    {
        float tissue = fbm(p * 0.72);
        float vein = abs(sin(p.x * 3.6 - p.y * 1.1 + p.z * 2.3 +
                             fbm(p * 0.5) * 7.0));
        float veins = 1.0 - smoothstep(0.035, 0.16, vein);
        return tissue * 0.82 - veins * 0.18;
    }
    if (type == 5) // Chitin: overlapping shell plates and growth bands.
    {
        float plates = geologyVoronoi(p * 3.2);
        float bands = sin(dot(p, normalize(vec3(0.7, 0.2, 0.65))) * 9.0 +
                          fbm(p * 0.65) * 3.0) *
                           0.5 +
                       0.5;
        return plates * 0.55 + bands * 0.45;
    }

    // Coral: calcareous lobes perforated by irregular pores.
    float lobes = fbm(p * 1.1);
    float pores = 1.0 - smoothstep(0.10, 0.31, geologyVoronoi(p * 5.2));
    return lobes * 0.48 - pores * 0.72;
}

vec3 organicNormal(vec3 p, vec3 normal, int type, float field,
                   float strength)
{
    const float epsilon = 0.022;
    vec3 gradient = vec3(
        organicField(p + vec3(epsilon, 0.0, 0.0), type) - field,
        organicField(p + vec3(0.0, epsilon, 0.0), type) - field,
        organicField(p + vec3(0.0, 0.0, epsilon), type) - field) / epsilon;
    vec3 geometricNormal = normalize(normal);
    gradient -= geometricNormal * dot(gradient, geometricNormal);
    return normalize(geometricNormal - gradient * strength);
}

Material woodTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.54;
    float surface = organicField(p, 0);
    float radius = length(p.xz);
    float ring = sin(radius * 18.0 + fbm(p * 0.55) * 4.5) * 0.5 + 0.5;
    float grain = noise(vec3(p.x * 4.0, p.y * 0.32, p.z * 4.0));
    float knot = 1.0 - smoothstep(0.08, 0.34, geologyVoronoi(p * 1.4));
    material.albedo = mix(vec3(0.16, 0.055, 0.018),
                          vec3(0.58, 0.29, 0.095), ring);
    material.albedo *= mix(0.76, 1.12, grain) * (1.0 - knot * 0.38);
    material.metallic = 0.0;
    material.roughness = clamp(0.48 + grain * 0.18, 0.42, 0.7);
    material.normal = organicNormal(p, normal, 0, surface, 0.038);
    return material;
}

Material barkTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.62;
    float surface = organicField(p, 1);
    float high = smoothstep(0.20, 0.78, surface);
    float lichen = smoothstep(0.67, 0.88, fbm(p * 1.5 + vec3(9.0)));
    material.albedo = mix(vec3(0.055, 0.020, 0.008),
                          vec3(0.30, 0.12, 0.035), high);
    material.albedo = mix(material.albedo, vec3(0.20, 0.27, 0.07), lichen * 0.35);
    material.metallic = 0.0;
    material.roughness = clamp(0.76 + (1.0 - high) * 0.16, 0.7, 0.95);
    material.normal = organicNormal(p, normal, 1, surface, 0.095);
    return material;
}

Material boneTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.68;
    float surface = organicField(p, 2);
    float age = fbm(p * 0.48);
    float pores = 1.0 - smoothstep(0.07, 0.23, geologyVoronoi(p * 7.0));
    material.albedo = mix(vec3(0.52, 0.43, 0.27),
                          vec3(0.91, 0.84, 0.65), age);
    material.albedo *= 1.0 - pores * 0.30;
    material.metallic = 0.0;
    material.roughness = clamp(0.38 + pores * 0.36, 0.34, 0.78);
    material.normal = organicNormal(p, normal, 2, surface, 0.045);
    return material;
}

Material leatherTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.76;
    float surface = organicField(p, 3);
    float cells = geologyVoronoi(p * 5.5);
    float wear = smoothstep(0.62, 0.86, fbm(p * 0.9 + vec3(5.0)));
    material.albedo = mix(vec3(0.09, 0.022, 0.012),
                          vec3(0.34, 0.095, 0.035), cells);
    material.albedo = mix(material.albedo, vec3(0.47, 0.20, 0.08), wear * 0.42);
    material.metallic = 0.0;
    material.roughness = clamp(0.50 - wear * 0.16 + cells * 0.14, 0.32, 0.68);
    material.normal = organicNormal(p, normal, 3, surface, 0.035);
    return material;
}

Material fleshTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.60;
    float surface = organicField(p, 4);
    float mottling = fbm(p * 0.72);
    float vein = abs(sin(p.x * 3.6 - p.y * 1.1 + p.z * 2.3 +
                         fbm(p * 0.5) * 7.0));
    float veins = 1.0 - smoothstep(0.035, 0.16, vein);
    material.albedo = mix(vec3(0.24, 0.025, 0.035),
                          vec3(0.69, 0.24, 0.20), mottling);
    material.albedo = mix(material.albedo, vec3(0.07, 0.025, 0.12), veins * 0.72);
    material.metallic = 0.0;
    material.roughness = clamp(0.42 + (1.0 - mottling) * 0.14, 0.38, 0.6);
    material.normal = organicNormal(p, normal, 4, surface, 0.022);
    return material;
}

Material chitinTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.67;
    float surface = organicField(p, 5);
    float plate = smoothstep(0.12, 0.72, geologyVoronoi(p * 3.2));
    float band = sin(dot(p, normalize(vec3(0.7, 0.2, 0.65))) * 9.0 +
                     fbm(p * 0.65) * 3.0) *
                         0.5 +
                     0.5;
    vec3 darkShell = vec3(0.025, 0.018, 0.035);
    vec3 brightShell = mix(vec3(0.14, 0.055, 0.20),
                           vec3(0.035, 0.24, 0.22), band);
    material.albedo = mix(darkShell, brightShell, plate);
    material.metallic = 0.08;
    material.roughness = clamp(0.16 + (1.0 - plate) * 0.30, 0.14, 0.48);
    material.normal = organicNormal(p, normal, 5, surface, 0.048);
    return material;
}

Material coralTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.66;
    float surface = organicField(p, 6);
    float pores = 1.0 - smoothstep(0.10, 0.31, geologyVoronoi(p * 5.2));
    float colonies = fbm(p * 1.1);
    material.albedo = mix(vec3(0.35, 0.055, 0.045),
                          vec3(0.92, 0.38, 0.24), colonies);
    material.albedo = mix(material.albedo, vec3(0.055, 0.018, 0.012), pores * 0.80);
    material.metallic = 0.0;
    material.roughness = clamp(0.65 + pores * 0.27, 0.58, 0.94);
    material.normal = organicNormal(p, normal, 6, surface, 0.085);
    return material;
}

float supernaturalField(vec3 p, int type)
{
    if (type == 0) // Arcane crystal: hard facets and internal growth planes.
    {
        float cells = geologyVoronoi(p * 2.8);
        float planes = sin(dot(p, normalize(vec3(0.6, 0.75, -0.28))) * 15.0) *
                           0.5 +
                       0.5;
        return cells * 0.72 + planes * 0.28;
    }
    if (type == 1) // Energy stone: fractured rock around luminous seams.
    {
        float warp = fbm(p * 0.55) - 0.5;
        float seam = abs(sin(p.x * 4.2 - p.y * 1.4 + p.z * 2.0 + warp * 8.0));
        return fbm(p * 1.45) * 0.62 -
               (1.0 - smoothstep(0.035, 0.18, seam)) * 0.72;
    }
    if (type == 2) // Alien tissue: swollen cells connected by sinew.
    {
        float cells = geologyVoronoi(p * 3.5);
        float sinew = abs(sin(p.x * 3.0 + p.y * 2.3 - p.z * 2.7 +
                              fbm(p * 0.7) * 5.0));
        return cells * 0.52 + sinew * 0.48;
    }
    if (type == 3) // Magical metal: forged flow crossed by engraved runes.
    {
        float flow = metalField(p, 5);
        vec3 grid = abs(fract(p * 1.7) - 0.5);
        float rune = 1.0 - smoothstep(0.035, 0.10, min(grid.x, min(grid.y, grid.z)));
        return flow * 0.72 - rune * 0.28;
    }
    if (type == 4) // Solid cloud: soft billows with fine turbulent edges.
    {
        return fbm(p * 0.72) * 0.72 + fbm(p * 2.8) * 0.28;
    }
    if (type == 5) // Hologram: almost-flat scan interference.
    {
        float scan = sin(p.y * 35.0 + @Uniform(GLOBAL_TIME) * 3.0) * 0.5 + 0.5;
        return scan * 0.28 + noise(p * 9.0) * 0.12;
    }

    // Corruption: invasive cells and branching channels.
    float spread = fbm(p * 0.58 + vec3(@Uniform(GLOBAL_TIME) * 0.025));
    float cells = geologyVoronoi(p * 3.8);
    float tendril = abs(sin(p.x * 3.4 + p.y * 1.8 + p.z * 2.6 + spread * 9.0));
    return spread * 0.46 + cells * 0.22 -
           (1.0 - smoothstep(0.03, 0.17, tendril)) * 0.48;
}

vec3 supernaturalNormal(vec3 p, vec3 normal, int type, float field,
                        float strength)
{
    const float epsilon = 0.022;
    vec3 gradient = vec3(
        supernaturalField(p + vec3(epsilon, 0.0, 0.0), type) - field,
        supernaturalField(p + vec3(0.0, epsilon, 0.0), type) - field,
        supernaturalField(p + vec3(0.0, 0.0, epsilon), type) - field) / epsilon;
    vec3 geometricNormal = normalize(normal);
    gradient -= geometricNormal * dot(gradient, geometricNormal);
    return normalize(geometricNormal - gradient * strength);
}

vec3 spectralPalette(float phase)
{
    return 0.52 + 0.48 * cos(6.2831853 *
        (phase + vec3(0.0, 0.33, 0.67)));
}

Material arcaneCrystalTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.64;
    float surface = supernaturalField(p, 0);
    float cells = geologyVoronoi(p * 2.8);
    float core = 1.0 - smoothstep(0.08, 0.36, cells);
    float colourShift = fbm(p * 0.45) + @Uniform(GLOBAL_TIME) * 0.025;
    material.albedo = mix(vec3(0.035, 0.10, 0.24),
                          spectralPalette(colourShift), core * 0.82);
    material.metallic = 0.16;
    material.roughness = clamp(0.07 + cells * 0.18, 0.055, 0.28);
    material.normal = supernaturalNormal(p, normal, 0, surface, 0.095);
    return material;
}

Material energyStoneTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.68;
    float surface = supernaturalField(p, 1);
    float warp = fbm(p * 0.55) - 0.5;
    float seam = abs(sin(p.x * 4.2 - p.y * 1.4 + p.z * 2.0 + warp * 8.0));
    float energy = 1.0 - smoothstep(0.035, 0.18, seam);
    material.albedo = mix(vec3(0.018, 0.022, 0.030),
                          vec3(0.025, 0.32, 0.72), energy);
    material.metallic = 0.04;
    material.roughness = mix(0.78, 0.18, energy);
    material.normal = supernaturalNormal(p, normal, 1, surface, 0.065);
    return material;
}

Material alienTissueTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.67;
    float surface = supernaturalField(p, 2);
    float cells = geologyVoronoi(p * 3.5);
    float pulse = sin(@Uniform(GLOBAL_TIME) * 2.2 + fbm(p * 0.55) * 8.0) *
                      0.5 +
                  0.5;
    material.albedo = mix(vec3(0.055, 0.012, 0.075),
                          vec3(0.18, 0.52, 0.16), smoothstep(0.18, 0.72, cells));
    material.albedo = mix(material.albedo, vec3(0.62, 0.08, 0.31), pulse * 0.28);
    material.metallic = 0.0;
    material.roughness = clamp(0.28 + cells * 0.24, 0.24, 0.56);
    material.normal = supernaturalNormal(p, normal, 2, surface, 0.055);
    return material;
}

Material magicalMetalTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.72;
    float surface = supernaturalField(p, 3);
    vec3 grid = abs(fract(p * 1.7) - 0.5);
    float rune = 1.0 - smoothstep(0.035, 0.10, min(grid.x, min(grid.y, grid.z)));
    float flow = metalField(p, 5);
    material.albedo = mix(vec3(0.08, 0.045, 0.16),
                          vec3(0.58, 0.44, 0.82), flow);
    material.albedo = mix(material.albedo, vec3(0.04, 0.75, 0.92), rune);
    material.metallic = mix(0.96, 0.35, rune);
    material.roughness = mix(0.20, 0.11, rune);
    material.normal = supernaturalNormal(p, normal, 3, surface, 0.026);
    return material;
}

Material cloudSolidTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.53 +
             vec3(@Uniform(GLOBAL_TIME) * 0.018, 0.0,
                  @Uniform(GLOBAL_TIME) * -0.012);
    float surface = supernaturalField(p, 4);
    float density = smoothstep(0.30, 0.78, fbm(p * 0.72));
    material.albedo = mix(vec3(0.18, 0.28, 0.46),
                          vec3(0.92, 0.96, 1.0), density);
    material.metallic = 0.0;
    material.roughness = clamp(0.82 - density * 0.26, 0.5, 0.88);
    material.normal = supernaturalNormal(p, normal, 4, surface, 0.032);
    return material;
}

Material holographicTexture(vec3 worldPos, vec3 normal, vec3 viewDir)
{
    Material material;
    vec3 p = worldPos * 0.75;
    float surface = supernaturalField(p, 5);
    float fresnel = pow(1.0 - max(dot(normalize(normal), viewDir), 0.0), 2.2);
    float scan = sin(p.y * 35.0 + @Uniform(GLOBAL_TIME) * 3.0) * 0.5 + 0.5;
    vec3 spectrum = spectralPalette(fresnel * 0.72 + scan * 0.18 +
                                    @Uniform(GLOBAL_TIME) * 0.035);
    material.albedo = mix(vec3(0.025, 0.12, 0.18), spectrum, 0.55 + fresnel * 0.4);
    material.metallic = 0.48;
    material.roughness = clamp(0.10 + scan * 0.12, 0.08, 0.24);
    material.normal = supernaturalNormal(p, normal, 5, surface, 0.008);
    return material;
}

Material corruptionTexture(vec3 worldPos, vec3 normal)
{
    Material material;
    vec3 p = worldPos * 0.65;
    float surface = supernaturalField(p, 6);
    float spread = smoothstep(0.36, 0.68,
        fbm(p * 0.58 + vec3(@Uniform(GLOBAL_TIME) * 0.025)));
    float tendril = smoothstep(-0.18, 0.06, -surface);
    material.albedo = mix(vec3(0.025, 0.022, 0.020),
                          vec3(0.20, 0.008, 0.24), spread);
    material.albedo = mix(material.albedo, vec3(0.62, 0.015, 0.42), tendril * 0.72);
    material.metallic = spread * 0.12;
    material.roughness = mix(0.82, 0.30, spread);
    material.normal = supernaturalNormal(p, normal, 6, surface, 0.072);
    return material;
}

vec3 supernaturalEmission(vec3 worldPos, int materialIndex)
{
    vec3 p = worldPos * 0.68;
    float pulse = sin(@Uniform(GLOBAL_TIME) * 2.4) * 0.5 + 0.5;

    if (materialIndex == 23)
    {
        float core = 1.0 - smoothstep(0.07, 0.28, geologyVoronoi(p * 2.8));
        return spectralPalette(fbm(p * 0.4) + @Uniform(GLOBAL_TIME) * 0.03) *
               core * (0.45 + pulse * 0.35);
    }
    if (materialIndex == 24)
    {
        float warp = fbm(p * 0.55) - 0.5;
        float seam = abs(sin(p.x * 4.2 - p.y * 1.4 + p.z * 2.0 + warp * 8.0));
        float energy = 1.0 - smoothstep(0.035, 0.18, seam);
        return vec3(0.01, 0.42, 1.25) * energy * (0.65 + pulse * 0.55);
    }
    if (materialIndex == 25)
    {
        float cells = 1.0 - smoothstep(0.08, 0.30, geologyVoronoi(p * 3.5));
        return vec3(0.55, 0.03, 0.34) * cells * pulse * 0.75;
    }
    if (materialIndex == 26)
    {
        vec3 grid = abs(fract(p * 1.7) - 0.5);
        float rune = 1.0 - smoothstep(0.035, 0.10, min(grid.x, min(grid.y, grid.z)));
        return vec3(0.02, 0.62, 1.0) * rune * (0.5 + pulse * 0.5);
    }
    if (materialIndex == 28)
    {
        return spectralPalette(p.y * 0.12 + @Uniform(GLOBAL_TIME) * 0.04) * 0.12;
    }
    if (materialIndex == 29)
    {
        float channel = smoothstep(-0.16, 0.04, -supernaturalField(p, 6));
        return vec3(0.72, 0.01, 0.46) * channel * (0.35 + pulse * 0.65);
    }
    return vec3(0.0);
}

float distributionGGX(vec3 normal, vec3 halfway, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float nDotH = max(dot(normal, halfway), 0.0);
    float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(PI * denominator * denominator, 0.000001);
}

float geometrySchlickGGX(float nDotDirection, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotDirection /
        max(nDotDirection * (1.0 - k) + k, 0.000001);
}

float geometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness)
{
    return geometrySchlickGGX(max(dot(normal, viewDir), 0.0), roughness) *
           geometrySchlickGGX(max(dot(normal, lightDir), 0.0), roughness);
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

vec3 evaluatePbrLight(Material material, vec3 viewDir, vec3 lightDir,
                      vec3 radiance)
{
    vec3 halfway = normalize(viewDir + lightDir);
    float nDotV = max(dot(material.normal, viewDir), 0.0);
    float nDotL = max(dot(material.normal, lightDir), 0.0);

    float distribution = distributionGGX(
        material.normal, halfway, material.roughness);
    float geometry = geometrySmith(
        material.normal, viewDir, lightDir, material.roughness);
    vec3 f0 = mix(vec3(0.04), material.albedo, material.metallic);
    vec3 fresnel = fresnelSchlick(max(dot(halfway, viewDir), 0.0), f0);

    vec3 specular = distribution * geometry * fresnel /
        max(4.0 * nDotV * nDotL, 0.0001);
    vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - material.metallic);
    return (diffuseWeight * material.albedo / PI + specular) *
        radiance * nDotL;
}

vec3 shadePbr(Material material, vec3 viewDir, vec3 worldPosition,
              vec3 playerPosition)
{
    // PLAYER_POSITION is the player's eye in the same X/elevation/Z
    // coordinate system as the interpolated world position. A point light
    // emits equally in every direction; only distance and the receiving
    // surface's angle affect its contribution.
    vec3 toPlayer = playerPosition - worldPosition;
    float playerDistance = max(length(toPlayer), 0.0001);
    vec3 playerDirection = toPlayer / playerDistance;
    float playerAttenuation =
        1.0 / (1.0 + playerDistance * 0.04 +
               playerDistance * playerDistance * 0.0015);
    vec3 direct = evaluatePbrLight(
        material, viewDir, playerDirection,
        vec3(14.0) * playerAttenuation);

    // Keep ambient illumination orientation-independent so opposite floor and
    // ceiling normals do not introduce a different colour cast.
    vec3 ambientIrradiance = vec3(0.12);
    vec3 f0 = mix(vec3(0.04), material.albedo, material.metallic);
    float nDotV = max(dot(material.normal, viewDir), 0.0);
    vec3 ambientFresnel = fresnelSchlick(nDotV, f0);
    vec3 ambientDiffuse = (vec3(1.0) - ambientFresnel) *
        (1.0 - material.metallic) * material.albedo * ambientIrradiance;
    vec3 ambientSpecular = ambientFresnel *
        mix(vec3(0.10), ambientIrradiance, 1.0 - material.roughness);

    return ambientDiffuse + ambientSpecular + direct;
}

void main()
{
    // Depth scaling factor
    float depth = gl_FragCoord.z / gl_FragCoord.w;

    depth /= @Uniform(VIEW_DISTANCE);
    depth = pow(clamp(1.0 - depth, 0.0, 1.0), 1.7);

    vec4 shadedColour = vec4(depth, depth, depth, 1.0);
    vec3 value = vec3(0.0);

    if (depth > 0.05)
    {
        vec3 viewDir = normalize(@ViewPos - @In(FRAGPOSITION));
        vec3 normalDir = normalize(@In(FRAGNORMAL));
        vec3 texturePosition =
            @In(FRAGPOSITION) / @Uniform(MATERIAL_SCALE);
        Material material;

        int materialIndex = @Uniform(MATERIAL_INDEX);
        switch (materialIndex)
        {
            case 0:
                material = marbleTexture(texturePosition, normalDir);
                break;
            case 1:
                material = graniteTexture(texturePosition, normalDir);
                break;
            case 2:
                material = slateTexture(texturePosition, normalDir);
                break;
            case 3:
                material = sandstoneTexture(texturePosition, normalDir);
                break;
            case 4:
                material = limestoneTexture(texturePosition, normalDir);
                break;
            case 5:
                material = basaltTexture(texturePosition, normalDir);
                break;
            case 6:
                material = obsidianTexture(texturePosition, normalDir);
                break;
            case 7:
                material = quartzTexture(texturePosition, normalDir);
                break;
            case 8:
                material = oreTexture(texturePosition, normalDir);
                break;
            case 9:
                material = rustedIronTexture(texturePosition, normalDir);
                break;
            case 10:
                material = galvanizedSteelTexture(texturePosition, normalDir);
                break;
            case 11:
                material = brushedMetalTexture(texturePosition, normalDir);
                break;
            case 12:
                material = hammeredMetalTexture(texturePosition, normalDir);
                break;
            case 13:
                material = patinatedCopperTexture(texturePosition, normalDir);
                break;
            case 14:
                material = damasceneSteelTexture(texturePosition, normalDir);
                break;
            case 15:
                material = heatTreatedMetalTexture(texturePosition, normalDir);
                break;
            case 16:
                material = woodTexture(texturePosition, normalDir);
                break;
            case 17:
                material = barkTexture(texturePosition, normalDir);
                break;
            case 18:
                material = boneTexture(texturePosition, normalDir);
                break;
            case 19:
                material = leatherTexture(texturePosition, normalDir);
                break;
            case 20:
                material = fleshTexture(texturePosition, normalDir);
                break;
            case 21:
                material = chitinTexture(texturePosition, normalDir);
                break;
            case 22:
                material = coralTexture(texturePosition, normalDir);
                break;
            case 23:
                material = arcaneCrystalTexture(texturePosition, normalDir);
                break;
            case 24:
                material = energyStoneTexture(texturePosition, normalDir);
                break;
            case 25:
                material = alienTissueTexture(texturePosition, normalDir);
                break;
            case 26:
                material = magicalMetalTexture(texturePosition, normalDir);
                break;
            case 27:
                material = cloudSolidTexture(texturePosition, normalDir);
                break;
            case 28:
                material = holographicTexture(texturePosition, normalDir, viewDir);
                break;
            case 29:
                material = corruptionTexture(texturePosition, normalDir);
                break;

            default:
                material.albedo = vec3(1.0, 0.0, 1.0);
                material.metallic = 0.0;
                material.roughness = 0.7;
                material.normal = normalDir;
                break;
        }

        // Cook-Torrance PBR lighting with GGX distribution, Smith geometry
        // masking and Schlick Fresnel.
        value = shadePbr(
            material, viewDir, @In(FRAGPOSITION),
            @Uniform(PLAYER_POSITION));
        value += supernaturalEmission(texturePosition, materialIndex);
        value = value / (value + vec3(1.0));
        value = pow(value, vec3(1.0 / 2.2));
    }

    @Out(vec4 COLOUR) = vec4(value, 1.0) * shadedColour;
##
}
