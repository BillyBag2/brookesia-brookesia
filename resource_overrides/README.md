# LittleFS resource overrides

This sparse tree contains project-owned resource changes that must survive
managed-component refreshes and resource staging. Keep each override at the same
relative path it has in the native `littlefs` tree.

Both builds use `cmake/resource_overrides.cmake` after their component resources
have been staged:

- Native overlays `system/` and `apps/` into this project's generated `littlefs/`
  tree before creating the partition image.
- WASM overlays `system/` into its system-resource root and `apps/` into its
  emulated LittleFS root.

Edit authoritative customizations here rather than editing `littlefs/` or
`managed_components/` directly. A native build may regenerate `littlefs/`, and
dependency fetching may regenerate managed components.

## M5Stack TAB5 desktop wallpaper

The wallpaper is currently disabled for performance testing. The background
screen draws a solid pale lime `#e8f5d0` surface instead. The image is not
registered, so the unused binary is not preloaded. Its binary and source assets
remain in the tree so it can be restored later.

The shared native/WASM wallpaper is
`system/super/shell/images/background/m5stack_tab5_portrait.bin`. It is an
opaque 720 x 1280 LVGL RGB565 image drawn 1:1 on the TAB5 display. It does not
tile or invoke LVGL's runtime image scaling. The portrait master is
`assets/m5stack_tab5_portrait_source.jpg`, derived from
`assets/broockesia-brookesia.png`; keep both source assets when producing later
revisions. The current
version preserves the source pixels: mirror it horizontally, rotate it 75
degrees clockwise initially, then adjust the crop 20 degrees clockwise about
the visible branch centre (equivalent to a final 55-degree clockwise source
rotation). Crop coordinates `(330, 48, 1314, 1798)` from the expanded final
rotation, fill only the newly exposed forest background, then resize to
720 x 1280 and encode as an optimized JPEG. The original animal and branch are
composited over that fill so their anatomy and texture remain unchanged.
The muted light-green `#c9dfc2` tint at approximately 60 percent opacity is
baked into `assets/m5stack_tab5_portrait_360x640_tinted.jpg`. Each tinted source
pixel is then duplicated into a 2 x 2 block before RGB565 conversion, providing
a deliberately cheap nearest-neighbour upscale without runtime interpolation.
When enabled, the matching image-set and screen overrides should register the
binary as `desktop.background`. The launcher override leaves its surface
transparent so the background layer shows through. There is no separate
translucent full-screen tint object to blend during redraws.

After changing the source image, regenerate the RGB565 binary with LVGL's
`scripts/LVGLImage.py`, then copy the same `.bin` file into the matching path
below `littlefs/` for the checked-in native tree. Both normal builds reapply
this override, so a regenerated native LittleFS tree and a freshly staged WASM
LittleFS receive the same wallpaper.
