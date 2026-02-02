-- Normalize NPC and system text languages to universal (0) for cross-faction readability.

-- Targeted normalization for common non-universal language IDs
UPDATE `acore_world`.`broadcast_text` SET `LanguageID` = 0 WHERE `LanguageID` IN (1,2,3,6,7,10,13,14,33,35);
UPDATE `acore_world`.`creature_text`  SET `Language`   = 0 WHERE `Language`   IN (1,2,3,6,7,10,13,14,33,35);
UPDATE `acore_world`.`npc_text` SET `lang0` = 0 WHERE `lang0` IN (1,2,3,6,7,10,13,14,33,35);
UPDATE `acore_world`.`npc_text` SET `lang1` = 0 WHERE `lang1` IN (1,2,3,6,7,10,13,14,33,35);
UPDATE `acore_world`.`npc_text` SET `lang2` = 0 WHERE `lang2` IN (1,2,3,6,7,10,13,14,33,35);
UPDATE `acore_world`.`npc_text` SET `lang3` = 0 WHERE `lang3` IN (1,2,3,6,7,10,13,14,33,35);
UPDATE `acore_world`.`npc_text` SET `lang4` = 0 WHERE `lang4` IN (1,2,3,6,7,10,13,14,33,35);
UPDATE `acore_world`.`npc_text` SET `lang5` = 0 WHERE `lang5` IN (1,2,3,6,7,10,13,14,33,35);
UPDATE `acore_world`.`npc_text` SET `lang6` = 0 WHERE `lang6` IN (1,2,3,6,7,10,13,14,33,35);
UPDATE `acore_world`.`npc_text` SET `lang7` = 0 WHERE `lang7` IN (1,2,3,6,7,10,13,14,33,35);

-- Catch-all: ensure no remaining nonzero language values
UPDATE broadcast_text SET LanguageID = 0 WHERE LanguageID <> 0;
UPDATE creature_text  SET Language   = 0 WHERE Language   <> 0;

UPDATE npc_text SET
  lang0=0, lang1=0, lang2=0, lang3=0,
  lang4=0, lang5=0, lang6=0, lang7=0
WHERE lang0<>0 OR lang1<>0 OR lang2<>0 OR lang3<>0 OR lang4<>0 OR lang5<>0 OR lang6<>0 OR lang7<>0;
