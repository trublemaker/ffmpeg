#ifndef __rasp_pi2__

#include "ffmpegdecoder.h"
#include <windows.h>

static AVPixelFormat hw_pix_fmt;
static AVBufferRef* hw_device_ctx=NULL;

FFmpegDecoder::FFmpegDecoder()
{
    fmtCtx = avformat_alloc_context();
    pkt = av_packet_alloc();
    yuvFrame = av_frame_alloc();
    rgbFrame = av_frame_alloc();
    nv21Frame= av_frame_alloc();
}

FFmpegDecoder::~FFmpegDecoder()
{
    if(!pkt) av_packet_free(&pkt);
    if(!yuvFrame) av_frame_free(&yuvFrame);
    if(!rgbFrame) av_frame_free(&rgbFrame);
    if(!videoCodecCtx) avcodec_free_context(&videoCodecCtx);
    if(!videoCodecCtx) avcodec_close(videoCodecCtx);
    if(!fmtCtx) avformat_close_input(&fmtCtx);
}

void FFmpegDecoder::setUrl(const QString url)
{
    _url= url;
}

int FFmpegDecoder::width()
{
    return w;
}

int FFmpegDecoder::height()
{
    return h;
}

AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts)
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
int hw_decoder_init(AVCodecContext *ctx, const AVHWDeviceType type)
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

