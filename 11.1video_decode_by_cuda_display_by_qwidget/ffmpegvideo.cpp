#include "ffmpegvideo.h"

#include <QDateTime>
#include <QApplication>
#include <QMainWindow>

#include <Windows.h>
#include <SDL.h>

#include <mutex>
#include <thread>
#include <vector>

#include <omp.h>

std::mutex g_mutex;

#include <tuple>
using std::tuple;

list<IMG> Images;

list<tuple<int64_t /*pts*/, AVFrame * /*buffer*/>> frameTupleList;

int useSDL = 1;

AVPixelFormat destFormat = AV_PIX_FMT_YUV420P; // SDL
AVFrame *sendFrame;

SDL_Window *win = nullptr;
SDL_Surface *_pScreens;
SDL_Surface *_pload;
SDL_Renderer *renderer;
SDL_Texture *texture;
SDL_Event SDLevent;

int g_videowidth = 1920 / 2;
int g_videoheight = 1080 / 2;

static enum AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;
static AVBufferRef *hw_device_ctx = NULL;
static int linesize = 1080;

FFmpegVideo::FFmpegVideo()
{
}

void FFmpegVideo::setPath(QString url)
{
    _filePath = url;
}

void FFmpegVideo::ffmpeg_init_variables()
{
    avformat_network_init();
    fmtCtx = avformat_alloc_context();
    pkt = av_packet_alloc();

    yuvFrame = av_frame_alloc();
    rgbFrame = av_frame_alloc();
    nv12Frame = av_frame_alloc();

    sendFrame = av_frame_alloc();

    initFlag = true;
}

void FFmpegVideo::ffmpeg_free_variables()
{
    if (!pkt)
        av_packet_free(&pkt);
    if (!yuvFrame)
        av_frame_free(&yuvFrame);
    if (!rgbFrame)
        av_frame_free(&rgbFrame);
    if (!nv12Frame)
        av_frame_free(&nv12Frame);
    if (!videoCodecCtx)
        avcodec_free_context(&videoCodecCtx);
    if (!videoCodecCtx)
        avcodec_close(videoCodecCtx);
    if (!fmtCtx)
        avformat_close_input(&fmtCtx);
}

