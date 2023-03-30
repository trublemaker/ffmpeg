#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    //a.setWindowIcon(QIcon("F:\\Dev\\ffmpeg\\11.1video_decode_by_cuda_display_by_qwidget\\9042709_media_video_list_icon.png"));

    return a.exec();
}
