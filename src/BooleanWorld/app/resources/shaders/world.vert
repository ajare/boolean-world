@@Version

// Pool elevation identifies a Planar reflection group using exact bounds.
// Keep it per-primitive: smooth interpolation can perturb even equal vertex
// values and make individual fragments fall back instead of reflecting.
// MPP's @Out declaration has no interpolation-qualifier syntax, so use a
// native GLSL varying. Reserve location 5 after the five @Out fields below;
// leaving it implicit can collide with MPP's explicitly located outputs.
// Both world fragment shaders must use the same reserved location.
layout(location = 5) flat out float liquidSurfaceHeight;

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
    @Out(vec2 TEXCOORDS) = @In(TEXCOORDS);
    vec4 surfaceData = @In(USER);
    @Out(vec3 SURFACE_UP) = normalize(@NormalMatrix * surfaceData.xyz);
    @Out(vec4 COLOUR) = @In(COLOUR);
    liquidSurfaceHeight = surfaceData.w;

    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}