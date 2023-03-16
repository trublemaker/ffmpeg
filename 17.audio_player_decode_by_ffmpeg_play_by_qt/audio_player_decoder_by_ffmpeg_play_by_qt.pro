QT += core multimedia testlib

TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle

# 包含FFmpeg库文件位置

# windows平台
win32{
INCLUDEPATH += C:\Qt\ffmpeg-6.0\include
LIBS += -LC:\Qt\ffmpeg-6.0/lib \
        -lavcodec \
        -lavdevice \
        -lavformat \
        -lavutil   \
        -lpostproc \
        -lswresample \
        -lswscale
}

# linux平台
unix{
    INCLUDEPATH+= .
    LIBS += -L/usr/lib/x86_64-linux-gnu/ \
            -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lpostproc \
            -lswresample -lswscale
}

SOURCES += \
        main.cpp
