# Full-screen camera viewfinder - parked 07.09, NOT solvable with an overlay

Owner's request: full-screen viewfinder, controls along the edges, transparent.
The change was made as an overlay `overlay/packages/apps/Aperture/.../layout/activity_camera.xml`
and **broke the build**. The cause is structural, not a typo.

## Why a layout overlay is not possible here

`DEVICE_PACKAGE_OVERLAYS` produces a SEPARATE RRO package
(`org.lineageos.aperture.auto_generated_rro_vendor__`). When it is built, `aapt2 link` only sees:
- `prebuilts/sdk/34/public/android.jar` - platform attributes;
- `package-res.apk` of Aperture itself.

But our layout references attributes from THIRD-PARTY libraries - `layout_constraint*` (ConstraintLayout)
and `scaleType` (CameraX `PreviewView`). Their definitions do not exist in the RRO context, and aapt2 fails:

```
error: attribute layout_constraintBottom_toBottomOf
  (aka org.lineageos.aperture.auto_generated_rro_vendor__:layout_constraintBottom_toBottomOf) not found.
```

Conclusion: **RRO works for values (dimens, bools, integers, colors), but not for layout files
that use library attributes.** This is a limitation of the mechanism, it cannot be worked around by trial and error.

## How to do it correctly

Patch the Aperture sources in the tree (`packages/apps/Aperture`) instead of applying an overlay: there
the libraries are in scope and the attributes resolve. Put the patch here too, under `patches/`, next
to `frameworks_base-*.patch` - following the convention already established in the tree.

The saved `activity_camera.xml` is the finished target layout, used to produce a diff against
the original `packages/apps/Aperture/app/src/main/res/layout/activity_camera.xml`.

What was done in it: the viewfinder is stretched to the bottom of its parent; `scaleType` `fitCenter` -> `fillCenter`
(losing part of the frame was confirmed acceptable on the device); the panels were given a `#4D000000` background; the panels
were shrunk from 72dp -> 56dp and the margin from 16dp -> 8dp.
