#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <SDL.h>

extern SDL_Window * win;
extern int useSDL;
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    SDL_Init(SDL_INIT_EVERYTHING);
    ui->setupUi(this);

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_btnPS_clicked()
{
    if(useSDL){
        win = SDL_CreateWindowFrom( (void*)ui->widget->winId());// ->windowHandle());、、window()->
    }

    QString url = ui->lineUrl->text().trimmed();
    if(url.isEmpty()){
        QMessageBox::information(this,tr("Warning"),"Please input url",QMessageBox::Ok);
        return;
    }

    ui->widget->play(url);
}

//cpp文件
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QSize size = ui->widget->size();
    qDebug()<<"resize" << size.width()<< size.height() << ui->widget->winId();

    //if (nullptr != m_Local_MatCmdWind)
    {
        //不能显示滚动条
        //m_Local_MatCmdWind->resize(frameGeometry().size());

        //可以显示出滚动条,但是效果不是很好
        //m_Local_MatCmdWind->resize(geometry().size());

        //可以显示出滚动条，显示效果也很好
       //m_Local_MatCmdWind->resize(ui.lab_central->size());
    }
}
