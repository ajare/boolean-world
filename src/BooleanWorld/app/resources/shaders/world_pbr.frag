@@Version

// Native varying paired with world.vert; Pool elevation is constant per triangle.
layout(location = 5) flat in float liquidSurfaceHeight;

// Global
@@Uniform(float VIEW_DISTANCE);
@@Uniform(float GLOBAL_TIME);
@@Uniform(float PIXEL_SIZE);
@@Uniform(vec3 PLAYER_POSITION);
@@Uniform(vec3 LIGHT_POSITION);
@@Uniform(float LIQUID_EYE_SURFACE_Z);
@@Uniform(vec3 LIQUID_EXTINCTION);
@@Uniform(vec3 LIQUID_TINT);
@@Uniform(float LIQUID_REFLECTANCE);
@@Uniform(float LIQUID_F0);
@@Uniform(float LIQUID_REFLECTION_MIP_LEVEL);
@@Uniform(vec3 LIQUID_AMBIENT_TINT);
@@Uniform(int LIQUID_WATER_PASS_ENABLED);
@@Uniform(int LIQUID_REFLECTION_ENABLED);
@@Uniform(int MPP_VIRTUAL_CAMERA);
@@Uniform(int MPP_WATER_REFLECTION_TECHNIQUE);
@@Uniform(int MPP_PLANAR_REFLECTION_COUNT);
@@Uniform(mat4 MPP_PLANAR_REFLECTION_VIEW_PROJECTION_0);
@@Uniform(float MPP_PLANAR_REFLECTION_ELEVATION_0);
@@Uniform(float MPP_PLANAR_REFLECTION_MINIMUM_ELEVATION_0);
@@Uniform(float MPP_PLANAR_REFLECTION_MAXIMUM_ELEVATION_0);
@@Uniform(mat4 MPP_PLANAR_REFLECTION_VIEW_PROJECTION_1);
@@Uniform(float MPP_PLANAR_REFLECTION_ELEVATION_1);
@@Uniform(float MPP_PLANAR_REFLECTION_MINIMUM_ELEVATION_1);
@@Uniform(float MPP_PLANAR_REFLECTION_MAXIMUM_ELEVATION_1);
@@Uniform(mat4 MPP_PLANAR_REFLECTION_VIEW_PROJECTION_2);
@@Uniform(float MPP_PLANAR_REFLECTION_ELEVATION_2);
@@Uniform(float MPP_PLANAR_REFLECTION_MINIMUM_ELEVATION_2);
@@Uniform(float MPP_PLANAR_REFLECTION_MAXIMUM_ELEVATION_2);
@@Uniform(mat4 MPP_PLANAR_REFLECTION_VIEW_PROJECTION_3);
@@Uniform(float MPP_PLANAR_REFLECTION_ELEVATION_3);
@@Uniform(float MPP_PLANAR_REFLECTION_MINIMUM_ELEVATION_3);
@@Uniform(float MPP_PLANAR_REFLECTION_MAXIMUM_ELEVATION_3);
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
@@Uniform(vec3 MATERIAL_COLOUR);
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
@@Uniform(int WALL_MASK_ENABLED);
@@Uniform(int WALL_MASK_CHANNEL);
@@Uniform(float WALL_MASK_BLEND_PARAMS[8]);
@@Uniform(vec3 WALL_MASK_BLEND_COLOUR);

// The wall mask interpolates the primary parameters and base colour toward
// their blend sets before the single material evaluation in main(). Every
// technique function below reads these instead of the raw uniforms, so the
// blend changes the primary's authored knobs without a second evaluation.
float blendedMaterialParams[8];
vec3 blendedMaterialColour;
## Texture
@@Texture(sampler2D TEX1);
@@Texture(sampler2D TEX2);
##
@@Texture(sampler2D PBR_SCENE_COLOUR_RESOLVED);
@@Texture(sampler2D PBR_PLANAR_REFLECTION_0);
@@Texture(sampler2D PBR_PLANAR_REFLECTION_1);
@@Texture(sampler2D PBR_PLANAR_REFLECTION_2);
@@Texture(sampler2D PBR_PLANAR_REFLECTION_3);
@@Texture(sampler2D PBR_SCENE_DEPTH);
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

// Liquid uses MPP's proven fixed-step SSR contract, with renderer-owned fixed
// tuning rather than per-Liquid-type quality controls.
float liquidViewDepth(vec2 uv, float depth)
{
    vec4 view = INVERSE_PROJECTION_MATRIX *
        vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return view.z / view.w;
}

float liquidSceneDepth(vec2 uv)
{
    ivec2 size = textureSize(@Texture(PBR_SCENE_DEPTH), 0);
    ivec2 texel = clamp(
        ivec2(uv * vec2(size)), ivec2(0), size - ivec2(1));
    return texelFetch(@Texture(PBR_SCENE_DEPTH), texel, 0).r;
}

vec2 liquidProject(vec3 viewPosition, out float valid)
{
    vec4 clip = PROJECTION_MATRIX * vec4(viewPosition, 1.0);
    valid = clip.w > 0.0001 ? 1.0 : 0.0;
    return (clip.xy / max(clip.w, 0.0001)) * 0.5 + 0.5;
}

float liquidHitStability(vec2 uv, float hitZ, float thickness)
{
    ivec2 size = textureSize(@Texture(PBR_SCENE_DEPTH), 0);
    ivec2 texel = clamp(
        ivec2(uv * vec2(size)), ivec2(1),
        max(size - ivec2(2), ivec2(1)));
    float left = liquidViewDepth(
        uv, texelFetch(@Texture(PBR_SCENE_DEPTH),
                       texel + ivec2(-1, 0), 0).r);
    float right = liquidViewDepth(
        uv, texelFetch(@Texture(PBR_SCENE_DEPTH),
                       texel + ivec2(1, 0), 0).r);
    float down = liquidViewDepth(
        uv, texelFetch(@Texture(PBR_SCENE_DEPTH),
                       texel + ivec2(0, -1), 0).r);
    float up = liquidViewDepth(
        uv, texelFetch(@Texture(PBR_SCENE_DEPTH),
                       texel + ivec2(0, 1), 0).r);
    float spread = max(
        max(abs(left - hitZ), abs(right - hitZ)),
        max(abs(down - hitZ), abs(up - hitZ)));
    return 1.0 - smoothstep(thickness, thickness * 3.0, spread);
}

float liquidDither(vec2 pixel)
{
    return fract(52.9829189 *
                 fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

float liquidMarch(vec3 origin, vec3 direction, out vec2 hitUv)
{
    const int steps = 32;
    const float maxDistance = 40.0;
    const float thickness = 0.5;
    const float stepSize = maxDistance / float(steps);
    hitUv = vec2(0.0);

    float originValid;
    vec2 originUv = liquidProject(origin, originValid);
    bool armed = originValid > 0.5 &&
        origin.z >= liquidViewDepth(originUv, liquidSceneDepth(originUv));
    float jitter = liquidDither(gl_FragCoord.xy);
    vec3 previous = origin;
    for (int i = 0; i < steps; ++i)
    {
        vec3 marchPoint = origin + direction *
            (stepSize * (float(i) + jitter));
        float valid;
        vec2 uv = liquidProject(marchPoint, valid);
        if (valid < 0.5 || uv.x < 0.0 || uv.x > 1.0 ||
            uv.y < 0.0 || uv.y > 1.0)
            break;

        float sceneZ = liquidViewDepth(uv, liquidSceneDepth(uv));
        bool behind = marchPoint.z < sceneZ;
        if (!behind)
        {
            armed = true;
        }
        else if (armed)
        {
            vec3 closer = previous;
            vec3 further = marchPoint;
            for (int refine = 0; refine < 5; ++refine)
            {
                vec3 middle = (closer + further) * 0.5;
                float middleValid;
                vec2 middleUv = liquidProject(middle, middleValid);
                float middleSceneZ = liquidViewDepth(
                    middleUv, liquidSceneDepth(middleUv));
                if (middle.z < middleSceneZ)
                {
                    further = middle;
                    uv = middleUv;
                }
                else
                {
                    closer = middle;
                }
            }

            // Thickness is deliberately tested only after sign-change
            // refinement; doing it in the coarse loop extrudes silhouettes.
            float hitSceneZ = liquidViewDepth(uv, liquidSceneDepth(uv));
            float confidence =
                (1.0 - smoothstep(
                    thickness * 0.5, thickness, hitSceneZ - further.z)) *
                liquidHitStability(uv, hitSceneZ, thickness);
            if (confidence > 0.0)
            {
                hitUv = uv;
                return confidence;
            }
            armed = false;
        }
        previous = marchPoint;
    }
    return 0.0;
}

vec3 liquidRippleNormal(vec3 worldPosition, vec3 viewerFacingNormal)
{
    // Two fixed, low-amplitude travelling wave octaves beat against one
    // another without introducing a Liquid-type authoring surface.
    vec2 p = worldPosition.xz;
    vec2 directionA = normalize(vec2(1.0, 0.63));
    vec2 directionB = normalize(vec2(-0.41, 1.0));
    float phaseA = dot(p, directionA) * 0.34 + @Uniform(GLOBAL_TIME) * 0.72;
    float phaseB = dot(p, directionB) * 0.79 - @Uniform(GLOBAL_TIME) * 0.47;
    vec2 gradient = directionA * (cos(phaseA) * 0.034) +
                    directionB * (cos(phaseB) * 0.018);
    float side = viewerFacingNormal.y < 0.0 ? -1.0 : 1.0;
    return normalize(
        viewerFacingNormal + side * vec3(-gradient.x, 0.0, -gradient.y));
}

vec2 liquidPlanarRippleOffset(vec3 rippleNormal)
{
    // Planar projection has no ray travel to magnify the perturbed normal the
    // way SSR does. Convert the normal to a surface slope and apply enough
    // normalized-image displacement to remain visible at Quarter resolution.
    vec2 slope = rippleNormal.xz / max(abs(rippleNormal.y), 0.2);
    return slope * 0.12;
}

vec2 encodeOctahedralNormal(vec3 normal)
{
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    vec2 oct = normal.xy;
    if (normal.z < 0.0)
        oct = (1.0 - abs(oct.yx)) * sign(oct.xy);
    return oct * 0.5 + 0.5;
}

const float PI = 3.14159265359;

vec3 snapToGrid(vec3 p, float gridSize)
{
    return round(p / gridSize) * gridSize;
}

float floorTileRandom(vec2 tileId)
{
    tileId = fract(tileId * vec2(0.3183099, 0.3678794) + 0.1);
    tileId *= 17.0;
    return fract(tileId.x * tileId.y * (tileId.x + tileId.y));
}

float embossTileDepthOffset(float depth, vec2 tileId)
{
    float factor = clamp(
        @Uniform(EMBOSS_DEPTH_VARIATION), 0.0, 1.0);
    return -max(depth, 0.0) * factor * floorTileRandom(tileId);
}

vec2 nearestHexagonCenter(vec2 position, float radius)
{
    float safeRadius = max(radius, 0.001);
    float q = (0.57735026919 * position.x - position.y / 3.0) /
        safeRadius;
    float r = (2.0 * position.y / 3.0) / safeRadius;

    vec3 cube = vec3(q, -q - r, r);
    vec3 roundedCube = round(cube);
    vec3 error = abs(roundedCube - cube);
    if (error.x > error.y && error.x > error.z)
    {
        roundedCube.x = -roundedCube.y - roundedCube.z;
    }
    else if (error.y > error.z)
    {
        roundedCube.y = -roundedCube.x - roundedCube.z;
    }
    else
    {
        roundedCube.z = -roundedCube.x - roundedCube.y;
    }

    return safeRadius * vec2(
        1.73205080757 * (roundedCube.x + roundedCube.z * 0.5),
        1.5 * roundedCube.z);
}

// Groove width is fixed in world units, so changing tile size expands only
// each tile's flat area rather than also softening its edges.
float tileGrooveHeight(float distanceToEdge, float depth)
{
    const float grooveWidth = 1.0;
    float groove = 1.0 - smoothstep(0.0, grooveWidth, distanceToEdge);
    return -max(depth, 0.0) * groove;
}

float hexagonalTileHeight(vec2 position, float radius, float depth)
{
    float safeRadius = max(radius, 0.001);
    vec2 local = position - nearestHexagonCenter(position, safeRadius);
    float distanceFromCenter = max(
        abs(local.x),
        max(abs(0.5 * local.x + 0.86602540378 * local.y),
            abs(-0.5 * local.x + 0.86602540378 * local.y)));
    float distanceToEdge = 0.86602540378 * safeRadius - distanceFromCenter;
    return embossTileDepthOffset(
               depth, (position - local) / safeRadius) +
        tileGrooveHeight(distanceToEdge, depth);
}

float squareTileHeight(vec2 position, float radius, float depth)
{
    float safeRadius = max(radius, 0.001);
    float tileSize = safeRadius * 2.0;
    vec2 local = abs(mod(position + safeRadius, tileSize) - safeRadius);
    vec2 tileId = floor((position + vec2(safeRadius)) / tileSize);
    float distanceToEdge = safeRadius - max(local.x, local.y);
    return embossTileDepthOffset(depth, tileId) +
        tileGrooveHeight(distanceToEdge, depth);
}

vec2 modularOpusLatticeCoordinates(vec2 position, float largeTileSize)
{
    float smallTileSize = largeTileSize * 0.5;
    float determinant = largeTileSize * largeTileSize +
        smallTileSize * smallTileSize;
    return vec2(
        (largeTileSize * position.x + smallTileSize * position.y) /
            determinant,
        (-smallTileSize * position.x + largeTileSize * position.y) /
            determinant);
}

vec2 modularOpusLargeTileOrigin(vec2 lattice, float largeTileSize)
{
    float smallTileSize = largeTileSize * 0.5;
    return lattice.x * vec2(largeTileSize, smallTileSize) +
        lattice.y * vec2(-smallTileSize, largeTileSize);
}

float distanceToSquareBoundary(
    vec2 position, vec2 origin, float tileSize)
{
    vec2 fromBox = abs(position - (origin + vec2(tileSize * 0.5))) -
        vec2(tileSize * 0.5);
    float signedDistance = length(max(fromBox, vec2(0.0))) +
        min(max(fromBox.x, fromBox.y), 0.0);
    return abs(signedDistance);
}

bool modularOpusPositionIsInLargeTile(
    vec2 position, float largeTileSize)
{
    float size = max(largeTileSize, 0.001);
    vec2 base = floor(modularOpusLatticeCoordinates(position, size));
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 origin = modularOpusLargeTileOrigin(
                base + vec2(float(x), float(y)), size);
            vec2 local = position - origin;
            if (local.x >= 0.0 && local.y >= 0.0 &&
                local.x <= size && local.y <= size)
                return true;
        }
    }
    return false;
}

