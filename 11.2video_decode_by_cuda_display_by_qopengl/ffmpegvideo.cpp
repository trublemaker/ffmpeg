#include "ffmpegvideo.h"

//#include <sys/time.h>
#include <time.h>
#include <QDebug>
#include <QTime>
#include <qdatetime.h>

#include <Windows.h>
#include <list>

int NSSleep(int msec);

list<IMG> Images;

typedef struct DecodeContext{
    AVBufferRef *hw_device_ref;
}DecodeContext;

DecodeContext decode = {NULL};

static enum AVPixelFormat hw_pix_fmt;
static AVBufferRef* hw_device_ctx=NULL;

FFmpegVideo::FFmpegVideo()
{}

FFmpegVideo::~FFmpegVideo()
{}

void FFmpegVideo::setPath(QString url)
{
    _filePath=url;
}

void FFmpegVideo::ffmpeg_init_variables()
{
    avformat_network_init();
    fmtCtx = avformat_alloc_context();
    pkt = av_packet_alloc();
    yuvFrame = av_frame_alloc();
    rgbFrame = av_frame_alloc();
    nv12Frame = av_frame_alloc();

    initFlag=true;
}

void FFmpegVideo::ffmpeg_free_variables()
{
    if(!pkt) av_packet_free(&pkt);
    if(!yuvFrame) av_frame_free(&yuvFrame);
    if(!rgbFrame) av_frame_free(&rgbFrame);
    if(!nv12Frame) av_frame_free(&nv12Frame);
    if(!videoCodecCtx) avcodec_free_context(&videoCodecCtx);
    if(!videoCodecCtx) avcodec_close(videoCodecCtx);
    if(!fmtCtx) avformat_close_input(&fmtCtx);
}

int FFmpegVideo::open_input_file()
{
    if(!initFlag){
        ffmpeg_init_variables();
        qDebug()<<"init variables done";
    }

    enum AVHWDeviceType type;
    int i;

    /* cuda dxva2 qsv d3d11va */
    type = av_hwdevice_find_type_by_name("cuda");//cuda
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", "h264_cuvid");
        fprintf(stderr, "Available device types:");
        while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return -1;
    }

    /* open the input file */
    if (avformat_open_input(&fmtCtx, _filePath.toLocal8Bit().data(), NULL, NULL) != 0) {
        return -1;
    }

    if (avformat_find_stream_info(fmtCtx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }

    /* find the video stream information */
    ret = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return -1;
    }
    videoStreamIndex = ret;

    for (i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(videoCodec, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    videoCodec->name, av_hwdevice_get_type_name(type));
            return -1;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    if (!(videoCodecCtx = avcodec_alloc_context3(videoCodec)))
        return AVERROR(ENOMEM);

    videoStream = fmtCtx->streams[videoStreamIndex];
    if (avcodec_parameters_to_context(videoCodecCtx, videoStream->codecpar) < 0)
        return -1;

    videoCodecCtx->get_format  = get_hw_format;

    if (hw_decoder_init(videoCodecCtx, type) < 0)
        return -1;

    if ((ret = avcodec_open2(videoCodecCtx, videoCodec, NULL)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%u\n", videoStreamIndex);
        return -1;
    }

    img_ctx = sws_getContext(videoCodecCtx->width,
                             videoCodecCtx->height,
                             AV_PIX_FMT_NV12,
                             videoCodecCtx->width,
                             videoCodecCtx->height,
                             AV_PIX_FMT_RGB32,
                             SWS_BICUBIC,NULL,NULL,NULL);

    numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32,videoCodecCtx->width,videoCodecCtx->height,1);
    out_buffer = (unsigned char *)av_malloc(numBytes*sizeof(uchar));

    int res = av_image_fill_arrays(
                rgbFrame->data,rgbFrame->linesize,
                out_buffer,AV_PIX_FMT_RGB32,
                videoCodecCtx->width,videoCodecCtx->height,1);
    if(res<0){
        qDebug()<<"Fill arrays failed.";
        return -1;
    }

    openFlag=true;
    return true;
}

AVPixelFormat FFmpegVideo::get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts)
{
    Q_UNUSED(ctx)
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hw_pix_fmt)
            return *p;
    }

    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

int FFmpegVideo::hw_decoder_init(AVCodecContext *ctx, const AVHWDeviceType type)
{
    int err = 0;

    if ((err = av_hwdevice_ctx_create(&hw_device_ctx, type,
                                      NULL, NULL, 0)) < 0) {
        fprintf(stderr, "Failed to create specified HW device.\n");
        return err;
    }
    ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

    return err;
}

void FFmpegVideo::stopThread()
{
    stopFlag=true;
}

