// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's fragment entry "fs" (record mui-0005), by the
// instance's kind:
// - a box: coverage from its rounded rect's signed distance in pixels,
//   split into the fill (inside the borders' inner edge, its radii shrunk
//   by the widths) and the borders, each side's color where that side is
//   nearest; its gradient, from the gradient table, over its fill;
// - a shadow: a Gaussian-blurred rounded rect in Evan Wallace's closed
//   form (the blur integrated along x exactly, sampled along y), drawn
//   outside its box, or inside it when inset.
// Gradients move through premultiplied Oklab, as the core's transitions
// do. Distances are measured in the quad's own pixels and made screen
// pixels by the transform's span for an edge a pixel wide at any scale.
// Each fragment is kept within its clips: its place on the screen
// brought back through each clip's transform, its parents' too, to a
// depth of 16, an inverted clip keeping the outside. Colors are
// premultiplied; coverage multiplies them.

#version 450

struct Instance
{
    vec4 rect;
    vec4 radii;
    vec4 fill;
    vec4 widths;
    vec4 colors[4];
    uvec4 tags;
};

struct Transform
{
    vec4 linear;
    vec4 offset;
};

struct Clip
{
    vec4 rect;
    vec4 radii;
    uvec4 tags;
};

struct Gradient
{
    uvec4 head;
    vec4 params;
    vec4 colors[4];
    vec4 positions;
};

layout(set = 0, binding = 0, std430) readonly buffer Instances
{
    Instance items[];
} instances;

layout(set = 0, binding = 1, std430) readonly buffer Gradients
{
    Gradient items[];
} gradients;

layout(set = 0, binding = 2, std430) readonly buffer Transforms
{
    Transform items[];
} transforms;

layout(set = 0, binding = 3, std430) readonly buffer Clips
{
    Clip items[];
} clips;

layout(push_constant) uniform Root
{
    vec4 frame;
} root;

layout(location = 0) in vec2 local;
layout(location = 1) flat in uint index;
layout(location = 2) flat in float span;
layout(location = 0) out vec4 outColor;

const uint kShadow = 2u;
const uint kLinear = 1u;

