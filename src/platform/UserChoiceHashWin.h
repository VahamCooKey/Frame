// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <QString>
#include <windows.h>

QString frameUserChoiceHash(const QString &extension, const QString &sid,
                           const QString &progId, FILETIME timestamp);
