#ifndef FFMPEGVIDEO_H
#define FFMPEGVIDEO_H

#include <QImage>
#include <QWidget>
#include <QPaintEvent>
#include <QThread>
#include <QPainter>
#include <QDebug>

#include <string>
#include <SDL.h>

extern "C"{
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/parseutils.h>
#include <libavutil/error.h>
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_qsv.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
}

using namespace std;

struct IMG{
    QImage  *img;
    int64_t pts;
};

class FFmpegVideo : public QThread
{
    Q_OBJECT
public:
    explicit FFmpegVideo();
    ~FFmpegVideo();

    void setPath(QString url);

    void ffmpeg_init_variables();
    void ffmpeg_free_variables();
    int open_input_file();
    static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                            const enum AVPixelFormat *pix_fmts);
    static int hw_decoder_init(AVCodecContext *ctx, const enum AVHWDeviceType type);

    void stopThread();

protected:
    void run();

signals:
    void sendQImage(const QImage &img);
    void sendQImage(const IMG &img);
private:
    AVFormatContext *fmtCtx       =NULL;
    const AVCodec         *videoCodec   =NULL;
    AVCodecContext  *videoCodecCtx=NULL;
    AVPacket        *pkt          = NULL;
    AVFrame         *yuvFrame     = NULL;
    AVFrame         *rgbFrame     = NULL;
    AVFrame         *nv12Frame    = NULL;
    AVStream        *videoStream  = NULL;

    int videowidth=0,videoheight=0;

    uchar *out_buffer;
    struct SwsContext *img_ctx=NULL;

    QString _filePath;

    int videoStreamIndex =-1;
    int numBytes = -1;

    int ret =0;

    bool initFlag=false,openFlag=false,stopFlag=false;
};



class PlayVideo : public QThread
{
    Q_OBJECT
public:
    explicit PlayVideo(){};
    ~PlayVideo(){};

    void stopThread(){};

protected:
    void run();

signals:
    void sendQImage(const IMG &img);
    void sendQImage(const QImage &img);
private:

    uchar *out_buffer;
    struct SwsContext *img_ctx=NULL;

    QString _filePath;

    int videoStreamIndex =-1;
    int numBytes = -1;

    int ret =0;

    bool initFlag=false,openFlag=false,stopFlag=false;
};


class FFmpegWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FFmpegWidget(QWidget *parent = nullptr);
    ~FFmpegWidget();

    void play(QString url);
    void stop();

protected:
    void paintEvent(QPaintEvent *);

private slots:
    void  receiveQImage(const IMG &rImg);
    void  receiveQImage(const QImage &rImg);
private:
    FFmpegVideo *ffmpeg;
    PlayVideo   *playf;

    QImage img;
};

#endif // FFMPEGVIDEO_H
