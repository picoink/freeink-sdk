# Reader icon subset

These unchanged Lucide SVGs match the original PicoInk reader's visual vocabulary.
`SOURCES.json` pins the upstream revision and individual SHA-256 digests. Retain
`LICENSE` when distributing the generated icons.

Generate the portable 40px header from the SDK root with the existing tool:

```sh
python3 libs/assets/Icons/tools/gen_icons.py \
  --manifest libs/assets/Icons/manifests/reader.txt \
  --svgdir libs/assets/Icons/reader/svg --sizes 40 \
  --out libs/ui/FreeInkUI/include/FreeInkUIReaderIcons.h
```

Requires librsvg (`rsvg-convert`) and Pillow. The output uses the SDK's existing
1-bit mask convention and `bitmapFromIcon` bridge. It contains no application or
hardware implementation. PicoInk SDK distributes a matching portable header.