float modularOpusTileHeight(vec2 position, float largeTileSize, float depth)
{
    float size = max(largeTileSize, 0.001);
    vec2 base = floor(modularOpusLatticeCoordinates(position, size));
    float smallTileSize = size * 0.5;
    float distanceToEdge = size;
    vec2 tileId = base;
    bool foundTile = false;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 lattice = base + vec2(float(x), float(y));
            vec2 origin = modularOpusLargeTileOrigin(lattice, size);
            distanceToEdge = min(
                distanceToEdge,
                distanceToSquareBoundary(position, origin, size));

            vec2 largeLocal = position - origin;
            if (!foundTile && largeLocal.x >= 0.0 &&
                largeLocal.y >= 0.0 && largeLocal.x <= size &&
                largeLocal.y <= size)
            {
                tileId = lattice;
                foundTile = true;
            }

            vec2 smallOrigin = origin + vec2(size, 0.0);
            vec2 smallLocal = position - smallOrigin;
            if (!foundTile && smallLocal.x >= 0.0 &&
                smallLocal.y >= 0.0 &&
                smallLocal.x <= smallTileSize &&
                smallLocal.y <= smallTileSize)
            {
                tileId = lattice + vec2(43.0, 17.0);
                foundTile = true;
            }
        }
    }
    return embossTileDepthOffset(depth, tileId) +
        tileGrooveHeight(distanceToEdge, depth);
}

float floorPatternHash(vec2 value)
{
    value = fract(value * vec2(0.3183099, 0.3678794) + 0.1);
    value *= 17.0;
    return fract(value.x * value.y * (value.x + value.y));
}

vec2 voronoiFeaturePoint(vec2 cell)
{
    return cell + vec2(
        floorPatternHash(cell + vec2(17.0, 3.0)),
        floorPatternHash(cell + vec2(5.0, 19.0)));
}

float roundedEdgeMinimum(float a, float b, float rounding)
{
    if (rounding <= 0.000001)
        return min(a, b);
    float blend = max(rounding - abs(a - b), 0.0) / rounding;
    return min(a, b) - blend * blend * rounding * 0.25;
}

