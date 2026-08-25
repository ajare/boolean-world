@@Version

void main()
{
    // @VecN(...) only resolves a single bare in/out/uniform/texture token, not
    // an arbitrary expression - @Vec3(@MMatrix * @Vec4(...)) mis-parses (the
    // parser's cast regex stops at the first ')', i.e. @Vec4's, not @Vec3's).
    // Line 6/10 below wrap a single token each and are fine as-is.
    @Out(vec3 FRAGPOSITION) = vec3(@MMatrix * vec4(@In(POSITION), 1.0));
    @Out(vec3 FRAGNORMAL) = normalize(@NormalMatrix * @Vec3(@In(NORMAL)));
    @Out(vec2 TEXCOORDS) = @In(TEXCOORDS);
    @Out(vec4 COLOUR) = @In(COLOUR);

    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}