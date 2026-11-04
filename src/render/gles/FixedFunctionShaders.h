// GLSL ES 3.0 equivalent of the D3D11 fixed-function pipeline.
#pragma once
namespace fut::render::xd3d {
inline constexpr char kVertexShader[]=R"GLSL(#version 300 es
precision highp float;
precision highp int;
struct Light {
    vec4 diffuse, specular, ambient;
    vec4 pos;      // view space, w: type (1.0 point, 2.0 spot, 3.0 directional)
    vec4 dir;      // view space, w: range
    vec4 att;      // a0, a1, a2, falloff
    vec4 spot;     // cos(theta / 2.0), cos(phi / 2.0)
};
layout(std140) uniform Draw {
    mat4 world, view, proj;
    mat4 texm[4];
    vec4 vp;            // x, y, width, height of the viewport
    vec4 ssOffset;      // the screen-space offset (xy)
    vec4 matDiffuse, matAmbient, matSpecular, matEmissive;
    vec4 ambient;       // RS AMBIENT
    vec4 misc0;         // power, lighting, colorVertex, normalizeNormals
    vec4 misc1;         // diffuse / ambient / specular / emissive material sources (0.0 material, 1.0 COLOR1, 2.0 COLOR2)
    vec4 misc2;         // hasNormal, hasDiffuse, hasSpecular, rhw
    vec4 misc3;         // specularEnable, localViewer, fogEnable, fog mode (0.0 linear 0x2601, 1.0 exp 0x800, 2.0 exp2 0x801)
    vec4 fogParams;     // K0, K1 (the XDK's fog constants), -, generation (0.0 specular alpha, 1.0 radial, 2.0 planar)
    vec4 fogColor;
    vec4 tfactor;
    vec4 alphaTest;     // enable, func (0.0 never .. 7.0 always), ref, flat shading
    ivec4 tci[4];          // coordinate set, generation (0.0 pass, 1.0 normal, 2.0 position, 3.0 reflection), count, projected
    ivec4 colorOp[4];      // op, arg1, arg2, arg0
    ivec4 alphaOp[4];
    ivec4 stageMisc[4];    // resultArg (1.0 current, 5.0 temp), texture bound, set dimension
    ivec4 lightCount;
    Light lights[8];
    vec4 texScale[4];   // uv scale (a linear texture's texel coordinates: 1.0 / size), z: the YUV conversion
};
struct VSIn {
    vec4 pos; vec4 nrm; vec4 d0; vec4 d1;
    vec4 uv0; vec4 uv1; vec4 uv2; vec4 uv3;
};
struct VSOut {
    vec4 pos;
    vec4 d0; vec4 d1;
    vec4 fd0; vec4 fd1;
    vec4 fog;
    vec4 t0; vec4 t1; vec4 t2; vec4 t3;
};


float saturate(float v) { return clamp(v,0.0,1.0); }
vec3 saturate(vec3 v) { return clamp(v,0.0,1.0); }
vec4 saturate(vec4 v) { return clamp(v,0.0,1.0); }
layout(location=0) in vec4 a_pos;
layout(location=1) in vec3 a_nrm;
layout(location=2) in vec4 a_d0;
layout(location=3) in vec4 a_d1;
layout(location=4) in vec4 a_uv0;
layout(location=5) in vec4 a_uv1;
layout(location=6) in vec4 a_uv2;
layout(location=7) in vec4 a_uv3;
out vec4 v_d0;
out vec4 v_d1;
flat out vec4 v_fd0;
flat out vec4 v_fd1;
out vec4 v_fog;
out vec4 v_t0;
out vec4 v_t1;
out vec4 v_t2;
out vec4 v_t3;
vec4 Source(int s, vec4 vertexColor, vec4 material) {
    return s == 1 && misc2.y > 0.5 ? vertexColor : s == 2 && misc2.z > 0.5 ? vertexColor : material;
}

vec4 TexCoord(int stage, vec4 uv[4], vec3 pv, vec3 nv) {
    ivec4 c = tci[stage];
    vec4 t;
    if (c.y == 1) t = vec4(nv, 1.0);
    else if (c.y == 2) t = vec4(pv, 1.0);
    else if (c.y == 3) { vec3 e = normalize(pv); t = vec4(e - 2.0 * dot(nv, e) * nv, 1.0); }
    else {
        t = uv[min(c.x, 3)];
        if (c.z > 0) {                                 // a passthrough set with a transform: (u, v, 1.0, 0.0) (L)
            int dim = stageMisc[stage].z;
            if (dim == 1) t = vec4(t.x, 1.0, 0.0, 0.0);
            else if (dim == 2) t = vec4(t.xy, 1.0, 0.0);
            else if (dim == 3) t = vec4(t.xyz, 1.0);
        }
    }
    if (c.z > 0) {
        vec4 r = texm[stage] * t;
        vec4 o = vec4(0.0, 0.0, 0.0, 1.0);
        o.x = r.x;
        if (c.z >= 2) o.y = r.y;
        if (c.z >= 3) o.z = r.z;
        if (c.z >= 4) o.w = r.w;
        if (c.w != 0) {                                // PROJECTED: divided by the last component (in the PS)
            float q = c.z == 2 ? r.y : c.z == 3 ? r.z : r.w;
            o = vec4(o.xy, 0.0, q);
        } else {
            o.w = 1.0;
        }
        return o;
    }
    return vec4(t.xy, t.z, 1.0);
}

VSOut VSMain(VSIn i) {
    VSOut o;
    vec3 pv = vec3(0.0), nv = vec3(0.0, 0.0, 1.0);
    // The screen-space offset: D3DDevice_SetScreenSpaceOffset 0x00235590 stores 0.53125 ([0x00242250]) + (x, y) and the
    // viewport constants 0x00238000 subtract 0.0 (0.5 only in a multisample mode; the game's surfaces are not
    // multisampled, 0x00146210: 0x11): the NV2A samples at pixel centres (+0.5, as D3D11), so the geometry moves by
    // 0.53125 + (x, y) pixels: D3D8's integer pixel centres plus 1.0 / 32.0 that keeps edges on integer coordinates off the
    // sample points (without it thin quads such as NiOutline's edge strips break up into dots).
    if (misc2.w > 0.5) {                               // XYZRHW: screen pixels
        float w = i.pos.w != 0.0 ? 1.0 / i.pos.w : 1.0;
        vec2 s = i.pos.xy + ssOffset.xy + 0.53125 - vp.xy;
        o.pos = vec4((s.x / vp.z * 2.0 - 1.0) * w, (1.0 - s.y / vp.w * 2.0) * w, i.pos.z * w, w);
        pv = vec3(i.pos.xy, i.pos.z);
    } else {
        vec4 pw = world * vec4(i.pos.xyz, 1.0);
        vec4 v = view * pw;
        vec4 c = proj * v;
        c.x += c.w / vp.z * 2.0 * (0.53125 + ssOffset.x);   // the screen-space offset above, in clip units
        c.y -= c.w / vp.w * 2.0 * (0.53125 + ssOffset.y);
        o.pos = c;
        pv = v.xyz;
        if (misc2.x > 0.5) {
            nv = (view * world * vec4(i.nrm.xyz, 0.0)).xyz;
            if (misc0.w > 0.5) nv = normalize(nv);
        }
    }
    vec4 cd = misc2.y > 0.5 ? i.d0 : vec4(1.0, 1.0, 1.0, 1.0);
    vec4 cs = misc2.z > 0.5 ? i.d1 : vec4(0.0, 0.0, 0.0, 0.0);
    if (misc0.y > 0.5 && misc2.w < 0.5) {
        bool cv = misc0.z > 0.5;
        vec4 md = cv ? Source(int(misc1.x), misc1.x == 2.0 ? cs : cd, matDiffuse) : matDiffuse;
        vec4 ma = cv ? Source(int(misc1.y), misc1.y == 2.0 ? cs : cd, matAmbient) : matAmbient;
        vec4 ms = cv ? Source(int(misc1.z), misc1.z == 2.0 ? cs : cd, matSpecular) : matSpecular;
        vec4 me = cv ? Source(int(misc1.w), misc1.w == 2.0 ? cs : cd, matEmissive) : matEmissive;
        vec3 a = ma.rgb * ambient.rgb, dif = vec3(0.0), spec = vec3(0.0);
        for (int k = 0; k < lightCount.x; ++k) {
            Light L = lights[k];
            vec3 l;
            float att = 1.0, sp = 1.0;
            if (L.pos.w > 2.5) {
                l = -normalize(L.dir.xyz);
            } else {
                vec3 d = L.pos.xyz - pv;
                float dist = length(d);
                l = d / max(dist, 1e-20);
                att = dist <= L.dir.w ? 1.0 / (L.att.x + L.att.y * dist + L.att.z * dist * dist) : 0.0;
                if (L.pos.w > 1.5) {
                    float rho = dot(-l, normalize(L.dir.xyz));
                    sp = rho > L.spot.x ? 1.0 : rho <= L.spot.y ? 0.0
                       : pow(saturate((rho - L.spot.y) / max(L.spot.x - L.spot.y, 1e-20)), L.att.w);
                }
            }
            float k2 = att * sp;
            a += k2 * ma.rgb * L.ambient.rgb;
            float nl = dot(nv, l);
            dif += k2 * max(0.0, nl) * L.diffuse.rgb * md.rgb;
            if (misc3.x > 0.5 && nl > 0.0) {
                vec3 e = misc3.y > 0.5 ? normalize(-pv) : vec3(0.0, 0.0, 1.0);   // (L: the non-local viewer)
                vec3 h = normalize(e + l);
                spec += k2 * pow(max(0.0, dot(nv, h)), misc0.x) * L.specular.rgb * ms.rgb;
            }
        }
        cd = vec4(saturate(me.rgb + a + dif), saturate(md.a));
        cs = vec4(misc3.x > 0.5 ? saturate(spec) : vec3(0.0, 0.0, 0.0), 0.0);
    }
    o.d0 = cd; o.d1 = cs; o.fd0 = cd; o.fd1 = cs;
    // orig: the XDK's fog state 0x0023D5C0 (K0 / K1 / mode / generation, below) with the fog plane (0.0, 0.0, 1.0, 0.0) of
    // SetShaderConstantMode 0x00237EA0: the depth is the eye-space z (signed), radial |eye| with RANGEFOGENABLE; the
    // NV2A's evaluation per xemu (L: per vertex, interpolated, clamped per pixel). renderer_census_port.md 5.
    float f = 1.0;
    if (misc3.z > 0.5) {
        int gen = int(fogParams.w);
        float d = gen == 1 ? length(pv) : pv.z;
        int m = int(misc3.w);
        if (gen == 0) f = cs.a;                        // table mode NONE: the vertex specular alpha
        else if (m == 0) f = fogParams.x + fogParams.y * d - 1.0;
        else if (m == 1) f = fogParams.x + exp2(16.0 * fogParams.y * d) - 1.5;
        else f = fogParams.x + exp2(-32.0 * (fogParams.y * d) * (fogParams.y * d)) - 1.5;
    }
    o.fog = vec4(f, 0.0, 0.0, 0.0);
    vec4 uv[4] = vec4[4](i.uv0, i.uv1, i.uv2, i.uv3);
    o.t0 = TexCoord(0, uv, pv, nv);
    o.t1 = TexCoord(1, uv, pv, nv);
    o.t2 = TexCoord(2, uv, pv, nv);
    o.t3 = TexCoord(3, uv, pv, nv);
    return o;
}


void main() { VSIn i; i.pos=a_pos; i.nrm=vec4(a_nrm,0.0); i.d0=a_d0; i.d1=a_d1; i.uv0=a_uv0; i.uv1=a_uv1; i.uv2=a_uv2; i.uv3=a_uv3; VSOut o=VSMain(i); gl_Position=o.pos; gl_Position.z=2.0*o.pos.z-o.pos.w; gl_PointSize=max(1.0,ssOffset.z);v_d0=o.d0;v_d1=o.d1;v_fd0=o.fd0;v_fd1=o.fd1;v_fog=o.fog;v_t0=o.t0;v_t1=o.t1;v_t2=o.t2;v_t3=o.t3;}
)GLSL";
inline constexpr char kFragmentShader[]=R"GLSL(#version 300 es
precision highp float;
precision highp int;
struct Light {
    vec4 diffuse, specular, ambient;
    vec4 pos;      // view space, w: type (1.0 point, 2.0 spot, 3.0 directional)
    vec4 dir;      // view space, w: range
    vec4 att;      // a0, a1, a2, falloff
    vec4 spot;     // cos(theta / 2.0), cos(phi / 2.0)
};
layout(std140) uniform Draw {
    mat4 world, view, proj;
    mat4 texm[4];
    vec4 vp;            // x, y, width, height of the viewport
    vec4 ssOffset;      // the screen-space offset (xy)
    vec4 matDiffuse, matAmbient, matSpecular, matEmissive;
    vec4 ambient;       // RS AMBIENT
    vec4 misc0;         // power, lighting, colorVertex, normalizeNormals
    vec4 misc1;         // diffuse / ambient / specular / emissive material sources (0.0 material, 1.0 COLOR1, 2.0 COLOR2)
    vec4 misc2;         // hasNormal, hasDiffuse, hasSpecular, rhw
    vec4 misc3;         // specularEnable, localViewer, fogEnable, fog mode (0.0 linear 0x2601, 1.0 exp 0x800, 2.0 exp2 0x801)
    vec4 fogParams;     // K0, K1 (the XDK's fog constants), -, generation (0.0 specular alpha, 1.0 radial, 2.0 planar)
    vec4 fogColor;
    vec4 tfactor;
    vec4 alphaTest;     // enable, func (0.0 never .. 7.0 always), ref, flat shading
    ivec4 tci[4];          // coordinate set, generation (0.0 pass, 1.0 normal, 2.0 position, 3.0 reflection), count, projected
    ivec4 colorOp[4];      // op, arg1, arg2, arg0
    ivec4 alphaOp[4];
    ivec4 stageMisc[4];    // resultArg (1.0 current, 5.0 temp), texture bound, set dimension
    ivec4 lightCount;
    Light lights[8];
    vec4 texScale[4];   // uv scale (a linear texture's texel coordinates: 1.0 / size), z: the YUV conversion
};
struct VSIn {
    vec4 pos; vec4 nrm; vec4 d0; vec4 d1;
    vec4 uv0; vec4 uv1; vec4 uv2; vec4 uv3;
};
struct VSOut {
    vec4 pos;
    vec4 d0; vec4 d1;
    vec4 fd0; vec4 fd1;
    vec4 fog;
    vec4 t0; vec4 t1; vec4 t2; vec4 t3;
};


