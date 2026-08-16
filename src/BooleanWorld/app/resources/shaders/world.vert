@@Version

void main()
{
    @Out(vec3 FRAGPOSITION) = @Vec3(@MMatrix * @Vec4(@In(POSITION)));
    @Out(vec3 NORMAL) = normalize(@NormalMatrix * @Vec3(@In(NORMAL)));
    @Out(vec2 TEXCOORDS) = @In(TEXCOORDS);
    @Out(vec4 COLOUR) = @In(COLOUR);

    gl_Position = @MCPMatrix * @Vec4(@In(POSITION));
}