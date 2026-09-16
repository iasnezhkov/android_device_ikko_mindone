# PIN, pattern and fingerprint in our design language

What the lead asked: can the launcher own the authentication screens, and can they be restyled and
fitted to a 432 × 496 dp panel. Written 8 September 2026 with facts from the ROM session,
which has the device tree and the device.

## 0. The short answers

| Question | Answer |
|---|---|
| Can the launcher app do this? | **No.** Not in `store`, not in `system`, not with the HOME role, not with accessibility. Android has no "keyguard provider" role the way it has HOME, dialer, SMS or IME |
| Can the ROM do this? | **Yes**, `frameworks/base` is built from source in our tree |
| Is there a plugin API, like the clock has? | **No.** Verified against the tree: the whole `plugins/` package is ActivityStarter, DarkIconDispatcher, DozeServicePlugin, Falsing*, FragmentBase, GlobalActions*, IntentButtonProvider, NavigationEdgeBackPlugin, Notification*, OverlayPlugin, PluginDependency, PluginUtils, SensorManagerPlugin, ToastPlugin, ViewProvider, VolumeDialog*. Nothing for the bouncer |
| Overlay or patch? | **Values overlay for colour and metrics, source patch for anything structural.** See §2 |
| Fingerprint UI? | **There is none to style.** See §3 |

Everything an *app* can do about authentication is `BiometricPrompt` (we choose the text, the
system draws the dialog) and `KeyguardManager.requestDismissKeyguard()` (we ask, the system draws).
Both are useful for locking things *inside* the launcher — hidden apps, a private favorite — and
neither touches device unlock. That distinction is worth keeping: a launcher that draws something
that looks like a PIN prompt for device unlock is a phishing surface, which is exactly why the
platform has no such API.

## 1. What is actually on screen at unlock

Three views under `KeyguardSecurityContainer`, one at a time: `KeyguardPINView`,
`KeyguardPatternView`, `KeyguardPasswordView`. Above them the clock (ours, via the plugin) and the
notification area. The fingerprint contributes no view here (§3).

## 2. The path: overlay for values, patch for structure

**Decided: a values-only RRO plus a source patch.** Not a layout overlay.

The device tree has `DEVICE_PACKAGE_OVERLAYS := device/ikko/mindone/overlay` and
`PRODUCT_ENFORCE_RRO_TARGETS := *`, so a static overlay directory is built into a runtime RRO
(`*__lineage_mindone__auto_generated_rro_vendor.apk`) that lands in the vendor image while the base
package stays stock. That is the cheapest possible change and it survives upstream merges.

It has one hard limit, learned the expensive way on the ROM side (fact F3908): **an RRO cannot
carry a layout that references a third-party library attribute.** The overlay is linked as its own
package, so `aapt2 link` sees only `android.jar` and the target package's own resources; anything
like `layout_constraint*` from ConstraintLayout fails with "attribute not found". The bouncer
layouts use ConstraintLayout. So:

| Change | Mechanism | Why |
|---|---|---|
| Colours of the keypad, digits, hint text, error text | RRO on `com.android.systemui`, `res/values/colors.xml` | Values link fine |
| Key size, spacing, text sizes, the vertical rhythm of the bouncer | RRO, `res/values/dimens.xml` | Values link fine, and this is most of the 432 × 496 fit |
| Turning features off (haptics, the "emergency call" affordance, ripple) | RRO, `res/values/bools.xml` / `config.xml` | Values link fine |
| Rearranging the keypad, changing the view hierarchy, replacing a widget | **Patch** `frameworks/base` | Layouts, and the ids the Java looks up |
| Typeface | **Patch or a font RRO** — needs a look; SystemUI resolves fonts through the framework theme, so it may fall out of the framework overlay we already ship | Unverified |

Patches live in `device/ikko/mindone/patches/` as `frameworks_base-NNNN-mindone-<topic>.patch` and
are applied by hand. There is already one (`frameworks_base-0001-mindone-ims-dynamic-receiver-flag.patch`)
to copy the style from: a narrow, commented, single-purpose diff.

**Order of work.** Do the values RRO first and look at the result on the device. On a short panel
most of the win is metric, not structural, and a values-only change costs nothing to revert. Only
what is still wrong after that earns a patch.

## 3. Fingerprint: nothing to draw