// The signed distance from a point, relative to a rounded rect's
// center, to its edge; radii top-left, top-right, bottom-right,
// bottom-left, y down.
float RoundedRect(vec2 p, vec2 extent, vec4 radii)
{
    float r = p.x < 0.0 ? (p.y < 0.0 ? radii.x : radii.w) : (p.y < 0.0 ? radii.y : radii.z);
    r = min(r, min(extent.x, extent.y));
    vec2 q = abs(p) - extent + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// How much of a pixel a shape covers, from the signed distance to its
// edge in the quad's pixels.
float Coverage(float distance)
{
    return clamp(0.5 - distance * span, 0.0, 1.0);
}

float Cbrt(float x)
{
    return sign(x) * pow(abs(x), 1.0 / 3.0);
}

// A premultiplied linear color in premultiplied Oklab, and back, held to
// the sRGB gamut, as src/color.c converts.
vec4 ToOklab(vec4 color)
{
    vec3 rgb = color.a > 0.0 ? color.rgb / color.a : vec3(0.0);
    float l = Cbrt(0.4122214708 * rgb.r + 0.5363325363 * rgb.g + 0.0514459929 * rgb.b);
    float m = Cbrt(0.2119034982 * rgb.r + 0.6806995451 * rgb.g + 0.1073969566 * rgb.b);
    float s = Cbrt(0.0883024619 * rgb.r + 0.2817188376 * rgb.g + 0.6299787005 * rgb.b);
    vec3 lab = vec3(0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                    1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                    0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s);
    return vec4(lab * color.a, color.a);
}

vec4 FromOklab(vec4 lab)
{
    float alpha = clamp(lab.a, 0.0, 1.0);
    if (alpha == 0.0)
    {
        return vec4(0.0);
    }
    vec3 c = lab.rgb / alpha;
    float l = c.x + 0.3963377774 * c.y + 0.2158037573 * c.z;
    float m = c.x - 0.1055613458 * c.y - 0.0638541728 * c.z;
    float s = c.x - 0.0894841775 * c.y - 1.2914855480 * c.z;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    vec3 rgb = vec3(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                    -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
    return vec4(clamp(rgb, 0.0, 1.0) * alpha, alpha);
}

// A gradient's color at a point of a box, both in pixels: along the line
// through the center at the angle (clockwise from toward the top), or
// outward as the ellipse to the farthest corner.
vec4 GradientAt(uint which, vec2 p, vec2 size)
{
    uint count = gradients.items[which].head.y;
    vec2 d = p - size * 0.5;
    float t;
    if (gradients.items[which].head.x == kLinear)
    {
        float angle = radians(gradients.items[which].params.x);
        vec2 direction = vec2(sin(angle), -cos(angle));
        float span = abs(size.x * direction.x) + abs(size.y * direction.y);
        t = span > 0.0 ? dot(d, direction) / span + 0.5 : 0.0;
    }
    else
    {
        vec2 reach = max(size * 0.5 * sqrt(2.0), vec2(1e-4));
        t = length(d / reach);
    }
    vec4 positions = gradients.items[which].positions;
    vec4 color = gradients.items[which].colors[0];
    for (uint i = 1u; i < count; i++)
    {
        float before = positions[i - 1u];
        float after = positions[i];
        if (t > before)
        {
            float f = after > before ? clamp((t - before) / (after - before), 0.0, 1.0) : 1.0;
            color = FromOklab(mix(ToOklab(gradients.items[which].colors[i - 1u]),
                                  ToOklab(gradients.items[which].colors[i]), f));
        }
    }
    return color;
}

vec4 Box(vec2 size)
{
    float scale = root.frame.z;
    vec4 radii = instances.items[index].radii * scale;
    // Top, right, bottom and left.
    vec4 widths = instances.items[index].widths * scale;
    float outer = RoundedRect(local - size * 0.5, size * 0.5, radii);
    vec2 low = vec2(widths.w, widths.x);
    vec2 high = max(size - vec2(widths.y, widths.z), low);
    vec4 inner = max(radii - vec4(max(widths.w, widths.x), max(widths.x, widths.y),
                                  max(widths.y, widths.z), max(widths.z, widths.w)),
                     0.0);
    float inside = RoundedRect(local - (low + high) * 0.5, (high - low) * 0.5, inner);
    float covered = Coverage(outer);
    float fill = min(Coverage(inside), covered);
    // The side nearest, in its own width.
    vec4 reach = vec4(local.y, size.x - local.x, size.y - local.y, local.x) / max(widths, 1e-4);
    int side = 0;
    side = reach.y < reach[side] ? 1 : side;
    side = reach.z < reach[side] ? 2 : side;
    side = reach.w < reach[side] ? 3 : side;
    vec4 color = instances.items[index].fill;
    uint gradient = instances.items[index].tags.w;
    if (gradient != 0u)
    {
        vec4 over = GradientAt(gradient, local, size);
        color = over + color * (1.0 - over.a);
    }
    return color * fill + instances.items[index].colors[side] * (covered - fill);
}

float Gaussian(float x, float sigma)
{
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (sqrt(2.0 * 3.14159265) * sigma);
}

vec2 Erf(vec2 x)
{
    vec2 s = sign(x);
    vec2 a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

// The blur along x of a row of a rounded rect, its corner's radius.
float ShadowRow(float x, float y, float sigma, float corner, vec2 extent)
{
    float delta = min(extent.y - corner - abs(y), 0.0);
    float curved = extent.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    vec2 integral = 0.5 + 0.5 * Erf((x + vec2(-curved, curved)) * (sqrt(0.5) / sigma));
    return integral.y - integral.x;
}

// How much of a rounded rect, blurred by a Gaussian of sigma, covers a
// point relative to its center.
float Blurred(vec2 p, vec2 extent, vec4 radii, float sigma)
{
    float corner = p.x < 0.0 ? (p.y < 0.0 ? radii.x : radii.w) : (p.y < 0.0 ? radii.y : radii.z);
    corner = min(corner, min(extent.x, extent.y));
    if (sigma < 0.01)
    {
        return Coverage(RoundedRect(p, extent, radii));
    }
    float low = p.y - extent.y;
    float high = p.y + extent.y;
    float start = clamp(-3.0 * sigma, low, high);
    float end = clamp(3.0 * sigma, low, high);
    float step = (end - start) / 4.0;
    float y = start + step * 0.5;
    float value = 0.0;
    for (int i = 0; i < 4; i++)
    {
        value += ShadowRow(p.x, p.y - y, sigma, corner, extent) * Gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

vec4 Shadow()
{
    float scale = root.frame.z;
    // In pixels from the quad's corner: the blurred shape and the box.
    vec2 origin = instances.items[index].rect.xy * scale;
    vec2 p = origin + local;
    vec4 shape = instances.items[index].widths * scale;
    vec4 box = instances.items[index].colors[0] * scale;
    vec4 boxRadii = instances.items[index].colors[1] * scale;
    float sigma = instances.items[index].colors[2].x * scale;
    bool inset = instances.items[index].colors[2].y != 0.0;
    vec4 radii = instances.items[index].radii * scale;
    float blurred = Blurred(p - (shape.xy + shape.zw * 0.5), shape.zw * 0.5, radii, sigma);
    float inBox = Coverage(RoundedRect(p - (box.xy + box.zw * 0.5), box.zw * 0.5, boxRadii));
    float coverage = inset ? inBox * (1.0 - blurred) : blurred * (1.0 - inBox);
    return instances.items[index].fill * coverage;
}

// How much of the fragment its clips keep.
float Clipped(uint clip)
{
    float scale = root.frame.z;
    vec2 p = gl_FragCoord.xy / scale;
    float kept = 1.0;
    for (int depth = 0; depth < 16 && clip != 0u; depth++)
    {
        uint which = clips.items[clip].tags.y;
        vec4 l = transforms.items[which].linear;
        vec2 d = p - transforms.items[which].offset.xy;
        float determinant = l.x * l.w - l.y * l.z;
        vec2 q = determinant != 0.0
                     ? vec2(l.w * d.x - l.z * d.y, l.x * d.y - l.y * d.x) / determinant
                     : vec2(-1e9);
        vec4 rect = clips.items[clip].rect;
        float distance = RoundedRect(q - (rect.xy + rect.zw * 0.5), rect.zw * 0.5,
                                     clips.items[clip].radii);
        float inside = clamp(0.5 - distance * scale * sqrt(abs(determinant)), 0.0, 1.0);
        kept *= clips.items[clip].tags.z != 0u ? 1.0 - inside : inside;
        clip = clips.items[clip].tags.x;
    }
    return kept;
}

void main()
{
    vec2 size = instances.items[index].rect.zw * root.frame.z;
    uint kind = instances.items[index].tags.x;
    vec4 color = kind == kShadow ? Shadow() : Box(size);
    outColor = color * Clipped(instances.items[index].tags.y);
}
