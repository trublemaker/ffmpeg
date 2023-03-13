TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

# 包含FFmpeg库文件位置

# windows平台
win32{
INCLUDEPATH += C:\Qt\ffmpeg-5.1.2\include
LIBS += -LC:\Qt\ffmpeg-5.1.2/lib \
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
        main.c
