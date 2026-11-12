// CONTRACT v1 — owned by lead. Amend via CONTRACTS.md only.
// v1: amendment A27 — cross-platform Phase 1. The one #ifdef that picks a
//     concrete INativeWindow lives in NativeWindowFactory.cpp, inside
//     src/platform/ where the project lint allows it. main.cpp calls this
//     and never names a concrete platform class.
#pragma once
#include "INativeWindow.h"
#include <memory>

std::unique_ptr<INativeWindow> createNativeWindow(QObject *parent = nullptr);