float saturate(float v) { return clamp(v,0.0,1.0); }
vec3 saturate(vec3 v) { return clamp(v,0.0,1.0); }
vec4 saturate(vec4 v) { return clamp(v,0.0,1.0); }
uniform sampler2D tex0,tex1,tex2,tex3;
in vec4 v_d0;
in vec4 v_d1;
flat in vec4 v_fd0;
flat in vec4 v_fd1;
in vec4 v_fog;
in vec4 v_t0;
in vec4 v_t1;
in vec4 v_t2;
in vec4 v_t3;
vec4 Arg(int a, vec4 diffuse, vec4 current, vec4 tex, vec4 spec, vec4 temp) {
    int s = a & 15;
    vec4 v = s == 0 ? diffuse : s == 1 ? current : s == 2 ? tex : s == 3 ? tfactor : s == 4 ? spec : temp;
    if ((a & 0x20) != 0) v = v.aaaa;
    if ((a & 0x10) != 0) v = 1.0 - v;
    return v;
}

// The texture-stage ops (Xbox numbering) on one channel set; `alpha` selects the alpha-only forms.
vec4 Op(int op, vec4 a1, vec4 a2, vec4 a0, vec4 diffuse, vec4 current, vec4 tex, vec4 tfac) {
    if (op == 2) return a1;
    if (op == 3) return a2;
    if (op == 4) return a1 * a2;
    if (op == 5) return a1 * a2 * 2.0;
    if (op == 6) return a1 * a2 * 4.0;
    if (op == 7) return a1 + a2;
    if (op == 8) return a1 + a2 - 0.5;
    if (op == 9) return (a1 + a2 - 0.5) * 2.0;
    if (op == 10) return a1 - a2;
    if (op == 11) return a1 + a2 - a1 * a2;
    if (op == 12) return mix(a2, a1, diffuse.a);
    if (op == 13) return mix(a2, a1, current.a);
    if (op == 14) return mix(a2, a1, tex.a);
    if (op == 15) return mix(a2, a1, tfac.a);
    if (op == 16) return a1 + a2 * (1.0 - tex.a);
    if (op == 18) return vec4(a1.rgb + a1.a * a2.rgb, a1.a);
    if (op == 19) return vec4(a1.rgb * a2.rgb + a1.a, a1.a * a2.a);
    if (op == 20) return vec4((1.0 - a1.a) * a2.rgb + a1.rgb, a1.a);
    if (op == 21) return vec4((1.0 - a1.rgb) * a2.rgb + a1.a, a1.a);
    if (op == 22) { float d = saturate(dot(a1.rgb - 0.5, a2.rgb - 0.5) * 4.0); return vec4(d, d, d, d); }
    if (op == 23) return a0 + a1 * a2;
    if (op == 24) return mix(a2, a1, a0);
    return a1;
}