void FFmpegVideo::run()
{
    int64_t start_time = ::GetTickCount64();

    double PCFreq = 0.0;
    LARGE_INTEGER freq;

    LARGE_INTEGER li;
    if(!QueryPerformanceFrequency(&li)){
        qDebug() <<"QueryPerformanceFrequency failed!";
    }

    PCFreq = double(li.QuadPart)/1000.0;

    QueryPerformanceCounter(&li);
    int64_t CounterStart = li.QuadPart;

    int64_t lipre=CounterStart;
    MMRESULT r = timeBeginPeriod(1);
    for(int i=0;i<10;i++){

        //Sleep(5);
        NSSleep(5);
        QueryPerformanceCounter(&li);
        double deltax = double(li.QuadPart-lipre)/PCFreq;
        lipre=li.QuadPart;
        qDebug()<< "sleep 5ms " <<QDateTime::currentDateTime().toString("hh:mm:ss.zzz")<<deltax;
    }
    timeEndPeriod(1);

    //__asm int 3;

    if(!openFlag){
        open_input_file();
    }

    TIMERR_NOERROR;
    //MMRESULT rx = timeBeginPeriod(1);
    ::Sleep(0);

    while(av_read_frame(fmtCtx,pkt)>=0){
        if(stopFlag) break;
        if(pkt->stream_index == videoStreamIndex){
            if(avcodec_send_packet(videoCodecCtx,pkt)>=0){
                int ret;
                while((ret=avcodec_receive_frame(videoCodecCtx,yuvFrame))>=0){
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        return;
                    else if (ret < 0) {
                        fprintf(stderr, "Error during decoding\n");
                        exit(1);
                    }

                    if(yuvFrame->format==videoCodecCtx->pix_fmt){
                        if((ret = av_hwframe_transfer_data(nv12Frame,yuvFrame,0))<0){
                            continue;
                        }
                    }

                    sws_scale(img_ctx,
                              (const uint8_t* const*)nv12Frame->data,
                              (const int*)nv12Frame->linesize,
                              0,
                              nv12Frame->height,
                              rgbFrame->data,rgbFrame->linesize);

                    AVRational time_base = videoCodecCtx->time_base;
                    AVRational time_base_q = {1,AV_TIME_BASE};

                    int64_t pts_time = av_rescale_q(yuvFrame->pkt_dts,time_base,time_base_q);
                    int64_t now_time = ::GetTickCount64()-start_time;

                    double xy = yuvFrame->pts* av_q2d(time_base);
                    double duration = pkt->duration* av_q2d(time_base);
                    static int64_t pts_time_pre=0;
                    //qDebug("%f  %f",xy, duration);//
                    //qDebug()<< pts_time << pts_time - pts_time_pre<< now_time<< pkt->duration ;

                    pts_time_pre=pts_time;
                    start_time = ::GetTickCount64();
                    //time_base_q = av_get_time_base_q();

                    //struct timeval tv;
                    //gettimeofday(&tv,NULL);
                    //int64_t now_time =  (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;

                    //double x = av_q2d(yuvFrame->time_base);
                    //double y = yuvFrame->pts*x;

                    static int64_t pts = 0 ;
                    int64_t delta=3600;

                    QueryPerformanceCounter(&li);
                    int64_t t1 = li.QuadPart;
                    static int64_t t1_pre=0;

                    int delta1=0;
                    double deltax=0;
                    if(t1_pre){
                        deltax = double(t1-t1_pre)/PCFreq;
                        delta1 = 40 -deltax;
                        //qDebug()<< "1" <<QDateTime::currentDateTime().toString("hh:mm:ss.zzz")<< deltax << delta1;
                        if(delta1>0){
                            //::Sleep(delta1);
                            //qDebug()<< "-" <<QDateTime::currentDateTime().toString("hh:mm:ss.zzz")<< deltax << delta1;
                        }

                    }

                    //MMRESULT r = timeBeginPeriod(1);
                    //::Sleep(10);
                    //timeEndPeriod(1);

                    //static LARGE_INTEGER lt1;
                    //QueryPerformanceCounter(&li);
                    //deltax = double(li.QuadPart-lt1.QuadPart)/PCFreq;
                    //qDebug()<< "2" <<QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << deltax << delta1  ;

                    QueryPerformanceCounter(&li);
                    deltax = double(li.QuadPart-CounterStart)/PCFreq;
                    //qDebug()<< "D" <<QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << deltax << delta1  ;

                    QImage *pimg = new QImage(out_buffer,
                                              videoCodecCtx->width,videoCodecCtx->height,
                                              QImage::Format_RGB32);

                    IMG img;img.img=pimg;img.pts=yuvFrame->pts;
                    Images.push_back(img);

                    /*
                    QImage img(out_buffer,
                               videoCodecCtx->width,videoCodecCtx->height,
                               QImage::Format_RGB32);
                    */
                    //emit sendQImage(img);

                    //QueryPerformanceCounter(&lt1);
                    t1_pre = t1;

                    if(pts){
                        delta = yuvFrame->pts-pts;
                    }
                    pts = yuvFrame->pts;
                    //qDebug()<< "2" << QDateTime::currentDateTime().toString("mm:ss.zzz")<< yuvFrame->pts << delta;
                    QThread::msleep(0);
                    //qDebug()<< "3" << QDateTime::currentDateTime().toString("mm:ss.zzz")<< yuvFrame->pts << delta;
                }
            }
            av_packet_unref(pkt);
        }
    }

    //timeEndPeriod(1);

    qDebug()<<"Thread stop now";
}