float voronoiTileHeight(vec2 position, float cellSize, float depth)
{
    float size = max(cellSize, 0.001);
    vec2 scaledPosition = position / size;
    vec2 base = floor(scaledPosition);
    vec2 nearestSite = vec2(0.0);
    vec2 nearestCell = base;
    float nearestDistanceSquared = 100.0;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 site = voronoiFeaturePoint(
                base + vec2(float(x), float(y)));
            float distanceSquared = dot(
                site - scaledPosition, site - scaledPosition);
            if (distanceSquared < nearestDistanceSquared)
            {
                nearestDistanceSquared = distanceSquared;
                nearestSite = site;
                nearestCell = base + vec2(float(x), float(y));
            }
        }
    }

    float distanceToEdge = 10.0;
    float rounding = clamp(
        @Uniform(EMBOSS_VORONOI_ROUNDING), 0.0, 1.0) * 0.25;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            vec2 site = voronoiFeaturePoint(
                base + vec2(float(x), float(y)));
            vec2 betweenSites = site - nearestSite;
            float siteSeparationSquared = dot(betweenSites, betweenSites);
            if (siteSeparationSquared > 0.000001)
            {
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

float runningBondTileHeight(
    vec2 position, float length, float widthPercent,
    float offsetPercent, float depth)
{
    float safeLength = max(length, 0.001);
    float widthFactor = clamp(widthPercent * 0.01, 0.01, 1.0);
    float tileWidth = safeLength * widthFactor;
    float row = floor(position.y / tileWidth);
    float rowOffset = mod(row, 2.0) * safeLength *
        clamp(offsetPercent * 0.01, 0.0, 1.0);
    float column = floor((position.x - rowOffset) / safeLength);
    vec2 local = vec2(
        mod(position.x - rowOffset, safeLength),
        mod(position.y, tileWidth));
    vec2 distanceToEdges = min(
        local, vec2(safeLength, tileWidth) - local);
    return embossTileDepthOffset(depth, vec2(column, row)) +
        tileGrooveHeight(
            min(distanceToEdges.x, distanceToEdges.y), depth);
}

float embossPatternHeight(
    vec2 position, float radius, float depth, int pattern,
    float runningBondWidthPercent, float runningBondOffsetPercent)
{
    if (pattern == 1)
    {
        return squareTileHeight(position, radius, depth);
    }
    if (pattern == 2)
    {
        return hexagonalTileHeight(position, radius, depth);
    }
    if (pattern == 3)
    {
        return runningBondTileHeight(
            position, radius, runningBondWidthPercent,
            runningBondOffsetPercent, depth);
    }
    if (pattern == 4)
    {
        return modularOpusTileHeight(position, radius, depth);
    }
    if (pattern == 5)
    {
        return voronoiTileHeight(position, radius, depth);
    }
    return 0.0;
}

bool gridTileUsesSecondaryMaterial(vec2 position, float radius)
{
    float tileSize = max(radius, 0.001) * 2.0;
    vec2 cell = floor((position + vec2(radius)) / tileSize);
    return mod(cell.x + cell.y, 2.0) > 0.5;
}

bool hexagonTileUsesSecondaryMaterial(vec2 position, float radius)
{
    float safeRadius = max(radius, 0.001);
    float q = (0.57735026919 * position.x - position.y / 3.0) /
        safeRadius;
    float r = (2.0 * position.y / 3.0) / safeRadius;
    vec3 cube = vec3(q, -q - r, r);
    vec3 roundedCube = round(cube);
    vec3 error = abs(roundedCube - cube);
    if (error.x > error.y && error.x > error.z)
        roundedCube.x = -roundedCube.y - roundedCube.z;
    else if (error.y > error.z)
        roundedCube.y = -roundedCube.x - roundedCube.z;
    else
        roundedCube.z = -roundedCube.x - roundedCube.y;

    // One of the three axial colour classes is primary. Every primary hexagon
    // is therefore surrounded by a complete six-hexagon secondary ring.
    return mod(roundedCube.x - roundedCube.z, 3.0) > 0.5;
}

bool modularOpusTileUsesSecondaryMaterial(
    vec2 position, float largeTileSize)
{
    // Large squares use the primary material; the offset half-size squares
    // filling the lattice gaps use the secondary material.
    return !modularOpusPositionIsInLargeTile(position, largeTileSize);
}

int floorMaterialIndex(vec3 worldPosition, int primaryMaterialIndex)
{
    int secondaryMaterialIndex = @Uniform(SECONDARY_MATERIAL_INDEX);
    if (@Uniform(USE_SECONDARY_MATERIAL) == 0 ||
        secondaryMaterialIndex < 0 ||
        secondaryMaterialIndex == primaryMaterialIndex)
        return primaryMaterialIndex;

    vec2 position = worldPosition.xz;
    float radius = @Uniform(EMBOSS_RADIUS);
    int pattern = @Uniform(EMBOSS_PATTERN);
    if (pattern == 1 &&
        gridTileUsesSecondaryMaterial(position, radius))
        return secondaryMaterialIndex;
    if (pattern == 2 &&
        hexagonTileUsesSecondaryMaterial(position, radius))
        return secondaryMaterialIndex;
    if (pattern == 4 &&
        modularOpusTileUsesSecondaryMaterial(position, radius))
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
    vec3 normal, vec3 worldPosition, float radius, float depth, int pattern,
    float runningBondWidth, float runningBondOffset)
{
    vec3 surfaceNormal = normalize(normal);
    vec3 axisU;
    vec3 axisV;
    embossSurfaceAxes(surfaceNormal, axisU, axisV);

    // Keep the finite-difference distance in world units too: scaling it with
    // the tile size blurred the fixed-width grooves on large tiles.
    const float epsilon = 0.02;
    vec2 position = vec2(
        dot(worldPosition, axisU), dot(worldPosition, axisV));
    float left = embossPatternHeight(
        position - vec2(epsilon, 0.0), radius, depth, pattern,
        runningBondWidth, runningBondOffset);
    float right = embossPatternHeight(
        position + vec2(epsilon, 0.0), radius, depth, pattern,
        runningBondWidth, runningBondOffset);
    float back = embossPatternHeight(
        position - vec2(0.0, epsilon), radius, depth, pattern,
        runningBondWidth, runningBondOffset);
    float front = embossPatternHeight(
        position + vec2(0.0, epsilon), radius, depth, pattern,
        runningBondWidth, runningBondOffset);
    vec2 gradient = vec2(right - left, front - back) / (2.0 * epsilon);

    // Tilt the normal away from the rising side of the height field, in the
    // surface's own plane. For a floor this is the plain xz gradient the
    // global floor pattern used to apply.
    return normalize(
        surfaceNormal - axisU * gradient.x - axisV * gradient.y);
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

// Marble's tunable knobs, unpacked once from the flat MATERIAL_PARAMS array
// so the material's own code reads named fields rather than magic indices.
// Slot order matches the Marble Technique schema in ProcMaterial -
// index 0 is warpScale, and so on - and each default below is the literal
// constant this used to be hardcoded to, so a freshly authored Primitive
// renders identically to before this was made tunable.
struct MarbleParams
{
    float warpScale;        // 0: how far the vein field warps off-grid.
    float veinsScale;       // 1: primary (broad) vein frequency.
    float veinsFineScale;   // 2: secondary (fine) vein frequency.
    float fineDetailScale;  // 3: normal-map bump strength from the field.
    float lightWarmMix;     // 4: cool/warm stone colour blend threshold.
    float veinMix;          // 5: how warm the vein colour itself skews.
    float cloudiness;       // 6: large-scale colour cloud frequency.
    float fbmScale;         // 7: overall world-space pattern scale.
};

MarbleParams unpackMarbleParams()
{
    MarbleParams result;
    result.warpScale = blendedMaterialParams[0];
    result.veinsScale = blendedMaterialParams[1];
    result.veinsFineScale = blendedMaterialParams[2];
    result.fineDetailScale = blendedMaterialParams[3];
    result.lightWarmMix = blendedMaterialParams[4];
    result.veinMix = blendedMaterialParams[5];
    result.cloudiness = blendedMaterialParams[6];
    result.fbmScale = blendedMaterialParams[7];
    return result;
}

// A continuous three-dimensional marble field. Because it is evaluated from
// world position rather than UVs, veins continue naturally across floors,
// walls and ceilings without seams or planar stretching.
float marbleField(vec3 p, MarbleParams params)
{
    vec3 warp = vec3(
        noise(p * 0.65 + vec3(7.1, 1.7, 4.3)),
        noise(p * 0.65 + vec3(2.8, 9.2, 5.6)),
        noise(p * 0.65 + vec3(5.4, 3.1, 8.7))) - 0.5;
    vec3 q = p + warp * params.warpScale;

    float turbulence = fbm(q * 1.15) - 0.5;
    float broadVein = abs(sin(q.x * params.veinsScale + q.y * 1.15 + q.z * 0.8 +
                              turbulence * 7.0));
    float fineVein = abs(sin(q.x * params.veinsFineScale - q.y * 1.7 + q.z * 2.1 +
                             fbm(q * 2.4) * 5.0));

    float broadMask = 1.0 - smoothstep(0.06, 0.30, broadVein);
    float fineMask = (1.0 - smoothstep(0.025, 0.13, fineVein)) * 0.45;
    return clamp(max(broadMask, fineMask), 0.0, 1.0);
}

Material marbleTexture(vec3 worldPos, vec3 normal)
{
    MarbleParams params = unpackMarbleParams();

    Material material;
    vec3 p = worldPos * params.fbmScale;
    float veins = marbleField(p, params);
    float cloud = fbm(p * params.cloudiness);
    float grain = noise(p * 7.0);

    vec3 coolStone = vec3(0.72, 0.76, 0.81);
    vec3 warmStone = vec3(0.93, 0.89, 0.82);
    vec3 stone = mix(coolStone, warmStone,
                     smoothstep(params.lightWarmMix, 0.85, cloud));
    stone *= mix(0.91, 1.06, grain);

    vec3 veinColour = mix(vec3(0.025, 0.030, 0.040),
                          vec3(0.20, 0.13, 0.10), cloud * params.veinMix);
    material.albedo = mix(stone, veinColour, veins);
    material.metallic = 0.0;
    material.roughness = clamp(mix(0.32, 0.18, veins) +
                               (grain - 0.5) * 0.10, 0.12, 0.48);

    // Derive a small-scale normal from the same scalar field. Projecting its
    // gradient onto the geometric tangent plane keeps the perturbation valid
    // for every surface orientation without requiring tangents or UVs.
    float epsilon = params.fineDetailScale;
    vec3 gradient = vec3(
        marbleField(p + vec3(epsilon, 0.0, 0.0), params) - veins,
        marbleField(p + vec3(0.0, epsilon, 0.0), params) - veins,
        marbleField(p + vec3(0.0, 0.0, epsilon), params) - veins) / epsilon;
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

float naturalRockField(vec3 p)
{
    float fractured = fbm(p * 0.82);
    float grains = noise(p * 7.5);
    float cells = geologyVoronoi(p * 2.6);
    return fractured * 0.58 + grains * 0.20 +
           (1.0 - smoothstep(0.10, 0.42, cells)) * 0.22;
}

// Scalar surface fields shared by the geology materials below. The type is a
// compile-time constant at each call site in practice, allowing drivers to
// discard all unrelated branches after specializing the material switch.
float geologyField(vec3 p, int type)
{
    if (type == 0) // Granite: interlocked mineral grains.
    {
        // Stone's medium_scale (MATERIAL_PARAMS[1]) - bound here rather than
        // in graniteTexture below because this exact expression is shared
        // verbatim by world_pbr_2d.frag's materialField type 1, and binding
        // it here keeps one default correct for both instead of two shaders
        // disagreeing about which constant medium_scale actually is.
        float mediumScale = blendedMaterialParams[1];
        float coarse = geologyVoronoi(p * 2.2);
        return fbm(p * 0.65) * 0.45 + noise(p * mediumScale) * 0.20 +
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
    if (type == 7) // Ore: host rock crossed by folded metallic deposits.
    {
        float warp = fbm(p * 0.62) - 0.5;
        float vein = abs(sin(
            p.x * 4.5 + p.y * 1.1 - p.z * 0.8 + warp * 8.0));
        float veinMask = 1.0 - smoothstep(0.06, 0.24, vein);
        return fbm(p * 1.6) * 0.42 + veinMask * 0.58;
    }
    if (type == 8) // Banded gneiss: folded metamorphic mineral layers.
    {
        float warp = fbm(p * 0.48) - 0.5;
        float bands = sin(dot(p, normalize(vec3(0.82, 0.24, -0.52))) *
                          8.0 + warp * 7.0) * 0.5 + 0.5;
        return bands * 0.72 + noise(p * 9.0) * 0.28;
    }
    if (type == 9) // Rock: fractured, coarse-grained stone.
        return naturalRockField(p);
    if (type == 10) // Mossy rock: stone softened by a fine organic layer.
        return naturalRockField(p) * 0.82 + fbm(p * 2.1) * 0.18;

    // Wet rock: eroded stone with shallow water-smoothed detail.
    return naturalRockField(p) * 0.78 + fbm(p * 0.42) * 0.22;
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

// Stone's tunable knobs. Slot order matches the Stone Technique schema in
// ProcMaterial, and each default is the literal this used to be
// hardcoded to - see the equivalent note on MarbleParams above.
// medium_scale (MATERIAL_PARAMS[1]) is not here - see the note in
// geologyField's granite branch above for why it is bound there instead.
struct StoneParams
{
    float baseScale;
    float stoneMix;
    float normalStrength;
    float feldsparFreq;
    float quartzFreq;
    float micaRoughness;
};

StoneParams unpackStoneParams()
{
    StoneParams result;
    result.baseScale = blendedMaterialParams[0];
    result.stoneMix = blendedMaterialParams[2];
    result.normalStrength = blendedMaterialParams[3];
    result.feldsparFreq = blendedMaterialParams[4];
    result.quartzFreq = blendedMaterialParams[5];
    result.micaRoughness = blendedMaterialParams[6];
    return result;
}

Material graniteTexture(vec3 worldPos, vec3 normal)
{
    StoneParams params = unpackStoneParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 0);
    float quartz = smoothstep(0.68, 0.88, noise(p * params.quartzFreq + vec3(2.0)));
    float feldspar = smoothstep(0.52, 0.78, noise(p * params.feldsparFreq + vec3(11.0)));
    float mica = smoothstep(0.88, 0.97, noise(p * 13.0 + vec3(23.0)));
    vec3 colour = mix(vec3(0.16, 0.15, 0.15), vec3(0.48, 0.42, 0.38), feldspar);
    colour = mix(colour, vec3(0.72, 0.70, 0.66), quartz);
    material.albedo = mix(colour, vec3(0.035), mica);
    material.metallic = mica * params.stoneMix;
    material.roughness = clamp(0.58 - quartz * 0.16 - mica * params.micaRoughness, 0.24, 0.68);
    material.normal = geologyNormal(p, normal, 0, surface, params.normalStrength);
    return material;
}

// Slate's two knobs, matching the Slate Technique schema in
// ProcMaterial. Each default is the literal this used to be hardcoded
// to, same discipline as MarbleParams/StoneParams above.
struct SlateParams
{
    float baseScale;
    float rustMix;
    float normalStrength;
    float rustFreq;
    float roughnessAmt;
};

SlateParams unpackSlateParams()
{
    SlateParams result;
    result.baseScale = blendedMaterialParams[0];
    result.rustMix = blendedMaterialParams[1];
    result.normalStrength = blendedMaterialParams[2];
    result.rustFreq = blendedMaterialParams[3];
    result.roughnessAmt = blendedMaterialParams[4];
    return result;
}

Material slateTexture(vec3 worldPos, vec3 normal)
{
    SlateParams params = unpackSlateParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 1);
    float layer = sin(p.y * 8.0 + p.x * 0.7 + fbm(p * 0.8) * 3.2) * 0.5 + 0.5;
    float rust = smoothstep(0.73, 0.93, fbm(p * params.rustFreq + vec3(8.0)));
    material.albedo = mix(vec3(0.075, 0.095, 0.115),
                          vec3(0.18, 0.21, 0.22), layer);
    material.albedo = mix(material.albedo, vec3(0.30, 0.13, 0.055), rust * params.rustMix);
    material.metallic = 0.0;
    material.roughness = clamp(0.42 + layer * params.roughnessAmt, 0.34, 0.68);
    material.normal = geologyNormal(p, normal, 1, surface, params.normalStrength);
    return material;
}

// Sandstone's two knobs, matching its ProcMaterial Technique schema.
struct SandstoneParams
{
    float baseScale;
    float grainScale;
    float normalStrength;
    float brightAmt;
    float roughnessAmt;
};

SandstoneParams unpackSandstoneParams()
{
    SandstoneParams result;
    result.baseScale = blendedMaterialParams[0];
    result.grainScale = blendedMaterialParams[1];
    result.normalStrength = blendedMaterialParams[2];
    result.brightAmt = blendedMaterialParams[3];
    result.roughnessAmt = blendedMaterialParams[4];
    return result;
}

Material sandstoneTexture(vec3 worldPos, vec3 normal)
{
    SandstoneParams params = unpackSandstoneParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 2);
    float band = sin(p.y * 5.5 + fbm(p * 0.45) * 4.0) * 0.5 + 0.5;
    float grain = noise(p * params.grainScale);
    vec3 pale = vec3(0.70, 0.43, 0.22);
    vec3 red = vec3(0.43, 0.16, 0.075);
    material.albedo = mix(pale, red, smoothstep(0.25, 0.8, band));
    material.albedo *= mix(0.86, params.brightAmt, grain);
    material.metallic = 0.0;
    material.roughness = clamp(0.72 + (grain - 0.5) * params.roughnessAmt, 0.58, 0.9);
    material.normal = geologyNormal(p, normal, 2, surface, params.normalStrength);
    return material;
}

// Limestone's two knobs, matching its ProcMaterial Technique schema.
struct LimestoneParams
{
    float baseScale;
    float poreMix;
    float normalStrength;
    float roughnessAmt;
};

LimestoneParams unpackLimestoneParams()
{
    LimestoneParams result;
    result.baseScale = blendedMaterialParams[0];
    result.poreMix = blendedMaterialParams[1];
    result.normalStrength = blendedMaterialParams[2];
    result.roughnessAmt = blendedMaterialParams[3];
    return result;
}

Material limestoneTexture(vec3 worldPos, vec3 normal)
{
    LimestoneParams params = unpackLimestoneParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 3);
    float deposits = fbm(p * 0.75);
    float pores = 1.0 - smoothstep(0.08, 0.28, geologyVoronoi(p * 5.0));
    material.albedo = mix(vec3(0.48, 0.45, 0.35),
                          vec3(0.82, 0.79, 0.66), deposits);
    material.albedo *= 1.0 - pores * params.poreMix;
    material.metallic = 0.0;
    material.roughness = clamp(0.62 + pores * params.roughnessAmt, 0.52, 0.9);
    material.normal = geologyNormal(p, normal, 3, surface, params.normalStrength);
    return material;
}

// Each geology material's two knobs, matching their ProcMaterial Technique schemas for
// its index. Same discipline as Slate/Sandstone/Limestone above: base_scale
// is always slot 0, the second slot is a constant local to that material's
// own *Texture function only - never inside geologyField/geologyNormal,
// which every geology material calls for its own type but which none of
// them may safely change the shared behaviour of.
struct BasaltParams
{
    float baseScale;
    float vesicleMix;
    float normalStrength;
    float metallicAmt;
    float roughnessAmt;
};
BasaltParams unpackBasaltParams() {
    BasaltParams r;
    r.baseScale = blendedMaterialParams[0];
    r.vesicleMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicAmt = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material basaltTexture(vec3 worldPos, vec3 normal)
{
    BasaltParams params = unpackBasaltParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 4);
    float grain = noise(p * 12.0);
    float vesicles = 1.0 - smoothstep(0.10, 0.32, geologyVoronoi(p * 3.8));
    material.albedo = mix(vec3(0.025, 0.027, 0.030),
                          vec3(0.12, 0.13, 0.14), grain);
    material.albedo *= 1.0 - vesicles * params.vesicleMix;
    material.metallic = params.metallicAmt;
    material.roughness = clamp(0.58 + vesicles * params.roughnessAmt, 0.48, 0.92);
    material.normal = geologyNormal(p, normal, 4, surface, params.normalStrength);
    return material;
}

struct ObsidianParams
{
    float baseScale;
    float inclusionMix;
    float normalStrength;
    float sheenExp;
    float roughnessAmt;
};
ObsidianParams unpackObsidianParams() {
    ObsidianParams r;
    r.baseScale = blendedMaterialParams[0];
    r.inclusionMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.sheenExp = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material obsidianTexture(vec3 worldPos, vec3 normal)
{
    ObsidianParams params = unpackObsidianParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 5);
    float sheen = pow(surface, params.sheenExp);
    float inclusions = smoothstep(0.84, 0.96, noise(p * 9.0));
    material.albedo = mix(vec3(0.006, 0.008, 0.012),
                          vec3(0.055, 0.025, 0.075), sheen);
    material.albedo = mix(material.albedo, vec3(0.17, 0.09, 0.05), inclusions * params.inclusionMix);
    material.metallic = 0.0;
    material.roughness = clamp(0.075 + inclusions * params.roughnessAmt + sheen * 0.035, 0.055, 0.34);
    material.normal = geologyNormal(p, normal, 5, surface, params.normalStrength);
    return material;
}

struct QuartzParams
{
    float baseScale;
    float amethystMix;
    float normalStrength;
    float edgeAmt;
    float roughnessAmt;
};
QuartzParams unpackQuartzParams() {
    QuartzParams r;
    r.baseScale = blendedMaterialParams[0];
    r.amethystMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.edgeAmt = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material quartzTexture(vec3 worldPos, vec3 normal)
{
    QuartzParams params = unpackQuartzParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 6);
    float cells = geologyVoronoi(p * 2.4);
    float amethyst = smoothstep(
        0.48, 0.88, fbm(p * 0.48 + vec3(17.0)));
    float edge = 1.0 - smoothstep(0.10, 0.30, cells);
    material.albedo = mix(vec3(0.72, 0.82, 0.88),
                          vec3(0.34, 0.14, 0.52), amethyst * params.amethystMix);
    material.albedo = mix(material.albedo, vec3(0.92, 0.98, 1.0), edge * params.edgeAmt);
    material.metallic = 0.0;
    material.roughness = clamp(0.11 + cells * params.roughnessAmt, 0.08, 0.34);
    material.normal = geologyNormal(p, normal, 6, surface, params.normalStrength);
    return material;
}

struct OreParams
{
    float baseScale;
    float veinScale;
    float normalStrength;
    float metallicDarken;
    float roughnessAmt;
    float veinThickness;
};
OreParams unpackOreParams() {
    OreParams r;
    r.baseScale = blendedMaterialParams[0];
    r.veinScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicDarken = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    r.veinThickness = blendedMaterialParams[5];
    return r;
}

Material oreTexture(vec3 worldPos, vec3 normal)
{
    OreParams params = unpackOreParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 7);
    float warp = fbm(p * 0.62) - 0.5;
    float vein = abs(sin(p.x * params.veinScale + p.y * 1.1 - p.z * 0.8 + warp * 8.0));
    float metal = 1.0 - smoothstep(
        params.veinThickness * 0.25, params.veinThickness, vein);
    float oxidation = smoothstep(
        0.62, 0.88, noise(p * 3.7 + vec3(31.0))) * metal;
    vec3 host = mix(vec3(0.055, 0.06, 0.065), vec3(0.20, 0.18, 0.15), fbm(p * 1.6));
    vec3 ore = mix(vec3(0.48, 0.45, 0.38), vec3(0.73, 0.43, 0.12), oxidation);
    material.albedo = mix(host, ore, metal);
    material.metallic = metal * (1.0 - oxidation * params.metallicDarken);
    material.roughness = clamp(mix(0.68, 0.20, metal) + oxidation * params.roughnessAmt, 0.16, 0.78);
    material.normal = geologyNormal(p, normal, 7, surface, params.normalStrength);
    return material;
}