void FFmpegDecoder::run()
{
    AVHWDeviceType type;

    /* cuda dxva2 qsv d3d11va */
    type = av_hwdevice_find_type_by_name("cuda");//cuda
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", "h264_cuvid");
        fprintf(stderr, "Available device types:");
        while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return ;
    }
    if(avformat_open_input(&fmtCtx,_url.toLocal8Bit().data(),NULL,NULL)<0){
        qDebug("Cannot open input file.\n");
        return;
    }

    if(avformat_find_stream_info(fmtCtx,NULL)<0){
        qDebug("Cannot find any stream in file.\n");
        return;
    }

    /* find the video stream information */
    int ret = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot find a video stream in the input file\n");
        return;
    }
    videoStreamIndex = ret;

    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(videoCodec, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n",
                    videoCodec->name, av_hwdevice_get_type_name(type));
            return ;
        }
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }

    int streamCnt=fmtCtx->nb_streams;
    for(int i=0;i<streamCnt;i++){
        if(fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            videoStreamIndex = i;
            continue;
        }
    }

    if(videoStreamIndex==-1){
        qDebug("Cannot find video stream in file %s.\n",_url.toLocal8Bit().data());
        return;
    }

    AVCodecParameters *videoCodecPara = fmtCtx->streams[videoStreamIndex]->codecpar;

    if(!(videoCodec = avcodec_find_decoder(videoCodecPara->codec_id))){
        qDebug("Cannot find valid decode codec.\n");
        return;
    }

    if(!(videoCodecCtx = avcodec_alloc_context3(videoCodec))){
        qDebug("Cannot find valid decode codec context.\n");
        return;
    }

    if(avcodec_parameters_to_context(videoCodecCtx,videoCodecPara)<0){
        qDebug("Cannot initialize parameters.\n");
        return;
    }

    videoCodecCtx->get_format = get_hw_format;

    if (hw_decoder_init(videoCodecCtx, type) < 0)
        return ;


    if(avcodec_open2(videoCodecCtx,videoCodec,NULL)<0){
        qDebug("Cannot open codec.\n");
        return;
    }

    w = videoCodecCtx->width;
    h = videoCodecCtx->height;

    numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,w,h,1);
    out_buffer = (unsigned char *)av_malloc(numBytes*sizeof(uchar));

    img_ctx = sws_getContext(videoCodecCtx->width,
                             videoCodecCtx->height,
                             AV_PIX_FMT_NV12, //AV_PIX_FMT_YUV420P AV_PIX_FMT_NV12
                             videoCodecCtx->width,
                             videoCodecCtx->height,
                             AV_PIX_FMT_YUV420P, //
                             SWS_BILINEAR,NULL,NULL,NULL); //SWS_FAST_BILINEAR

    int res = av_image_fill_arrays(
                rgbFrame->data,rgbFrame->linesize,
                out_buffer,AV_PIX_FMT_YUV420P,
                videoCodecCtx->width,videoCodecCtx->height,1);

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

    while(av_read_frame(fmtCtx,pkt)>=0){
        if(pkt->stream_index == videoStreamIndex){
            if(avcodec_send_packet(videoCodecCtx,pkt)>=0){
                int ret;
                while((ret=avcodec_receive_frame(videoCodecCtx,yuvFrame))>=0){
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        return;
                    else if (ret < 0) {
                        fprintf(stderr, "Error during decoding\n");
                        continue;
                    }

                    if(yuvFrame->format==videoCodecCtx->pix_fmt){

#if 0
                            //*test av_hwframe_map and av_hwframe_transfer_data
                            LARGE_INTEGER li1,li2;
                            QueryPerformanceCounter(&li);
                            av_hwframe_transfer_data(nv21Frame,yuvFrame,0);
                            QueryPerformanceCounter(&li1);
                            av_hwframe_map(nv21Frame,yuvFrame,0);
                            QueryPerformanceCounter(&li2);
                            int64_t ticks1 = li1.QuadPart-li.QuadPart
                                    ,ticks2=li2.QuadPart-li1.QuadPart;
                            qDebug()<< ticks1 << ticks2 << (ticks1)/PCFreq << (ticks2)/PCFreq;
                            //146951 21 10.2632 0.00146667 //4k
#endif
                        //*
                        if( av_hwframe_map(nv21Frame,yuvFrame,0) == 0 ){

                        }
                        else if((ret = av_hwframe_transfer_data(nv21Frame,yuvFrame,0))<0){
                            continue;
                        }//*/
                    }

                    int f1 = yuvFrame->format;
                    int f2 = rgbFrame->format;
                    int f3 = AV_PIX_FMT_YUV420P;
                    int f4 = AV_PIX_FMT_NV12;

                    //default YUV420P
                    //rgb = AV_PIX_FMT_NV12 = 23

                    if(isFirst){
                        isFirst=false;
                        emit sigFirst(out_buffer,w,h);
                    }

                    //uint8_t* dst_data[4] = { 0 };
                    //int dst_linesize[4] = { 0 };

                    //numBytes = sizeof(AVFrame);

                    //numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32,videoCodecCtx->width,videoCodecCtx->height,1);
                    //numBytes = av_image_get_buffer_size(AV_PIX_FMT_NV12,videoCodecCtx->width,videoCodecCtx->height,1);
                    //numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,videoCodecCtx->width,videoCodecCtx->height,1);

                    //out_buffer = (unsigned char *)av_malloc(numBytes*sizeof(uchar));
                    //memset(out_buffer,0,numBytes);
                    //dst_data[0]=out_buffer;
                    //dst_data[1]=out_buffer+numBytes/3;
                    //dst_data[2]=out_buffer+numBytes*2/3;
                    //dst_data[3]=out_buffer;

                    int r = sws_scale(img_ctx,
                              (const uint8_t* const*)nv21Frame->data,
                              (const int*)nv21Frame->linesize,
                              0,
                              nv21Frame->height,
                              rgbFrame->data,//(uint8_t* [])out_buffer
                              rgbFrame->linesize);

                    if(r){ //AVERROR
                        //char buf[256]={0};
                        //av_strerror(r,buf,sizeof(buf)-1);
                    }

                    int bytes =0;
                    for(int i=0;i<h;i++){
                        memcpy(out_buffer+bytes,rgbFrame->data[0]+rgbFrame->linesize[0]*i,w);
                        bytes+=w;
                    }

                    int u=h>>1;
                    for(int i=0;i<u;i++){
                        memcpy(out_buffer+bytes,rgbFrame->data[1]+rgbFrame->linesize[1]*i,w/2);
                        bytes+=w/2;
                    }

                    for(int i=0;i<u;i++){
                        memcpy(out_buffer+bytes,rgbFrame->data[2]+rgbFrame->linesize[2]*i,w/2);
                        bytes+=w/2;
                    }

                    emit newFrame();

                    QThread::msleep(0);
                }
            }
            av_packet_unref(pkt);
        }
    }
}
#endif