AVPixelFormat FFmpegVideo::get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts)
{
    Q_UNUSED(ctx)
    const enum AVPixelFormat *p;

    // pix_fmts to string
    qDebug() << "opened hw format:" << av_get_pix_fmt_name(hw_pix_fmt);

    for (p = pix_fmts; *p != -1; p++)
    {
        qDebug() << "Compare:" << av_get_pix_fmt_name(*p) << *p;
        if (*p == hw_pix_fmt||*p==0)
        {
            hw_pix_fmt = *p;
            qDebug("use hw format: %s[%s]", av_get_pix_fmt_name(*p), av_get_pix_fmt_name(hw_pix_fmt));
            return *p;
        }
    }

    qDebug("Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

int FFmpegVideo::hw_decoder_init(AVCodecContext *ctx, const AVHWDeviceType type)
{
    int err = 0;

    if ((err = av_hwdevice_ctx_create(&hw_device_ctx, type,
                                      NULL, NULL, 0)) < 0)
    {
        qDebug("Failed to create specified HW device.\n");
        return err;
    }
    else
    {
        qDebug("Create specified HW device [%s] OK.", av_hwdevice_get_type_name(type));
    }
    ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

    return err;
}

void FFmpegVideo::stopThread()
{
    stopFlag = true;
}

int FFmpegVideo::open_input_file()
{
    if (!initFlag)
    {
        ffmpeg_init_variables();
        qDebug() << "init variables done";
    }

    enum AVHWDeviceType type;
    int i = 0;

    /* cuda dxva2 d3d11va qsv h264_qsv*/
    type = av_hwdevice_find_type_by_name(hwType.toLocal8Bit().data());

    if (type == AV_HWDEVICE_TYPE_NONE)
    {
        qDebug("Device type %s is not supported.\n", "h264_cuvid");
        qDebug("Available device types:");
        while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            qDebug(" %s", av_hwdevice_get_type_name(type));
        // qDebug( "\n");
        return -1;
    }

    /* open the input file */
    fprintf(stderr, "open %s.\n", _filePath.toLocal8Bit().data());
    if (avformat_open_input(&fmtCtx, _filePath.toLocal8Bit().data(), NULL, NULL) != 0)
    {
        return -1;
    }

    const QWidgetList &list = QApplication::topLevelWidgets();

        for(QWidget * w : list){
            QMainWindow *mainWindow = qobject_cast<QMainWindow*>(w);
            if(mainWindow){
                mainWindow->setWindowTitle(QCoreApplication::translate(_filePath.toLocal8Bit().data(), _filePath.toLocal8Bit().data(), nullptr));
                qDebug() << "MainWindow found" << w;
            }
        }

    if (avformat_find_stream_info(fmtCtx, NULL) < 0)
    {
        qDebug("Cannot find input stream information.\n");
        return -1;
    }

    /* find the video stream information */
    ret = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);
    if (ret < 0)
    {
        qDebug("Cannot find a video stream in the input file\n");
        return -1;
    }
    videoStreamIndex = ret;

    // find the audio stream information
    ret = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &audioCodec, 0);
    if (ret < 0)
    {
        qDebug("Cannot find a audio stream in the input file\n");
        return -1;
    }
    else
    {
        fprintf(stderr, "find audio stream index: %d", ret);
    }
    audioStreamIndex = ret;

    // 获取支持该decoder的hw配置型
    for (i = 0;; i++)
    {
        const AVCodecHWConfig *config = avcodec_get_hw_config(videoCodec, i);
        if (!config)
        {
            qDebug("Decoder %s does not support device type %s.",
                   videoCodec->name, av_hwdevice_get_type_name(type));
            break;
        }
        qDebug("hw device pix fmt %s.", av_get_pix_fmt_name(config->pix_fmt));
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == type)
        {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    if (!(videoCodecCtx = avcodec_alloc_context3(videoCodec)))
        return AVERROR(ENOMEM);

    videoStream = fmtCtx->streams[videoStreamIndex];
    if (avcodec_parameters_to_context(videoCodecCtx, videoStream->codecpar) < 0)
        return -1;

    if (AV_PIX_FMT_NONE != hw_pix_fmt)
    {
        videoCodecCtx->get_format = get_hw_format;

        QThread::msleep(5);
        if (hw_decoder_init(videoCodecCtx, type) < 0)
            ; // return -1;
    }

    QThread::msleep(5);

    AVDictionary *captureOptions = NULL; // 创建一个设置键值对
    // av_dict_set(&captureOptions, "avioflags", "direct", 0);//消除ffmpeg编码引起的画面断层
    //  av_dict_set_int(&captureOptions, "rtbufsize",  3*1024*1024, 0);
    // av_dict_set(&captureOptions, "video_size", "1280X680", 0);
    av_dict_set(&captureOptions, "stimeout", "10", 0);
    av_dict_set(&captureOptions, "threads", "auto", 0);
    // av_dict_set(&captureOptions, "framerate", "30", 0);//设置帧数 作者：落天尘心 https://www.bilibili.com/read/cv17801080 出处：bilibili

    if ((ret = avcodec_open2(videoCodecCtx, videoCodec, &captureOptions)) < 0)
    {
        qDebug("Failed to open codec for stream #%u\n", videoStreamIndex);
        return -1;
    }

    videowidth = videoCodecCtx->width;
    videoheight = videoCodecCtx->height;

    g_videoheight = videoCodecCtx->height;
    g_videowidth = videoCodecCtx->width;

    if (!useSDL) //
    {
        destFormat = AV_PIX_FMT_RGB32;
    }

    // AV_PIX_FMT_P010LE one 4K
    AVPixelFormat src_fmt = AV_PIX_FMT_NV12; // AV_PIX_FMT_NV12;//videoCodecCtx->pix_fmt;AV_PIX_FMT_NV12;
    img_ctx = sws_getContext(videoCodecCtx->width,
                             videoCodecCtx->height,
                             src_fmt, // AV_PIX_FMT_NV12,//AV_PIX_FMT_NV21//videoCodecCtx->pix_fmt, //AV_PIX_FMT_NV12 ,//  AV_PIX_FMT_NV12,
                             videoCodecCtx->width,
                             videoCodecCtx->height,
                             destFormat,  // AV_PIX_FMT_RGB32, AV_PIX_FMT_NV12 AV_PIX_FMT_YUV420P
                             SWS_BICUBIC, // SWS_BICUBIC, SWS_BILINEAR SWS_FAST_BILINEAR
                             NULL, NULL, NULL);

    sendFrame->width = videoCodecCtx->width;
    sendFrame->height = videoCodecCtx->height;

    numBytes = av_image_get_buffer_size(destFormat, videoCodecCtx->width, videoCodecCtx->height, 1);
    qDebug() << "VideoWidth:" << videoCodecCtx->width << "VideoHeight:" << videoCodecCtx->height << "BufferSize:" << numBytes;

    if (videoCodecCtx->width == 0 || videoCodecCtx->height == 0 || numBytes <= 0)
    {
        return -1;
    }

    out_buffer = (unsigned char *)av_malloc(numBytes * sizeof(uchar));

    int res = av_image_fill_arrays(
        rgbFrame->data, rgbFrame->linesize,
        out_buffer,
        destFormat,
        videoCodecCtx->width, videoCodecCtx->height, 1);

    if (res < 0)
    {
        qDebug() << "Fill arrays failed.";
        return -1;
    }

    openFlag = true;
    return true;
}

void FFmpegVideo::run()
{
    int debugstep = 0;
    double PCFreq = 0.0;
    LARGE_INTEGER freq;

    LARGE_INTEGER li;
    if (!QueryPerformanceFrequency(&li))
    {
        qDebug() << "QueryPerformanceFrequency failed!";
    }

    PCFreq = double(li.QuadPart) / 1000.0;

    QueryPerformanceCounter(&li);
    int64_t CounterStart = li.QuadPart;

    bool useHWDecodeFlag = true;

    //useHWDecodeFlag = (AV_PIX_FMT_NONE != hw_pix_fmt );//&& yuvFrame->format == videoCodecCtx->pix_fmt);

    if (!openFlag)
    {
        open_input_file();
    }

    while (av_read_frame(fmtCtx, pkt) >= 0)
    {
        if (stopFlag)
            break;
        if (pkt->stream_index == videoStreamIndex)
        {
            if (debugstep)
                fprintf(stderr, " %2d %s ", debugstep + 1, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());
            if (avcodec_send_packet(videoCodecCtx, pkt) >= 0)
            {
                if (debugstep)
                    fprintf(stderr, " %2d %s ", 2, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());

                int ret;
                while ((ret = avcodec_receive_frame(videoCodecCtx, yuvFrame)) >= 0)
                {
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        return;
                    else if (ret < 0)
                    {
                        qDebug("Error during decoding\n");
                        exit(1);
                    }

                    if (debugstep) fprintf(stderr, " %2d %s ", 3, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());

                    if (AV_PIX_FMT_NONE == hw_pix_fmt)
                    {
                        hw_pix_fmt;
                        hw_pix_fmt = hw_pix_fmt;
                    }
                    if (AV_PIX_FMT_NONE != hw_pix_fmt && yuvFrame->format == videoCodecCtx->pix_fmt)
                    {
                        // nv12Frame->format=AV_PIX_FMT_NV12;
                        static bool hwframe_mapok = 1;
                        int ret = -1;

                        if (hwframe_mapok)
                        {
#ifdef QT_DEBUG_HWMAP
                            QueryPerformanceCounter(&li);
                            int64_t CounterStart = li.QuadPart;
#endif
                            ret = av_hwframe_map(nv12Frame, yuvFrame, 0);
                            if (debugstep)fprintf(stderr, " %2d %s ", 31, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());
#ifdef QT_DEBUG_HWMAP
                            QueryPerformanceCounter(&li);
                            double times = 1.0 * (li.QuadPart - CounterStart) / PCFreq;
                            qDebug("\033[32mav_hwframe_map Time:%d %.2fms", li.QuadPart - CounterStart, times);
#endif

                            if (ret < 0)
                            {
                                hwframe_mapok = false;
                                char buf[256] = {0};
                                av_strerror(ret, buf, sizeof(buf) - 1);
                                qCritical() << "av_hwframe_map error: " << buf;
                                // av_frame_unref(nv12Frame);
                            }
                            else
                            {
                                nv12Frame->height = yuvFrame->height;
                                nv12Frame->width = yuvFrame->width;
                                static bool infoFlag = false;
                                if (!infoFlag)
                                {
                                    infoFlag = 1;
                                    qDebug() << "av_hwframe_map ok";
                                }
                            }
                        }

                        static bool hw_transfer_flag = true;
                        if (!hwframe_mapok && hw_transfer_flag )
                        {
#ifdef QT_DEBUG_HWMAP
                            QueryPerformanceCounter(&li);
                            int64_t CounterStart = li.QuadPart;
#endif
                            ret = av_hwframe_transfer_data(nv12Frame, yuvFrame, 0);
                            if (debugstep) fprintf(stderr, " %2d %s ", 32, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());
#ifdef QT_DEBUG_HWMAP
                            QueryPerformanceCounter(&li);
                            double times = 1.0 * (li.QuadPart - CounterStart) / PCFreq;
                            qDebug("\033[32mav_hwframe_transfer_data Time:%d %.2fms", li.QuadPart - CounterStart, times);
#endif

#ifdef QT_DEBUG
                            AVPixelFormat f1 = static_cast<AVPixelFormat>(yuvFrame->format);
                            AVPixelFormat f2 = static_cast<AVPixelFormat>(nv12Frame->format);
                            AVPixelFormat f3 = AV_PIX_FMT_YUV420P;
                            AVPixelFormat f4 = AV_PIX_FMT_NV12;
                            f4 = f4;
#endif
                            if ((ret) < 0)
                            {
                                hw_transfer_flag  = false;
                                static bool outflag = false;
                                if (!outflag)
                                {
                                    char buf[256] = {0};
                                    av_strerror(ret, buf, sizeof(buf) - 1);
                                    qCritical() << "av_hwframe_transfer_data error: " << buf << nv12Frame->format;
                                    outflag = true;
                                }
                                //if (nv12Frame->format == AV_PIX_FMT_NONE)  continue;
                            }
                        }
                    }

                    if (!useSDL && nv12Frame->format != AV_PIX_FMT_NV12)
                    {
                        static bool changed = false; // AV_PIX_FMT_P010LE
                        if (!changed)
                        {
                            qDebug() << "Format:" << nv12Frame->format;
                            qDebug() << "change src format from " << av_get_pix_fmt_name((AVPixelFormat)(nv12Frame->format)) << "to" << av_get_pix_fmt_name((AVPixelFormat)destFormat);
                            AVPixelFormat src_fmt = AV_PIX_FMT_P010LE; // videoCodecCtx->pix_fmt;AV_PIX_FMT_NV12;
                            img_ctx = sws_getContext(videoCodecCtx->width,
                                                     videoCodecCtx->height,
                                                     (AVPixelFormat)nv12Frame->format, // AV_PIX_FMT_NV12,//AV_PIX_FMT_NV21//videoCodecCtx->pix_fmt, //AV_PIX_FMT_NV12 ,//  AV_PIX_FMT_NV12,
                                                     videoCodecCtx->width,
                                                     videoCodecCtx->height,
                                                     destFormat,        // AV_PIX_FMT_RGB32, AV_PIX_FMT_NV12 AV_PIX_FMT_YUV420P
                                                     SWS_FAST_BILINEAR, // SWS_BICUBIC, SWS_BILINEAR
                                                     NULL, NULL, NULL);
                            changed = true;
                        }
                    }

                    // use SDL OK!!!
                    if (useSDL)
                    {
                        uchar *out_buffer1 = nullptr;
                        //if (AV_PIX_FMT_NONE != hw_pix_fmt)
                        {
                            if ( 1 || nv12Frame->format == AV_PIX_FMT_P010LE)
                            {
                                //AV_PIX_FMT_NV12,   ///< planar YUV 4:2:0, 12bpp, 1 plane for Y and 1 plane for the UV components, which are interleaved (first byte U and the following byte V)
                                //AV_PIX_FMT_P010LE, ///< like NV12, with 10bpp per component, data in the high bits, zeros in the low bits, little-endian

                                if (debugstep) fprintf(stderr, " %2d %s ", 81, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());
                                //numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height, 1);
                                //numBytes = av_image_get_buffer_size(AV_PIX_FMT_NV12, videoCodecCtx->width, videoCodecCtx->height, 1);
                                //numBytes = av_image_get_buffer_size(AV_PIX_FMT_P010LE, videoCodecCtx->width, videoCodecCtx->height, 1);

                                if (debugstep) fprintf(stderr, " %2d %s ", 82, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());
                                destFormat =AV_PIX_FMT_P010LE;
                                linesize = nv12Frame->linesize[0];

                                AVFrame* dst_frame = av_frame_alloc();
                                AVFrame* src_frame;

                                if(AV_PIX_FMT_NONE == hw_pix_fmt || AV_PIX_FMT_YUV420P == hw_pix_fmt || nv12Frame->format == -1 ){
                                    src_frame = yuvFrame;
                                }else{
                                    src_frame = nv12Frame;
                                }
                                dst_frame->format = src_frame->format;
                                dst_frame->width  = src_frame->width;
                                dst_frame->height = src_frame->height;
                                dst_frame->channels = src_frame->channels;
                                dst_frame->channel_layout = src_frame->channel_layout;
                                dst_frame->nb_samples = src_frame->nb_samples;
                                int ret = av_frame_get_buffer(dst_frame, 0);

                                av_frame_copy(dst_frame, src_frame);
                                if (debugstep) fprintf(stderr, " %2d %s ", 83, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());
                                av_frame_copy_props(dst_frame, src_frame);

                                if (debugstep) fprintf(stderr, " %2d %s \n", 84, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());
                                if (debugstep) fflush(stderr);

                                dst_frame->pts    = yuvFrame->pts;
                                dst_frame->pkt_dts= yuvFrame->pkt_dts;

                                g_mutex.lock();
                                frameTupleList.push_back(make_tuple(yuvFrame->pts, dst_frame));
                                g_mutex.unlock();

                                av_frame_unref(nv12Frame);
                                av_frame_unref(yuvFrame);

                                QThread::msleep(0);
                                continue;
                            }else  if (nv12Frame->format != AV_PIX_FMT_NV12)
                            {
                                static bool changed = false; // AV_PIX_FMT_P010LE
                                if (!changed)
                                {
                                    qDebug() << "Format:" << nv12Frame->format;
                                    qDebug() << "change src format from " << av_get_pix_fmt_name((AVPixelFormat)(nv12Frame->format)) << "to" << av_get_pix_fmt_name(AV_PIX_FMT_NV12);
                                    AVPixelFormat src_fmt = AV_PIX_FMT_P010LE; // videoCodecCtx->pix_fmt;AV_PIX_FMT_NV12;
                                    img_ctx = sws_getContext(videoCodecCtx->width,
                                                             videoCodecCtx->height,
                                                             (AVPixelFormat)nv12Frame->format, // AV_PIX_FMT_NV12 videoCodecCtx->pix_fmt, //AV_PIX_FMT_NV12
                                                             videoCodecCtx->width,
                                                             videoCodecCtx->height,
                                                             AV_PIX_FMT_NV12, // AV_PIX_FMT_RGB32, AV_PIX_FMT_NV12 AV_PIX_FMT_YUV420P
                                                             SWS_BICUBIC,     // SWS_BICUBIC, SWS_BILINEAR SWS_FAST_BILINEAR
                                                             NULL, NULL, NULL);
                                    changed = true;
                                }

                                if (debugstep)
                                    fprintf(stderr, " %2d %s ", 7, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());

                                //*
                                ret = sws_scale(img_ctx,
                                                (const uint8_t *const *)nv12Frame->data,
                                                (const int *)nv12Frame->linesize,
                                                0,
                                                nv12Frame->height,
                                                rgbFrame->data, rgbFrame->linesize);
                                if (debugstep)
                                    fprintf(stderr, " %2d %s ", 8, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());

                                if ((ret) < 0)
                                {
                                    char txtbuf[256] = {0};
                                    av_strerror(ret, txtbuf, sizeof(txtbuf) - 1);
                                    qCritical() << "sws_scale error: " << txtbuf;
                                    av_frame_unref(nv12Frame);
                                    av_frame_unref(yuvFrame);
                                    continue;
                                }

                                //*/

                                numBytes = av_image_get_buffer_size(AV_PIX_FMT_NV12, videoCodecCtx->width, videoCodecCtx->height, 1);
                                out_buffer1 = (unsigned char *)av_malloc(numBytes * sizeof(uchar));
                                // memset(out_buffer1, 'A', numBytes);
                                //*
                                av_image_copy_to_buffer(out_buffer1, numBytes,
                                                        (const uint8_t *const *)rgbFrame->data, // const uint8_t * const src_data[4],
                                                        (const int *)rgbFrame->linesize,        // const int src_linesize[4],
                                                        AV_PIX_FMT_NV12,                        // enum AVPixelFormat pix_fmt, int width, int height, int align);
                                                        nv12Frame->width, nv12Frame->height,
                                                        1);
                                //*/
                                // memcpy(out_buffer1, out_buffer, numBytes);

                                linesize = rgbFrame->linesize[0];
                                destFormat =AV_PIX_FMT_NV12;
                            }
                            else
                            {
                                //AV_PIX_FMT_NV12,   ///< planar YUV 4:2:0, 12bpp, 1 plane for Y and 1 plane for the UV components, which are interleaved (first byte U and the following byte V)
                                //AV_PIX_FMT_P010LE, ///< like NV12, with 10bpp per component, data in the high bits, zeros in the low bits, little-endian

                                numBytes = av_image_get_buffer_size(AV_PIX_FMT_P010LE, videoCodecCtx->width, videoCodecCtx->height, 1);
                                numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height, 1);
                                numBytes = av_image_get_buffer_size(AV_PIX_FMT_NV12, videoCodecCtx->width, videoCodecCtx->height, 1);
                                out_buffer1 = (unsigned char *)av_malloc(numBytes * sizeof(uchar));
                                // memset(out_buffer1, 'A', numBytes);

                                av_image_copy_to_buffer(out_buffer1, numBytes,
                                                        (const uint8_t *const *)nv12Frame->data, // const uint8_t * const src_data[4],
                                                        (const int *)nv12Frame->linesize,        // const int src_linesize[4],
                                                        AV_PIX_FMT_NV12,                         // enum AVPixelFormat pix_fmt, int width, int height, int align);
                                                        nv12Frame->width, nv12Frame->height,
                                                        1);
                                destFormat =AV_PIX_FMT_NV12;
                                linesize = nv12Frame->linesize[0];
                            }

                            // memcpy(out_buffer1, out_buffer, numBytes);

                            av_frame_unref(nv12Frame);
                            av_frame_unref(yuvFrame);

                            // if(debugstep)fprintf(stderr," %2d %s ",debugstep+5,QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str() );
                            g_mutex.lock();
                            //frameTupleList.push_back(make_tuple(yuvFrame->pts, out_buffer1));
                            av_free(out_buffer1);
                            g_mutex.unlock();
                            if (debugstep) fprintf(stderr, " %2d %s \n", 9, QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str());
                        }
                        //else // AV_PIX_FMT_NONE==hw_pix_fmt do not use hw
                        {
                            static bool changed = false; // AV_PIX_FMT_P010LE
                            if (!changed && AV_PIX_FMT_YUV420P !=yuvFrame->format )
                            {
                                qDebug() << "Format:" << yuvFrame->format;
                                qDebug() << "change src format from " << av_get_pix_fmt_name((AVPixelFormat)(yuvFrame->format)) << "to" << av_get_pix_fmt_name(AV_PIX_FMT_YUV420P);
                                AVPixelFormat src_fmt = AV_PIX_FMT_P010LE; // videoCodecCtx->pix_fmt;AV_PIX_FMT_NV12;
                                img_ctx = sws_getContext(videoCodecCtx->width,
                                                         videoCodecCtx->height,
                                                         (AVPixelFormat)yuvFrame->format, // AV_PIX_FMT_NV12 videoCodecCtx->pix_fmt, //AV_PIX_FMT_NV12
                                                         videoCodecCtx->width,
                                                         videoCodecCtx->height,
                                                         AV_PIX_FMT_YUV420P, // AV_PIX_FMT_RGB32, AV_PIX_FMT_NV12 AV_PIX_FMT_YUV420P
                                                         SWS_FAST_BILINEAR,     // SWS_BICUBIC, SWS_BILINEAR SWS_FAST_BILINEAR
                                                         NULL, NULL, NULL);
                                changed = true;
                            }

                            if(AV_PIX_FMT_YUV420P !=yuvFrame->format)
                            {
                                //*
                                ret = sws_scale(img_ctx,
                                                (const uint8_t *const *)yuvFrame->data,
                                                (const int *)yuvFrame->linesize,
                                                0,
                                                yuvFrame->height,
                                                rgbFrame->data, rgbFrame->linesize);

                                if ((ret) < 0)
                                {
                                    char txtbuf[256] = {0};
                                    av_strerror(ret, txtbuf, sizeof(txtbuf) - 1);
                                    qCritical() << "sws_scale error: " << txtbuf;
                                    av_frame_unref(yuvFrame);
                                    continue;
                                }

                                numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height, 1);
                                out_buffer1 = (unsigned char *)av_malloc(numBytes * sizeof(uchar));
                                // memset(out_buffer1, 'A', numBytes);
                                //*
                                av_image_copy_to_buffer(out_buffer1, numBytes,
                                                        (const uint8_t *const *)rgbFrame->data, // const uint8_t * const src_data[4],
                                                        (const int *)rgbFrame->linesize,        // const int src_linesize[4],
                                                        AV_PIX_FMT_YUV420P,                        // enum AVPixelFormat pix_fmt, int width, int height, int align);
                                                        yuvFrame->width, yuvFrame->height,
                                                        1);
                                //*/
                                // memcpy(out_buffer1, out_buffer, numBytes);

                                linesize = rgbFrame->linesize[0];
                                destFormat =AV_PIX_FMT_YUV420P;
                            }else{
                                numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height, 1);
                                out_buffer1 = (unsigned char *)av_malloc(numBytes * sizeof(uchar));
                                // memset(out_buffer1, 'A', numBytes);
                                //*
                                av_image_copy_to_buffer(out_buffer1, numBytes,
                                                        (const uint8_t *const *)yuvFrame->data, // const uint8_t * const src_data[4],
                                                        (const int *)yuvFrame->linesize,        // const int src_linesize[4],
                                                        AV_PIX_FMT_YUV420P,                        // enum AVPixelFormat pix_fmt, int width, int height, int align);
                                                        yuvFrame->width, yuvFrame->height,
                                                        1);
                                //*/
                                // memcpy(out_buffer1, out_buffer, numBytes);

                                linesize = yuvFrame->linesize[0];
                                destFormat =AV_PIX_FMT_YUV420P;
                            }

                            av_frame_unref(yuvFrame);

                            // if(debugstep)fprintf(stderr," %2d %s ",debugstep+5,QDateTime::currentDateTime().toString("ss.zzz").toStdString().c_str() );
                            g_mutex.lock();
                            frameTupleList.push_back(make_tuple(yuvFrame->pts, yuvFrame));
                            g_mutex.unlock();
                        }
                    }
                    else
                    {
                        //*
                        ret = sws_scale(img_ctx,
                                        (const uint8_t *const *)nv12Frame->data,
                                        (const int *)nv12Frame->linesize,
                                        0,
                                        nv12Frame->height,
                                        rgbFrame->data, rgbFrame->linesize);

                        if ((ret) < 0)
                        {
                            char buf[256] = {0};
                            av_strerror(ret, buf, sizeof(buf) - 1);
                            qCritical() << "sws_scale error: " << buf;
                            av_frame_unref(nv12Frame);
                            av_frame_unref(yuvFrame);
                            continue;
                        }

                        //*/
                        av_frame_unref(nv12Frame);
                        av_frame_unref(yuvFrame);

                        // numBytes;
                        if (1)
                        { // thread fresh
                            QImage *pimg = new QImage(out_buffer,
                                                      videoCodecCtx->width, videoCodecCtx->height,
                                                      QImage::Format_RGB32);

                            IMG img;
                            img.img = pimg;
                            img.pts = yuvFrame->pts;
                            Images.push_back(img);
                        }
                        else
                        {
                            QImage rImg(out_buffer,
                                        videoCodecCtx->width, videoCodecCtx->height,
                                        QImage::Format_RGB32);
                            emit sendQImage(rImg);
                            QThread::msleep(1);
                        }
                    }

                    QThread::msleep(0);
                }
            }
            av_packet_unref(pkt);
        }
        else if (pkt->stream_index == audioStreamIndex)
        {
            // qDebug()<<"audioStream";
        }
        else
        {
            int index = pkt->stream_index;
            index++;
            //????
            // qDebug()<<"otherStream"<<pkt->stream_index;
        }
    }

    // av_hwdevice_ctx_free(&hw_device_ctx);
    av_frame_free(&yuvFrame);
    av_frame_free(&nv12Frame);
    av_frame_free(&rgbFrame);
    // av_frame_free(&sendFrame);
    av_packet_free(&pkt);
    avcodec_free_context(&videoCodecCtx);
    // av_close_input_file(pFormatCtx);
    // avformat_free_context(pFormatCtx);
    sws_freeContext(img_ctx);
    av_freep(&out_buffer);

    qDebug() << "Thread stop now " << __FUNCTION__;
}

