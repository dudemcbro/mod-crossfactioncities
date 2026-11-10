# Installation

## Requirements

- AzerothCore WotLK (3.3.5a). The patches apply cleanly to stock
  [azerothcore-wotlk](https://github.com/azerothcore/azerothcore-wotlk) `master`
  (commit 3e08bfc, October 2026), to the
  [mod-playerbots AzerothCore fork](https://github.com/mod-playerbots/azerothcore-wotlk)
  (`Playerbot` branch, January and September 2026) and to the `custom` branch of
  [azerothcore-wotlk-playerbots-custom](https://github.com/dudemcbro/azerothcore-wotlk-playerbots-custom).
  Other cores may need them adjusted.
- bash and git (the apply scripts use `git apply`).
- The core patches in [`patches/`](../patches/NOTE.md):
  - `core-gameobject-faction-hook.patch`: a GameObject faction hook. Without it the
    module still builds, but the gameobject part of `NeutralMailboxes` and all of
    `NeutralGameObjects` silently do nothing.
  - `core-taxi-crossfaction-hook.patch`: cross-faction taxi node and mount selection.

Players only need to clear their client's `Cache` folder after you turn on
`NpcSpeechUniversal` or `UnlockFactionItems`. With `UnlockFactionAchievements` or
`LearnLanguages`, players build the optional patch from their own client so the other
faction's achievements show in the achievement window and the extra languages can be used
([client-patch.md](client-patch.md)).

## Steps

1. Clone the module into the core's `modules` directory:

   ```sh
   cd <azerothcore>/modules
   git clone https://github.com/dudemcbro/mod-crossfactioncities.git
   ```

2. Apply the core patches from the root of the AzerothCore source tree. Each script
   applies its patch once and does nothing if it is already applied:

   ```sh
   cd <azerothcore>
   modules/mod-crossfactioncities/patches/apply-core-gameobject-faction-hook.sh
   modules/mod-crossfactioncities/patches/apply-core-taxi-crossfaction-hook.sh
   ```

   See [patches/NOTE.md](../patches/NOTE.md) for how to check the patches against
   your core first.

3. Re-run CMake and rebuild the core.
4. Copy the config: the build installs `mod_crossfactioncities.conf.dist` to your
   server's `etc/modules` directory. Copy it to `mod_crossfactioncities.conf` there and
   edit it (see [configuration.md](configuration.md)).
5. Optionally set up the database for cross-faction quests (below).
6. Restart `worldserver`.

## Database setup

`sql/world/base/allow_crossfaction_quests.sql` lets players see and take the other
faction's quests. The worldserver's updater only imports a module's `data/sql/world`,
`data/sql/characters` and `data/sql/auth` folders, so this file is not imported
automatically. Run it by hand on the world database (usually `acore_world`), from the
root of the AzerothCore source tree:

```sh
mysql -u <user> -p acore_world < modules/mod-crossfactioncities/sql/world/base/allow_crossfaction_quests.sql
```

| File | What it does |
| --- | --- |
| `sql/world/base/allow_crossfaction_quests.sql` | Saves every quest's `AllowableRaces` in `quest_template_allowableraces_backup`, then sets `quest_template.AllowableRaces = 0` for all quests. Safe to run again: the first backup of each quest is kept. |
| `sql/world/base/restore_crossfaction_quests.sql` | Restores `AllowableRaces` from the backup table and drops it. Safe to run again. |

These are the only files that change the database. Every other feature works on the
server's in-memory data.

## Updating

```sh
cd <azerothcore>/modules/mod-crossfactioncities
git pull
```

Run the apply scripts again (they skip patches that are already applied; if a patch
changed, reverse the old version with `git apply -R` first), re-run CMake and rebuild.
Compare your `mod_crossfactioncities.conf` with the new `.conf.dist` for new options.
An option missing from your config uses the value shipped in the `.conf.dist`.

## Uninstall

1. If you set `LearnLanguages` above 0, set it to 0 and restart once while the module is
   still installed, so the extra languages are removed as players log in. Without the
   module the core also deletes them at each character's next login, but leaves the
   language spells behind.
2. Restore the quest race masks (from the root of the AzerothCore source tree):

   ```sh
   mysql -u <user> -p acore_world < modules/mod-crossfactioncities/sql/world/base/restore_crossfaction_quests.sql
   ```

3. Reverse the core patches you applied, from the core root:

   ```sh
   git apply -R modules/mod-crossfactioncities/patches/core-taxi-crossfaction-hook.patch
   git apply -R modules/mod-crossfactioncities/patches/core-gameobject-faction-hook.patch
   ```

4. Delete `modules/mod-crossfactioncities` and your server's
   `etc/modules/mod_crossfactioncities.conf`, re-run CMake and rebuild.

Items, achievements and inns go back to normal on their own: the module never wrote
those changes to the database. Achievements a player already earned stay earned.
