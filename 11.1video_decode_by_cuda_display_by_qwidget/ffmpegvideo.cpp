#include "ffmpegvideo.h"

#include <QDateTime>

#include <Windows.h>
#include <SDL.h>

list<IMG> Images;



typedef struct DecodeContext{
    AVBufferRef *hw_device_ref;
}DecodeContext;

DecodeContext decode = {NULL};

static enum AVPixelFormat hw_pix_fmt;
static AVBufferRef* hw_device_ctx=NULL;

FFmpegVideo::FFmpegVideo()
{
    int i=0;
}

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

    /*
     * cuda dxva2 qsv d3d11va
     */
    type = av_hwdevice_find_type_by_name("cuda");

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

    //获取支持该decoder的hw配置型
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

    AVPixelFormat src = videoCodecCtx->pix_fmt;AV_PIX_FMT_NV12;
    img_ctx = sws_getContext(videoCodecCtx->width,
                             videoCodecCtx->height,
                             AV_PIX_FMT_NV21,//AV_PIX_FMT_NV12//videoCodecCtx->pix_fmt, //AV_PIX_FMT_NV12 ,//  AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P
                             videoCodecCtx->width,
                             videoCodecCtx->height,
                             AV_PIX_FMT_NV21,//AV_PIX_FMT_RGB32, AV_PIX_FMT_NV12
                             SWS_BICUBIC,//SWS_BICUBIC, SWS_BILINEAR
                             NULL,NULL,NULL);

    numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,videoCodecCtx->width,videoCodecCtx->height,1);
    out_buffer = (unsigned char *)av_malloc(numBytes*sizeof(uchar));

    int res = av_image_fill_arrays(
                rgbFrame->data,rgbFrame->linesize,
                out_buffer,AV_PIX_FMT_YUV420P,
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

SDL_Window  *   win;
SDL_Surface *   _pScreens ;
SDL_Surface *   _pload ;
SDL_Renderer*   renderer;
SDL_Texture *   texture;

void FFmpegVideo::run()
{
    if(!openFlag){
        open_input_file();
    }

    //创建输出窗口
    win = SDL_CreateWindow("SDL Video Player",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        1920/2+5, 1080/2+5,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if (!win) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create window!");
        //goto __FAIL;
    }

    //SDL渲染器
    renderer = SDL_CreateRenderer(win, -1, 0);
    if (!renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create renderer!");
        //goto __FAIL;
    }

    //创建显示帧
    Uint32 pixformat = SDL_PIXELFORMAT_IYUV;
    texture = SDL_CreateTexture(renderer,
        pixformat,
        SDL_TEXTUREACCESS_STREAMING,
        1920,
        1080);

    if (!texture)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create Texture!");
        //goto __FAIL;
    }


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

                    if(0){
                        SDL_UpdateYUVTexture(texture, NULL, nv12Frame->data[0],
                                nv12Frame->linesize[0], nv12Frame->data[1], nv12Frame->linesize[1],
                                nv12Frame->data[2], nv12Frame->linesize[2]);
                        //SDL_RenderClear(pSDLRenderer);
                        //SDL_RenderCopy(pSDLRenderer, texture, NULL, NULL);
                        //SDL_RenderPresent(pSDLRenderer);
                    }

                    //图像数据格式
                    uint8_t* dst_data[4] = { 0 };
                    int dst_linesize[4] = { 0 };

                    sws_scale(img_ctx,
                              (const uint8_t* const*)nv12Frame->data,
                              (const int*)nv12Frame->linesize,
                              0,
                              nv12Frame->height,
                              rgbFrame->data,
                              rgbFrame->linesize);

                    if(1-1){
                        //渲染图像
                        SDL_UpdateYUVTexture(texture, NULL,
                            rgbFrame->data[0], rgbFrame->linesize[0],
                            rgbFrame->data[1], rgbFrame->linesize[1],
                            rgbFrame->data[2], rgbFrame->linesize[2]);

                        SDL_Rect rect;
                        rect.x = 0;
                        rect.y = 0;
                        rect.w = 1920/2;
                        rect.h = 1080/2;

                        SDL_RenderClear(renderer);
                        SDL_RenderCopy(renderer, texture, NULL, &rect);
                        SDL_RenderPresent(renderer);
                        SDL_Delay(1000 / 30);
                        //av_packet_unref(&packet);
                        //SDL_PollEvent(&event);
                    }
                    //QImage img(out_buffer,
                    //           videoCodecCtx->width,videoCodecCtx->height,
                    //           QImage::Format_RGB32);
                    //emit sendQImage(img);

                    if(0){
                        SDL_Surface* pSDLSurface = SDL_CreateRGBSurfaceFrom(out_buffer,
                                                               nv12Frame->width,
                                                               nv12Frame->height,
                                                               4 * 8,
                                                               nv12Frame->width * 4,
                                                               0x000000FF,
                                                               0x0000FF00,
                                                               0x00FF0000,
                                                               0xFF000000
                                                               );
                        //SDL_Texture* pSDLTexture = SDL_CreateTextureFromSurface(pSDLRenderer, pSDLSurface);

                        SDL_FreeSurface(pSDLSurface);
        //                pSDLSurface = SDL_LoadBMP("testBMP/1.bmp");
        //                pSDLTexture = SDL_CreateTextureFromSurface(pSDLRenderer, pSDLSurface);

                        // 清除Renderer
                        //SDL_RenderClear(pSDLRenderer);
                        // Texture复制到Renderer
                        //SDL_RenderCopy(pSDLRenderer, pSDLTexture, 0, 0);
                        // 更新Renderer显示
                        //SDL_RenderPresent(pSDLRenderer);
                    }

                    if(1){
                        QImage *pimg = new QImage(out_buffer,
                                              videoCodecCtx->width,videoCodecCtx->height,
                                              QImage::Format_RGB32);

                        IMG img;img.img=pimg;img.pts=yuvFrame->pts;
                        Images.push_back(img);
                    }

                    QThread::msleep(0);
                }
            }
            av_packet_unref(pkt);
        }
    }
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
        //QThread::msleep(sleeptime);
        //av_usleep(sleeptime*1000);
        ::Sleep(sleeptime);

    }

    rx = timeEndPeriod(1);

    qDebug()<<"PlayVideo Thread end now";
}


