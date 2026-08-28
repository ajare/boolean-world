@@Version

@@Texture(sampler2D TEX1);

void main()
{
    // One visible fragment is necessary rendering, not overdraw. Standard
    // source-alpha accumulation contributes 0.12 red for that first fragment;
    // remove that baseline so only fragments after the first remain visible.
    const float alpha = 0.12;
    vec4 accumulated = texture(@Texture(TEX1), @In(TEXCOORDS));
    float excess = max(accumulated.r - alpha, 0.0) / (1.0 - alpha);
    @Out(vec4 COLOUR) = vec4(excess, excess * 0.12, excess * 0.02, 1.0);
}
