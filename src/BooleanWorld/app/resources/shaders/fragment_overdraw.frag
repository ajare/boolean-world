@@Version

void main()
{
    // Standard alpha blending accumulates this low-opacity contribution. A
    // fragment reached once is dark red; repeated rasterization gets steadily
    // brighter without evaluating a material, texture, shadow, or light.
    @Out(vec4 COLOUR) = vec4(1.0, 0.12, 0.02, 0.12);
}
