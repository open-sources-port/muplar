# AOSP Launcher3 Compatibility Fixture

Muplar pins the AOSP Android 15 `Launcher3` module at the revision recorded in
`PIN.env`. Launcher3 is a Soong platform application, not a standalone Android
SDK/Gradle project. Its build depends on hidden platform APIs, generated flags,
SystemUI libraries, AndroidX, Kotlin, Dagger, and framework resources.

Fetch and inspect the exact source without storing it in Git:

```sh
platform/android-aarch64/compat/launcher3/fetch-source.sh
platform/android-aarch64/compat/launcher3/analyze-framework-api.py \
  build/launcher3/source build/launcher3/framework-api-report.txt
```

Build the pinned module in a matching AOSP checkout:

```sh
source build/envsetup.sh
lunch aosp_arm64-userdebug
m Launcher3
```

Then import the resulting `Launcher3.apk` into Muplar's ignored build tree:

```sh
platform/android-aarch64/compat/launcher3/import-apk.sh \
  /path/to/aosp/out/target/product/<product>/system_ext/priv-app/Launcher3/Launcher3.apk
```

The importer validates the package and launch activity and writes source pin,
APK hash, and provenance alongside `build/launcher3/fixture/Launcher3.apk`.
No Launcher3 source or binary is checked into the Muplar repository.

An official system-image APK can be used for early runtime compatibility, but
must identify its independent provenance rather than claim the source pin:

```sh
LAUNCHER3_ARTIFACT_PROVIDER=official-system-image \
LAUNCHER3_ARTIFACT_REF=system-images\;android-35\;default\;arm64-v8a_r02 \
LAUNCHER3_ARTIFACT_CHECKSUM=2026a06409db630b56711afdbffb457c1dbaed49 \
  platform/android-aarch64/compat/launcher3/import-apk.sh Launcher3QuickStep.apk
```

The complete download/extraction/import flow is automated by:

```sh
platform/android-aarch64/compat/launcher3/fetch-official-apk.sh
```
