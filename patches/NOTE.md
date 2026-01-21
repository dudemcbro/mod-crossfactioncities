# Core patch note

`core-gameobject-faction-hook.patch` is a core change that must be applied to the AzerothCore source tree.

This module repo is prepared in `/data/modules` for later transfer/integration. Until you apply it against the *actual* target core checkout, you must assume:

- The patch may need path adjustments depending on the core tree layout.
- The patch must be validated with `git apply --check` from the core repo root before applying.

## Integration checklist (run on the target server in the AzerothCore repo root)

1) Validate the patch applies cleanly:

   git apply --check /path/to/mod-crossfactioncities/patches/core-gameobject-faction-hook.patch

2) Apply the patch:

   /path/to/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh

3) Re-run the apply script to confirm idempotency:

   /path/to/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh

If `git apply --check` fails due to file paths not matching your core tree, regenerate the patch against your core checkout and replace `core-gameobject-faction-hook.patch` with the corrected version.
