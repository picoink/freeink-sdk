# Reader and keyboard icon subset

These unchanged Lucide SVGs match the original PicoInk reader's visual vocabulary.
`SOURCES.json` pins the upstream revision and individual SHA-256 digests. Retain
`LICENSE` when distributing the generated icons.

Generate the portable reader and keyboard headers from the SDK root:

```sh
python3 libs/assets/Icons/tools/gen_icons.py \
  --manifest libs/assets/Icons/manifests/reader.txt \
  --svgdir libs/assets/Icons/reader/svg --sizes 24,40 \
  --out libs/ui/FreeInkUI/include/FreeInkUIReaderIcons.h

python3 libs/assets/Icons/tools/gen_icons.py \
  --manifest libs/assets/Icons/manifests/keyboard.txt \
  --svgdir libs/assets/Icons/reader/svg --sizes 24,28,32 --raw \
  --out libs/ui/FreeInkUI/include/FreeInkUIKeyboardIconData.h

python3 libs/assets/Icons/tools/test_gen_icons.py
```

Requires librsvg (`rsvg-convert`) and Pillow. SVGs are rasterized at 4x resolution,
area averaged and quantized to 16 coverage levels. Each row packs two pixels per
byte, high nibble first; 0 is transparent and 15 is opaque. Optical centers are
weighted by coverage. `bitmapFromIcon` supplies a `Mask4` bitmap: the Gray8 target
blends it with the existing background and requested foreground, including white
and disabled colors. Monochrome targets use ordered dithering. The keyboard data
header has no dependency on the Icons library.

It contains no application or hardware implementation. PicoInk SDK distributes
matching portable headers. All consumers must regenerate their icons when
updating to the 4-bit `Icon` format.
