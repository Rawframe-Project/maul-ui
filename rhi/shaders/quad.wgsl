// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's WGSL (record mui-0005): the entries of
// quad.vert and quad.frag.

struct Root {
    frame: vec4f,
}

var<immediate> root: Root;

struct Instance {
    rect: vec4f,
    radii: vec4f,
    fill: vec4f,
    widths: vec4f,
    colors: array<vec4f, 4>,
    meta: vec4u,
}

@group(0) @binding(0) var<storage, read> instances: array<Instance>;

struct Between {
    @builtin(position) position: vec4f,
    @location(0) local: vec2f,
    @location(1) @interpolate(flat) index: u32,
}

const kCorners = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0),
                                 vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0));

@vertex
fn vs(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> Between {
    let rect = instances[instance].rect * root.frame.z;
    let corner = kCorners[vertex];
    let local = corner * rect.zw + (corner * 2.0 - 1.0);
    let pixel = rect.xy + local;
    var out: Between;
    out.local = local;
    out.index = instance;
    out.position = vec4f(pixel.x * root.frame.x - 1.0, 1.0 - pixel.y * root.frame.y, 0.0, 1.0);
    return out;
}

// The signed distance from a point, relative to a rounded rect's
// center, to its edge; radii top-left, top-right, bottom-right,
// bottom-left, y down.
fn roundedRect(p: vec2f, extent: vec2f, radii: vec4f) -> f32 {
    var r = select(select(radii.z, radii.y, p.y < 0.0), select(radii.w, radii.x, p.y < 0.0),
                   p.x < 0.0);
    r = min(r, min(extent.x, extent.y));
    let q = abs(p) - extent + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2f(0.0))) - r;
}

@fragment
fn fs(in: Between) -> @location(0) vec4f {
    let scale = root.frame.z;
    let size = instances[in.index].rect.zw * scale;
    let radii = instances[in.index].radii * scale;
    // Top, right, bottom and left.
    let widths = instances[in.index].widths * scale;
    let outer = roundedRect(in.local - size * 0.5, size * 0.5, radii);
    let low = vec2f(widths.w, widths.x);
    let high = max(size - vec2f(widths.y, widths.z), low);
    let inner = max(radii - vec4f(max(widths.w, widths.x), max(widths.x, widths.y),
                                  max(widths.y, widths.z), max(widths.z, widths.w)),
                    vec4f(0.0));
    let inside = roundedRect(in.local - (low + high) * 0.5, (high - low) * 0.5, inner);
    let covered = clamp(0.5 - outer, 0.0, 1.0);
    let fill = min(clamp(0.5 - inside, 0.0, 1.0), covered);
    // The side nearest, in its own width.
    let reach = vec4f(in.local.y, size.x - in.local.x, size.y - in.local.y, in.local.x) /
                max(widths, vec4f(1e-4));
    var side = 0u;
    side = select(side, 1u, reach.y < reach[side]);
    side = select(side, 2u, reach.z < reach[side]);
    side = select(side, 3u, reach.w < reach[side]);
    return instances[in.index].fill * fill + instances[in.index].colors[side] * (covered - fill);
}
