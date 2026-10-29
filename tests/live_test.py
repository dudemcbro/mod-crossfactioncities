#!/usr/bin/env python3
"""Live test of mod-crossfactioncities on a local AzerothCore dev server.

Runs the worldserver in three config phases, logs in an Alliance and a Horde test
character with the headless client in wowclient.py, and checks packets and database rows.

    python3 tests/live_test.py --server-dir /path/to/azerothcore/env/dist

The server directory must contain bin/worldserver, bin/authserver and etc/ with the
configs, and the databases must be reachable with the mysql command line client (see
--mysql). The test creates two GM accounts (CFCTESTA, CFCTESTH) and new characters on
every run, edits etc/modules/mod_crossfactioncities.conf while it runs and restores it
afterwards. Use a dev server, never a live realm. See docs/testing.md.
"""

import argparse
import os
import random
import re
import shutil
import signal
import string
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wowclient as wc  # noqa: E402

# Test data from the stock world database and DBC files.
HORDE_INN_TRIGGER = 843              # Razor Hill inn, Horde only
HORDE_INN_POS = "341.42 -4684.7 31 1"
HORDE_INN2_TRIGGER = 742             # Crossroads inn, Horde only
HORDE_INN2_POS = "-405.311 -2645.29 112 1"
STORMWIND_POS = "-8673 431 101 0"    # next to entry 28347, greets players in Common on sight
STORMWIND_GREETER = 28347
STORMWIND_FACTION = 72
ORCISH_GOSSIP_TEXT = 34              # npc_text in Orcish
HORDE_TABARD = 15197                 # Scout's Tabard, Horde only, no level
TABARD_SLOT = 18
MAGHAR_FACTION, MAGHAR_ACHIEVEMENT = 941, 901        # Horde only: Exalted with the Mag'har
WARSONG_FACTION, WARSONG_ACHIEVEMENT = 889, 712      # Horde only: Exalted with the Warsong Outriders

LANGUAGE_SKILLS = {98: "Common", 109: "Orcish", 111: "Dwarven", 113: "Darnassian", 115: "Taurahe",
                   137: "Thalassian", 313: "Gnomish", 315: "Troll", 673: "Gutterspeak", 759: "Draenei"}
LANGUAGE_SPELLS = {98: 668, 109: 669, 111: 672, 113: 671, 115: 670, 137: 813, 313: 7340, 315: 7341, 673: 17737, 759: 29932}

PHASES = {
    "A": {"NeutralInns": 1, "LearnLanguages": 2, "NpcSpeechUniversal": 1, "UnlockFactionItems": 1, "UnlockFactionAchievements": 1},
    "B": {"NeutralInns": 0, "LearnLanguages": 1, "NpcSpeechUniversal": 0, "UnlockFactionItems": 0, "UnlockFactionAchievements": 0},
    "C": {"NeutralInns": 0, "LearnLanguages": 0, "NpcSpeechUniversal": 0, "UnlockFactionItems": 0, "UnlockFactionAchievements": 0},
    "D": {"NeutralInns": 0, "LearnLanguages": 2, "NpcSpeechUniversal": 0, "UnlockFactionItems": 0, "UnlockFactionAchievements": 0},
    "E": {"NeutralInns": 0, "LearnLanguages": 0, "NpcSpeechUniversal": 0, "UnlockFactionItems": 0, "UnlockFactionAchievements": 0},
}

# Phases D and E add playercreateinfo_skills rows that give every playable race every language at
# creation, as some servers do, and remove them again at the end.
EVERY_RACE_MASK = 1791
CREATE_SKILL_COMMENT = "cfc-live-test"


