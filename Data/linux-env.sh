#!/bin/bash -e
# linux-env.sh

appdir="$(readlink -f "$(dirname "${BASH_SOURCE[0]}")/..")"

# Prefer libraries and Qt plugins bundled in the AppImage. Relying on
# distro-provided Qt paths can make the xcb platform plugin undiscoverable.
export LD_LIBRARY_PATH="${appdir}/usr/lib:/usr/lib/:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="${appdir}/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="${appdir}/usr/plugins/platforms"
export QML2_IMPORT_PATH="${appdir}/usr/qml"

if [[ $(env | grep -i wayland) ]]; then
    # wxWidgets 3.14 is GTK3, which seemingly has an issue or two when
    # running under Wayland. Explicitly setting this for Slippi avoids
    # those issues.
    export GDK_BACKEND=x11
    
    # Disable Webkit compositing on Wayland cause it breaks stuff
    export WEBKIT_DISABLE_COMPOSITING_MODE=1
fi
