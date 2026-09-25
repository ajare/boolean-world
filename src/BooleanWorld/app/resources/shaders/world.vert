@@Version

@@Uniform(int PORTAL_VIEW_ENABLED);

// Pool elevation identifies a Planar reflection group using exact bounds.
// Keep it per-primitive: smooth interpolation can perturb even equal vertex
// values and make individual fragments fall back instead of reflecting.
// MPP's @Out declaration has no interpolation-qualifier syntax, so use a
// native GLSL varying. Reserve location 6 after the six @Out fields below;
// leaving it implicit can collide with MPP's explicitly located outputs.
// Both world fragment shaders must use the same reserved location.
layout(location = 6) flat out float liquidSurfaceHeight;
layout(location = 7) flat out vec2 wallData;
layout(location = 8) out vec2 portalClipDepth;

void main()
{
    // @VecN(...) resolves a single bare in/out/uniform/texture token, not an
    // arbitrary expression: its cast regex stops at the first ')', so
    // @Vec3(@MMatrix * @Vec4(...)) is reported as an unknown variable. The
    // generated GLSL still comes out correct, and the runtime never inspects
    // the parser's error list, but spelling it out plainly avoids the bogus
    // error for any consumer that does check. Lines below wrap one token each.
    @Out(vec3 FRAGPOSITION) = vec3(@MMatrix * vec4(@In(POSITION), 1.0));
    @Out(vec3 FRAGNORMAL) = normalize(@NormalMatrix * @Vec3(@In(NORMAL)));
    @Out(vec2 TEXCOORDS) = @In(TEXCOORDS).xy;
    wallData = @In(TEXCOORDS).zw;
    vec4 surfaceData = @In(USER);
    @Out(vec3 SURFACE_UP) = normalize(@NormalMatrix * surfaceData.xyz);
    @Out(vec3 PROJECTION_NORMAL) =
        normalize(@NormalMatrix * surfaceData.xyz);
    @Out(vec4 COLOUR) = @In(COLOUR);
    liquidSurfaceHeight = surfaceData.w;

    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
    portalClipDepth = gl_Position.zw;
    if (@Uniform(PORTAL_VIEW_ENABLED) == 2)
    {
        // Keep the aperture covering the view as the eye reaches its plane.
        // Ordinary near clipping removes it before traversal has occurred.
        if (abs(dot(@Out(FRAGNORMAL), @ViewPos - @Out(FRAGPOSITION))) < 0.00001)
            gl_Position = @MCPMatrix * vec4(@In(POSITION) - @In(NORMAL) * 0.0001, 1.0);
        // Clip XY/W normally, but defer near-depth clamping to fragments.
        // Clamping each vertex separately breaks apertures crossing the eye
        // plane at an oblique angle.
        portalClipDepth = gl_Position.zw;
        gl_Position.z = -gl_Position.w;
    }
}