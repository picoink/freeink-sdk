# Optional image backend

`ImageAccelerator` implements `book::ImageBackend`. Pass it to `ImageRenderer`
or `PageRenderer::renderImages`; existing consumers retain the software path.
Create, execute and destroy an instance on one worker. `Work` supplies cancellation,
a display-bandwidth lease and a transient-memory ceiling. No ESP-IDF type appears
in the public interface. Row data is borrowed only during its callback; the backend
releases compressed/raw DMA buffers before returning. Cancelled work returns
`Stopped`, which must not restart the software decoder.

On the tested S31 IDF 6.1 toolchain, AUTO decodes standard baseline JPEG to GRAY
or YUV444 and extracts full-range luma. The installed driver's unswizzled YUV444
DMA bytes are V,U,Y (verified against software on 4:4:4/4:2:2/4:2:0 and padded
images). Its RGB converter uses limited-range video coefficients; RGB plus PPA
conversion is available explicitly for diagnostics but is not the default because
it changes JPEG gray levels. Other platforms decline baseline acceleration.

Progressive JPEG reuses the SDK's full-resolution `ProgressiveJpeg` decoder.
PNG, unsupported JPEG dimensions/sampling, insufficient memory and driver failures
fall back to FreeInkBook. The default transient ceiling is 8 MiB with at least
2 MiB free PSRAM preserved, checked against both free memory and the largest block.
JPEG DMA allocations use the driver allocator; PPA output uses the actual cache
line alignment. Transactions complete before buffers are freed or reused.

Area reduction is shared with the software renderer and preserves thin strokes.
PPA bilinear scaling/rotation and asynchronous memory copy remain disabled until
end-to-end hardware/quality comparisons justify them. Unit-scale RGB-to-gray PPA
was executed on hardware, but rejected as the default color path as described
above. See the consuming PicoInk project's `docs/responsiveness-validation.md`
for device results; host builds alone do not establish hardware acceleration.