// NV097_SET_CONTROL0's colour-space conversion (D3DRS_YUVENABLE) of a YUY2 texel (r = Y, g = Cb, b = Cr). Stand-in
// (L): the XMV library's own software converter 0x002653FF / 0x002655EA (BT.601 studio range: y' = (Y - 16.0) 38139.0,
// R = y' + 52298.0 (Cr - 128.0), G = y' - 12812.0 (Cb - 128.0) - 26640.0 (Cr - 128.0), B = y' + 66126.0 (Cb - 128.0), / 2.0^15.0); the
// NV2A's coefficients and rounding are not known (docs/analysis/movies_port.md open question 1.0).
vec4 YuvToRgb(vec4 c) {
    float y = (c.r * 255.0 - 16.0) * 38139.0, u = c.g * 255.0 - 128.0, v = c.b * 255.0 - 128.0;
    vec3 rgb = vec3(y + 52298.0 * v, y - 12812.0 * u - 26640.0 * v, y + 66126.0 * u) / 32768.0;
    return vec4(saturate(rgb / 255.0), c.a);
}

vec4 Sample(int s, vec4 t) {
    vec2 uv = t.w != 0.0 && t.w != 1.0 ? t.xy / t.w : t.xy;
    uv *= texScale[s].xy;
    if (texScale[s].w > 0.5) uv.y=1.0-uv.y;
    vec4 c;
    if (s == 0) c = texture(tex0, uv);
    else if (s == 1) c = texture(tex1, uv);
    else if (s == 2) c = texture(tex2, uv);
    else c = texture(tex3, uv);
    return texScale[s].z > 0.5 ? YuvToRgb(c) : c;
}

