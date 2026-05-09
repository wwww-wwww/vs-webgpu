# vs-webgpu

## Example

```python
device = core.webgpu.Device()

shader = """
@group(0) @binding(0) var inputTex: texture_storage_2d<rgba8unorm, read>;
@group(0) @binding(1) var outputTex: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let size = textureDimensions(outputTex);
    if (id.x >= size.x || id.y >= size.y) { return; }

    let color = textureLoad(inputTex, id.xy);
    textureStore(outputTex, id.xy, color);
}"""

pipeline = core.webgpu.Pipeline(device, shader)
clip = core.webgpu.Compute(clip, device, pipeline)
```
