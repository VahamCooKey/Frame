#include "NativeWindowFactory.h"

#if defined(Q_OS_WIN)
#include "NativeWindowWin32.h"
#elif defined(Q_OS_MACOS)
#include "NativeWindowMac.h"
#else
#include "NativeWindowLinux.h"
#endif

std::unique_ptr<INativeWindow> createNativeWindow(QObject *parent)
{
#if defined(Q_OS_WIN)
    return std::make_unique<NativeWindowWin32>(parent);
#elif defined(Q_OS_MACOS)
    return std::make_unique<NativeWindowMac>(parent);
#else
    return std::make_unique<NativeWindowLinux>(parent);
#endif
}
