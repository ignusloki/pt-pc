// Bounded Catmull-Rom in linear light: retain texture detail without negative
// HDR ringing. For minification integrate four locations in the warped footprint.
vec4 LensCubicWeights(float f) {
    return vec4(-0.5*f + f*f - 0.5*f*f*f, 1.0-2.5*f*f+1.5*f*f*f,
                0.5*f+2.0*f*f-1.5*f*f*f, -0.5*f*f+0.5*f*f*f);
}
float LensSample(uint source, vec2 uv, vec2 size, float dispersion, int channel) {
    vec2 q = LensSource(uv, size, dispersion);
    vec2 dx = dFdx(q), dy = dFdy(q);
    float footprint = max(length(dx*size), length(dy*size));
    float area=0.0;
    if (footprint > 1.1) {
        float sum = 0.0;
        for (int i=0; i<4; ++i) {
            vec2 offset = ((i&1)==0 ? -0.25 : 0.25)*dx + ((i&2)==0 ? -0.25 : 0.25)*dy;
            sum += OpticalLinearSample(source, q+offset)[channel];
        }
        area=sum*0.25;
        if(footprint>=1.4) return area;
    }
    vec2 p = q*size-0.5, f=fract(p);
    ivec2 base = ivec2(floor(p)), hi=ivec2(size)-1;
    vec4 wx=LensCubicWeights(f.x), wy=LensCubicWeights(f.y);
    float sum=0.0, low=1e30, high=0.0;
    for(int y=0;y<4;++y) for(int x=0;x<4;++x) {
        float c=SrgbDecode(max(ImgFetch(source,clamp(base+ivec2(x-1,y-1),ivec2(0),hi)).rgb,vec3(0)))[channel];
        sum+=c*wx[x]*wy[y];
        if(x>=1 && x<=2 && y>=1 && y<=2) {low=min(low,c);high=max(high,c);}
    }
    // Blend footprint regimes continuously; avoid a visible ring at the switch.
    return mix(clamp(sum,low,high),area,smoothstep(1.1,1.4,footprint));
}