static int ConvertP010toNV12(AVFrame* p010Frame,AVFrame* nv12Frame){
    uint8_t* p010_src[2];
    p010_src[0] = p010Frame->data[0];
    p010_src[1] = p010Frame->data[1];

    uint8_t* nv12_dst[2];
    nv12_dst[0] = nv12Frame->data[0];
    nv12_dst[1] = nv12Frame->data[1];

    uint16_t Y, U, V;

    //yuvFrame->format = p010Frame->format;
    nv12Frame->width = p010Frame->width;
    nv12Frame->height = p010Frame->height;
    //yuvFrame->channels = p010Frame->channels;
    ///yuvFrame->channel_layout = p010Frame->channel_layout;
    //yuvFrame->nb_samples = p010Frame->nb_samples;

    int numBytesd = av_image_get_buffer_size(AV_PIX_FMT_NV12, nv12Frame->width, nv12Frame->height, 1);

    //qDebug("total:%d [%d*%d=%d] Y:%d",
    //       numBytesd, nv12Frame->width, nv12Frame->height,nv12Frame->width*nv12Frame->height,
    //       nv12Frame->data[1]-nv12Frame->data[0]);

    if (0)
    {
#pragma omp parallel for
        for (int i = 0; i < numBytesd; i++)
        {
            *(nv12_dst[0]++) = p010_src[0][i * 2 + 1];
        }
        if (1)  return 0;
    }

//#pragma omp parallel sections
    {
        // Y
//#pragma omp section
#pragma omp parallel for
        for (int i = 0; i < p010Frame->width * p010Frame->height; i++)
        {
            *(nv12_dst[0]++) = *((uint8_t *)p010_src[0] + i * 2 + 1);
        }

        // UV
//#pragma omp section
#pragma omp parallel for
        for (int i = 0; i < p010Frame->width * p010Frame->height / 2; i++)
        {
            *(nv12_dst[1]++) = *((uint8_t *)p010_src[1] + i * 2 + 1);
        }
    }
    return 0;
}

