@@Version

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
    @Out(vec4 COLOUR) = @In(COLOUR);
    @Out(float LIQUID_SURFACE_HEIGHT) = @In(LIQUID_SURFACE_HEIGHT);

    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}