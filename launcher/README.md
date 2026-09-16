# ROM integration (LineageOS 23.2, Android 16)

The launcher ships as a privileged system app. Launcher3QuickStep (TrebuchetQuickStep) stays
in the image only to provide Recents and gesture animations; our app wins the HOME role.

> Platform target confirmed with the ROM session on 8 September 2026 against the live device tree
> (`device/ikko/mindone`, adapted from a LineageOS device tree for the same chipset). Earlier
> revisions of this file said 22.x / Android 15; everything below was written against 15 and is
> re-checked item by item as it is used.

## Where our files go in the device tree

The device tree is `device/ikko/mindone` and already has the two conventions we need:

| Ours | Theirs | How it is consumed |
|---|---|---|
| `rom/overlay/**` | `device/ikko/mindone/overlay/**` | `DEVICE_PACKAGE_OVERLAYS`. With `PRODUCT_ENFORCE_RRO_TARGETS := *` the build turns each into a runtime RRO (`*__lineage_mindone__auto_generated_rro_vendor.apk`) in the vendor image; the base package stays stock |
| `rom/permissions/privapp-permissions-app.launcher.xml` | `device/ikko/mindone/configs/permissions/` | `PRODUCT_COPY_FILES` into `etc/permissions/` |
| `rom/permissions/default-permissions-app.launcher.xml` | same directory | `PRODUCT_COPY_FILES` into `etc/default-permissions/` |
| source patches | `device/ikko/mindone/patches/<repo>-NNNN-mindone-<topic>.patch` | applied by hand, no local manifest |

Two things to know before relying on the overlay path:

- **A values-only overlay is safe; a layout overlay often is not.** An RRO is linked as its own
  package and `aapt2 link` sees only `android.jar` plus the target package's resources, so a layout
  that references a library attribute (`layout_constraint*` from ConstraintLayout, and similar)
  fails to link. The ROM session hit exactly this on the camera app. Anything structural is a source
  patch, not an overlay.
- `ro.control_privapp_permissions` is `log`, not `enforce`, so a missing allowlist entry shows up in
  the log rather than as a boot failure. Do not read a quiet boot as proof the allowlist is right.

## Steps

1. Build: `./gradlew :app:assembleSystemRelease` → copy `app/build/outputs/apk/system/release/app-system-release.apk`
   here as `Launcher.apk`. Sign with the release key; `presigned: true` keeps that signature.
   Switch to `certificate: "platform"` only if a signature-level permission becomes necessary.
   Note that the ROM currently signs with the AOSP **testkey**, so `certificate: "platform"` there
   means the test key — fine for development, not for anything shipped.
2. Put this `rom/` directory into the device tree (e.g. `device/ikko/mindone/launcher/`) and add
   `$(call inherit-product, device/ikko/mindone/launcher/launcher.mk)` to the device makefile.
3. Keep `TrebuchetQuickStep` in `PRODUCT_PACKAGES` (LineageOS adds it in `vendor/lineage`).
   Do not change `config_recentsComponentName`.
4. Default HOME: `HomeRoleBehavior.getFallbackHolder()` (packages/modules/Permission) picks the
   system HOME activity with the highest `android:priority`. The `system` flavor manifest sets
   `priority="1"`; Trebuchet is 0. Verify after first boot that no chooser appears:
   `adb shell cmd role get-role-holders android.app.role.HOME`.
   Fallback if a device still shows the chooser: patch Trebuchet's manifest in the ROM to drop
   `android.intent.category.HOME` from its `Launcher` activity, keeping `RecentsActivity`.
5. Notification access is pre-granted via `config_defaultListenerAccessPackages` (overlay).
   Check: `adb shell settings get secure enabled_notification_listeners`.
6. Runtime permissions are pre-granted via `default-permissions-app.launcher.xml`.
7. Reclaim the top strip: the stock firmware reports a 100 px display cutout and a 40 dp status bar
   although the panel has no camera hole. In the device overlay clear `config_mainBuiltInDisplayCutout`
   and set `status_bar_height` to 28 dp. The launcher draws nothing in the strip: it hides the
   SystemUI clock via `Settings.Secure.icon_blacklist` (WRITE_SECURE_SETTINGS, priv-app) while it is
   the default home and puts it back when it is not, because Home carries the time itself. The strip
   keeps signal, Wi-Fi and notifications. Decided 13 September 2026; see DESIGN-REVIEW.md §1.
   Check: `adb shell settings get secure icon_blacklist` → `clock`.
8. Lock screen, AOD and the PIN entry are SystemUI work (`frameworks/base/packages/SystemUI`),
   tracked in `systemui/README.md` and `systemui/KEYGUARD.md`; they only share tokens and
   typography with the launcher.

## What the app does differently as a system install

Detected at runtime by `CapabilitiesProbe` (priv-app path, listener granted, default home),
not by build flavor alone: onboarding steps are skipped, and the ribbon's double-tap-to-sleep
can use a system path instead of the accessibility service.