void PlayVideo::run()
{
    double PCFreq = 0.0;
    LARGE_INTEGER freq;

    LARGE_INTEGER li;
    if(!QueryPerformanceFrequency(&li)){
        qDebug() <<"QueryPerformanceFrequency failed!";
    }

    PCFreq = double(li.QuadPart)/1000.0;

    QueryPerformanceCounter(&li);
    int64_t CounterStart = li.QuadPart;

    int64_t lipre=CounterStart;

    MMRESULT rx = timeBeginPeriod(1);
    ::Sleep(0);

    int64_t pts_pre=0;
    bool disp=0;
    int sleeptime=37;

    if(rx != TIMERR_NOERROR){
        qDebug() <<"timeBeginPeriod failed!";
        sleeptime=390;
    }

    while(1){
        if(stopFlag) break;

        if(Images.size()>0){
            IMG img = Images.front();
            Images.pop_front();

            if(!disp)
            {
                QueryPerformanceCounter(&li);
                //(li.QuadPart-lipre)/PCFreq;

                QThread::yieldCurrentThread();
                emit sendQImage(img);//

                //qDebug()<< "S" <<QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*")<<img.pts-pts_pre<<(li.QuadPart-lipre)/PCFreq<<Images.size();
                //QThread::yieldCurrentThread();
                //disp=1;
                lipre= li.QuadPart;
            }

            if(Images.size()>6){
                //qDebug()<< "S" <<QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*")<<img.pts-pts_pre<<Images.size();
                sleeptime--;
                if(sleeptime<=25)sleeptime=25;
            }else
            {
                sleeptime++;
                if(sleeptime>=37)sleeptime=37;
            }

            pts_pre=img.pts;
            //delete img.img;
        }
        else{

        }

        //NSSleep(sleeptime);
        //QThread::usleep(sleeptime*1000);
        QThread::msleep(sleeptime);

    }

    rx = timeEndPeriod(1);

    qDebug()<<"PlayVideo Thread end now";
}

FFmpegWidget::FFmpegWidget(QWidget *parent) : QWidget(parent)
{
    ffmpeg = new FFmpegVideo;
    playf  = new PlayVideo;

    //connect(ffmpeg,SIGNAL(snedQImage(IMG)),this,SLOT(receiveQImage(IMG)),Qt::DirectConnection);
    connect(ffmpeg,&FFmpegVideo::finished,ffmpeg,&FFmpegVideo::deleteLater);
    connect(playf, SIGNAL(sendQImage(IMG)),this,SLOT(receiveQImage(IMG)),Qt::DirectConnection);
    //connect(playf, &PlayVideo::finished,playf, &PlayVideo::deleteLater);
}

FFmpegWidget::~FFmpegWidget()
{
    qDebug()<<"exit player";
    if(ffmpeg->isRunning()){
        stop();
    }
}

void FFmpegWidget::play(QString url)
{
    playf->start();

    ffmpeg->setPath(url);
    ffmpeg->start();

}

void FFmpegWidget::stop()
{
    if(ffmpeg->isRunning()){
        ffmpeg->requestInterruption();
        ffmpeg->quit();
        ffmpeg->wait(100);
    }
    ffmpeg->ffmpeg_free_variables();
}

void FFmpegWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.drawImage(0,0,img);
}

void FFmpegWidget::receiveQImage(const IMG &rImg)
{
    QDateTime startDT = QDateTime::currentDateTime();
    static QDateTime preDT = QDateTime::currentDateTime();

    int delta = (startDT.time().msec()-preDT.time().msec());
    if(delta<0) delta+=1000;

    //delta-=40;

    static int64_t pts_pre=0;
    //QThread::yieldCurrentThread();

    //img = rImg.img->scaled(nw,nh);//KeepAspectRatioByExpanding  KeepAspectRatio SmoothTransformation FastTransformation

    img = rImg.img->scaled(this->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation);
    qDebug()<< "R" <<startDT.toString("-hh:mm:ss.zzz-")<< QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*") << delta << rImg.pts-pts_pre<<rImg.pts;
    pts_pre=rImg.pts; preDT = startDT;
    delete rImg.img;

    update();
    QThread::yieldCurrentThread();
}
