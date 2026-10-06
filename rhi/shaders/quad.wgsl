// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reference renderer's WGSL (record mui-0005): the entries of
// quad.vert and quad.frag, line for line.

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
    tags: vec4u,
}

struct Gradient {
    head: vec4u,
    params: vec4f,
    colors: array<vec4f, 4>,
    positions: vec4f,
}

@group(0) @binding(0) var<storage, read> instances: array<Instance>;
@group(0) @binding(1) var<storage, read> gradients: array<Gradient>;

const kShadow = 2u;
const kLinear = 1u;

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

fn cbrt(x: f32) -> f32 {
    return sign(x) * pow(abs(x), 1.0 / 3.0);
}

// A premultiplied linear color in premultiplied Oklab, and back, held to
// the sRGB gamut, as src/color.c converts.
fn toOklab(color: vec4f) -> vec4f {
    let rgb = select(vec3f(0.0), color.rgb / color.a, color.a > 0.0);
    let l = cbrt(0.4122214708 * rgb.r + 0.5363325363 * rgb.g + 0.0514459929 * rgb.b);
    let m = cbrt(0.2119034982 * rgb.r + 0.6806995451 * rgb.g + 0.1073969566 * rgb.b);
    let s = cbrt(0.0883024619 * rgb.r + 0.2817188376 * rgb.g + 0.6299787005 * rgb.b);
    let lab = vec3f(0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                    1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                    0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s);
    return vec4f(lab * color.a, color.a);
}