static int ConvertYUV420P10LEtoYUV420P(AVFrame* p010Frame,AVFrame* yuvFrame){
    uint16_t* p010_src[2];
    p010_src[0] = (uint16_t*)p010Frame->data[0];
    p010_src[1] = (uint16_t*)p010Frame->data[1];
    p010_src[2] = (uint16_t*)p010Frame->data[2];

    uint8_t* yuv420_dst[3];
    yuv420_dst[0] = yuvFrame->data[0];
    yuv420_dst[1] = yuvFrame->data[1];
    yuv420_dst[2] = yuvFrame->data[2];

    uint16_t Y, U, V;

    //yuvFrame->format = p010Frame->format;
    yuvFrame->width = p010Frame->width;
    yuvFrame->height = p010Frame->height;
    //yuvFrame->channels = p010Frame->channels;
    ///yuvFrame->channel_layout = p010Frame->channel_layout;
    //yuvFrame->nb_samples = p010Frame->nb_samples;

//*
    if(0){
        int numBytess = av_image_get_buffer_size(AV_PIX_FMT_YUV420P10LE, yuvFrame->width, yuvFrame->height, 1);
        int numBytesd = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, yuvFrame->width, yuvFrame->height, 1);
#pragma omp parallel for
    for(int i = 0 ; i < numBytesd; i ++ ){
        Y = *((uint16_t*)p010_src[0] + i) >> 2;
        *(yuv420_dst[0]++) = (uint8_t)(Y);
    }
    if(1) return 0;
    }
