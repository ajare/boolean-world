@@Version

void main()
{
    vec4 transformedVertex = @MCPMatrix * @Vec4(@In(POSITION));
    vec2 centredPosition = vec2(
        transformedVertex.x - @HalfWindowSize.x,
        transformedVertex.y - @HalfWindowSize.y);

    @Out(vec2 TEXCOORDS) = @In(TEXCOORDS);
    gl_Position = vec4(centredPosition / @HalfWindowSize, 0.0, 1.0);
}
