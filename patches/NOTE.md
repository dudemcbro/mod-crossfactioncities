# Core patch note

This module includes core patches that must be applied to the AzerothCore source tree:
- `core-gameobject-faction-hook.patch` (GameObject faction calc hook)
- `core-taxi-crossfaction-hook.patch` (cross-faction taxi node/mount selection gated by `AllowTwoSide.Interaction.Group`)
- `core-taxi-crossfaction-gossip-fallback.patch` (gossip fallback when the client flight map is empty on faction-locked taxi nodes)

The patches were generated against the mod-playerbots fork of azerothcore-wotlk. Other or newer core checkouts may differ, so:

- The patch may need path adjustments depending on the core tree layout.
- The patch must be validated with `git apply --check` from the core repo root before applying.
- Apply `core-taxi-crossfaction-hook.patch` before relying on `core-taxi-crossfaction-gossip-fallback.patch`; the fallback only runs when the nearest taxi node lookup can find faction-locked nodes, which the hook patch enables.

## Integration checklist (run on the target server in the AzerothCore repo root)

1) Validate the patches apply cleanly:

   git apply --check /path/to/mod-crossfactioncities/patches/core-gameobject-faction-hook.patch
   git apply --check /path/to/mod-crossfactioncities/patches/core-taxi-crossfaction-hook.patch
   git apply --check /path/to/mod-crossfactioncities/patches/core-taxi-crossfaction-gossip-fallback.patch

2) Apply the patches:

   /path/to/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh
   /path/to/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-hook.sh
   /path/to/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-gossip-fallback.sh

3) Re-run the apply scripts to confirm idempotency:

   /path/to/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh
   /path/to/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-hook.sh
   /path/to/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-gossip-fallback.sh

If `git apply --check` fails due to file paths not matching your core tree, regenerate the patch against your core checkout and replace `core-gameobject-faction-hook.patch` with the corrected version.
