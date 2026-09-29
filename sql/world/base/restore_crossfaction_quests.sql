-- Restore quest AllowableRaces values that were backed up by allow_crossfaction_quests.sql.
-- Run against the WORLD database (for example acore_world).
-- This roll-back also removes the backup table so the module can start fresh if rerun.
-- Repeat safe: the CREATE makes the UPDATE a no-op when the backup is already gone.

CREATE TABLE IF NOT EXISTS quest_template_allowableraces_backup (
  ID int unsigned NOT NULL PRIMARY KEY,
  AllowableRaces int unsigned NOT NULL
) ENGINE=InnoDB;

UPDATE quest_template qt
JOIN quest_template_allowableraces_backup b ON qt.ID = b.ID
SET qt.AllowableRaces = b.AllowableRaces;

DROP TABLE IF EXISTS quest_template_allowableraces_backup;
