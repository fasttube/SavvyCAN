#!/bin/bash

# Configuration
DEPLOY_DIR="./SavvyCAN_Ready_For_Windows"
MINGW_BIN="/usr/x86_64-w64-mingw32/sys-root/mingw/bin"
MINGW_PLUGINS="/usr/x86_64-w64-mingw32/sys-root/mingw/lib/qt5/plugins"

echo "Step 1: Creating directory structure..."
rm -rf "$DEPLOY_DIR" # Clear old attempts
mkdir -p "$DEPLOY_DIR/platforms"

echo "Step 2: Copying the compiled EXE and candle_api..."
if [ -f "./release/SavvyCAN.exe" ]; then
    cp "./release/SavvyCAN.exe" "$DEPLOY_DIR/"
else
    echo "ERROR: SavvyCAN.exe not found in release folder!"
    exit 1
fi

if [ -f "./libs/candle_api/candle_api.dll" ]; then
    cp "./libs/candle_api/candle_api.dll" "$DEPLOY_DIR/"
else
    echo "ERROR: candle_api.dll not found in ./libs/candle_api/"
    exit 1
fi

echo "Step 3: Harvesting Qt and System DLLs..."
# Added Qt5PrintSupport and Qt5Qml
QT_DLLS=(
    "Qt5Core.dll" "Qt5Gui.dll" "Qt5Widgets.dll" "Qt5SerialBus.dll" 
    "Qt5SerialPort.dll" "Qt5Network.dll" "Qt5Help.dll" "Qt5Sql.dll" 
    "Qt5Xml.dll" "Qt5PrintSupport.dll" "Qt5Qml.dll"
)

SYSTEM_DLLS=(
    "libwinpthread-1.dll" "libstdc++-6.dll" "libgcc_s_seh-1.dll" "zlib1.dll"
    "iconv.dll" "libpcre2-16-0.dll" "libpcre2-8-0.dll" "libzstd.dll" "libharfbuzz-0.dll" 
    "libpng16-16.dll" "libcrypto-3-x64.dll" "libssl-3-x64.dll"
    "libfreetype-6.dll" "libglib-2.0-0.dll" "libintl-8.dll" "libpcre-1.dll"
    "libbrotlicommon.dll" "libbrotlidec.dll" "libbz2-1.dll" "libgraphite2.dll"
)

for dll in "${QT_DLLS[@]}" "${SYSTEM_DLLS[@]}"; do
    if [ -f "$MINGW_BIN/$dll" ]; then
        cp "$MINGW_BIN/$dll" "$DEPLOY_DIR/"
    else
        echo "WARNING: Could not find $dll in $MINGW_BIN. You may need to locate it manually."
    fi
done

echo "Step 4: Copying Plugin DLLs (The GSUSB bridge)..."
cp "$MINGW_PLUGINS/platforms/qwindows.dll" "$DEPLOY_DIR/platforms/"

echo "Step 5: Zipping it up..."
zip -q -r SavvyCAN_Windows_Build.zip "$DEPLOY_DIR"

echo "----------------------------------------------------"
echo "DONE! Your complete package is ready."
echo "----------------------------------------------------"