vec4 PSMain(VSOut i) {
    vec4 diffuse = alphaTest.w > 0.5 ? i.fd0 : i.d0;
    vec4 spec = alphaTest.w > 0.5 ? i.fd1 : i.d1;
    vec4 current = diffuse, temp = vec4(0.0);
    vec4 t[4] = vec4[4](i.t0, i.t1, i.t2, i.t3);
    for (int s = 0; s < 4; ++s) {
        ivec4 c = colorOp[s], a = alphaOp[s];
        if (c.x == 1) break;                           // DISABLE
        vec4 tex = stageMisc[s].y != 0 ? Sample(s, t[s]) : vec4(0.0, 0.0, 0.0, 1.0);
        vec4 cr = Op(c.x, Arg(c.y, diffuse, current, tex, spec, temp), Arg(c.z, diffuse, current, tex, spec, temp),
                       Arg(c.w, diffuse, current, tex, spec, temp), diffuse, current, tex, tfactor);
        vec4 ar = a.x == 1 ? current
                  : Op(a.x, Arg(a.y | 0x20, diffuse, current, tex, spec, temp),
                       Arg(a.z | 0x20, diffuse, current, tex, spec, temp), Arg(a.w | 0x20, diffuse, current, tex, spec, temp),
                       diffuse, current, tex, tfactor);
        vec4 r = saturate(vec4(cr.rgb, ar.a));
        if (stageMisc[s].x == 5) temp = r; else current = r;
    }
    vec4 c = current;
    if (misc3.x > 0.5) c.rgb = saturate(c.rgb + spec.rgb);
    if (misc3.z > 0.5) c.rgb = mix(fogColor.rgb, c.rgb, saturate(i.fog.x));
    if (alphaTest.x > 0.5) {
        int f = int(alphaTest.y);
        float r = alphaTest.z, v = round(c.a * 255.0) / 255.0;
        bool ok = f == 7 || (f == 1 && v < r) || (f == 2 && v == r) || (f == 3 && v <= r) || (f == 4 && v > r) ||
                  (f == 5 && v != r) || (f == 6 && v >= r);
        if (!ok) discard;
    }
    return c;
}


out vec4 fragmentColor;
void main() { VSOut i; i.pos=gl_FragCoord;i.d0=v_d0;i.d1=v_d1;i.fd0=v_fd0;i.fd1=v_fd1;i.fog=v_fog;i.t0=v_t0;i.t1=v_t1;i.t2=v_t2;i.t3=v_t3;fragmentColor=PSMain(i);}
)GLSL";
}
