SOURCES += \
    $$PWD/gs_usb.cpp \
    $$PWD/gs_usb_reader.cpp

HEADERS  += \
    $$PWD/gs_usb_definitions.h \
    $$PWD/gs_usb_functions.h \
    $$PWD/gs_usb.h \
    $$PWD/gs_usb_reader.h

win32-g++ {
    LIBS += -L$$PWD/../../libs/candle_api -llibcandle_api.dll
}

win32-msvc* {
    LIBS += $$PWD/../../libs/candle_api/candle_api.lib
}
