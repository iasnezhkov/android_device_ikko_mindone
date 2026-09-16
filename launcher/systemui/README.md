# Lock screen and always-on display in the ROM

Screens 11 and 12 of the brief are SystemUI, not launcher, work. The least invasive route is a
**SystemUI clock plugin**: the keyguard already delegates its clock faces to `ClockProviderPlugin`
implementations, which is how Pixel ships its clock styles. A plugin gives us the big lock-screen
clock, the AOD face, burn-in shifting and doze animation without patching Keyguard. What a plugin
cannot draw (the "3 new · Telegram, Slack" line, the PIN hint, hiding the stock notification shelf)
is a small Keyguard patch listed at the end.

> 🔴 **The platform moved under this document.** It was written against `android15-release`
> (LineageOS 22.x). The ROM is **LineageOS 23.2 / Android 16**, and the clock plugin moved with it:
> the ROM session finds `ClockProviderPlugin.kt` at
> `packages/SystemUI/plugin/src/com/android/systemui/plugins/keyguard/ui/clocks/`, not at
> `.../plugins/clocks/`. The plugin route still exists and is still the right one, but **every
> interface name in §1 below must be re-read from the Android 16 tree before any of it is written.**
> Confirmed on 8 September 2026 with the ROM session; the keyguard side of the same question is in
> `KEYGUARD.md`.

Originally verified against `frameworks/base` at `android15-release` (paths as of Android 15):

- `packages/SystemUI/plugin/src/com/android/systemui/plugins/clocks/ClockProviderPlugin.kt`
- `packages/SystemUI/customization/src/com/android/systemui/shared/clocks/ClockRegistry.kt`

## 1. The plugin

A separate APK, `LauncherClock`, signed with the platform key, declaring the SystemUI plugin
permission and the action SystemUI scans for:

```xml
<uses-permission android:name="com.android.systemui.permission.PLUGIN" />
<service android:name=".LauncherClockPlugin" android:exported="true">
    <intent-filter><action android:name="com.android.systemui.action.PLUGIN_CLOCK_PROVIDER" /></intent-filter>
</service>
```

Implementation surface (names from `ClockProviderPlugin.kt`):

| Interface | We implement | Notes |
|---|---|---|
| `ClockProviderPlugin : Plugin, ClockProvider` | `getClocks()` → one `ClockMetadata(clockId = "launcher")`; `createClock(settings)` → our controller; `getClockThumbnail(id)` | Registered by `ClockRegistry`; selectable in the wallpaper picker, and the ROM sets it as default |
| `ClockController` | `smallClock` and `largeClock` (`ClockFaceController`), `config: ClockConfig(id, name, description)`, `events: ClockEvents`, `initialize(...)` | Large = lock screen 72 sp light clock, small = the header-sized clock when notifications push it up |
| `ClockFaceController` | `view` (a custom View drawing time in Golos), `layout` (`DefaultClockFaceLayout(view)` is enough), `config: ClockFaceConfig(tickRate = PER_MINUTE)`, `events`, `animations` | `ClockFaceConfig.hasCustomWeatherDataDisplay` lets us draw the weather line ourselves |
| `ClockFaceEvents` | `onTimeTick()` redraws; `onRegionDarknessChanged`, `onFontSettingChanged`, `onTargetRegionChanged` | Region darkness = paper vs ink |
| `ClockEvents` | `onTimeZoneChanged`, `onTimeFormatChanged`, `onLocaleChanged`, `onWeatherDataChanged(WeatherData)`, `onAlarmDataChanged`, `onZenDataChanged` | SystemUI pushes weather from its smartspace provider; on our ROM we feed Open-Meteo through the same path or draw our own |
| `ClockAnimations` | `enter()`, `doze(fraction)`, `charge()`, `onPositionUpdated(...)` | `doze` = lock → AOD: cross-fade the 72 sp face to the 44 sp `#5F5F5A` AOD face; no springs |
| `ClockFaceLayout.applyAodBurnIn(AodClockBurnInModel)` | scale/translationX/translationY | SystemUI drives burn-in shifting itself; we only apply the model |

