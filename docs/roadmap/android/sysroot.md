# Android Sysroot

Stable area: reproducible Android ARM64 runtime inputs for generated
`build/sysroot`.

## Policy

- [x] Keep Android runtime roots outside git, for example
      `~/.muplar/sysroots/android-arm64/api-35`.
- [x] Keep the sysroot recipe in git:
      `sysroots/android-arm64-api35.recipe.json`.
- [x] Generate `build/sysroot` from explicit local inputs.
- [x] Copy NDK fixture runtime files through `tools/prepare-android-sysroot.sh`,
      not ad hoc test-script copies.
- [ ] Add hash recording for imported Android root files once a canonical
      source is selected.

## Commands

Prepare the native-test sysroot inputs without importing ART:

```sh
tools/prepare-android-sysroot.sh --sysroot build/sysroot --no-android-root
```

Prepare with a cached Android ART root:

```sh
tools/prepare-android-sysroot.sh \
  --android-root ~/.muplar/sysroots/android-arm64/api-35 \
  --sysroot build/sysroot \
  --strict-art
```

Verify Java/ART readiness:

```sh
tools/check-android-art-sysroot.sh --sysroot build/sysroot
```
