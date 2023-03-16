QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# 包含FFmpeg库文件位置

# windows平台
win32{
INCLUDEPATH += C:\Qt\ffmpeg-6.0\include
LIBS += -LC:\Qt\ffmpeg-6.0\lib \
        -L"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0\um\x64" \
        -lavcodec \
        -lavdevice \
        -lavformat \
        -lavutil   \
        -lpostproc \
        -lswresample \
        -lswscale \
        -lWinmm
}

# linux平台
unix{
    INCLUDEPATH+= .
    LIBS += -L/usr/lib/x86_64-linux-gnu/ \
            -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lpostproc \
            -lswresample -lswscale
}


SOURCES += \
    ffmpegvideo.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    ffmpegvideo.h \
    mainwindow.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