fn fromOklab(lab: vec4f) -> vec4f {
    let alpha = clamp(lab.a, 0.0, 1.0);
    if (alpha == 0.0) {
        return vec4f(0.0);
    }
    let c = lab.rgb / alpha;
    var l = c.x + 0.3963377774 * c.y + 0.2158037573 * c.z;
    var m = c.x - 0.1055613458 * c.y - 0.0638541728 * c.z;
    var s = c.x - 0.0894841775 * c.y - 1.2914855480 * c.z;
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    let rgb = vec3f(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                    -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
    return vec4f(clamp(rgb, vec3f(0.0), vec3f(1.0)) * alpha, alpha);
}

// A gradient's color at a point of a box, both in pixels: along the line
// through the center at the angle (clockwise from toward the top), or
// outward as the ellipse to the farthest corner.
fn gradientAt(which: u32, p: vec2f, size: vec2f) -> vec4f {
    let count = gradients[which].head.y;
    let d = p - size * 0.5;
    var t = 0.0;
    if (gradients[which].head.x == kLinear) {
        let angle = radians(gradients[which].params.x);
        let direction = vec2f(sin(angle), -cos(angle));
        let span = abs(size.x * direction.x) + abs(size.y * direction.y);
        t = select(0.0, dot(d, direction) / span + 0.5, span > 0.0);
    } else {
        let reach = max(size * 0.5 * sqrt(2.0), vec2f(1e-4));
        t = length(d / reach);
    }
    let positions = gradients[which].positions;
    var color = gradients[which].colors[0];
    for (var i = 1u; i < count; i++) {
        let before = positions[i - 1u];
        let after = positions[i];
        if (t > before) {
            let f = select(1.0, clamp((t - before) / (after - before), 0.0, 1.0), after > before);
            color = fromOklab(mix(toOklab(gradients[which].colors[i - 1u]),
                                  toOklab(gradients[which].colors[i]), f));
        }
    }
    return color;
}

fn box(index: u32, local: vec2f, size: vec2f) -> vec4f {
    let scale = root.frame.z;
    let radii = instances[index].radii * scale;
    // Top, right, bottom and left.
    let widths = instances[index].widths * scale;
    let outer = roundedRect(local - size * 0.5, size * 0.5, radii);
    let low = vec2f(widths.w, widths.x);
    let high = max(size - vec2f(widths.y, widths.z), low);
    let inner = max(radii - vec4f(max(widths.w, widths.x), max(widths.x, widths.y),
                                  max(widths.y, widths.z), max(widths.z, widths.w)),
                    vec4f(0.0));
    let inside = roundedRect(local - (low + high) * 0.5, (high - low) * 0.5, inner);
    let covered = clamp(0.5 - outer, 0.0, 1.0);
    let fill = min(clamp(0.5 - inside, 0.0, 1.0), covered);
    // The side nearest, in its own width.
    let reach = vec4f(local.y, size.x - local.x, size.y - local.y, local.x) /
                max(widths, vec4f(1e-4));
    var side = 0u;
    side = select(side, 1u, reach.y < reach[side]);
    side = select(side, 2u, reach.z < reach[side]);
    side = select(side, 3u, reach.w < reach[side]);
    var color = instances[index].fill;
    let gradient = instances[index].tags.w;
    if (gradient != 0u) {
        let over = gradientAt(gradient, local, size);
        color = over + color * (1.0 - over.a);
    }
    return color * fill + instances[index].colors[side] * (covered - fill);
}

fn gaussian(x: f32, sigma: f32) -> f32 {
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (sqrt(2.0 * 3.14159265) * sigma);
}

fn erf2(v: vec2f) -> vec2f {
    let s = sign(v);
    let a = abs(v);
    var x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

// The blur along x of a row of a rounded rect, its corner's radius.
fn shadowRow(x: f32, y: f32, sigma: f32, corner: f32, extent: vec2f) -> f32 {
    let delta = min(extent.y - corner - abs(y), 0.0);
    let curved = extent.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    let integral = 0.5 + 0.5 * erf2((x + vec2f(-curved, curved)) * (sqrt(0.5) / sigma));
    return integral.y - integral.x;
}

// How much of a rounded rect, blurred by a Gaussian of sigma, covers a
// point relative to its center.
fn blurred(p: vec2f, extent: vec2f, radii: vec4f, sigma: f32) -> f32 {
    var corner = select(select(radii.z, radii.y, p.y < 0.0), select(radii.w, radii.x, p.y < 0.0),
                        p.x < 0.0);
    corner = min(corner, min(extent.x, extent.y));
    if (sigma < 0.01) {
        return clamp(0.5 - roundedRect(p, extent, radii), 0.0, 1.0);
    }
    let low = p.y - extent.y;
    let high = p.y + extent.y;
    let start = clamp(-3.0 * sigma, low, high);
    let end = clamp(3.0 * sigma, low, high);
    let step = (end - start) / 4.0;
    var y = start + step * 0.5;
    var value = 0.0;
    for (var i = 0; i < 4; i++) {
        value += shadowRow(p.x, p.y - y, sigma, corner, extent) * gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

fn shadow(index: u32, local: vec2f) -> vec4f {
    let scale = root.frame.z;
    // In pixels from the quad's corner: the blurred shape and the box.
    let origin = instances[index].rect.xy * scale;
    let p = origin + local;
    let shape = instances[index].widths * scale;
    let boxRect = instances[index].colors[0] * scale;
    let boxRadii = instances[index].colors[1] * scale;
    let sigma = instances[index].colors[2].x * scale;
    let inset = instances[index].colors[2].y != 0.0;
    let radii = instances[index].radii * scale;
    let cover = blurred(p - (shape.xy + shape.zw * 0.5), shape.zw * 0.5, radii, sigma);
    let toBox = roundedRect(p - (boxRect.xy + boxRect.zw * 0.5), boxRect.zw * 0.5, boxRadii);
    let inBox = clamp(0.5 - toBox, 0.0, 1.0);
    let coverage = select(cover * (1.0 - inBox), inBox * (1.0 - cover), inset);
    return instances[index].fill * coverage;
}

@fragment
fn fs(in: Between) -> @location(0) vec4f {
    let size = instances[in.index].rect.zw * root.frame.z;
    if (instances[in.index].tags.x == kShadow) {
        return shadow(in.index, in.local);
    }
    return box(in.index, in.local, size);
}