class Context:
    def __init__(self, args):
        self.args = args
        self.bin = os.path.join(args.server_dir, "bin")
        self.etc = os.path.join(args.server_dir, "etc")
        self.module_conf = os.path.join(self.etc, "modules", "mod_crossfactioncities.conf")
        self.results = []
        self.world = None
        self.log_dir = None

    # -- database ------------------------------------------------------------------
    def sql(self, database, query):
        command = self.args.mysql.split() + ["-N", "-B", database, "-e", query]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("mysql failed: %s" % result.stderr.strip())
        return [line.split("\t") for line in result.stdout.splitlines() if line]

    # -- results -------------------------------------------------------------------
    def check(self, condition, label, detail=""):
        self.results.append((bool(condition), label))
        print("  %s %s%s" % ("PASS" if condition else "FAIL", label, (" (%s)" % detail) if detail and not condition else ""))
        return condition

    # -- servers -------------------------------------------------------------------
    def set_options(self, options):
        with open(self.module_conf) as handle:
            text = handle.read()
        for name, value in options.items():
            pattern = r"(?m)^CrossFactionCities\.%s\s*=.*$" % name
            line = "CrossFactionCities.%s = %s" % (name, value)
            text, count = re.subn(pattern, line, text)
            if not count:
                text += "\n" + line + "\n"
        with open(self.module_conf, "w") as handle:
            handle.write(text)

    def start_world(self):
        """Starts the worldserver and returns the module's startup log lines. Each start
        logs to a new folder (AC_LOGS_DIR overrides LogsDir), so old lines never count."""
        self.log_dir = tempfile.mkdtemp(prefix="cfc-live-test-logs-")
        env = dict(os.environ, AC_LOGS_DIR=self.log_dir)
        self.world = subprocess.Popen([os.path.join(self.bin, "worldserver"), "-c", os.path.join(self.etc, "worldserver.conf")],
                                      cwd=self.bin, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL, start_new_session=True)
        deadline = time.time() + 600
        while time.time() < deadline:
            if self.world.poll() is not None:
                raise RuntimeError("worldserver exited with code %s, logs in %s" % (self.world.returncode, self.log_dir))
            lines = self.module_log_lines()
            if any("Horde NPC factions" in line for line in lines):
                time.sleep(2)
                return self.module_log_lines()
            time.sleep(1)
        raise RuntimeError("worldserver did not start, logs in %s" % self.log_dir)

    def module_log_lines(self):
        lines = []
        for name in sorted(os.listdir(self.log_dir)):
            with open(os.path.join(self.log_dir, name), errors="replace") as handle:
                lines += [line for line in handle.read().splitlines() if "mod-crossfactioncities" in line]
        return lines

    def stop_world(self):
        if self.world and self.world.poll() is None:
            self.world.send_signal(signal.SIGTERM)
            try:
                self.world.wait(120)
            except subprocess.TimeoutExpired:
                self.world.kill()
        self.world = None
        # Keep the logs of a run with failures for inspection.
        if self.log_dir and all(ok for ok, _ in self.results):
            shutil.rmtree(self.log_dir, ignore_errors=True)
        elif self.log_dir:
            print("  (worldserver logs kept in %s)" % self.log_dir)
        self.log_dir = None

    def ensure_authserver(self):
        if subprocess.run(["pgrep", "-x", "authserver"], capture_output=True).returncode == 0:
            return
        subprocess.Popen([os.path.join(self.bin, "authserver"), "-c", os.path.join(self.etc, "authserver.conf")],
                         cwd=self.bin, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(3)

    def ensure_account(self, name):
        salt, verifier = wc.make_verifier(name, name)
        self.sql(self.args.auth_db, "INSERT INTO account (username, salt, verifier, expansion) VALUES ('%s', X'%s', X'%s', 2) "
                 "ON DUPLICATE KEY UPDATE salt = VALUES(salt), verifier = VALUES(verifier), expansion = 2" % (name, salt.hex(), verifier.hex()))
        self.sql(self.args.auth_db, "REPLACE INTO account_access (id, gmlevel, RealmID) SELECT id, 3, -1 FROM account WHERE username = '%s'" % name)

    def connect(self, account):
        key = wc.auth_login(self.args.host, self.args.auth_port, account, account)
        return wc.WorldClient(self.args.host, self.args.world_port, account, key)


class Character:
    def __init__(self, ctx, account, name, race, language):
        self.ctx, self.account, self.name, self.race, self.language = ctx, account, name, race, language
        self.client = None
        self.guid = None

    def login(self, create=False):
        self.client = self.ctx.connect(self.account)
        if create:
            self.client.create_character(self.name, self.race, 1)
        self.guid = [c for c in self.client.characters() if c["name"] == self.name][0]["guid"]
        self.client.login(self.guid)
        self.cmd(".gm off")

    def cmd(self, text, wait=2.0):
        return self.client.command(text, self.language, wait)

    def save(self):
        replies = self.cmd(".save", 1.5)
        time.sleep(1)
        return replies

    def logout(self):
        if self.client:
            self.save()
            self.client.logout()
            self.client = None

    def skills(self):
        return {int(r[0]) for r in self.ctx.sql(self.ctx.args.char_db, "SELECT skill FROM character_skills WHERE guid = %d" % self.guid)}

    def spells(self):
        return {int(r[0]) for r in self.ctx.sql(self.ctx.args.char_db, "SELECT spell FROM character_spell WHERE guid = %d" % self.guid)}

    def item_slots(self, entry):
        return [(int(r[0]), int(r[1])) for r in self.ctx.sql(self.ctx.args.char_db,
                "SELECT ci.bag, ci.slot FROM character_inventory ci JOIN item_instance ii ON ii.guid = ci.item "
                "WHERE ci.guid = %d AND ii.itemEntry = %d ORDER BY ci.slot" % (self.guid, entry))]

    def achievements(self):
        return {int(r[0]) for r in self.ctx.sql(self.ctx.args.char_db, "SELECT achievement FROM character_achievement WHERE guid = %d" % self.guid)}

    def rest_state(self):
        row = self.ctx.sql(self.ctx.args.char_db, "SELECT innTriggerId, is_logout_resting FROM characters WHERE guid = %d" % self.guid)[0]
        return int(row[0]), int(row[1])


# -- packet helpers -----------------------------------------------------------------
def item_query(client, entry):
    client.send(wc.CMSG_ITEM_QUERY_SINGLE, struct.pack("<I", entry))
    data = client.wait_for(wc.SMSG_ITEM_QUERY_SINGLE_RESPONSE, 5, lambda d: struct.unpack_from("<I", d)[0] == entry)
    reader = wc.Reader(data)
    reader.take("IIIi")
    reader.cstring()
    reader.take("BBB")
    _display, _quality, _flags, flags2, _buy, _sell, _inv_type, _classes, races = reader.take("IIIIIIIii")
    return flags2, races


def gossip_text_language(client, text_id):
    client.send(wc.CMSG_NPC_TEXT_QUERY, struct.pack("<IQ", text_id, 0))
    data = client.wait_for(wc.SMSG_NPC_TEXT_UPDATE, 5, lambda d: struct.unpack_from("<I", d)[0] == text_id)
    reader = wc.Reader(data)
    reader.take("If")
    reader.cstring()
    reader.cstring()
    return reader.take("I")


def autoequip(client, slot):
    since = time.time()
    client.send(wc.CMSG_AUTOEQUIP_ITEM, struct.pack("<BB", 255, slot))
    client.pump(2)
    failures = [d[0] for d in client.received(wc.SMSG_INVENTORY_CHANGE_FAILURE, since)]
    return failures[0] if failures else 0


def achievement_earned(client, achievement, since):
    for data in client.received(wc.SMSG_ACHIEVEMENT_EARNED, since):
        reader = wc.Reader(data)
        reader.packed_guid()
        if reader.take("I") == achievement:
            return True
    return False


def forced_reactions(client, since):
    reactions = {}
    for data in client.received(wc.SMSG_SET_FORCED_REACTIONS, since):
        reader = wc.Reader(data)
        reactions = dict(reader.take("II") for _ in range(reader.take("I")))
    return reactions


def greeter_chat(client, since, timeout):
    """Language of the first chat line from the Stormwind greeter after `since`."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        client.pump(1)
        for data in client.received(wc.SMSG_MESSAGECHAT, since):
            chat_type, language, sender, _text = wc.parse_chat(data)
            if chat_type == wc.CHAT_MSG_MONSTER_SAY and (sender >> 24) & 0xFFFFFF == STORMWIND_GREETER:
                return language
    return None


# -- phases -------------------------------------------------------------------------
def phase_a(ctx, ally, horde):
    print("\n== Phase A: every option on, LearnLanguages = 2")
    lines = ctx.start_world()
    ctx.check(any("LearnLanguages=2" in l for l in lines), "startup applied LearnLanguages")

    # Common's stock row for other races leaves out Undead: without the module's row the
    # core drops the language at every login and the module has to add it back.
    undead = Character(ctx, horde.account, "Cfu" + horde.name[3:], 5, wc.LANG_ORCISH)   # undead warrior
    undead.login(create=True)
    undead.logout()
    undead.login()
    ctx.check(LANGUAGE_SPELLS[98] in undead.client.initial_spells(),
              "Undead character still has Common at its next login")
    undead.logout()

    ally.login(create=True)
    horde.login(create=True)
    ctx.check(any("saved" in r.lower() for r in ally.save()), "GM chat commands work (.save)")
    horde.save()

    print("-- languages")
    ctx.check(98 in ally.skills(), "Alliance character still knows Common after creation")
    ctx.check(set(LANGUAGE_SKILLS) <= ally.skills(), "Alliance character knows every race language",
              sorted(set(LANGUAGE_SKILLS) - ally.skills()))
    ctx.check(set(LANGUAGE_SKILLS) <= horde.skills(), "Horde character knows every race language",
              sorted(set(LANGUAGE_SKILLS) - horde.skills()))
    ctx.check(LANGUAGE_SPELLS[109] in ally.spells(), "Alliance character has the Language: Orcish spell")

    print("-- faction-locked items")
    flags2, races = item_query(ally.client, HORDE_TABARD)
    ctx.check(flags2 & 3 == 0, "item query no longer marks the tabard Horde only", "Flags2=%#x" % flags2)
    ally.cmd(".additem %d" % HORDE_TABARD)
    ally.save()
    slots = ally.item_slots(HORDE_TABARD)
    error = autoequip(ally.client, slots[0][1]) if slots else -1
    ally.save()
    ctx.check(error == 0 and (0, TABARD_SLOT) in ally.item_slots(HORDE_TABARD),
              "Alliance character equips a Horde-only tabard", "error %s, slots %s" % (error, ally.item_slots(HORDE_TABARD)))

    print("-- achievements")
    since = time.time()
    ally.cmd(".modify reputation %d 42000" % MAGHAR_FACTION, 3)
    ally.save()
    ctx.check(achievement_earned(ally.client, MAGHAR_ACHIEVEMENT, since) and MAGHAR_ACHIEVEMENT in ally.achievements(),
              "Alliance character earns the Horde-only achievement Mag'har of Draenor")

    print("-- inns")
    ally.cmd(".go xyz %s" % HORDE_INN_POS, 5)
    ally.save()
    ctx.check(ally.rest_state() == (HORDE_INN_TRIGGER, 1), "Alliance character rests in the Razor Hill inn",
              "innTriggerId, resting = %s" % (ally.rest_state(),))

    print("-- NPC speech and neutral cities")
    ctx.check(gossip_text_language(horde.client, ORCISH_GOSSIP_TEXT) == 0, "Orcish gossip text is sent as Universal")
    since = time.time()
    horde.cmd(".go xyz %s" % STORMWIND_POS, 3)
    reactions = forced_reactions(horde.client, since)
    ctx.check(reactions.get(STORMWIND_FACTION) == 3, "Horde character in Stormwind gets Stormwind NPCs forced neutral",
              "reactions %s" % reactions)
    language = greeter_chat(horde.client, since, 15)
    ctx.check(language == 0, "Stormwind greeter's Common speech arrives as Universal", "language %s" % language)

    print("-- NpcSpeechUniversal follows .reload config")
    ctx.set_options({"NpcSpeechUniversal": 0})
    horde.cmd(".reload config", 3)
    ctx.check(gossip_text_language(horde.client, ORCISH_GOSSIP_TEXT) == 1, "after .reload config with it off, gossip text keeps Orcish")

    # Leave Stormwind, so the greeter has not already greeted this character in phase B.
    horde.cmd(".go xyz %s" % HORDE_INN_POS, 3)
    ally.logout()
    horde.logout()
    ctx.stop_world()


def phase_b(ctx, ally, horde):
    print("\n== Phase B: startup options off, NpcSpeechUniversal off, LearnLanguages = 1")
    ctx.start_world()
    ally.login()
    horde.login()
    ally.save()
    horde.save()

    print("-- languages")
    extra = {98, 109}
    ctx.check(ally.skills() & set(LANGUAGE_SKILLS) == extra, "Alliance character keeps Common and Orcish only",
              sorted(ally.skills() & set(LANGUAGE_SKILLS)))
    ctx.check(horde.skills() & set(LANGUAGE_SKILLS) == extra, "Horde character keeps Orcish and Common only",
              sorted(horde.skills() & set(LANGUAGE_SKILLS)))
    ctx.check(LANGUAGE_SPELLS[113] not in ally.spells(), "removed languages lose their spell too")

    print("-- faction-locked items")
    flags2, _ = item_query(ally.client, HORDE_TABARD)
    ctx.check(flags2 & 1, "item query marks the tabard Horde only again", "Flags2=%#x" % flags2)
    ally.cmd(".additem %d" % HORDE_TABARD)
    ally.save()
    bag_slots = [slot for bag, slot in ally.item_slots(HORDE_TABARD) if bag == 0 and slot >= 23]
    error = autoequip(ally.client, bag_slots[0]) if bag_slots else 0
    ctx.check(error != 0, "Alliance character cannot equip a Horde-only tabard", "error %s" % error)

    print("-- achievements")
    since = time.time()
    ally.cmd(".modify reputation %d 42000" % WARSONG_FACTION, 3)
    ally.save()
    ctx.check(not achievement_earned(ally.client, WARSONG_ACHIEVEMENT, since) and WARSONG_ACHIEVEMENT not in ally.achievements(),
              "Alliance character does not earn the Horde-only achievement Warsong Outrider")

    print("-- inns")
    ally.cmd(".go xyz %s" % HORDE_INN2_POS, 5)
    ally.save()
    ctx.check(ally.rest_state()[0] != HORDE_INN2_TRIGGER, "Alliance character does not rest in the Crossroads inn",
              "innTriggerId, resting = %s" % (ally.rest_state(),))

    print("-- NPC speech")
    ctx.check(gossip_text_language(horde.client, ORCISH_GOSSIP_TEXT) == 1, "Orcish gossip text keeps its language")
    since = time.time()
    horde.cmd(".go xyz %s" % STORMWIND_POS, 3)
    language = greeter_chat(horde.client, since, 15)
    ctx.check(language == 7, "Stormwind greeter speaks Common", "language %s" % language)

    ally.logout()
    horde.logout()
    ctx.stop_world()


def phase_c(ctx, ally, horde):
    print("\n== Phase C: LearnLanguages = 0")
    ctx.start_world()
    ally.login()
    horde.login()
    ally.save()
    horde.save()
    ctx.check(ally.skills() & set(LANGUAGE_SKILLS) == {98}, "Alliance character is back to Common only",
              sorted(ally.skills() & set(LANGUAGE_SKILLS)))
    ctx.check(horde.skills() & set(LANGUAGE_SKILLS) == {109}, "Horde character is back to Orcish only",
              sorted(horde.skills() & set(LANGUAGE_SKILLS)))
    ctx.check(LANGUAGE_SPELLS[109] not in ally.spells(), "Alliance character lost the Language: Orcish spell")
    ally.logout()
    horde.logout()
    ctx.stop_world()


def set_creation_languages(ctx, every_race):
    ctx.sql(ctx.args.world_db, "DELETE FROM playercreateinfo_skills WHERE comment = '%s'" % CREATE_SKILL_COMMENT)
    if every_race:
        values = ", ".join("(%d, 0, %d, 0, '%s')" % (EVERY_RACE_MASK, skill, CREATE_SKILL_COMMENT) for skill in LANGUAGE_SKILLS)
        ctx.sql(ctx.args.world_db, "INSERT INTO playercreateinfo_skills (raceMask, classMask, skill, `rank`, comment) VALUES " + values)


def phase_d(ctx, ally, horde):
    print("\n== Phase D: every race gets every language at creation, LearnLanguages = 2")
    set_creation_languages(ctx, True)
    ctx.start_world()
    ally.login()
    horde.login()
    ally.save()
    horde.save()
    # The characters were created before the rows existed and only know their own language.
    ctx.check(set(LANGUAGE_SKILLS) <= ally.skills(), "existing Alliance character learns every race language",
              sorted(set(LANGUAGE_SKILLS) - ally.skills()))
    ctx.check(set(LANGUAGE_SKILLS) <= horde.skills(), "existing Horde character learns every race language",
              sorted(set(LANGUAGE_SKILLS) - horde.skills()))
    ally.logout()
    horde.logout()
    ctx.stop_world()


def phase_e(ctx, ally, horde):
    print("\n== Phase E: every race gets every language at creation, LearnLanguages = 0")
    ctx.start_world()
    ally.login()
    ally.save()
    ctx.check(set(LANGUAGE_SKILLS) <= ally.skills(), "languages the race gets at creation are kept",
              sorted(set(LANGUAGE_SKILLS) - ally.skills()))
    ally.logout()
    ctx.stop_world()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--server-dir", required=True, help="folder with bin/ and etc/ of the dev server")
    parser.add_argument("--mysql", default="mysql", help='mysql client command with credentials (default: "%(default)s")')
    parser.add_argument("--auth-db", default="acore_auth")
    parser.add_argument("--char-db", default="acore_characters")
    parser.add_argument("--world-db", default="acore_world")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--auth-port", type=int, default=3724)
    parser.add_argument("--world-port", type=int, default=8085)
    args = parser.parse_args()

    ctx = Context(args)
    if subprocess.run(["pgrep", "-x", "worldserver"], capture_output=True).returncode == 0:
        sys.exit("A worldserver is already running; stop it first.")

    backup = ctx.module_conf + ".live-test-backup"
    shutil.copyfile(ctx.module_conf, backup)
    suffix = "".join(random.choice(string.ascii_lowercase) for _ in range(5))
    try:
        ctx.ensure_authserver()
        for account in ("CFCTESTA", "CFCTESTH"):
            ctx.ensure_account(account)
        ally = Character(ctx, "CFCTESTA", "Cfa" + suffix, 1, wc.LANG_COMMON)    # human warrior
        horde = Character(ctx, "CFCTESTH", "Cfh" + suffix, 2, wc.LANG_ORCISH)   # orc warrior
        for name, phase in (("A", phase_a), ("B", phase_b), ("C", phase_c), ("D", phase_d), ("E", phase_e)):
            ctx.set_options(PHASES[name])
            phase(ctx, ally, horde)
    finally:
        ctx.stop_world()
        set_creation_languages(ctx, False)
        shutil.move(backup, ctx.module_conf)

    failed = [label for ok, label in ctx.results if not ok]
    print("\n%d checks, %d passed, %d failed" % (len(ctx.results), len(ctx.results) - len(failed), len(failed)))
    for label in failed:
        print("  FAILED: " + label)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
