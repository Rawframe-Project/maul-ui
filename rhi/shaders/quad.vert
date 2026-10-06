// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's vertex entry "vs" (record mui-0005): each
// instance a quad of six vertices, no vertex buffer, its record read
// from the instance buffer in table 0, grown by a screen pixel each way
// for the edge's coverage, and its corners carried through the command's
// transform. The fragment entry works in the quad's own pixels, before
// the transform, and is handed how many screen pixels one of them spans
// (the transform's scale, the square root of its determinant's size).
// The root block holds 2 over the target's size in pixels and the
// pixels per unit; positions are y up, as Maul RHI's are.

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

layout(set = 0, binding = 0, std430) readonly buffer Instances
{
    Instance items[];
} instances;

layout(set = 0, binding = 2, std430) readonly buffer Transforms
{
    Transform items[];
} transforms;

layout(push_constant) uniform Root
{
    vec4 frame;
} root;

layout(location = 0) out vec2 outLocal;
layout(location = 1) flat out uint outIndex;
layout(location = 2) flat out float outSpan;

const vec2 kCorners[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 1.0),
                                vec2(1.0, 0.0), vec2(1.0, 1.0));

void main()
{
    float scale = root.frame.z;
    vec4 rect = instances.items[gl_InstanceIndex].rect;
    uint which = instances.items[gl_InstanceIndex].tags.z;
    vec4 linear = transforms.items[which].linear;
    vec2 offset = transforms.items[which].offset.xy;
    float span = sqrt(abs(linear.x * linear.w - linear.y * linear.z));
    vec2 corner = kCorners[gl_VertexIndex];
    vec2 local = corner * rect.zw * scale + (corner * 2.0 - 1.0) / max(span, 1e-3);
    vec2 before = rect.xy + local / scale;
    vec2 after = vec2(linear.x * before.x + linear.z * before.y + offset.x,
                      linear.y * before.x + linear.w * before.y + offset.y);
    vec2 pixel = after * scale;
    outLocal = local;
    outIndex = uint(gl_InstanceIndex);
    outSpan = span;
    gl_Position = vec4(pixel.x * root.frame.x - 1.0, 1.0 - pixel.y * root.frame.y, 0.0, 1.0);
}
