# Core patches

This module ships core patches that are applied to the AzerothCore source tree:

| Patch | What it adds | Apply script |
| --- | --- | --- |
| `core-gameobject-faction-hook.patch` | A hook that lets the module change the faction a GameObject shows to a player | `apply-core-gameobject-faction-hook.sh` |
| `core-taxi-crossfaction-hook.patch` | Cross-faction taxi node and mount selection, active when `AllowTwoSide.Interaction.Group = 1` | `apply-core-taxi-crossfaction-hook.sh` |
| `core-taxi-crossfaction-gossip-fallback.patch` | A gossip list of destinations when the client flight map is empty on faction-locked taxi nodes | `apply-core-taxi-crossfaction-gossip-fallback.sh` |

The patches apply cleanly to the `custom` branch of azerothcore-wotlk-playerbots-custom
and were made against the mod-playerbots fork of azerothcore-wotlk. Other or newer
cores may differ, so check them with `git apply --check` before applying.

Apply `core-taxi-crossfaction-hook.patch` before relying on
`core-taxi-crossfaction-gossip-fallback.patch`: the fallback only runs when the nearest
taxi node lookup can find faction-locked nodes, which the hook patch enables.

## Checklist

Run these from the root of the AzerothCore source tree, with the module cloned to
`modules/mod-crossfactioncities`.

1. Check that the patches apply cleanly:

   ```sh
   git apply --check modules/mod-crossfactioncities/patches/core-gameobject-faction-hook.patch
   git apply --check modules/mod-crossfactioncities/patches/core-taxi-crossfaction-hook.patch
   git apply --check modules/mod-crossfactioncities/patches/core-taxi-crossfaction-gossip-fallback.patch
   ```

2. Apply them:

   ```sh
   modules/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh
   modules/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-hook.sh
   modules/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-gossip-fallback.sh
   ```

   Each script prints `APPLIED` the first time and `OK: already applied` when run
   again, so running them twice is safe.

3. Re-run CMake and rebuild the core.

To remove a patch, run `git apply -R` with the same file from the core root.

If `git apply --check` fails because your core tree differs, regenerate that patch
against your core checkout and replace the file in `patches/`.
