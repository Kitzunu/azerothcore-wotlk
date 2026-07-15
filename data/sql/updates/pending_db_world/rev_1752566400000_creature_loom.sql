--
-- Loom index table. The DB does NOT store script text — it only points at the `.loom` file that
-- holds an entity's behavior; the worldserver reads and compiles that file at load. This keeps the
-- SQL dumps tiny (one short row per scripted entity) instead of embedding multi-line YAML blobs.
--
-- `file` is relative to the Loom content root (worldserver.conf `LoomContentDir`, default
-- `<DataDir>/loom`). Reusable threads live under `<root>/threads/` and are auto-scanned — they
-- need no DB rows.

DROP TABLE IF EXISTS `loom_thread`;

DROP TABLE IF EXISTS `creature_loom`;
CREATE TABLE `creature_loom` (
    `entryorguid`  INT          NOT NULL                COMMENT 'entry (>0) or -guid (<0), same convention as smart_scripts',
    `source_type`  TINYINT      NOT NULL DEFAULT 0      COMMENT '0 creature, 1 gameobject, 2 areatrigger',
    `file`         VARCHAR(255) NOT NULL                COMMENT 'path to the .loom file, relative to the Loom content root',
    `comment`      VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`entryorguid`, `source_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
