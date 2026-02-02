-- Allow all races/factions to see quests during cross-faction city visits.
-- A backup table stores the original AllowableRaces mask for rollback if needed.
CREATE TABLE IF NOT EXISTS quest_template_allowableraces_backup (
  ID int unsigned NOT NULL PRIMARY KEY,
  AllowableRaces int unsigned NOT NULL
) ENGINE=InnoDB;
REPLACE INTO quest_template_allowableraces_backup (ID, AllowableRaces)
SELECT ID, AllowableRaces
FROM quest_template;
UPDATE quest_template
SET AllowableRaces = 0
WHERE AllowableRaces <> 0;


