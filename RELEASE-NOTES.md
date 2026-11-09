# Sonic Power Management Release Notes

SonicDE's power service, handling screen brightness, battery, sleep and power-saving settings.

## 6.8.2 (2026-11-10)

- Now presents itself with SonicDE names and identity throughout the desktop.
- Behind the scenes: dropped a test dependency that's no longer needed.

## 6.8.0.1 (2026-10-23)

- Screen brightness can now be adjusted on displays without hardware brightness control, using software dimming, while hardware control is still preferred where available. Brightness also recovers properly when you plug in a monitor or the power service restarts.

## 6.8.0 (2026-10-20)

- Streamlined for SonicDE's X11 desktop: Wayland support has been removed.
- Behind the scenes: updated to build against SonicDE 6.8.0, developer tooling support and a version number update.