Tokens come from the same `theme` values (copy the ink/paper palettes and the type roles; the
plugin cannot depend on the launcher APK).

## 2. Keyguard patch (small, in `frameworks/base/packages/SystemUI`)

1. Notification summary line under the clock: replace the shelf on the lock screen with a single
   line "N new · App, App" (words, no icons) — a `KeyguardStatusView` sibling fed by
   `NotificationListener` counts, muted apps excluded, mirroring `HubGrouper.unreadCount`.
2. Hide the stock lock-screen notification shelf and media when our line is shown
   (`config_keyguardShowNotifications` style flags plus a Lineage overlay).
3. PIN with a hardware keyboard: `KeyguardPINView` already accepts key events; only the
   "type PIN to unlock" hint (12 sp, `onSurfaceVariant`) is new.
4. AOD: `config_dozeAlwaysOnDisplayAvailable=true`, default "show on lift/tap" not "always"
   (`doze_always_on` off, `doze_pulse_on_pick_up` on) — brief §6 I and the 2200 mAh budget.
5. Reclaim the top strip: `config_mainBuiltInDisplayCutout` empty, `status_bar_height` 28 dp
   (see `../README.md`).

## 3. Peak widget: the launcher's own settings on the lock screen (3.10)

The plugin runs in SystemUI's process and cannot read the launcher's DataStore. The launcher
therefore mirrors the five values the lock screen cares about into `Settings.Secure`
(`core/env/SystemSettingsMirror.kt`), which it may do only in the `system` build: the permission is
`WRITE_SECURE_SETTINGS`, declared in `app/src/system/AndroidManifest.xml` and granted through
`rom/permissions/privapp-permissions-app.launcher.xml`. Everywhere else the mirror is a no-op — it
checks the permission at run time, not the flavor.

| Key | Type | Meaning |
|---|---|---|
| `launcher_primary_hand` | int, 0 = right, 1 = left | Which edge carries the reachable controls |
| `launcher_show_weather` | int, 0/1 | Whether the header line carries the temperature |
| `launcher_show_battery` | int, 0/1 | Whether the header line carries the charge |
| `launcher_paper_theme` | int, 0/1 | Paper rather than ink |
| `launcher_accent` | string, `AMBER`/`SLATE`/`SAGE`/`PLAIN` | Which accent is chosen |

Read them with a `ContentObserver` on each `Settings.Secure.getUriFor(key)`, registered while the
keyguard view is attached, so a change on Home reaches the lock screen without a reboot.

Structure of the peak widget, top to bottom, mirrored horizontally by `launcher_primary_hand`:

1. **Time** — `display_lock` 72 sp on the lock screen, `display_aod` 44 sp in `#5F5F5A` on AOD,
   tabular figures.
2. **Date** — `body_m` 14 sp, `onSurfaceVariant`, directly under the time.
3. **Weather** — one line, "21° Overcast", only when `launcher_show_weather` is 1. Words, not
   glyphs; there is still no weather glyph set (see the launcher's `OpenMeteo.label`).
4. **Charge** — on the opposite edge from the clock, `label_m` 12 sp, only when
   `launcher_show_battery` is 1.
5. **The notification summary line** from §2 below everything else.

`launcher_paper_theme` decides the palette when SystemUI's own `onRegionDarknessChanged` disagrees
with the launcher (a light wallpaper under a dark launcher); the launcher's choice wins, because
the two screens should not disagree about which theme the phone is in.

## 4. Order of work

1. Plugin skeleton with the large and small faces, installed as `priv-app` with the platform
   signature; verify it appears in `ClockRegistry` (`adb shell dumpsys activity service SystemUI`
   shows registered clocks) — on the MindOne only when asked.
2. AOD face and `doze(fraction)`; burn-in model applied.
3. Keyguard patch for the summary line; overlays for AOD and the status bar.
4. Paper theme via `onRegionDarknessChanged`, overridden by `launcher_paper_theme`.
5. Peak widget: read the five `Settings.Secure` keys, mirror the layout by the primary hand.
