#pragma once

// Diagnostic bulk population of Qt's font database on a worker thread.
// main.cpp schedules this only with FRAME_FONT_WARMUP=1. Font fallback can
// require expensive lazy population, but proactive styles() calls also hold
// the shared font-database lock and can stall GUI font lookups. A61 therefore
// leaves normal launches to Qt's on-demand loading.
//
// Idempotent: a second call while or after the first ran does nothing, so
// main.cpp can race its "player settled" trigger against a plain timeout.
void startFontDatabaseWarmup();
