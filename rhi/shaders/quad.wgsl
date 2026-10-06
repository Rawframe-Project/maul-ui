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

struct Transform {
    linear: vec4f,
    offset: vec4f,
}

struct Clip {
    rect: vec4f,
    radii: vec4f,
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
@group(0) @binding(2) var<storage, read> transforms: array<Transform>;
@group(0) @binding(3) var<storage, read> clips: array<Clip>;
@group(1) @binding(0) var imageTexture: texture_2d<f32>;
@group(1) @binding(1) var imageSampler: sampler;

const kShadow = 2u;
const kImage = 3u;
const kGlyph = 4u;
const kLinear = 1u;

struct Between {
    @builtin(position) position: vec4f,
    @location(0) local: vec2f,
    @location(1) @interpolate(flat) index: u32,
    @location(2) @interpolate(flat) span: f32,
}

// The fragment's span, as quad.frag's input.
var<private> span: f32;

const kCorners = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0),
                                 vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0));

@vertex
fn vs(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> Between {
    let scale = root.frame.z;
    let rect = instances[instance].rect;
    let which = instances[instance].tags.z;
    let linear = transforms[which].linear;
    let offset = transforms[which].offset.xy;
    let quadSpan = sqrt(abs(linear.x * linear.w - linear.y * linear.z));
    let corner = kCorners[vertex];
    let local = corner * rect.zw * scale + (corner * 2.0 - 1.0) / max(quadSpan, 1e-3);
    let before = rect.xy + local / scale;
    let after = vec2f(linear.x * before.x + linear.z * before.y + offset.x,
                      linear.y * before.x + linear.w * before.y + offset.y);
    let pixel = after * scale;
    var out: Between;
    out.local = local;
    out.index = instance;
    out.span = quadSpan;
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

// How much of a pixel a shape covers, from the signed distance to its
// edge in the quad's pixels.
fn coverage(distance: f32) -> f32 {
    return clamp(0.5 - distance * span, 0.0, 1.0);
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
    let covered = coverage(outer);
    let fill = min(coverage(inside), covered);
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
        return coverage(roundedRect(p, extent, radii));
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
    let inBox = coverage(toBox);
    let coverage = select(cover * (1.0 - inBox), inBox * (1.0 - cover), inset);
    return instances[index].fill * coverage;
}

// Where a point of an image samples along one axis: within its first
// inset the first slice, within its last the last, the middle stretched
// between, each across its part of the uv rect.
fn along(p: f32, length: f32, low: f32, high: f32, uvLow: f32, uvHigh: f32, start: f32,
         end: f32) -> f32 {
    let first = start + p / max(low, 1e-6) * uvLow;
    let last = end - (length - p) / max(high, 1e-6) * uvHigh;
    let middle = mix(start + uvLow, end - uvHigh, (p - low) / max(length - low - high, 1e-6));
    return select(select(middle, last, p > length - high), first, p < low);
}

// The uv an image's fragment samples, in logical units within its rect.
fn imageUv(index: u32, local: vec2f) -> vec2f {
    let rect = instances[index].rect;
    let p = clamp(local / root.frame.z, vec2f(0.0), rect.zw);
    let uv = instances[index].colors[0];
    let drawn = instances[index].colors[1];
    let texels = instances[index].colors[2];
    return vec2f(along(p.x, rect.z, drawn.w, drawn.y, texels.w, texels.y, uv.x, uv.z),
                 along(p.y, rect.w, drawn.x, drawn.z, texels.x, texels.z, uv.y, uv.w));
}

fn imageColor(index: u32, local: vec2f, size: vec2f, uv: vec2f, dx: vec2f, dy: vec2f) -> vec4f {
    let texel = textureSampleGrad(imageTexture, imageSampler, uv, dx, dy);
    let covered = coverage(roundedRect(local - size * 0.5, size * 0.5, vec4f(0.0)));
    return texel * instances[index].fill * covered;
}

fn glyph(index: u32, local: vec2f, size: vec2f) -> vec4f {
    let uv = instances[index].colors[0];
    let bounds = instances[index].colors[1];
    let at = clamp(mix(uv.xy, uv.zw, local / size), bounds.xy, bounds.zw);
    let sampled = textureSampleLevel(imageTexture, imageSampler, at, 0.0).r;
    let field = instances[index].colors[2].x;
    let coverage = select(clamp(0.5 + (sampled - 128.0 / 255.0) * field * span, 0.0, 1.0),
                          sampled, field == 0.0);
    return instances[index].fill * coverage;
}

// How much of the fragment its clips keep.
fn clipped(first: u32, position: vec2f) -> f32 {
    let scale = root.frame.z;
    let p = position / scale;
    var clip = first;
    var kept = 1.0;
    for (var depth = 0; depth < 16 && clip != 0u; depth++) {
        let which = clips[clip].tags.y;
        let l = transforms[which].linear;
        let d = p - transforms[which].offset.xy;
        let determinant = l.x * l.w - l.y * l.z;
        let q = select(vec2f(-1e9),
                       vec2f(l.w * d.x - l.z * d.y, l.x * d.y - l.y * d.x) / determinant,
                       determinant != 0.0);
        let rect = clips[clip].rect;
        let distance = roundedRect(q - (rect.xy + rect.zw * 0.5), rect.zw * 0.5, clips[clip].radii);
        let inside = clamp(0.5 - distance * scale * sqrt(abs(determinant)), 0.0, 1.0);
        kept *= select(inside, 1.0 - inside, clips[clip].tags.z != 0u);
        clip = clips[clip].tags.x;
    }
    return kept;
}

@fragment
fn fs(in: Between) -> @location(0) vec4f {
    span = in.span;
    let size = instances[in.index].rect.zw * root.frame.z;
    let kind = instances[in.index].tags.x;
    let uv = imageUv(in.index, in.local);
    let dx = dpdx(uv);
    let dy = dpdy(uv);
    var color: vec4f;
    if (kind == kShadow) {
        color = shadow(in.index, in.local);
    } else if (kind == kImage) {
        color = imageColor(in.index, in.local, size, uv, dx, dy);
    } else if (kind == kGlyph) {
        color = glyph(in.index, in.local, size);
    } else {
        color = box(in.index, in.local, size);
    }
    return color * clipped(instances[in.index].tags.y, in.position.xy);
}
