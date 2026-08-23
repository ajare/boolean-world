@@Version

@@Texture(sampler2D TEX1);

void main()
{
    vec2 centred = @In(TEXCOORDS) * 2.0 - 1.0;
    float distanceFromCentre = dot(centred, centred);
    float edgeMask = smoothstep(0.30, 1.45, distanceFromCentre);
    float vignette = 1.0 - edgeMask * 0.58;

    vec4 sceneColour = texture(@Texture(TEX1), @In(TEXCOORDS));
    @Out(vec4 COLOUR) = vec4(sceneColour.rgb * vignette, sceneColour.a);
}
