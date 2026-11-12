#pragma once

#include <QtTest>

#include "core/PlayerController.h"

// PlayerController::seek() ignores a request while the load is still pending,
// and duration() can already be known by then. A test that seeks as soon as
// the duration appears therefore sometimes loses the seek and plays the file
// from the start (seen on CI as an 8-second wait for EOF). Repeat the request
// until the controller accepts it; a repeated target replaces the queued one.
inline bool seekWhenReady(PlayerController &player, double seconds, int timeoutMs = 5000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        player.seek(seconds);
        if (player.seekPending())
            return true;
        QTest::qWait(20);
    }
    return false;
}
