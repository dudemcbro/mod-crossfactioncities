-- Allow all races/factions to see quests during cross-faction city visits.
-- Run against the WORLD database (for example acore_world).
-- A backup table stores the original AllowableRaces mask for rollback if needed.
-- Repeat safe: INSERT IGNORE keeps the first backup of each quest, so a second
-- run cannot overwrite the original masks with the already-cleared value 0.
CREATE TABLE IF NOT EXISTS quest_template_allowableraces_backup (
  ID int unsigned NOT NULL PRIMARY KEY,
  AllowableRaces int unsigned NOT NULL
) ENGINE=InnoDB;
INSERT IGNORE INTO quest_template_allowableraces_backup (ID, AllowableRaces)
SELECT ID, AllowableRaces
FROM quest_template;
UPDATE quest_template
SET AllowableRaces = 0
WHERE AllowableRaces <> 0;