struct BandedGneissParams
{
    float baseScale;
    float garnetScale;
    float normalStrength;
    float roughnessAmt;
    float metallicAmt;
};
BandedGneissParams unpackBandedGneissParams() {
    BandedGneissParams r;
    r.baseScale = blendedMaterialParams[0];
    r.garnetScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.roughnessAmt = blendedMaterialParams[3];
    r.metallicAmt = blendedMaterialParams[4];
    return r;
}

Material bandedGneissTexture(vec3 worldPos, vec3 normal)
{
    BandedGneissParams params = unpackBandedGneissParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 8);
    float warp = fbm(p * 0.48) - 0.5;
    float band = sin(dot(p, normalize(vec3(0.82, 0.24, -0.52))) *
                     8.0 + warp * 7.0) * 0.5 + 0.5;
    float garnet = smoothstep(0.91, 0.975, noise(p * params.garnetScale + vec3(7.0)));
    material.albedo = mix(
        vec3(0.075, 0.080, 0.085), vec3(0.66, 0.61, 0.54),
        smoothstep(0.30, 0.70, band));
    material.albedo = mix(material.albedo, vec3(0.30, 0.045, 0.055), garnet);
    material.metallic = params.metallicAmt;
    material.roughness = clamp(0.48 + (noise(p * 8.0) - 0.5) * params.roughnessAmt,
                               0.36, 0.62);
    material.normal = geologyNormal(p, normal, 8, surface, params.normalStrength);
    return material;
}

struct RockParams
{
    float baseScale;
    float mineralScale;
    float normalStrength;
    float brightAmt;
    float roughnessAmt;
};
RockParams unpackRockParams() {
    RockParams r;
    r.baseScale = blendedMaterialParams[0];
    r.mineralScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.brightAmt = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material rockTexture(vec3 worldPos, vec3 normal)
{
    RockParams params = unpackRockParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 9);
    float mineral = noise(p * params.mineralScale);
    float weather = fbm(p * 0.55);
    material.albedo = mix(
        vec3(0.16, 0.15, 0.135), vec3(0.43, 0.41, 0.37), weather);
    material.albedo *= mix(0.82, params.brightAmt, mineral);
    material.metallic = 0.0;
    material.roughness = clamp(0.68 + (mineral - 0.5) * params.roughnessAmt, 0.58, 0.82);
    material.normal = geologyNormal(p, normal, 9, surface, params.normalStrength);
    return material;
}

struct MossyRockParams
{
    float baseScale;
    float mossScale;
    float normalStrength;
    float mossBias;
    float roughnessMax;
};
MossyRockParams unpackMossyRockParams() {
    MossyRockParams r;
    r.baseScale = blendedMaterialParams[0];
    r.mossScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.mossBias = blendedMaterialParams[3];
    r.roughnessMax = blendedMaterialParams[4];
    return r;
}

Material mossyRockTexture(vec3 worldPos, vec3 normal)
{
    MossyRockParams params = unpackMossyRockParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 10);
    float moisture = fbm(p * 0.82 + vec3(4.0, 9.0, 2.0));
    float upward = max(normalize(normal).y, 0.0);
    float moss = smoothstep(0.43, 0.68, moisture) * (params.mossBias + upward * (1.0 - params.mossBias));
    float fineMoss = noise(p * params.mossScale);
    vec3 stone = mix(vec3(0.13, 0.13, 0.115), vec3(0.38, 0.37, 0.32),
                     fbm(p * 0.55));
    vec3 mossColour = mix(
        vec3(0.055, 0.105, 0.025), vec3(0.25, 0.34, 0.07), fineMoss);
    material.albedo = mix(stone, mossColour, moss);
    material.metallic = 0.0;
    material.roughness = mix(params.roughnessMax, 0.92, moss);
    material.normal = geologyNormal(p, normal, 10, surface, params.normalStrength);
    return material;
}

struct WetRockParams
{
    float baseScale;
    float wetnessThreshold;
    float wetnessUpper;
    float darkAmt;
    float roughnessMax;
};
WetRockParams unpackWetRockParams() {
    WetRockParams r;
    r.baseScale = blendedMaterialParams[0];
    r.wetnessThreshold = blendedMaterialParams[1];
    r.wetnessUpper = blendedMaterialParams[2];
    r.darkAmt = blendedMaterialParams[3];
    r.roughnessMax = blendedMaterialParams[4];
    return r;
}

