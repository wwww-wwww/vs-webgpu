# vs-webgpu

## Examples

### Copy

```python
device = core.webgpu.Device()

shader = """
struct Uniforms {
    frame: f32,
    pad0: f32,
    pad1: f32,
    pad2: f32,
}

@group(0) @binding(0) var inputTex: texture_2d<f32>;
@group(0) @binding(1) var outputTex: texture_storage_2d<rgba16float, write>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(3) var<uniform> baseUniforms: Uniforms;

@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let size = textureDimensions(outputTex);
    if (id.x >= size.x || id.y >= size.y) { return; }

    let _s = samp;
    let _u = baseUniforms;

    let color = textureLoad(inputTex, id.xy, 0);
    textureStore(outputTex, id.xy, color);
}"""

pipeline = core.webgpu.Pipeline(device, shader)
clip = core.webgpu.Compute(clip, device, pipeline)
```

### haasn deband

```python
device = core.webgpu.Device()

shader = """
struct Uniforms {
    frame: f32,
    pad0: f32,
    pad1: f32,
    pad2: f32,
}

@group(0) @binding(0) var inputTex: texture_2d<f32>;
@group(0) @binding(1) var outputTex: texture_storage_2d<rgba16float, write>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(3) var<uniform> baseUniforms: Uniforms;

fn sample_yuv(uv: vec2<f32>) -> vec4<f32> {
    return textureSampleBaseClampToEdge(inputTex, samp, uv);
}

fn mod289(x: f32)  -> f32 { return x - floor(x / 289.0) * 289.0; }
fn permute(x: f32) -> f32 { return mod289((34.0*x + 1.0) * x); }
fn rand(x: f32)    -> f32 { return fract(x / 41.0); }

struct AverageOut {
    avg: vec3<f32>,
    h: f32,
}

fn average(uv: vec2<f32>, size: vec2<f32>, range: f32, h0: f32) -> AverageOut {
    var h = h0;

    // Compute a random rangle and distance
    let dist = rand(h) * range;     h = permute(h);
    let dir  = rand(h) * 6.2831853; h = permute(h);

    let o = vec2<f32>(cos(dir), sin(dir)) * dist / size;

    // Sample at quarter-turn intervals around the source pixel
    let avg =
        sample_yuv(uv + vec2(o.x, o.y)).rgb +
        sample_yuv(uv + vec2(-o.x, o.y)).rgb +
        sample_yuv(uv + vec2(-o.x, -o.y)).rgb +
        sample_yuv(uv + vec2(o.x, -o.y)).rgb;

    // Return the (normalized) average
    return AverageOut(avg / 4.0, h);
}

@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let size = textureDimensions(inputTex);

    if (id.x >= size.x || id.y >= size.y) { return; }

    let uv = (vec2<f32>(id.xy) + 0.5) / vec2<f32>(size);

    let m = vec3(uv, baseUniforms.frame / 1000) + vec3(1.0);
    var h = permute(permute(permute(m.x) + m.y) + m.z);

    let grain = 64.0;
    let threshold = 48.0;
    let range = 15.0;

    let sample = textureLoad(inputTex, id.xy, 0);

    var debanded = sample.rgb;

    for (var i = 1; i <= 3; i++) {
        // Use the average instead if the difference is below the threshold
        let avg = average(uv, vec2<f32>(size), f32(i) * range, h);
        h = avg.h;
        let diff = abs(debanded - avg.avg);
        let comp_val = threshold / (f32(i) * 16384.0);
        debanded = mix(avg.avg, debanded, vec3<f32>(diff > vec3<f32>(comp_val)));
    }

    var noise = vec3(0.0);
    noise.x = rand(h); h = permute(h);
    noise.y = rand(h); h = permute(h);
    noise.z = rand(h); h = permute(h);
    debanded += (grain / 8192.0) * (noise - vec3(0.5));

    textureStore(outputTex, id.xy, vec4<f32>(debanded, sample.a));
}
"""

pipeline = core.webgpu.Pipeline(device, shader)

clip = clip.resize.Point(clip, format=vs.YUV444PH)
clip = core.webgpu.Compute(clip, device, pipeline)
```