//*/

#pragma omp parallel sections
    {
    //Y
#pragma omp section
    for (int i = 0; i < p010Frame->width * p010Frame->height; i++) {
        Y = *(p010_src[0] ++) >>2;
        *(yuv420_dst[0]++) = (uint8_t)Y;
    }
    //UV
#pragma omp section
    for (int i = 0; i < p010Frame->width * p010Frame->height / 2; i++) {
        U = *(p010_src[1] ++) >>2;
        *(yuv420_dst[1]++) =(uint8_t) U;
    }
}
    return 0;
}

void PlayVideo::run()
{
    int frametime = 40; // ms
    double PCFreq = 0.0;
    LARGE_INTEGER freq;
    struct SwsContext *img_ctx=NULL;

    LARGE_INTEGER li;
    if (!QueryPerformanceFrequency(&li))
    {
        qDebug() << "QueryPerformanceFrequency failed!";
    }

    PCFreq = double(li.QuadPart) / 1000.0;

    QueryPerformanceCounter(&li);
    int64_t CounterStart = li.QuadPart;

    int64_t lipre = CounterStart;

    MMRESULT rx = timeBeginPeriod(1);
    ::Sleep(0);

    int64_t pts_pre = 0;
    int64_t pts_start = 0;
    bool disp = 0;
    int sleeptime = 10;

    if (rx != TIMERR_NOERROR)
    {
        qDebug() << "timeBeginPeriod failed!";
        sleeptime = 390;
    }

    sendFrame = av_frame_alloc();
    bool sdl_inited = false;

    int64_t count = 0;

    QueryPerformanceCounter(&li);
    CounterStart = 0; // li.QuadPart;

    int w, h;
    SDL_Rect rect;

    w = omp_get_num_procs();
    omp_set_num_threads(min(w,6));

    static unsigned char *out_buffer = nullptr;
    AVFrame * yuvFrame = av_frame_alloc();

    while (!stopFlag)
    {
        if ( frameTupleList.size() > 0 )
        {
            tuple<int64_t /*pts*/, AVFrame * /*buffer*/> tuple = frameTupleList.front();

            int64_t pts = get<0>(tuple);
            //uchar *buf = get<1>(tuple);
            AVFrame *sendFrame1 = get<1>(tuple);

            if (useSDL)
            {
                if (!sdl_inited)
                {
                    sdl_inited = true;

                    if (0)
                    {
                        // 创建输出窗口
                        win = SDL_CreateWindow("SDL Video Player",
                                               SDL_WINDOWPOS_UNDEFINED,
                                               SDL_WINDOWPOS_UNDEFINED,
                                               1920 / 2 + 5, 1080 / 2 + 5,
                                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
                    }
                    else
                    {
                        // win = SDL_CreateWindowFrom( (void*)this->windowHandle() );//  (void*)ui->widget->window()->winId());// ->windowHandle());
                    }

                    if (!win)
                    {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create window!");
                        // goto __FAIL;
                    }

                    // SDL渲染器 0:d3d 1:d3d11 2:opengl 3:opengles2 4:software
                    const char *ACCTYPE[5] = {"d3d", "d3d11", "opengl", "opengles2", "software"};
                    if (accelsType != 4)
                        renderer = SDL_CreateRenderer(win, accelsType, SDL_RENDERER_ACCELERATED); // 【-1，0】 0 SDL_RENDERER_ACCELERATED
                    else
                        renderer = SDL_CreateRenderer(win, -1, 0); // 【-1，0】 0 SDL_RENDERER_ACCELERATED

                    qDebug("SDL use accel type:%d:%s", accelsType, ACCTYPE[accelsType]);

                    if (!renderer)
                    {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create renderer!");
                        // goto __FAIL;
                    }

                    SDL_GetWindowSize(win, &w, &h);

                    // 创建显示帧 // YUV420P，即是SDL_PIXELFORMAT_IYUV
                    Uint32 pixformat = SDL_PIXELFORMAT_IYUV; // SDL_PIXELFORMAT_IYUV; SDL_PIXELFORMAT_NV12
                    if(destFormat !=AV_PIX_FMT_YUV420P)
                        pixformat =SDL_PIXELFORMAT_NV12;

                    QString formatStr = av_get_pix_fmt_name((AVPixelFormat)(sendFrame1->format));

                    pts_start = sendFrame1->pts;
                    qDebug() << "Format:" <<  formatStr.toUpper()<<" PTS:"<<pts_start;

                    switch (sendFrame1->format) {
                    case AV_PIX_FMT_YUV420P:
                        pixformat =SDL_PIXELFORMAT_IYUV;
                        break;
                    case AV_PIX_FMT_YUV420P10LE:
                        pixformat =SDL_PIXELFORMAT_IYUV;//Convert to AV_PIX_FMT_YUV420P
                        break;
                    case AV_PIX_FMT_P010LE:
                        pixformat =SDL_PIXELFORMAT_NV12;
                        break;
                    case AV_PIX_FMT_NV12:
                        pixformat =SDL_PIXELFORMAT_NV12;
                        break;
                    default:
                        pixformat =SDL_PIXELFORMAT_NV12;
                    }

                    const char *name = SDL_GetPixelFormatName(pixformat);

                    texture = SDL_CreateTexture(renderer,
                                                pixformat,
                                                SDL_TEXTUREACCESS_STREAMING, // SDL_TEXTUREACCESS_STATIC, SDL_TEXTUREACCESS_STREAMING SDL_TEXTUREACCESS_TARGET
                                                sendFrame1->width,
                                                sendFrame1->height);

                    if (!texture){
                        qDebug("Failed to create Texture %s %d*%d! Exit Thread!",name,g_videowidth,g_videoheight);
                        fprintf(stderr, "Failed to create Texture %s! Exit Thread!",name);
                        fflush(stderr);
                        return; // goto __FAIL;
                    }
                }
            }

            if (pts_pre == 0)  pts_pre = pts;

            // SDL 渲染图像使用NV12格式
            //SDL_UpdateTexture(texture, NULL, buf, linesize); // sendFrame->linesize[0]);

            switch (sendFrame1->format) {
            case AV_PIX_FMT_YUV420P:
                SDL_UpdateYUVTexture(texture, NULL,
                    sendFrame1->data[0], sendFrame1->linesize[0],
                    sendFrame1->data[1], sendFrame1->linesize[1],
                    sendFrame1->data[2], sendFrame1->linesize[2]
                        );
                break;
            case AV_PIX_FMT_YUV420P10LE:
            {
                AV_PIX_FMT_YUV420P ;AV_PIX_FMT_NV12 ;
                static int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, sendFrame1->width, sendFrame1->height, 1);

                if(!out_buffer)  out_buffer = (unsigned char *)av_malloc(numBytes * sizeof(uchar));

                int res1 = -1235 ;
                    res1 = av_image_fill_arrays(
                        yuvFrame->data, yuvFrame->linesize,
                        out_buffer,
                        AV_PIX_FMT_YUV420P,
                        sendFrame1->width, sendFrame1->height, 1);

                    if (res1 < 0)
                    {
                        qDebug() << "Fill arrays failed.";
                        return ;
                    }

                ConvertYUV420P10LEtoYUV420P(sendFrame1,yuvFrame);
                SDL_UpdateYUVTexture(texture, NULL,
                    yuvFrame->data[0], yuvFrame->linesize[0],
                    yuvFrame->data[1], yuvFrame->linesize[1],
                    yuvFrame->data[2], yuvFrame->linesize[2]
                        );

                av_frame_unref(yuvFrame);
                //av_frame_free()
             }
                break;
            case AV_PIX_FMT_P010LE:
            {
                AV_PIX_FMT_YUV420P ;AV_PIX_FMT_NV12 ;
                int numBytes = av_image_get_buffer_size(AV_PIX_FMT_NV12, sendFrame1->width, sendFrame1->height, 1);
                if(!out_buffer)  out_buffer = (unsigned char *)av_malloc(numBytes * sizeof(uchar));
                static int res = -1235 ;
                    res = av_image_fill_arrays(
                        yuvFrame->data, yuvFrame->linesize,
                        out_buffer,
                        AV_PIX_FMT_NV12,
                        sendFrame1->width, sendFrame1->height, 1);

                    if (res < 0)
                    {
                        qDebug() << "Fill arrays failed.";
                        return ;
                    }

                ConvertP010toNV12(sendFrame1,yuvFrame);
                SDL_UpdateNVTexture(texture, NULL,
                        yuvFrame->data[0], yuvFrame->linesize[0],
                        yuvFrame->data[1], yuvFrame->linesize[1]);

                av_frame_unref(yuvFrame);
                //av_frame_free()
             }
                break;
            case AV_PIX_FMT_NV12:
                 SDL_UpdateNVTexture(texture, NULL,
                         sendFrame1->data[0], sendFrame1->linesize[0],
                         sendFrame1->data[1], sendFrame1->linesize[1] );
                break;
            //default:
                //SDL_UpdateTexture(texture, NULL, buf, linesize); // sendFrame->linesize[0]);
            }

            rect.x = 0;
            rect.y = 0;
            rect.w = w;
            rect.h = h;

            // QueryPerformanceCounter(&li);
            // CounterStart=li.QuadPart;
            // qDebug()<< "Render " <<QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*") << (CounterStart-lipre)/PCFreq << pts-pts_pre ;

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);

            // SDL_Delay(5);
            //  av_packet_unref(&packet);

            SDL_PollEvent(&SDLevent);

            //av_free(buf);

            QueryPerformanceCounter(&li);
            if (CounterStart == 0)
            {
                CounterStart = li.QuadPart;
            }
            int64_t totaltimes = 1.0 * (li.QuadPart - CounterStart) / PCFreq;

            // QueryPerformanceCounter(&li);
            // CounterStart=li.QuadPart;

            int deltatime = totaltimes - (frametime * count);
            int sleepms = 1.0 * frametime - deltatime;
            if (sleepms < 0)
                sleepms = 0; // frametime / 3;
            else if (sleepms > frametime)
                sleepms = 0;

            g_mutex.lock();
            frameTupleList.pop_front();
            g_mutex.unlock();

            if(pts_start==0 && sendFrame1->pts>0) pts_start=sendFrame1->pts;
            pts_pre = sendFrame1->pts-pts_start;

            ::Sleep(sleepms);
            //::WaitForSingleObject()
            // QueryPerformanceCounter(&li);
            lipre = CounterStart;

            bool outinfos = 0;
            if (outinfos)
            {
                qDebug("\033[36m %s sleep %2dms TotalFrames:%5d TotalTime:%6d deltaTime:%2d",
                       QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*").toStdString().c_str(),
                       sleepms, count, totaltimes, deltatime);
            }
#define LOGCOUNT (25 * 1)
            if (0 ||
                (count % LOGCOUNT == 0))
            {
                qDebug("\033[38m%s Render sleep %2dms %4d PlayTime:%3d:%02d:%02d.%03d (%5d.%03ds) deltaTime:%2d pts:%d",
                       QDateTime::currentDateTime().toString("hh:mm:ss.zzz").toStdString().c_str(),
                       sleepms, count,
                       totaltimes / 60 / 60 / 1000, totaltimes / 60 / 1000 % 60, totaltimes / 1000 % 60, totaltimes % 1000, totaltimes / 1000, totaltimes % 1000,
                       deltatime,pts_pre/3600
                       );
            }

            av_frame_unref(sendFrame1);
            av_frame_free(&sendFrame1);

            count++;
        }
        else if (Images.size() > 0)
        {
            IMG img = Images.front();
            Images.pop_front();

            if (!disp)
            {
                QueryPerformanceCounter(&li);
                //(li.QuadPart-lipre)/PCFreq;

                QThread::yieldCurrentThread();
                emit sendQImage(img); //

                // qDebug()<< "S" <<QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*")<<img.pts-pts_pre<<(li.QuadPart-lipre)/PCFreq<<Images.size();
                // QThread::yieldCurrentThread();
                // disp=1;
                lipre = li.QuadPart;
            }

            if (Images.size() > 6)
            {
                // qDebug()<< "S" <<QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*")<<img.pts-pts_pre<<Images.size();
                // sleeptime--;
                // if(sleeptime<=25)sleeptime=25;
            }
            else
            {
                // sleeptime++;
                // if(sleeptime>=38)sleeptime=38;
            }

            pts_pre = img.pts;
            ::Sleep(sleeptime);
            // delete img.img;
        }
        else
        {
            // NSSleep(sleeptime);
            // qDebug()<<"PlayVideo Thread sleep";
            QThread::msleep(1);
            // av_usleep(sleeptime*1000);
            //::Sleep(sleeptime);
        }
    }

    if (useSDL)
    {
        if (win)
            SDL_DestroyWindow(win);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyTexture(texture);
    }
    rx = timeEndPeriod(1);

    qDebug() << "PlayVideo Thread end now" << __FUNCTION__;
}