Material wetRockTexture(vec3 worldPos, vec3 normal)
{
    WetRockParams params = unpackWetRockParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = geologyField(p, 11);
    float wetness = smoothstep(
        params.wetnessThreshold, params.wetnessUpper, fbm(p * 0.46 + vec3(12.0, 3.0, 8.0)));
    vec3 dryStone = mix(vec3(0.14, 0.14, 0.135), vec3(0.39, 0.38, 0.35),
                        fbm(p * 0.62));
    material.albedo = dryStone * mix(0.72, params.darkAmt, wetness);
    material.metallic = 0.0;
    material.roughness = mix(params.roughnessMax, 0.075, wetness);
    material.normal = geologyNormal(p, normal, 11, surface,
                                    mix(0.062, 0.035, wetness));
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

// Each metal's two knobs, matching their ProcMaterial Technique schemas for its
// index - same "base_scale in slot 0, second slot local to this function
// only" discipline as the geology materials above; none of these may safely
// change metalField/metalNormal, which every metal calls.
struct RustedIronParams
{
    float baseScale;
    float rustScale;
    float normalStrength;
    float metallicMax;
    float roughnessMax;
};
RustedIronParams unpackRustedIronParams() {
    RustedIronParams r;
    r.baseScale = blendedMaterialParams[0];
    r.rustScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicMax = blendedMaterialParams[3];
    r.roughnessMax = blendedMaterialParams[4];
    return r;
}

Material rustedIronTexture(vec3 worldPos, vec3 normal)
{
    RustedIronParams params = unpackRustedIronParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = metalField(p, 0);
    float corrosion = smoothstep(0.42, 0.72, fbm(p * 1.15));
    float pits = 1.0 - smoothstep(0.08, 0.25, geologyVoronoi(p * 6.0));
    vec3 iron = vec3(0.22, 0.23, 0.24);
    vec3 rust = mix(vec3(0.18, 0.045, 0.012),
                    vec3(0.58, 0.19, 0.035), noise(p * params.rustScale));
    float rustMask = clamp(corrosion + pits * 0.45, 0.0, 1.0);
    material.albedo = mix(iron, rust, rustMask);
    material.metallic = mix(params.metallicMax, 0.0, rustMask);
    material.roughness = mix(0.28, params.roughnessMax, rustMask);
    material.normal = metalNormal(p, normal, 0, surface, params.normalStrength);
    return material;
}

struct GalvanizedSteelParams
{
    float baseScale;
    float facetMix;
    float normalStrength;
    float metallicAmt;
    float roughnessAmt;
};
GalvanizedSteelParams unpackGalvanizedSteelParams() {
    GalvanizedSteelParams r;
    r.baseScale = blendedMaterialParams[0];
    r.facetMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicAmt = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material galvanizedSteelTexture(vec3 worldPos, vec3 normal)
{
    GalvanizedSteelParams params = unpackGalvanizedSteelParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = metalField(p, 1);
    float crystals = geologyVoronoi(p * 3.6);
    float facet = clamp(crystals * params.facetMix, 0.0, 1.0);
    material.albedo = mix(vec3(0.42, 0.45, 0.47),
                          vec3(0.74, 0.77, 0.78), facet);
    material.metallic = params.metallicAmt;
    material.roughness = clamp(0.25 + facet * params.roughnessAmt, 0.22, 0.48);
    material.normal = metalNormal(p, normal, 1, surface, params.normalStrength);
    return material;
}

struct BrushedMetalParams
{
    float baseScale;
    float scratchMix;
    float normalStrength;
    float metallicAmt;
    float roughnessAmt;
};
BrushedMetalParams unpackBrushedMetalParams() {
    BrushedMetalParams r;
    r.baseScale = blendedMaterialParams[0];
    r.scratchMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicAmt = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material brushedMetalTexture(vec3 worldPos, vec3 normal)
{
    BrushedMetalParams params = unpackBrushedMetalParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = metalField(p, 2);
    float scratch = smoothstep(0.58, 0.86, surface);
    material.albedo = mix(vec3(0.42, 0.44, 0.46),
                          vec3(0.68, 0.70, 0.72), surface);
    material.albedo *= 1.0 - scratch * params.scratchMix;
    material.metallic = params.metallicAmt;
    material.roughness = clamp(0.20 + scratch * params.roughnessAmt, 0.18, 0.52);
    material.normal = metalNormal(p, normal, 2, surface, params.normalStrength);
    return material;
}

struct HammeredMetalParams
{
    float baseScale;
    float dentScale;
    float normalStrength;
    float metallicAmt;
    float roughnessAmt;
};
HammeredMetalParams unpackHammeredMetalParams() {
    HammeredMetalParams r;
    r.baseScale = blendedMaterialParams[0];
    r.dentScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicAmt = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material hammeredMetalTexture(vec3 worldPos, vec3 normal)
{
    HammeredMetalParams params = unpackHammeredMetalParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = metalField(p, 3);
    float dents = geologyVoronoi(p * params.dentScale);
    material.albedo = mix(vec3(0.24, 0.25, 0.27),
                          vec3(0.52, 0.55, 0.58), smoothstep(0.1, 0.7, dents));
    material.metallic = params.metallicAmt;
    material.roughness = clamp(0.30 + (1.0 - surface) * params.roughnessAmt, 0.28, 0.58);
    material.normal = metalNormal(p, normal, 3, surface, params.normalStrength);
    return material;
}

struct PatinatedCopperParams
{
    float baseScale;
    float exposedScale;
    float normalStrength;
    float metallicMax;
    float roughnessMax;
};
PatinatedCopperParams unpackPatinatedCopperParams() {
    PatinatedCopperParams r;
    r.baseScale = blendedMaterialParams[0];
    r.exposedScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicMax = blendedMaterialParams[3];
    r.roughnessMax = blendedMaterialParams[4];
    return r;
}

Material patinatedCopperTexture(vec3 worldPos, vec3 normal)
{
    PatinatedCopperParams params = unpackPatinatedCopperParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = metalField(p, 4);
    float patina = smoothstep(0.43, 0.67, surface);
    float exposed = smoothstep(0.66, 0.82, noise(p * params.exposedScale + vec3(13.0)));
    patina *= 1.0 - exposed;
    vec3 copper = vec3(0.72, 0.27, 0.09);
    vec3 verdigris = mix(vec3(0.025, 0.20, 0.15),
                         vec3(0.12, 0.48, 0.39), fbm(p * 1.6));
    material.albedo = mix(copper, verdigris, patina);
    material.metallic = mix(params.metallicMax, 0.03, patina);
    material.roughness = mix(0.20, params.roughnessMax, patina);
    material.normal = metalNormal(p, normal, 4, surface, params.normalStrength);
    return material;
}

struct DamasceneSteelParams
{
    float baseScale;
    float layerThreshold;
    float normalStrength;
    float metallicAmt;
    float roughnessMax;
};
DamasceneSteelParams unpackDamasceneSteelParams() {
    DamasceneSteelParams r;
    r.baseScale = blendedMaterialParams[0];
    r.layerThreshold = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicAmt = blendedMaterialParams[3];
    r.roughnessMax = blendedMaterialParams[4];
    return r;
}

Material damasceneSteelTexture(vec3 worldPos, vec3 normal)
{
    DamasceneSteelParams params = unpackDamasceneSteelParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = metalField(p, 5);
    float layers = smoothstep(params.layerThreshold, 0.68, surface);
    material.albedo = mix(vec3(0.12, 0.13, 0.15),
                          vec3(0.62, 0.65, 0.68), layers);
    material.metallic = params.metallicAmt;
    material.roughness = mix(params.roughnessMax, 0.17, layers);
    material.normal = metalNormal(p, normal, 5, surface, params.normalStrength);
    return material;
}

struct HeatTreatedMetalParams
{
    float baseScale;
    float oxideMix;
    float normalStrength;
    float metallicAmt;
    float roughnessAmt;
};
HeatTreatedMetalParams unpackHeatTreatedMetalParams() {
    HeatTreatedMetalParams r;
    r.baseScale = blendedMaterialParams[0];
    r.oxideMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicAmt = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material heatTreatedMetalTexture(vec3 worldPos, vec3 normal)
{
    HeatTreatedMetalParams params = unpackHeatTreatedMetalParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = metalField(p, 6);
    float band = clamp(surface, 0.0, 1.0);
    vec3 straw = vec3(0.78, 0.38, 0.08);
    vec3 violet = vec3(0.28, 0.06, 0.42);
    vec3 blue = vec3(0.035, 0.16, 0.48);
    vec3 oxideColour = mix(straw, violet, smoothstep(0.18, 0.58, band));
    oxideColour = mix(oxideColour, blue, smoothstep(0.55, 0.88, band));
    material.albedo = mix(vec3(0.38, 0.40, 0.42), oxideColour, params.oxideMix);
    material.metallic = params.metallicAmt;
    material.roughness = clamp(0.19 + noise(p * 6.0) * params.roughnessAmt, 0.18, 0.34);
    material.normal = metalNormal(p, normal, 6, surface, params.normalStrength);
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

// Port of "Procedural Wood texture" by dean_the_coder:
// https://www.shadertoy.com/view/mdy3R1
// Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported.
// Function names are prefixed because this material intentionally retains the
// source shader's own hash/noise stack rather than changing the other materials.
float wood2Sum(vec2 value)
{
    return dot(value, vec2(1.0));
}

float wood2Hash31(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 333.3456);
    return fract(wood2Sum(p.xy) * p.z);
}

float wood2Hash21(vec2 p)
{
    return wood2Hash31(p.xyx);
}

float wood2Noise31(vec3 p)
{
    const vec3 stepVector = vec3(7.0, 157.0, 113.0);
    vec3 cell = floor(p);
    p = fract(p);
    p = p * p * (3.0 - 2.0 * p);
    vec4 values = vec4(0.0, stepVector.yz, wood2Sum(stepVector.yz)) +
                  dot(cell, stepVector);
    values = mix(fract(sin(values) * 43758.545),
                 fract(sin(values + stepVector.x) * 43758.545), p.x);
    values.xy = mix(values.xz, values.yw, p.y);
    return mix(values.x, values.y, p.z);
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
    vec4 seeds = vec4(seed, 0.0, 1.0, 2.0);
    return vec3(wood2Hash21(seeds.xy), wood2Hash21(seeds.xz),
                wood2Hash21(seeds.xw)) * 100.0 + 100.0;
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

float wood2Remap01(float value, float minimumValue, float maximumValue)
{
    return clamp((value - minimumValue) /
                 (maximumValue - minimumValue), 0.0, 1.0);
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

struct Wood2Params
{
    float baseScale;
    float roughness;
};
Wood2Params unpackWood2Params()
{
    Wood2Params result;
    result.baseScale = blendedMaterialParams[0];
    result.roughness = blendedMaterialParams[1];
    return result;
}

Material wood2Texture(vec3 worldPos, vec3 normal)
{
    Wood2Params params = unpackWood2Params();
    Material material;
    material.albedo = wood2Colour(worldPos * params.baseScale);
    material.metallic = 0.0;
    material.roughness = params.roughness;
    material.normal = normalize(normal);
    return material;
}

// Each organic material's two knobs, matching their ProcMaterial Technique schemas for
// its index - same discipline as the geology/metal materials above; none of
// these may safely change organicField/organicNormal, which every organic
// material calls.
struct WoodParams
{
    float baseScale;
    float ringScale;
    float normalStrength;
    float roughnessAmt;
    float knotDim;
};
WoodParams unpackWoodParams() {
    WoodParams r;
    r.baseScale = blendedMaterialParams[0];
    r.ringScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.roughnessAmt = blendedMaterialParams[3];
    r.knotDim = blendedMaterialParams[4];
    return r;
}

Material woodTexture(vec3 worldPos, vec3 normal)
{
    WoodParams params = unpackWoodParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = organicField(p, 0);
    float radius = length(p.xz);
    float ring = sin(radius * params.ringScale + fbm(p * 0.55) * 4.5) * 0.5 + 0.5;
    float grain = noise(vec3(p.x * 4.0, p.y * 0.32, p.z * 4.0));
    float knot = 1.0 - smoothstep(0.08, 0.34, geologyVoronoi(p * 1.4));
    material.albedo = mix(vec3(0.16, 0.055, 0.018),
                          vec3(0.58, 0.29, 0.095), ring);
    material.albedo *= mix(0.76, 1.12, grain) * (1.0 - knot * params.knotDim);
    material.metallic = 0.0;
    material.roughness = clamp(0.48 + grain * params.roughnessAmt, 0.42, 0.7);
    material.normal = organicNormal(p, normal, 0, surface, params.normalStrength);
    return material;
}

struct BarkParams
{
    float baseScale;
    float ridgeScale;
    float normalStrength;
    float lichenBlend;
    float roughnessAmt;
};
BarkParams unpackBarkParams() {
    BarkParams r;
    r.baseScale = blendedMaterialParams[0];
    r.ridgeScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.lichenBlend = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material barkTexture(vec3 worldPos, vec3 normal)
{
    BarkParams params = unpackBarkParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = organicField(p, 1);
    float high = smoothstep(0.20, 0.78, surface);
    float lichen = smoothstep(0.67, 0.88, fbm(p * 1.5 + vec3(9.0)));
    material.albedo = mix(vec3(0.055, 0.020, 0.008),
                          vec3(0.30, 0.12, 0.035), high);
    material.albedo = mix(material.albedo, vec3(0.20, 0.27, 0.07), lichen * params.lichenBlend);
    material.metallic = 0.0;
    material.roughness = clamp(0.76 + (1.0 - high) * params.roughnessAmt, 0.7, 0.95);
    material.normal = organicNormal(p, normal, 1, surface, params.normalStrength);
    return material;
}

struct BoneParams
{
    float baseScale;
    float poreScale;
    float normalStrength;
    float poreDarken;
    float roughnessAmt;
};
BoneParams unpackBoneParams() {
    BoneParams r;
    r.baseScale = blendedMaterialParams[0];
    r.poreScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.poreDarken = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material boneTexture(vec3 worldPos, vec3 normal)
{
    BoneParams params = unpackBoneParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = organicField(p, 2);
    float age = fbm(p * 0.48);
    float pores = 1.0 - smoothstep(0.07, 0.23, geologyVoronoi(p * params.poreScale));
    material.albedo = mix(vec3(0.52, 0.43, 0.27),
                          vec3(0.91, 0.84, 0.65), age);
    material.albedo *= 1.0 - pores * params.poreDarken;
    material.metallic = 0.0;
    material.roughness = clamp(0.38 + pores * params.roughnessAmt, 0.34, 0.78);
    material.normal = organicNormal(p, normal, 2, surface, params.normalStrength);
    return material;
}

struct LeatherParams
{
    float baseScale;
    float wearMix;
    float normalStrength;
    float roughnessAmt;
};
LeatherParams unpackLeatherParams() {
    LeatherParams r;
    r.baseScale = blendedMaterialParams[0];
    r.wearMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.roughnessAmt = blendedMaterialParams[3];
    return r;
}

Material leatherTexture(vec3 worldPos, vec3 normal)
{
    LeatherParams params = unpackLeatherParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = organicField(p, 3);
    float cells = geologyVoronoi(p * 5.5);
    float wear = smoothstep(0.62, 0.86, fbm(p * 0.9 + vec3(5.0)));
    material.albedo = mix(vec3(0.09, 0.022, 0.012),
                          vec3(0.34, 0.095, 0.035), cells);
    material.albedo = mix(material.albedo, vec3(0.47, 0.20, 0.08), wear * params.wearMix);
    material.metallic = 0.0;
    material.roughness = clamp(0.50 - wear * params.roughnessAmt + cells * 0.14, 0.32, 0.68);
    material.normal = organicNormal(p, normal, 3, surface, params.normalStrength);
    return material;
}

struct FleshParams
{
    float baseScale;
    float veinMix;
    float normalStrength;
    float roughnessAmt;
};
FleshParams unpackFleshParams() {
    FleshParams r;
    r.baseScale = blendedMaterialParams[0];
    r.veinMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.roughnessAmt = blendedMaterialParams[3];
    return r;
}

Material fleshTexture(vec3 worldPos, vec3 normal)
{
    FleshParams params = unpackFleshParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = organicField(p, 4);
    float mottling = fbm(p * 0.72);
    float vein = abs(sin(p.x * 3.6 - p.y * 1.1 + p.z * 2.3 +
                         fbm(p * 0.5) * 7.0));
    float veins = 1.0 - smoothstep(0.035, 0.16, vein);
    material.albedo = mix(vec3(0.24, 0.025, 0.035),
                          vec3(0.69, 0.24, 0.20), mottling);
    material.albedo = mix(material.albedo, vec3(0.07, 0.025, 0.12), veins * params.veinMix);
    material.metallic = 0.0;
    material.roughness = clamp(0.42 + (1.0 - mottling) * params.roughnessAmt, 0.38, 0.6);
    material.normal = organicNormal(p, normal, 4, surface, params.normalStrength);
    return material;
}

struct ChitinParams
{
    float baseScale;
    float plateScale;
    float normalStrength;
    float metallicAmt;
    float roughnessAmt;
};
ChitinParams unpackChitinParams() {
    ChitinParams r;
    r.baseScale = blendedMaterialParams[0];
    r.plateScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicAmt = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material chitinTexture(vec3 worldPos, vec3 normal)
{
    ChitinParams params = unpackChitinParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = organicField(p, 5);
    float plate = smoothstep(0.12, 0.72, geologyVoronoi(p * params.plateScale));
    float band = sin(dot(p, normalize(vec3(0.7, 0.2, 0.65))) * 9.0 +
                     fbm(p * 0.65) * 3.0) *
                         0.5 +
                     0.5;
    vec3 darkShell = vec3(0.025, 0.018, 0.035);
    vec3 brightShell = mix(vec3(0.14, 0.055, 0.20),
                           vec3(0.035, 0.24, 0.22), band);
    material.albedo = mix(darkShell, brightShell, plate);
    material.metallic = params.metallicAmt;
    material.roughness = clamp(0.16 + (1.0 - plate) * params.roughnessAmt, 0.14, 0.48);
    material.normal = organicNormal(p, normal, 5, surface, params.normalStrength);
    return material;
}

struct CoralParams
{
    float baseScale;
    float poreMix;
    float normalStrength;
    float roughnessAmt;
};
CoralParams unpackCoralParams() {
    CoralParams r;
    r.baseScale = blendedMaterialParams[0];
    r.poreMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.roughnessAmt = blendedMaterialParams[3];
    return r;
}

Material coralTexture(vec3 worldPos, vec3 normal)
{
    CoralParams params = unpackCoralParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = organicField(p, 6);
    float pores = 1.0 - smoothstep(0.10, 0.31, geologyVoronoi(p * 5.2));
    float colonies = fbm(p * 1.1);
    material.albedo = mix(vec3(0.35, 0.055, 0.045),
                          vec3(0.92, 0.38, 0.24), colonies);
    material.albedo = mix(material.albedo, vec3(0.055, 0.018, 0.012), pores * params.poreMix);
    material.metallic = 0.0;
    material.roughness = clamp(0.65 + pores * params.roughnessAmt, 0.58, 0.94);
    material.normal = organicNormal(p, normal, 6, surface, params.normalStrength);
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

// Each supernatural material's two knobs, matching its ProcMaterial Technique
// schema - same discipline as every batch above. None of these may
// safely change supernaturalField/supernaturalNormal, which every
// supernatural material calls; time-driven animation constants
// (GLOBAL_TIME's own coefficients) are left alone rather than made tunable.
struct ArcaneCrystalParams
{
    float baseScale;
    float coreMix;
    float normalStrength;
    float roughnessAmt;
};
ArcaneCrystalParams unpackArcaneCrystalParams() {
    ArcaneCrystalParams r;
    r.baseScale = blendedMaterialParams[0];
    r.coreMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.roughnessAmt = blendedMaterialParams[3];
    return r;
}

Material arcaneCrystalTexture(vec3 worldPos, vec3 normal)
{
    ArcaneCrystalParams params = unpackArcaneCrystalParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = supernaturalField(p, 0);
    float cells = geologyVoronoi(p * 2.8);
    float core = 1.0 - smoothstep(0.08, 0.36, cells);
    float colourShift = fbm(p * 0.45) + @Uniform(GLOBAL_TIME) * 0.025;
    material.albedo = mix(vec3(0.035, 0.10, 0.24),
                          spectralPalette(colourShift), core * params.coreMix);
    material.metallic = 0.16;
    material.roughness = clamp(0.07 + cells * params.roughnessAmt, 0.055, 0.28);
    material.normal = supernaturalNormal(p, normal, 0, surface, params.normalStrength);
    return material;
}

struct EnergyStoneParams
{
    float baseScale;
    float energyScale;
    float normalStrength;
    float roughnessMax;
    float metallicAmt;
};
EnergyStoneParams unpackEnergyStoneParams() {
    EnergyStoneParams r;
    r.baseScale = blendedMaterialParams[0];
    r.energyScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.roughnessMax = blendedMaterialParams[3];
    r.metallicAmt = blendedMaterialParams[4];
    return r;
}

Material energyStoneTexture(vec3 worldPos, vec3 normal)
{
    EnergyStoneParams params = unpackEnergyStoneParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = supernaturalField(p, 1);
    float warp = fbm(p * 0.55) - 0.5;
    float seam = abs(sin(p.x * params.energyScale - p.y * 1.4 + p.z * 2.0 + warp * 8.0));
    float energy = 1.0 - smoothstep(0.035, 0.18, seam);
    material.albedo = mix(vec3(0.018, 0.022, 0.030),
                          vec3(0.025, 0.32, 0.72), energy);
    material.metallic = params.metallicAmt;
    material.roughness = mix(params.roughnessMax, 0.18, energy);
    material.normal = supernaturalNormal(p, normal, 1, surface, params.normalStrength);
    return material;
}

struct AlienTissueParams
{
    float baseScale;
    float cellScale;
    float normalStrength;
    float pulseBlend;
    float roughnessAmt;
};
AlienTissueParams unpackAlienTissueParams() {
    AlienTissueParams r;
    r.baseScale = blendedMaterialParams[0];
    r.cellScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.pulseBlend = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material alienTissueTexture(vec3 worldPos, vec3 normal)
{
    AlienTissueParams params = unpackAlienTissueParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = supernaturalField(p, 2);
    float cells = geologyVoronoi(p * params.cellScale);
    float pulse = sin(@Uniform(GLOBAL_TIME) * 2.2 + fbm(p * 0.55) * 8.0) *
                      0.5 +
                  0.5;
    material.albedo = mix(vec3(0.055, 0.012, 0.075),
                          vec3(0.18, 0.52, 0.16), smoothstep(0.18, 0.72, cells));
    material.albedo = mix(material.albedo, vec3(0.62, 0.08, 0.31), pulse * params.pulseBlend);
    material.metallic = 0.0;
    material.roughness = clamp(0.28 + cells * params.roughnessAmt, 0.24, 0.56);
    material.normal = supernaturalNormal(p, normal, 2, surface, params.normalStrength);
    return material;
}

struct MagicalMetalParams
{
    float baseScale;
    float runeScale;
    float normalStrength;
    float metallicMax;
    float roughnessMax;
};
MagicalMetalParams unpackMagicalMetalParams() {
    MagicalMetalParams r;
    r.baseScale = blendedMaterialParams[0];
    r.runeScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.metallicMax = blendedMaterialParams[3];
    r.roughnessMax = blendedMaterialParams[4];
    return r;
}

Material magicalMetalTexture(vec3 worldPos, vec3 normal)
{
    MagicalMetalParams params = unpackMagicalMetalParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = supernaturalField(p, 3);
    vec3 grid = abs(fract(p * params.runeScale) - 0.5);
    float rune = 1.0 - smoothstep(0.035, 0.10, min(grid.x, min(grid.y, grid.z)));
    float flow = metalField(p, 5);
    material.albedo = mix(vec3(0.08, 0.045, 0.16),
                          vec3(0.58, 0.44, 0.82), flow);
    material.albedo = mix(material.albedo, vec3(0.04, 0.75, 0.92), rune);
    material.metallic = mix(params.metallicMax, 0.35, rune);
    material.roughness = mix(params.roughnessMax, 0.11, rune);
    material.normal = supernaturalNormal(p, normal, 3, surface, params.normalStrength);
    return material;
}

struct CloudSolidParams
{
    float baseScale;
    float densityThreshold;
    float normalStrength;
    float roughnessAmt;
};
CloudSolidParams unpackCloudSolidParams() {
    CloudSolidParams r;
    r.baseScale = blendedMaterialParams[0];
    r.densityThreshold = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.roughnessAmt = blendedMaterialParams[3];
    return r;
}

Material cloudSolidTexture(vec3 worldPos, vec3 normal)
{
    CloudSolidParams params = unpackCloudSolidParams();

    Material material;
    vec3 p = worldPos * params.baseScale +
             vec3(@Uniform(GLOBAL_TIME) * 0.018, 0.0,
                  @Uniform(GLOBAL_TIME) * -0.012);
    float surface = supernaturalField(p, 4);
    float density = smoothstep(params.densityThreshold, 0.78, fbm(p * 0.72));
    material.albedo = mix(vec3(0.18, 0.28, 0.46),
                          vec3(0.92, 0.96, 1.0), density);
    material.metallic = 0.0;
    material.roughness = clamp(0.82 - density * params.roughnessAmt, 0.5, 0.88);
    material.normal = supernaturalNormal(p, normal, 4, surface, params.normalStrength);
    return material;
}

struct HolographicParams
{
    float baseScale;
    float scanScale;
    float normalStrength;
    float fresnelExp;
    float roughnessAmt;
};
HolographicParams unpackHolographicParams() {
    HolographicParams r;
    r.baseScale = blendedMaterialParams[0];
    r.scanScale = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.fresnelExp = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material holographicTexture(vec3 worldPos, vec3 normal, vec3 viewDir)
{
    HolographicParams params = unpackHolographicParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = supernaturalField(p, 5);
    float fresnel = pow(1.0 - max(dot(normalize(normal), viewDir), 0.0), params.fresnelExp);
    float scan = sin(p.y * params.scanScale + @Uniform(GLOBAL_TIME) * 3.0) * 0.5 + 0.5;
    vec3 spectrum = spectralPalette(fresnel * 0.72 + scan * 0.18 +
                                    @Uniform(GLOBAL_TIME) * 0.035);
    material.albedo = mix(vec3(0.025, 0.12, 0.18), spectrum, 0.55 + fresnel * 0.4);
    material.metallic = 0.48;
    material.roughness = clamp(0.10 + scan * params.roughnessAmt, 0.08, 0.24);
    material.normal = supernaturalNormal(p, normal, 5, surface, params.normalStrength);
    return material;
}

struct CorruptionParams
{
    float baseScale;
    float spreadThreshold;
    float normalStrength;
    float tendrilBlend;
    float roughnessMax;
};
CorruptionParams unpackCorruptionParams() {
    CorruptionParams r;
    r.baseScale = blendedMaterialParams[0];
    r.spreadThreshold = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.tendrilBlend = blendedMaterialParams[3];
    r.roughnessMax = blendedMaterialParams[4];
    return r;
}

Material corruptionTexture(vec3 worldPos, vec3 normal)
{
    CorruptionParams params = unpackCorruptionParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = supernaturalField(p, 6);
    float spread = smoothstep(params.spreadThreshold, 0.68,
        fbm(p * 0.58 + vec3(@Uniform(GLOBAL_TIME) * 0.025)));
    float tendril = smoothstep(-0.18, 0.06, -surface);
    material.albedo = mix(vec3(0.025, 0.022, 0.020),
                          vec3(0.20, 0.008, 0.24), spread);
    material.albedo = mix(material.albedo, vec3(0.62, 0.015, 0.42), tendril * params.tendrilBlend);
    material.metallic = spread * 0.12;
    material.roughness = mix(params.roughnessMax, 0.30, spread);
    material.normal = supernaturalNormal(p, normal, 6, surface, params.normalStrength);
    return material;
}

vec3 supernaturalEmission(vec3 worldPos, int materialIndex)
{
    vec3 p = worldPos * 0.68;
    float pulse = sin(@Uniform(GLOBAL_TIME) * 2.4) * 0.5 + 0.5;

    if (materialIndex == 24)
    {
        float core = 1.0 - smoothstep(0.07, 0.28, geologyVoronoi(p * 2.8));
        return spectralPalette(fbm(p * 0.4) + @Uniform(GLOBAL_TIME) * 0.03) *
               core * (0.45 + pulse * 0.35);
    }
    if (materialIndex == 25)
    {
        float warp = fbm(p * 0.55) - 0.5;
        float seam = abs(sin(p.x * 4.2 - p.y * 1.4 + p.z * 2.0 + warp * 8.0));
        float energy = 1.0 - smoothstep(0.035, 0.18, seam);
        return vec3(0.01, 0.42, 1.25) * energy * (0.65 + pulse * 0.55);
    }
    if (materialIndex == 26)
    {
        float cells = 1.0 - smoothstep(0.08, 0.30, geologyVoronoi(p * 3.5));
        return vec3(0.55, 0.03, 0.34) * cells * pulse * 0.75;
    }
    if (materialIndex == 27)
    {
        vec3 grid = abs(fract(p * 1.7) - 0.5);
        float rune = 1.0 - smoothstep(0.035, 0.10, min(grid.x, min(grid.y, grid.z)));
        return vec3(0.02, 0.62, 1.0) * rune * (0.5 + pulse * 0.5);
    }
    if (materialIndex == 29)
    {
        return spectralPalette(p.y * 0.12 + @Uniform(GLOBAL_TIME) * 0.04) * 0.12;
    }
    if (materialIndex == 30)
    {
        float channel = smoothstep(-0.16, 0.04, -supernaturalField(p, 6));
        return vec3(0.72, 0.01, 0.46) * channel * (0.35 + pulse * 0.65);
    }
    return vec3(0.0);
}

// Manufactured surfaces: frosted glass, brick masonry and circuit boards.
// Brick and circuit board are inherently flat, axis-aligned patterns, so
// they build a local tangent frame from the geometric normal rather than
// reading world position along fixed axes. That keeps the coursing/etch
// pattern aligned to whatever surface it lands on without needing
// vertex-supplied UVs or tangents.
void surfaceTangentBasis(vec3 normal, out vec3 tangent, out vec3 bitangent)
{
    vec3 reference = abs(normal.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    tangent = normalize(cross(reference, normal));
    bitangent = cross(normal, tangent);
}

float frostedGlassField(vec3 p)
{
    // Overlapping etched facets: broad frosted cells crossed by fine
    // directional scratches left by the sanding/etching process.
    float facets = geologyVoronoi(p * 8.5);
    float scratches = noise(vec3(p.x * 26.0, p.y * 3.0, p.z * 26.0));
    return (1.0 - smoothstep(0.05, 0.34, facets)) * 0.65 + scratches * 0.35;
}

vec3 frostedGlassNormal(vec3 p, vec3 normal, float field, float strength)
{
    const float epsilon = 0.014;
    vec3 gradient = vec3(
        frostedGlassField(p + vec3(epsilon, 0.0, 0.0)) - field,
        frostedGlassField(p + vec3(0.0, epsilon, 0.0)) - field,
        frostedGlassField(p + vec3(0.0, 0.0, epsilon)) - field) / epsilon;
    vec3 geometricNormal = normalize(normal);
    gradient -= geometricNormal * dot(gradient, geometricNormal);
    return normalize(geometricNormal - gradient * strength);
}

// Frosted glass's two knobs, matching their ProcMaterial Technique schemas for its
// index. frostedGlassField/frostedGlassNormal are only ever called from
// here, so unlike the shared *Field helpers above there is no risk in
// principle to changing them - they are left as-is anyway, so every bind
// point here stays the same simple "local to this function" shape as the
// rest of the file.
struct FrostedGlassParams
{
    float baseScale;
    float frostThreshold;
    float normalStrength;
    float brightAmt;
    float roughnessMax;
};
FrostedGlassParams unpackFrostedGlassParams() {
    FrostedGlassParams r;
    r.baseScale = blendedMaterialParams[0];
    r.frostThreshold = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.brightAmt = blendedMaterialParams[3];
    r.roughnessMax = blendedMaterialParams[4];
    return r;
}

Material frostedGlassTexture(vec3 worldPos, vec3 normal)
{
    FrostedGlassParams params = unpackFrostedGlassParams();

    Material material;
    vec3 p = worldPos * params.baseScale;
    float surface = frostedGlassField(p);
    float frostDensity = smoothstep(params.frostThreshold, 0.82, surface);

    // The renderer has no transmission channel, so the "seen through"
    // quality is approximated with a pale, desaturated albedo whose
    // roughness thins out wherever the etch is sparse.
    vec3 clearTint = vec3(0.85, 0.91, 0.94);
    material.albedo = clearTint * mix(0.78, params.brightAmt, frostDensity);
    material.metallic = 0.0;
    material.roughness = clamp(mix(0.34, params.roughnessMax, frostDensity), 0.28, 0.86);
    material.normal = frostedGlassNormal(p, normal, surface, params.normalStrength);
    return material;
}

// brickHeight is a parameter, not a hardcoded local, because brick_height
// (MATERIAL_PARAMS[1]) has to reach both this and brickTexture's own copy of
// the same layout math consistently. brickReliefField is only ever called
// from brickTexture, so this stays exactly as safe as every function above
// that instead reads MATERIAL_PARAMS directly.
float brickReliefField(vec2 uv, float brickHeight)
{
    float brickWidth = 1.0;
    float mortarWidth = 0.05;
    float row = floor(uv.y / brickHeight);
    float rowOffset = mod(row, 2.0) * brickWidth * 0.5;
    vec2 local = vec2(
        mod(uv.x - rowOffset, brickWidth), mod(uv.y, brickHeight));
    vec2 distanceToEdge = min(local, vec2(brickWidth, brickHeight) - local);
    float mortar = min(distanceToEdge.x, distanceToEdge.y);
    return 1.0 - smoothstep(0.0, mortarWidth, mortar);
}

struct BrickParams
{
    float baseScale;
    float brickHeight;
    float normalStrength;
    float weatherBright;
    float roughnessAmt;
};
BrickParams unpackBrickParams() {
    BrickParams r;
    r.baseScale = blendedMaterialParams[0];
    r.brickHeight = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.weatherBright = blendedMaterialParams[3];
    r.roughnessAmt = blendedMaterialParams[4];
    return r;
}

Material brickTexture(vec3 worldPos, vec3 normal)
{
    BrickParams params = unpackBrickParams();

    Material material;
    vec3 geometricNormal = normalize(normal);
    vec3 tangent, bitangent;
    surfaceTangentBasis(geometricNormal, tangent, bitangent);

    vec3 p = worldPos * params.baseScale;
    vec2 uv = vec2(dot(p, tangent), dot(p, bitangent));

    float brickWidth = 1.0;
    float row = floor(uv.y / params.brickHeight);
    float rowOffset = mod(row, 2.0) * brickWidth * 0.5;
    vec2 cell = vec2(floor((uv.x - rowOffset) / brickWidth), row);

    float mortarMask = brickReliefField(uv, params.brickHeight);
    float shade = floorPatternHash(cell + vec2(4.0, 9.0));
    float weather = noise(vec3(uv * 3.3, shade * 11.0));

    vec3 fired = mix(vec3(0.36, 0.12, 0.075), vec3(0.66, 0.30, 0.16), shade);
    fired *= mix(0.80, params.weatherBright, weather);
    vec3 mortarColour = mix(vec3(0.55, 0.53, 0.49), vec3(0.68, 0.66, 0.62), weather);

    material.albedo = mix(fired, mortarColour, mortarMask);
    material.metallic = 0.0;
    material.roughness = clamp(
        mix(0.58, 0.90, mortarMask) + (weather - 0.5) * params.roughnessAmt, 0.55, 0.94);

    const float epsilon = 0.01;
    float gradientU = brickReliefField(uv + vec2(epsilon, 0.0), params.brickHeight) - mortarMask;
    float gradientV = brickReliefField(uv + vec2(0.0, epsilon), params.brickHeight) - mortarMask;
    vec3 bump = (tangent * gradientU + bitangent * gradientV) / epsilon;
    material.normal = normalize(geometricNormal - bump * params.normalStrength);

    return material;
}

float circuitReliefField(vec2 uv)
{
    vec2 grid = uv * 9.0;
    vec2 cell = floor(grid);
    vec2 local = fract(grid) - 0.5;
    float trace = 1.0 - smoothstep(0.045, 0.10, min(abs(local.x), abs(local.y)));
    float traceActive = step(0.4, floorPatternHash(cell));
    float pad = 1.0 - smoothstep(0.11, 0.17, length(local));
    float padActive = step(0.82, floorPatternHash(cell + vec2(5.0, 2.0)));
    return max(trace * traceActive, pad * padActive);
}

struct CircuitBoardParams
{
    float baseScale;
    float fineTraceMix;
    float normalStrength;
    float copperRoughMax;
    float metallicAmt;
};
CircuitBoardParams unpackCircuitBoardParams() {
    CircuitBoardParams r;
    r.baseScale = blendedMaterialParams[0];
    r.fineTraceMix = blendedMaterialParams[1];
    r.normalStrength = blendedMaterialParams[2];
    r.copperRoughMax = blendedMaterialParams[3];
    r.metallicAmt = blendedMaterialParams[4];
    return r;
}

Material circuitBoardTexture(vec3 worldPos, vec3 normal)
{
    CircuitBoardParams params = unpackCircuitBoardParams();

    Material material;
    vec3 geometricNormal = normalize(normal);
    vec3 tangent, bitangent;
    surfaceTangentBasis(geometricNormal, tangent, bitangent);

    vec3 p = worldPos * params.baseScale;
    vec2 uv = vec2(dot(p, tangent), dot(p, bitangent));

    float coarseTraces = circuitReliefField(uv);
    float fineTraces = circuitReliefField(uv * 2.2 + vec2(11.0, 4.0)) * params.fineTraceMix;
    float copperMask = clamp(max(coarseTraces, fineTraces), 0.0, 1.0);

    vec2 padGrid = uv * 9.0;
    vec2 padCell = floor(padGrid);
    float padActive = step(0.82, floorPatternHash(padCell + vec2(5.0, 2.0)));
    float padLocal = 1.0 - smoothstep(
        0.11, 0.17, length(fract(padGrid) - 0.5));
    float isPad = padActive * padLocal;

    float fleck = step(0.985, noise(vec3(uv * 42.0, 3.0)));
    vec3 solderMask = vec3(0.035, 0.16, 0.075) +
        vec3(0.62, 0.62, 0.58) * fleck * 0.35;
    vec3 copper = mix(
        vec3(0.55, 0.32, 0.09), vec3(0.85, 0.72, 0.35), isPad);

    material.albedo = mix(solderMask, copper, copperMask);
    material.metallic = copperMask * params.metallicAmt;
    material.roughness = clamp(mix(params.copperRoughMax, 0.16, copperMask), 0.14, 0.6);

    const float epsilon = 0.01;
    float gradientU = circuitReliefField(uv + vec2(epsilon, 0.0)) - coarseTraces;
    float gradientV = circuitReliefField(uv + vec2(0.0, epsilon)) - coarseTraces;
    vec3 bump = (tangent * gradientU + bitangent * gradientV) / epsilon;
    material.normal = normalize(geometricNormal - bump * params.normalStrength);

    return material;
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

PbrLighting shadePbr(Material material, vec3 viewDir, vec3 worldPosition,
                     vec3 lightPosition)
{
    // LIGHT_POSITION uses the same X/elevation/Z coordinate system as the
    // interpolated world position. A point light emits equally in every
    // direction; only distance and the receiving surface's angle affect it.
    vec3 toLight = lightPosition - worldPosition;
    float lightDistance = max(length(toLight), 0.0001);
    vec3 lightDirection = toLight / lightDistance;
    float lightAttenuation = playerTorchAttenuation(lightDistance);
    vec3 direct = evaluatePbrLight(
        material, viewDir, lightDirection,
        vec3(14.0) * lightAttenuation);
    // Visibility belongs only to the permanently lit Player Torch's direct
    // term. Ambient illumination below remains present in occluded regions.
    direct *= playerTorchVisibility(
        worldPosition, material.normal, lightDirection);

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

    PbrLighting lighting;
    lighting.direct = direct;
    lighting.ambient = ambientDiffuse + ambientSpecular;
    return lighting;
}

Material plainGreyMaterial(vec3 normal)
{
    Material material;
    material.albedo = vec3(0.5, 0.5, 0.5);
    material.metallic = 0.0;
    material.roughness = 0.0;
    material.normal = normalize(normal);
    return material;
}

Material evaluateMaterial(
    vec3 texturePosition, vec3 normalDir, vec3 viewDir, int materialIndex)
{
    Material material;
    switch (materialIndex)
    {
        case 1: material = marbleTexture(texturePosition, normalDir); break;
        case 2: material = graniteTexture(texturePosition, normalDir); break;
        case 3: material = slateTexture(texturePosition, normalDir); break;
        case 4: material = sandstoneTexture(texturePosition, normalDir); break;
        case 5: material = limestoneTexture(texturePosition, normalDir); break;
        case 6: material = basaltTexture(texturePosition, normalDir); break;
        case 7: material = obsidianTexture(texturePosition, normalDir); break;
        case 8: material = quartzTexture(texturePosition, normalDir); break;
        case 9: material = oreTexture(texturePosition, normalDir); break;
        case 10: material = rustedIronTexture(texturePosition, normalDir); break;
        case 11: material = galvanizedSteelTexture(texturePosition, normalDir); break;
        case 12: material = brushedMetalTexture(texturePosition, normalDir); break;
        case 13: material = hammeredMetalTexture(texturePosition, normalDir); break;
        case 14: material = patinatedCopperTexture(texturePosition, normalDir); break;
        case 15: material = damasceneSteelTexture(texturePosition, normalDir); break;
        case 16: material = heatTreatedMetalTexture(texturePosition, normalDir); break;
        case 17: material = woodTexture(texturePosition, normalDir); break;
        case 18: material = barkTexture(texturePosition, normalDir); break;
        case 19: material = boneTexture(texturePosition, normalDir); break;
        case 20: material = leatherTexture(texturePosition, normalDir); break;
        case 21: material = fleshTexture(texturePosition, normalDir); break;
        case 22: material = chitinTexture(texturePosition, normalDir); break;
        case 23: material = coralTexture(texturePosition, normalDir); break;
        case 24: material = arcaneCrystalTexture(texturePosition, normalDir); break;
        case 25: material = energyStoneTexture(texturePosition, normalDir); break;
        case 26: material = alienTissueTexture(texturePosition, normalDir); break;
        case 27: material = magicalMetalTexture(texturePosition, normalDir); break;
        case 28: material = cloudSolidTexture(texturePosition, normalDir); break;
        case 29: material = holographicTexture(texturePosition, normalDir, viewDir); break;
        case 30: material = corruptionTexture(texturePosition, normalDir); break;
        case 31: material = frostedGlassTexture(texturePosition, normalDir); break;
        case 32: material = brickTexture(texturePosition, normalDir); break;
        case 33: material = circuitBoardTexture(texturePosition, normalDir); break;
        case 34: material = bandedGneissTexture(texturePosition, normalDir); break;
        case 35: material = rockTexture(texturePosition, normalDir); break;
        case 36: material = mossyRockTexture(texturePosition, normalDir); break;
        case 37: material = wetRockTexture(texturePosition, normalDir); break;
        case 38: material = wood2Texture(texturePosition, normalDir); break;
        case 0: material = plainGreyMaterial(normalDir); break;
        case 39: // BW_WALL_BACK_FACE_MATERIAL_INDEX (Defines.h): a plain
                 // white matte surface for the unmapped side of a wall.
            material.albedo = vec3(1.0, 1.0, 1.0);
            material.metallic = 0.0;
            material.roughness = 0.85;
            material.normal = normalDir;
            break;
        default:
            material.albedo = vec3(1.0, 0.0, 1.0);
            material.metallic = 0.0;
            material.roughness = 0.7;
            material.normal = normalDir;
            break;
    }
    return material;
}

// Shared by the 3D and horizontal PBR programs. Horizontal batches always
// bind WALL_NORMAL_MAP_ENABLED=0; keeping the complete contract here and in
// world_pbr_2d.frag prevents the two world pipelines from drifting.
vec2 wallImageUv()
{
    // Linear tangent space: +X follows increasing physical wall U
    // (orientation.v0 to orientation.v1), +Y follows increasing world
    // elevation, and +Z points outward. In renderer coordinates that U axis
    // is cross(wallNormal, worldUp), not the opposite cross product.
    vec2 uv = @In(TEXCOORDS);
    // Units per repeat specifies the image's world-space width. Preserve the
    // source image's natural proportions when deriving its world-space height.
    // The wall mask deliberately receives these same scaled UVs - it has no
    // aspect-ratio correction of its own.
    uv.y *= @Uniform(WALL_NORMAL_MAP_ASPECT_RATIO);
    return uv;
}

vec3 applyWallNormalMap(vec3 geometricNormal)
{
    vec3 surfaceNormal = normalize(geometricNormal);
    if (@Uniform(WALL_NORMAL_MAP_ENABLED) == 0)
        return surfaceNormal;

    vec3 sampled = texture(
        @Texture(TEX1), wallImageUv()).rgb * 2.0 - 1.0;
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

// One channel of the mask image, sampled with the shared wall image UVs,
// drives the blend weight. Unmasked walls bind a 1x1 zero mask texture and
// pass the primary parameters as the blend set, so this path never branches
// on mask availability: the enabled flag and the zero texture both drive the
// weight to zero for a wall without a mask.
float wallMaskWeight()
{
    vec4 maskSample = texture(@Texture(TEX2), wallImageUv());
    int channel = clamp(@Uniform(WALL_MASK_CHANNEL), 0, 3);
    float value = channel == 0 ? maskSample.r
                : channel == 1 ? maskSample.g
                : channel == 2 ? maskSample.b
                : maskSample.a;
    return value * float(@Uniform(WALL_MASK_ENABLED));
}

// Interpolate MATERIAL_PARAMS toward WALL_MASK_BLEND_PARAMS per fragment,
// before the single material evaluation in main().
void blendMaterialParams()
{
    float weight = wallMaskWeight();
    for (int i = 0; i < 8; ++i)
    {
        blendedMaterialParams[i] = mix(
            @Uniform(MATERIAL_PARAMS[i]),
            @Uniform(WALL_MASK_BLEND_PARAMS[i]),
            weight);
    }
    blendedMaterialColour = mix(
        @Uniform(MATERIAL_COLOUR),
        @Uniform(WALL_MASK_BLEND_COLOUR),
        weight);
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
    int bucketMaterialIndex = clamp(@Uniform(MATERIAL_INDEX), 0, 40);

    // Liquid is an interface, not another lit volume. The water pass has no
    // depth attachment, so reject interfaces hidden by the sampled opaque
    // depth before marching that same point-sampled buffer.
    if (bucketMaterialIndex == 40)
    {
        vec2 screenUv = gl_FragCoord.xy * VIEWPORT_SIZE.zw;
        bool hasWaterPass = @Uniform(LIQUID_WATER_PASS_ENABLED) != 0;
        bool reflectionEnabled = hasWaterPass &&
            @Uniform(LIQUID_REFLECTION_ENABLED) != 0;
        bool planarReflection =
            @Uniform(MPP_WATER_REFLECTION_TECHNIQUE) != 0;
        if (hasWaterPass && gl_FragCoord.z > liquidSceneDepth(screenUv))
            discard;

        vec3 worldPos = @In(FRAGPOSITION);
        vec3 viewDir = normalize(@ViewPos - worldPos);
        vec3 interfaceNormal = normalize(@In(FRAGNORMAL));
        if (dot(interfaceNormal, viewDir) < 0.0)
            interfaceNormal = -interfaceNormal;
        interfaceNormal = liquidRippleNormal(worldPos, interfaceNormal);

        // The same distorted, viewer-facing normal drives both the reflected
        // ray and the two-sided Schlick response.
        float nDotV = clamp(dot(interfaceNormal, viewDir), 0.0, 1.0);
        float f0 = clamp(@Uniform(LIQUID_F0), 0.0, 1.0);
        float schlick = f0 + (1.0 - f0) * pow(1.0 - nDotV, 5.0);
        // F5's generic toggle removes the reflected interface, including the
        // miss fallback, while leaving depth rejection and absorbed scene
        // colour beneath the interface intact under either technique.
        float alpha = reflectionEnabled
            ? clamp(@Uniform(LIQUID_REFLECTANCE), 0.0, 1.0) * schlick
            : 0.0;

        vec3 viewPosition = vec3(VIEW_MATRIX * vec4(worldPos, 1.0));
        vec3 viewNormal = normalize(mat3(VIEW_MATRIX) * interfaceNormal);
        vec3 hitColour = vec3(0.0);
        float confidence = 0.0;
        if (reflectionEnabled && planarReflection)
        {
            int planarIndex = -1;
            mat4 reflectedViewProjection = mat4(1.0);
            int planarCount = @Uniform(MPP_PLANAR_REFLECTION_COUNT);
            if (planarCount > 0 &&
                liquidSurfaceHeight >=
                    @Uniform(MPP_PLANAR_REFLECTION_MINIMUM_ELEVATION_0) &&
                liquidSurfaceHeight <=
                    @Uniform(MPP_PLANAR_REFLECTION_MAXIMUM_ELEVATION_0))
            {
                planarIndex = 0;
                reflectedViewProjection =
                    @Uniform(MPP_PLANAR_REFLECTION_VIEW_PROJECTION_0);
            }
            else if (planarCount > 1 &&
                liquidSurfaceHeight >=
                    @Uniform(MPP_PLANAR_REFLECTION_MINIMUM_ELEVATION_1) &&
                liquidSurfaceHeight <=
                    @Uniform(MPP_PLANAR_REFLECTION_MAXIMUM_ELEVATION_1))
            {
                planarIndex = 1;
                reflectedViewProjection =
                    @Uniform(MPP_PLANAR_REFLECTION_VIEW_PROJECTION_1);
            }
            else if (planarCount > 2 &&
                liquidSurfaceHeight >=
                    @Uniform(MPP_PLANAR_REFLECTION_MINIMUM_ELEVATION_2) &&
                liquidSurfaceHeight <=
                    @Uniform(MPP_PLANAR_REFLECTION_MAXIMUM_ELEVATION_2))
            {
                planarIndex = 2;
                reflectedViewProjection =
                    @Uniform(MPP_PLANAR_REFLECTION_VIEW_PROJECTION_2);
            }
            else if (planarCount > 3 &&
                liquidSurfaceHeight >=
                    @Uniform(MPP_PLANAR_REFLECTION_MINIMUM_ELEVATION_3) &&
                liquidSurfaceHeight <=
                    @Uniform(MPP_PLANAR_REFLECTION_MAXIMUM_ELEVATION_3))
            {
                planarIndex = 3;
                reflectedViewProjection =
                    @Uniform(MPP_PLANAR_REFLECTION_VIEW_PROJECTION_3);
            }

            vec4 reflectedClip = reflectedViewProjection *
                vec4(worldPos, 1.0);
            bool selectedElevation = planarIndex >= 0;
            bool validProjection = selectedElevation && reflectedClip.w > 0.0 &&
                reflectedClip.z >= -reflectedClip.w &&
                reflectedClip.z <= reflectedClip.w;
            vec2 hitUv = reflectedClip.xy / max(reflectedClip.w, 0.00001) *
                0.5 + 0.5;
            // The same ripple normal that perturbs the Fresnel response and SSR
            // ray bends the projected Planar lookup across the interface.
            hitUv += liquidPlanarRippleOffset(interfaceNormal);
            vec4 planarSample = vec4(0.0);
            if (planarIndex == 0)
                planarSample = texture(@Texture(PBR_PLANAR_REFLECTION_0), hitUv);
            else if (planarIndex == 1)
                planarSample = texture(@Texture(PBR_PLANAR_REFLECTION_1), hitUv);
            else if (planarIndex == 2)
                planarSample = texture(@Texture(PBR_PLANAR_REFLECTION_2), hitUv);
            else if (planarIndex == 3)
                planarSample = texture(@Texture(PBR_PLANAR_REFLECTION_3), hitUv);
            vec2 edgeDistance = min(hitUv, vec2(1.0) - hitUv);
            float edgeFade = clamp(
                min(edgeDistance.x, edgeDistance.y) / 0.04, 0.0, 1.0);
            confidence = validProjection
                ? clamp(planarSample.a * edgeFade, 0.0, 1.0)
                : 0.0;
            hitColour = planarSample.rgb;
        }
        else if (reflectionEnabled)
        {
            vec3 reflectionDirection = normalize(
                reflect(normalize(viewPosition), viewNormal));
            vec2 hitUv;
            confidence = liquidMarch(
                viewPosition, reflectionDirection, hitUv);

            // Trust dies continuously at unstable silhouettes, screen exits,
            // and grazing angles. Every miss reaches ambient fallback.
            vec2 edgeDistance = min(hitUv, vec2(1.0) - hitUv);
            confidence *= clamp(
                min(edgeDistance.x, edgeDistance.y) / 0.1, 0.0, 1.0);
            confidence *= smoothstep(0.1, 0.35, nDotV);
            confidence = clamp(confidence, 0.0, 1.0);
            hitColour = textureLod(
                @Texture(PBR_SCENE_COLOUR_RESOLVED), hitUv,
                clamp(@Uniform(LIQUID_REFLECTION_MIP_LEVEL), 0.0, 4.0)).rgb;
        }

        float fragmentDistance = length(@Uniform(LIGHT_POSITION) - worldPos);
        float fadeToBlack = pow(clamp(
            1.0 - fragmentDistance / @Uniform(VIEW_DISTANCE), 0.0, 1.0),
            1.7);
        vec3 fallback = vec3(0.12) *
            @Uniform(LIQUID_AMBIENT_TINT) * fadeToBlack;
        vec3 reflectionColour = mix(fallback, hitColour, confidence);

        // Fixed-function alpha blending overlays reflection over the absorption
        // already in WaterComposite. Do not light or absorb this interface a
        // second time: alpha is exactly reflectance × Schlick(F0, N·V).
        @Out(vec4 COLOUR) = vec4(reflectionColour, alpha);
        @Out(vec4 BLOOM_MASK) = vec4(0.0);
        @Out(vec2 SHADING_NORMAL) = encodeOctahedralNormal(viewNormal);
        @Out(float LIQUID_RETENTION) = 1.0;
        return;
    }

    // Fade every contribution to black at the world-view boundary. This is
    // intentionally separate from the Torch's configurable direct-light
    // attenuation: ambient and emissive terms must disappear there too.
    float fragmentDistance = length(
        @Uniform(LIGHT_POSITION) - @In(FRAGPOSITION));
    float fadeToBlack = pow(clamp(
        1.0 - fragmentDistance / @Uniform(VIEW_DISTANCE), 0.0, 1.0), 1.7);

    vec3 shadingNormal = normalize(@In(FRAGNORMAL));
    vec3 viewDir = normalize(@ViewPos - @In(FRAGPOSITION));
    vec3 normalDir = applyWallNormalMap(shadingNormal);
    vec3 texturePosition = snapToGrid(
        @In(FRAGPOSITION) / @Uniform(MATERIAL_SCALE),
        @Uniform(PIXEL_SIZE));
    int materialIndex = floorMaterialIndex(
        @In(FRAGPOSITION), clamp(@Uniform(MATERIAL_INDEX), 0, 39));
    materialIndex = clamp(materialIndex, 0, 39);
    blendMaterialParams();
    Material material = evaluateMaterial(
        texturePosition, normalDir, viewDir, materialIndex);

    // The blended base colour tints the surface's own colour before any
    // lighting; the per-vertex tint (white unless the editor is marking a
    // surface out) is applied on top and leaves the material untouched when
    // white.
    material.albedo *= blendedMaterialColour * @In(COLOUR).rgb;

    // Whatever this material embosses, on whatever surface it was
    // applied to - floor, ceiling or wall.
    if (@Uniform(EMBOSS_PATTERN) != 0)
    {
        material.normal = embossSurface(
            material.normal, @In(FRAGPOSITION),
            @Uniform(EMBOSS_RADIUS), @Uniform(EMBOSS_DEPTH),
            @Uniform(EMBOSS_PATTERN),
            @Uniform(EMBOSS_RUNNING_BOND_WIDTH),
            @Uniform(EMBOSS_RUNNING_BOND_OFFSET));
    }

    // Cook-Torrance PBR lighting with GGX distribution, Smith geometry
    // masking and Schlick Fresnel.
    shadingNormal = material.normal;
    PbrLighting lighting = shadePbr(
        material, viewDir, @In(FRAGPOSITION),
        @Uniform(LIGHT_POSITION));
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
    @Out(COLOUR) = vec4(value * fadeToBlack, @In(COLOUR).a);
    @Out(BLOOM_MASK) = vec4(0.0);
    @Out(SHADING_NORMAL) = encodeOctahedralNormal(
        normalize(mat3(VIEW_MATRIX) * shadingNormal));
    @Out(LIQUID_RETENTION) = dot(outTransmittance, vec3(1.0 / 3.0));
##
}
