// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's vertex entry "vs" (record mui-0005):
// each instance a quad of six vertices, no vertex buffer, its record
// read from the instance buffer in table 0, grown by a pixel each way
// for the edge's coverage. The root block holds 2 over the target's
// size in pixels and the pixels per unit; positions are y up, as Maul
// RHI's are.

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

layout(set = 0, binding = 0, std430) readonly buffer Instances
{
    Instance items[];
} instances;

layout(push_constant) uniform Root
{
    vec4 frame;
} root;

layout(location = 0) out vec2 outLocal;
layout(location = 1) flat out uint outIndex;

const vec2 kCorners[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, 1.0),
                                vec2(1.0, 0.0), vec2(1.0, 1.0));

void main()
{
    vec4 rect = instances.items[gl_InstanceIndex].rect * root.frame.z;
    vec2 corner = kCorners[gl_VertexIndex];
    vec2 local = corner * rect.zw + (corner * 2.0 - 1.0);
    vec2 pixel = rect.xy + local;
    outLocal = local;
    outIndex = uint(gl_InstanceIndex);
    gl_Position = vec4(pixel.x * root.frame.x - 1.0, 1.0 - pixel.y * root.frame.y, 0.0, 1.0);
}
