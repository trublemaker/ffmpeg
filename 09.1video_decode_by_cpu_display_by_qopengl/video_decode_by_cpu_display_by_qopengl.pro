#-------------------------------------------------
#
# Project created by QtCreator 2021-04-02T17:31:10
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

win32:{
INCLUDEPATH += C:\Qt\ffmpeg-6.0\include
LIBS += -LC:\Qt\ffmpeg-6.0/lib \
        -lavcodec -lavdevice -lavfilter \
        -lavformat -lavutil -lpostproc \
        -lswscale
}

unix:{
LIBS += -lavcodec -lavdevice -lavfilter \
        -lavformat -lavutil -lpostproc \
        -lswscale
}

SOURCES += \
        main.cpp \
        mainwindow.cpp \
    ffmpegdecoder.cpp \
    i420render.cpp

HEADERS += \
        mainwindow.h \
    ffmpegdecoder.h \
    i420render.h

FORMS += \
        mainwindow.ui

#static const char *const hw_type_names[] = {
#    [AV_HWDEVICE_TYPE_CUDA]   = "cuda",   //CUDA是Nvidia出的一个GPU计算库
#    [AV_HWDEVICE_TYPE_DRM]    = "drm",  //DRM 是linux 下的图形渲染架构(Direct Render Manager)
#    [AV_HWDEVICE_TYPE_DXVA2]  = "dxva2",//微软dx套件，使用D3D9
#    [AV_HWDEVICE_TYPE_D3D11VA] = "d3d11va",//微软dx套件，使用D3D11
#    [AV_HWDEVICE_TYPE_OPENCL] = "opencl",//第一个面向异构系统(此系统中可由CPU，GPU或其它类型的处理器架构组成)的并行编程的开放式标准。面向GPU编程
#    [AV_HWDEVICE_TYPE_QSV]    = "qsv",//英特尔Quick Sync Video，号称地球最强
#    [AV_HWDEVICE_TYPE_VAAPI]  = "vaapi",  //Video Acceleration Api，UNINX下的编码接口，intel提供
#    [AV_HWDEVICE_TYPE_VDPAU]  = "vdpau",  //Video Decode and Presentation API for Unix ，NVIDIA提供的
#    [AV_HWDEVICE_TYPE_VIDEOTOOLBOX] = "videotoolbox", // mac iOS
#    [AV_HWDEVICE_TYPE_MEDIACODEC] = "mediacodec",  // Android
