# CaptureView UI style

The setup UI and the planned application icon share one restrained blue visual
identity. The icon concept is the circular **A (CV)** mark: a cable-shaped C,
white V, and deep blue field.

## Palette

| Role | Color | Use |
|---|---|---|
| CaptureView blue | `#185294` | Primary action, future icon field, focus accents |
| Pressed blue | `#113F74` | Pressed primary action |
| Canvas | `#F6F8FB` | Setup-window background |
| Surface | `#FFFFFF` | Device-settings card and secondary action |
| Border | `#DAE0E8` | Card and secondary-action outlines |
| Primary text | `#1D2A3A` | Heading, labels, values |
| Secondary text | `#657180` | Descriptions and helper text |

The icon may use a subtle darker edge within the same blue family, but the UI
should avoid gradients and large saturated areas. This keeps the setup screen
quiet while allowing the A icon and **Start capture** action to read as one
brand.

## Icon assets

- `assets/captureview.png` is the RGBA source generated from concept A.
- `assets/captureview.ico` contains 16, 24, 32, 48, 64, 128, and 256 px PNG
  entries and is embedded through `src/resources.rc`.
- Regenerate the ICO without installing an image package:

  ```text
  python tools/make_icon.py assets/captureview.png assets/captureview.ico
  ```

At 16–32 px the cable curve, V, and plug silhouettes take priority over small
connector detail. The transparent corners must remain real alpha, not a
checkerboard preview baked into the source.

## Layout principles

- Keep native combo boxes for keyboard, accessibility, and device-name behavior.
- Group related selectors on a white card rather than adding tabs or a sidebar.
- Use one blue primary action and one neutral secondary action.
- Scale every measurement from 96 DPI and retain Per-Monitor DPI V2 behavior.
- Prefer Segoe UI Variable when present and allow Windows font fallback.
