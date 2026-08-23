@@Version

@@Texture(sampler2D TEX1);

@@Uniform(vec3 VIGNETTE_COLOUR);
@@Uniform(float VIGNETTE_STRENGTH);
@@Uniform(float VIGNETTE_INNER_RADIUS);
@@Uniform(float VIGNETTE_FALLOFF_WIDTH);

void main()
{
    vec2 centred = @In(TEXCOORDS) * 2.0 - 1.0;
    float distanceFromCentre = length(centred);
    float innerRadius = max(@Uniform(VIGNETTE_INNER_RADIUS), 0.0);
    float falloffWidth = max(@Uniform(VIGNETTE_FALLOFF_WIDTH), 0.001);
    float edgeMask = smoothstep(
        innerRadius, innerRadius + falloffWidth, distanceFromCentre);
    float vignetteAmount =
        edgeMask * clamp(@Uniform(VIGNETTE_STRENGTH), 0.0, 1.0);

    vec4 sceneColour = texture(@Texture(TEX1), @In(TEXCOORDS));
    vec3 vignetteColour = mix(
        sceneColour.rgb, @Uniform(VIGNETTE_COLOUR), vignetteAmount);
    @Out(vec4 COLOUR) = vec4(vignetteColour, sceneColour.a);
}
