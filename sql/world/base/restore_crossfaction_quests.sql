-- Restore quest AllowableRaces values that were backed up by allow_crossfaction_quests.sql.
-- This roll-back also removes the backup table so the module can start fresh if rerun.

UPDATE quest_template qt
JOIN quest_template_allowableraces_backup b ON qt.ID = b.ID
SET qt.AllowableRaces = b.AllowableRaces;

DROP TABLE IF EXISTS quest_template_allowableraces_backup;
