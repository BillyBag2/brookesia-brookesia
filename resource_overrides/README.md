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