The ROM ships the HIDL service `android.hardware.biometrics.fingerprint@2.1-service` over the
Silead sensor (`sileadfp`). **HIDL 2.1 has no under-display support at all** — the sensor never
reports a location, so SystemUI never instantiates `UdfpsView`. There is no fingerprint UI on the
lock screen to restyle beyond the generic "touch the sensor" hint string, which is a values change
like any other.

Two things still unknown, and neither is worth guessing at:

- Where the sensor physically is (side, rear). The ROM session knows the HAL, not the placement.
- Whether `SideFpsController` is even reachable with a 2.1 HAL. Probably not, for the same reason
  as UDFPS, but nobody has checked.

If the sensor ever moves to an AIDL HAL this section is wrong and should be re-read.

## 4. The values we would set

Our tokens, as the ROM would consume them. Ink is the primary theme; the launcher's own
`launcher_paper_theme` key (see `README.md` §3) says which one the user is in.

| Role | Ink | Paper |
|---|---|---|
| Background | `#000000` | `#E9E6DF` |
| Surface / key face | `#0B0B0C` | `#F3F1EB` |
| Hairline | `#232327` | `#D2CEC4` |
| Text primary (digits) | `#ECEAE3` | `#2A2A28` |
| Text secondary (hint) | `#9A9A93` | `#6B6B66` |
| Text muted | `#5F5F5A` | `#9A9A93` |
| Accent (focus only) | `#D9A441` | `#805A0E` |
| State layer | `#1AECEAE3` | `#1A2A2A28` |

Metrics, from `theme/src/main/res/values/dimens.xml`: touch target 48 dp, corner scale
4/8/12/16/28, spacing on a 4 dp grid, motion `short3` 150 ms / `short4` 200 ms / `medium1` 250 ms
with `PathInterpolator(0.2, 0, 0, 1)`. Type: digits at `headline_s` 24 sp or `title_l` 22 sp
depending on how tall the keypad ends up; the hint at `body_m` 14 sp; the error at `label_m` 12 sp.

A concrete, ready-to-drop values overlay is in `../overlay/packages/SystemUI/res/values/`. It is
**deliberately unfinished**: it carries our colours under our own names and a header explaining
what has to be filled in, because the actual SystemUI resource names for Android 16 have not been
read yet (§5). Dropping it in as-is changes nothing; it is the skeleton plus the palette.

## 5. What has to be read from the tree before this can be written

I cannot see `frameworks/base` from the launcher checkout — the LineageOS tree lives in the build
VM. These are for the ROM session, and each is a command rather than a question:

```sh
cd frameworks/base/packages/SystemUI

# 1. Which bouncer layouts exist, and do they really use ConstraintLayout?
ls res/layout/keyguard_*view*.xml
grep -l "ConstraintLayout" res/layout/keyguard_*.xml

# 2. The resource names the keypad actually uses, so the overlay can name them.
grep -rn "num_pad\|numeric_key\|keyguard_.*_key" res/values/dimens.xml res/values/colors.xml

# 3. Which colours the bouncer resolves — SystemUI theme attrs or hardcoded @color?
grep -rn "textColor\|background" res/layout/keyguard_pin_view.xml

# 4. Does SystemUI declare <overlayable>? If it does, only the listed resources are overridable
#    by a non-signature RRO, and this whole plan depends on which policy they carry.
find res/values -name 'overlayable*.xml' -exec cat {} +

# 5. Does the bouncer already have a compact/short-screen path we should use instead of fighting it?
grep -rn "isSmallScreen\|one_handed\|compact" res/values/bools.xml res/layout/keyguard_pin_view.xml
```

Answer 4 decides whether the values RRO works at all: if SystemUI declares `<overlayable>` with a
`signature` policy, our RRO must be signed with the platform key — which it is, since the build
generates and signs it itself. If it declares nothing, a vendor RRO can override everything. If it
declares a narrow public policy, some of §4 becomes a patch too.

## 6. Scope, honestly

This is a real piece of work and it is not the launcher's. It touches a repository the launcher
does not build, in a tree that is mid-bring-up and currently has bigger problems (a boot-looping
codec HAL, camera binaries in the wrong path). It should not start until the ROM boots reliably,
and when it does start it belongs in the ROM session's queue, not this one — the launcher's part is
this specification, the palette, and the `Settings.Secure` keys already described in `README.md`.
