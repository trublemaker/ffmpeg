#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>
#include <QLabel>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_btnPS_clicked();
    virtual void resizeEvent(QResizeEvent *event) override;

    void on_lineUrl_currentTextChanged(const QString &arg1);

    void on_lineUrl_currentIndexChanged(int index);

private:
    Ui::MainWindow *ui;
    QLabel *titleLabel=0;
    bool isPlay=false;
};
#endif // MAINWINDOW_H
