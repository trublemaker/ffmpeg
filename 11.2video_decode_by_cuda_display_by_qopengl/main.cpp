#include "mainwindow.h"

#include <QApplication>

int NSSleep(int msec);

int main(int argc, char *argv[])
{
    NSSleep(3000);
    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    return a.exec();
}

#include <Windows.h>

/*
纳秒休眠，符号ns（英语：nanosecond ）.
1纳秒等于十亿分之一秒（10-9秒）
1 纳秒 = 1000皮秒
1,000 纳秒 = 1微秒
1,000,000 纳秒 = 1毫秒
1,000,000,000 纳秒 = 1秒
*/

int NSSleep(int msec)
{
    HANDLE hTimer = NULL;
    LARGE_INTEGER liDueTime={0,0};

    liDueTime.QuadPart = -msec*10000;

    // Create a waitable timer.
    hTimer = CreateWaitableTimer(NULL, TRUE, L"WaitableTimer");
    if (!hTimer)
    {
        printf("CreateWaitableTimer failed (%d)\n", GetLastError());
        return 1;
    }

    // Set a timer to wait for 10 seconds.
    if (!SetWaitableTimer(
        hTimer, &liDueTime, 0, NULL, NULL, 0))
    {
        printf("SetWaitableTimer failed (%d)\n", GetLastError());
        return 2;
    }

    // Wait for the timer.
    if (WaitForSingleObject(hTimer, INFINITE) != WAIT_OBJECT_0)
        printf("WaitForSingleObject failed (%d)\n", GetLastError());

    CloseHandle(hTimer);
    return 0;
}

