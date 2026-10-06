// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's fragment entry "fs" (record mui-0005): a
// box's coverage from its rounded rect's signed distance in pixels, split
// into the fill (inside the borders' inner edge, its radii shrunk by the
// widths) and the borders, each side's color where that side is nearest;
// colors are premultiplied, coverage multiplies them.

#version 450

struct Instance
{
    vec4 rect;
    vec4 radii;
    vec4 fill;
    vec4 widths;
    vec4 colors[4];
    uvec4 meta;
};

layout(set = 0, binding = 0, std430) readonly buffer Instances
{
    Instance items[];
} instances;

layout(push_constant) uniform Root
{
    vec4 frame;
} root;

layout(location = 0) in vec2 local;
layout(location = 1) flat in uint index;
layout(location = 0) out vec4 outColor;

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

void main()
{
    float scale = root.frame.z;
    vec2 size = instances.items[index].rect.zw * scale;
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
    float covered = clamp(0.5 - outer, 0.0, 1.0);
    float fill = min(clamp(0.5 - inside, 0.0, 1.0), covered);
    // The side nearest, in its own width.
    vec4 reach = vec4(local.y, size.x - local.x, size.y - local.y, local.x) / max(widths, 1e-4);
    int side = 0;
    side = reach.y < reach[side] ? 1 : side;
    side = reach.z < reach[side] ? 2 : side;
    side = reach.w < reach[side] ? 3 : side;
    outColor = instances.items[index].fill * fill +
               instances.items[index].colors[side] * (covered - fill);
}