FFmpegWidget::FFmpegWidget(QWidget *parent) : QWidget(parent)
{
    ffmpeg = new FFmpegVideo;

    connect(ffmpeg, SIGNAL(sendQImage(QImage)), this, SLOT(receiveQImage(QImage)), Qt::DirectConnection);
    connect(ffmpeg, &FFmpegVideo::finished, ffmpeg, &FFmpegVideo::deleteLater);

    playf = new PlayVideo;

    // connect(ffmpeg,SIGNAL(snedQImage(IMG)),this,SLOT(receiveQImage(IMG)),Qt::DirectConnection);
    connect(playf, SIGNAL(sendQImage(IMG)), this, SLOT(receiveQImage(IMG)), Qt::DirectConnection);
    connect(playf, SIGNAL(sendQImage(QImage)), this, SLOT(receiveQImage(QImage)), Qt::DirectConnection);
}

FFmpegWidget::~FFmpegWidget()
{
    qDebug() << "exit player";
    if (ffmpeg->isRunning())
    {
        stop();
    }
}

void FFmpegWidget::play(QString url)
{
    if (0)
    {
        SDL_Window *window;                                          //->winId()
        window = SDL_CreateWindowFrom((void *)this->windowHandle()); //  (void*)ui->widget->window()->winId());// ->windowHandle());
        SDL_Surface *_pScreens = SDL_GetWindowSurface(window);

        SDL_LoadBMP("C:\\Dev\\vlcsnap.bmp");
        return;
    }

    ffmpeg->setPath(url);

    ffmpeg->start();
    ffmpeg->setPriority(QThread::TimeCriticalPriority);

    playf->start();
    playf->setPriority(QThread::TimeCriticalPriority);
}