FFmpegWidget::FFmpegWidget(QWidget *parent) : QWidget(parent)
{
    ffmpeg = new FFmpegVideo;
    connect(ffmpeg,SIGNAL(sendQImage(QImage)),this,SLOT(receiveQImage(QImage)),Qt::DirectConnection);
    connect(ffmpeg,&FFmpegVideo::finished,ffmpeg,&FFmpegVideo::deleteLater);

    playf  = new PlayVideo;

    //connect(ffmpeg,SIGNAL(snedQImage(IMG)),this,SLOT(receiveQImage(IMG)),Qt::DirectConnection);
    connect(playf, SIGNAL(sendQImage(IMG)),this,SLOT(receiveQImage(IMG)),Qt::DirectConnection);

    if(1)
    {
        //_window = SDL_CreateWindow("SDL",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
        //                          800, 600, SDL_WINDOW_OPENGL);//From((void*)ui->widget->window()->winId());// ->windowHandle());
        //_pScreens = SDL_GetWindowSurface(_window);

        //pSDLRenderer = SDL_CreateRenderer(_window, -1, 0);

        //_pload = SDL_LoadBMP("C:\\Dev\\vlcsnap.bmp");

         //SDL_BlitSurface(_pload,NULL,_pScreens,NULL);
        // SDL_UpdateWindowSurface(_window);
        return;
    }

    SDL_Window * window;  //->winId()
    window = SDL_CreateWindowFrom( (void*)this->winId() );//  (void*)ui->widget->window()->winId());// ->windowHandle());
    SDL_Surface * _pScreens = SDL_GetWindowSurface(window);

    SDL_LoadBMP("C:\\Dev\\vlcsnap.bmp");
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

    if(0) {
        SDL_Window * window;  //->winId()
        window = SDL_CreateWindowFrom( (void*)this->windowHandle() );//  (void*)ui->widget->window()->winId());// ->windowHandle());
        SDL_Surface * _pScreens = SDL_GetWindowSurface(window);

        SDL_LoadBMP("C:\\Dev\\vlcsnap.bmp");

        return ;
    }

    ffmpeg->setPath(url);
    ffmpeg->start();
    playf->start();
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
    //painter.drawImage(0,0,img);
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

    //img = rImg.img->scaled(this->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation);
    qDebug()<< "R" <<startDT.toString("-hh:mm:ss.zzz-")<< QDateTime::currentDateTime().toString("*hh:mm:ss.zzz*") << delta << rImg.pts-pts_pre<<rImg.pts;
    pts_pre=rImg.pts; preDT = startDT;
    delete rImg.img;

    update();
    QThread::yieldCurrentThread();
}
