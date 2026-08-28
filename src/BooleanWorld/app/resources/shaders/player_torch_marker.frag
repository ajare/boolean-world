@@Version

void main()
{
    // This is a position marker, not a surface lit by the torch it encloses.
    @Out(vec4 COLOUR) = vec4(1.0, 1.0, 0.0, 1.0);
    @Out(vec4 BLOOM_MASK) = vec4(0.0);
    @Out(vec2 SHADING_NORMAL) = vec2(0.5);
}