void FFmpegWidget::stop()
{
    if (ffmpeg->isRunning())
    {
        ffmpeg->stopThread();
        playf->stopThread();
        QThread::msleep(20);
        ffmpeg->requestInterruption();
        ffmpeg->quit();
        ffmpeg->wait(200);
    }

    if (playf->isRunning())
    {
        QThread::msleep(50);
        playf->requestInterruption();
        playf->quit();
        playf->wait(200);
    }

    ffmpeg->ffmpeg_free_variables();
}

void FFmpegWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    // qDebug()<< "Draw" << QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*")  ;
    painter.drawImage(0, 0, img);
    // qDebug()<< "Draw" << QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*")  ;
}

void FFmpegWidget::receiveQImage(const IMG &rImg)
{
    static int64_t times = 0;

    QDateTime startDT = QDateTime::currentDateTime();
    static QDateTime preDT = QDateTime::currentDateTime();

    int delta = (startDT.time().msec() - preDT.time().msec());
    if (delta < 0)
        delta += 1000;

    // delta-=40;

    static int64_t pts_pre = 0;
    // QThread::yieldCurrentThread();

    // img = rImg.img->scaled(nw,nh);//KeepAspectRatioByExpanding  KeepAspectRatio SmoothTransformation FastTransformation

    img = rImg.img->scaled(this->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // qDebug()<< "R" <<startDT.toString("-hh:mm:ss.zzz-")<< QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*") << delta << rImg.pts-pts_pre<<rImg.pts;
    pts_pre = rImg.pts;
    preDT = startDT;
    delete rImg.img;

    // QPainter painter(this);
    // painter.drawImage(0,0,img);

    // update();
    this->repaint();

    if (times % 25 == 0)
    {
        qDebug() << "R" << startDT.toString("-hh:mm:ss.zzz-") << QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*") << delta << rImg.pts - pts_pre << times;
    }
    times++;
    QThread::yieldCurrentThread();
}

void FFmpegWidget::receiveQImage(const QImage &rImg)
{
    static int64_t times = 0;
    QDateTime startDT = QDateTime::currentDateTime();
    static QDateTime preDT = QDateTime::currentDateTime();

    int delta = (startDT.time().msec() - preDT.time().msec());
    if (delta < 0)
        delta += 1000;

    // delta-=40;

    static int64_t pts_pre = 0;

    img = rImg.scaled(this->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // update();
    repaint();
    if (times % 25 == 0)
    {
        qDebug() << "R" << startDT.toString("-hh:mm:ss.zzz-") << QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*") << delta;
    }
    times++;
    // QThread::yieldCurrentThread();
}
