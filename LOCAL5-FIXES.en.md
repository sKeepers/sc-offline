# Local 5 fixes

Corrects malformed UTF-8 loadout command titles (66/64 bytes exceeded the SDK63-byte limit), and the characters tab owner id (loadout.npcs). Registers this tab after the successful tick subscription. Borrowed Player/Characters/Crew owner handles are cleared on failed load/unload.

Launcher catalog IDs now distinguish identical translations, with explicit stable keys for command rows and copied action labels. Catalogs are immutable after Seal; each earlier Load starts from compiled English. Empty game roots no longer read the current working directory; non-ASCII localization directory names are skipped safely; ANSI user.cfg comments do not hide ASCII g_language. Catalog UTF-8 remains strict. First duplicate assignment semantics are retained: game last-wins behavior was not established.

Independent checks: original Local4 has3static metadata errors and7native localization regression failures; Local5 passes21command titles+tab metadata and10native scenarios.13additional checks pass through the real sco-core command/UI host and actual loadout builtin; game endpoints, draw callbacks and borrow helpers are stubs. This does not verify rendering or equipment in game.

Release EXE/DLL build,306en/ru catalog keys and font coverage, SDK47files300includes/19gate-tests/4NPC-release-tests, installed launcher self-test passed. User data preserved during local installation. In-game Player/Characters/SQ42 checks are pending. Companion follow/guard remain quarantined. PR127 attachments are not included yet.

Changes implemented locally with independently reviewed Claude suggestions. Incoming Claude programs/tests were not executed. Upstream and third-party license/attribution remain in the source package.
