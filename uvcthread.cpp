#include "uvcthread.h"
#include <QDebug>
#include <QSemaphore>

#include <QDateTime>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>
#include <linux/videodev2.h>

#define CAPTURE_MAX_BUFFER 5
#define Color_Mode 1
#define Edge_Detection_Mode 2    // 0:拉普拉斯     1:Robert     2:Sobel     3:Prewittuint
const uint DataWidth = 320;
const uint DataHeight = 240;
char *CAPTURE_DEVICE = "/dev/video0";

struct buf_info{
    int index;
    unsigned int length;
    void *start;
};
struct video_dev      
{
    int fd;
    int cap_width, cap_height;
    struct buf_info buff_info[CAPTURE_MAX_BUFFER];
    int numbuffer;
} videodev;
volatile short RGB_Data[320*240*3];
volatile uchar *UVC_P;

int initCapture(void);
int startCapture(void);
int captureFrame(void);
int stopCapture(void);
void closeCapture(void);
void yuyv2rgb(const uchar *yuv);

volatile bool Get_Ready = false;      // true:获取数据信号
volatile bool Get_Over = false;       // true:Flir获取数据完成
volatile bool UVC_Init_Over = false;  // true:UVC初始化完成
volatile bool Show_Over = true;       // true:一帧融合图像显示完成

QSemaphore Init_Over_sem(1);
QSemaphore Get_Ready_sem(1);
QSemaphore Get_Over_sem(1);
QSemaphore Show_Over_sem(1);

int  subInitCapture(void);
void vidioc_enuminput(int fd);

UVCThread::UVCThread()
{

}

void UVCThread::run()
{
    bool temp = false;
    while(1)
    {
        if(initCapture()  < 0) return;
        if(startCapture() < 0) return;
        Show_Over_sem.acquire();
        Show_Over = true;       // true:一帧融合图像显示完成
        Show_Over_sem.release();
        Init_Over_sem.acquire();
        UVC_Init_Over = true;
        Init_Over_sem.release();
        while (1)
        {
            // 等待上一帧融合图像显示完成
            do{
                Show_Over_sem.acquire();
                temp = Show_Over;
                Show_Over_sem.release();
            } while(!temp);               // 等待Flir获取数据完成

            // 开始下一次获取
            Show_Over_sem.acquire();
            Show_Over = false;        // true:一帧融合图像显示完成
            Show_Over_sem.release();

            Get_Over_sem.acquire();
            Get_Over = false;         // true:Flir获取数据完成
            Get_Over_sem.release();

            Get_Ready_sem.acquire();
            Get_Ready = true;         // true:获取数据信号
            Get_Ready_sem.release();
            //msleep(10);   // slow
            if(captureFrame() < 0)    // 获取UVC数据出错
            {
                qDebug() << "Update Data Error!";
                stopCapture();
                closeCapture();
                break;
            }
            else
            {
                do{
                    Get_Over_sem.acquire();
                    temp = Get_Over;
                      Get_Over_sem.release();
                } while(!temp);               // 等待Flir获取数据完成
                //qDebug() << "Get_Over";

                Get_Ready_sem.acquire();
                Get_Ready = false;
                Get_Ready_sem.release();

                ShowUVCSignal_Send();         // 不断地发送更新摄像头数据信号，and不断地刷新RGB图像
            }
            msleep(60);   //99
        }
        msleep(100);
    }
}

int subInitCapture(void)
{
    int err, fd = videodev.fd;

    struct v4l2_dbg_chip_info chip;
    if ((err = ioctl(fd, VIDIOC_DBG_G_CHIP_INFO, &chip)) < 0)
        qWarning() << "VIDIOC_DBG_G_CHIP_INFO error " << errno;
    else
        qDebug() << "chip info " << chip.name;

    bool support_fmt;
    struct v4l2_fmtdesc ffmt;
    memset(&ffmt, 0, sizeof(ffmt));
    ffmt.index = 0;
    ffmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    support_fmt = false;
    while ((err = ioctl(fd, VIDIOC_ENUM_FMT, &ffmt)) == 0) {
        qDebug() << "fmt" << ffmt.pixelformat << (char *)ffmt.description;
        if (ffmt.pixelformat == V4L2_PIX_FMT_YUYV)
            support_fmt = true;
        ffmt.index++;
    }
    if (!support_fmt) {
        qWarning() << "V4L2_PIX_FMT_YUYV is not supported by this camera";
        return -1;
    }

    bool support_320x240;
    struct v4l2_frmsizeenum fsize;
    memset(&fsize, 0, sizeof(fsize));
    fsize.index = 0;
    fsize.pixel_format = V4L2_PIX_FMT_YUYV;
    support_320x240 = false;
    while ((err = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fsize)) == 0) {
        qDebug() << "frame size " << fsize.discrete.width << fsize.discrete.height;
        if (fsize.discrete.width == 320 && fsize.discrete.height == 240)
            support_320x240 = true;
        fsize.index++;
    }
    if (!support_320x240) {
        qWarning() << "frame size 320x240 is not supported by this camera";
        return -1;
    }

    vidioc_enuminput(fd);

    int index;
    if ((err = ioctl(fd, VIDIOC_G_INPUT, &index)) < 0)
        qWarning() << "VIDIOC_G_INPUT fail" << errno;
    else
        qDebug() << "current input index =" << index;

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if ((err = ioctl(fd, VIDIOC_G_FMT, &fmt)) < 0)
        //qWarning() <int startCapture(void)< "VIDIOC_G_FMT fail" << errno;
        qDebug() << "VIDIOC_G_FMT fail" << errno;
    else
        qDebug() << "fmt width =" << fmt.fmt.pix.width
                 << " height =" << fmt.fmt.pix.height
                 << " pfmt =" << fmt.fmt.pix.pixelformat;

    fmt.fmt.pix.width = 320;
    fmt.fmt.pix.height = 240;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;     // yuyv 4:2:2   每两个Y共用一组UV分量
    if ((err = ioctl(fd, VIDIOC_S_FMT, &fmt)) < 0)
        qWarning() << "VIDIOC_S_FMT fail" << errno;
    else
        qDebug() << "VIDIOC_S_FMT success";

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if ((err = ioctl(fd, VIDIOC_G_FMT, &fmt)) < 0)
        qWarning() << "VIDIOC_G_FMT fail" << errno;
    else
        qDebug() << "fmt width =" << fmt.fmt.pix.width
                 << " height =" << fmt.fmt.pix.height
                 << " pfmt =" << fmt.fmt.pix.pixelformat;
    Q_ASSERT(fmt.fmt.pix.width == 320);
    Q_ASSERT(fmt.fmt.pix.height == 240);
    Q_ASSERT(fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV);

    videodev.cap_width = fmt.fmt.pix.width;
    videodev.cap_height = fmt.fmt.pix.height;

    return 0;
}

void vidioc_enuminput(int fd)
{
    int err;
    struct v4l2_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;
    //qDebug() << "enuminput_fd =" << fd;
    while ((err = ioctl(fd, VIDIOC_ENUMINPUT, &input)) == 0)
    {
        qDebug() << "input name =" << (char *)input.name
                 << " type =" << input.type
                 << " status =" << input.status
                 << " std =" << input.std;
        input.index++;
    }
}

int initCapture(void)
{
    int err;
    int fd = open(CAPTURE_DEVICE, O_RDWR);
    if (fd < 0)
    {
        qWarning() << "open /dev/video0 fail " << fd;
        return fd;
    }
    videodev.fd = fd;
    //qDebug() << "videodev.fd =" << videodev.fd;

    struct v4l2_capability cap;
    if ((err = ioctl(fd, VIDIOC_QUERYCAP, &cap)) < 0) {
        qWarning() << "VIDIOC_QUERYCAP error " << err;
        goto err1;
    }
    qDebug() << "card =" << (char *)cap.card
             << " driver =" << (char *)cap.driver
             << " bus =" << (char *)cap.bus_info;

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
        qDebug() << "/dev/video0: Capable off capture";
    else
    {
        qWarning() << "/dev/video0: Not capable of capture";
        goto err1;
    }

    if (cap.capabilities & V4L2_CAP_STREAMING)
        qDebug() << "/dev/video0: Capable of streaming";
    else {
        qWarning() << "/dev/video0: Not capable of streaming";
        goto err1;
    }
    if ((err = subInitCapture()) < 0)
    {
        qDebug() << "subInitCapture error!";
        goto err1;
    }
    struct v4l2_requestbuffers reqbuf;
    reqbuf.count = 5;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if ((err = ioctl(fd, VIDIOC_REQBUFS, &reqbuf)) < 0) {
        qWarning() << "Cannot allocate memory";
        goto err1;
    }
    videodev.numbuffer = reqbuf.count;
    qDebug() << "buffer actually allocated" << reqbuf.count;

    uint i;
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    for (i = 0; i < reqbuf.count; i++) {
        buf.type = reqbuf.type;
        buf.index = i;
        buf.memory = reqbuf.memory;
        err = ioctl(fd, VIDIOC_QUERYBUF, &buf);
        Q_ASSERT(err == 0);

        videodev.buff_info[i].length = buf.length;
        videodev.buff_info[i].index = i;
        videodev.buff_info[i].start =
                (uchar *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

        Q_ASSERT(videodev.buff_info[i].start != MAP_FAILED);

        memset((void *) videodev.buff_info[i].start, 0x80,
               videodev.buff_info[i].length);

        err = ioctl(fd, VIDIOC_QBUF, &buf);
        Q_ASSERT(err == 0);
    }

    return 0;

err1:
    close(fd);
    return err;
}

int startCapture(void)
{
    int a, ret;

    /* Start Streaming. on capture device */
    a = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(videodev.fd, VIDIOC_STREAMON, &a);
    if (ret < 0) {
        qDebug() << "capture VIDIOC_STREAMON error fd=" << videodev.fd;
        return ret;
    }
    qDebug() << "Stream on...";

    return 0;
}

int captureFrame(void)
{
    int ret;
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_USERPTR;

    /* Dequeue capture buffer */
    ret = ioctl(videodev.fd, VIDIOC_DQBUF, &buf);
    if (ret < 0)
    {
        qDebug() << "Cap VIDIOC_DQBUF";
        return ret;
    }
//    QDateTime time = QDateTime::currentDateTime(); // 获取系统 年月日时分秒
//    QString New_Timer = time.toString("mmss");
//    qDebug() << "U:"<< New_Timer;
    //qDebug() << "videodev.cap_width =" << videodev.cap_width << "videodev.cap_height =" << videodev.cap_height;
    ret = ioctl(videodev.fd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
        qDebug() << "Cap VIDIOC_QBUF";
        return ret;
    }
    UVC_P = (uchar*)videodev.buff_info[buf.index].start;
    //yuyv2rgb((const uchar *)videodev.buff_info[buf.index].start);
    return 0;
}

int stopCapture(void)
{
    int a, ret;

    qDebug() << "Stream off!!\n";

    a = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(videodev.fd, VIDIOC_STREAMOFF, &a);
    if (ret < 0) {
        qDebug() << "VIDIOC_STREAMOFF";
        return ret;
    }

    return 0;
}

void closeCapture(void)
{
    int i;
    struct buf_info *buff_info;

    /* Un-map the buffers */
    for (i = 0; i < CAPTURE_MAX_BUFFER; i++){
        buff_info = &videodev.buff_info[i];
        if (buff_info->start) {
            munmap(buff_info->start, buff_info->length);
            buff_info->start = NULL;
        }
    }

    if (videodev.fd >= 0) {
        close(videodev.fd);
        videodev.fd = -1;
    }
}

void yuyv2rgb(const uchar *yuv)   // 每次取yuyv4个字节，也就是两个像素点，转换rgb，6个字节，还是两个像素点    yw*yh = 320*240
{
    volatile short *rgb = RGB_Data;
    uint i = 0;
    int temp1 = 0;

    // YUYV to RGB
    for (int y = 0; y < DataHeight; ++y)
    {
        for (int x = 0; x < DataWidth/2; ++x)
        {
            uint temp = i * 6;
            i++;
            temp1 = (DataWidth*2*y+x*4);    // 像素点起始坐标（0，0）YUV 4:2:2采样，每两个Y共用一组UV分量
            uchar y1 = yuv[temp1];               // “Y”表示明亮度,也就是灰度值
            uchar y2 = yuv[temp1 + 2];

#ifdef Color_Mode
            uchar v  = yuv[temp1 + 3];
            uchar u  = yuv[temp1 + 1];           // “U”和“V”表示的则是色度，作用是描述影像色彩及饱和度，用于指定像素的颜色
            float tempv = 1.042 * (v-128);
            float tempu = 1.772 * (u-128);
            float tempuv= 0.34414 * (u-128) + 0.71414 * (v-128);
            rgb[temp + 0] = y1 + tempv;
            rgb[temp + 1] = y1 - tempuv;
            rgb[temp + 2] = y1 + tempu;
            rgb[temp + 3] = y2 + tempv;
            rgb[temp + 4] = y2 - tempuv;
            rgb[temp + 5] = y2 + tempu;
#else
            // grayscale
            rgb[temp + 0] = y1;
            rgb[temp + 1] = y1;
            rgb[temp + 2] = y1;
            rgb[temp + 3] = y2;
            rgb[temp + 4] = y2;
            rgb[temp + 5] = y2;
#endif
//            if(rgb[temp + 0] < 0)   rgb[temp + 0] = 0;
//            if(rgb[temp + 1] < 0)   rgb[temp + 1] = 0;
//            if(rgb[temp + 2] < 0)   rgb[temp + 2] = 0;
//            if(rgb[temp + 3] < 0)   rgb[temp + 3] = 0;
//            if(rgb[temp + 4] < 0)   rgb[temp + 4] = 0;
//            if(rgb[temp + 5] < 0)   rgb[temp + 5] = 0;
//            if(rgb[temp + 0] > 255) rgb[temp + 0] = 255;
//            if(rgb[temp + 1] > 255) rgb[temp + 1] = 255;
//            if(rgb[temp + 2] > 255) rgb[temp + 2] = 255;
//            if(rgb[temp + 3] > 255) rgb[temp + 3] = 255;
//            if(rgb[temp + 4] > 255) rgb[temp + 4] = 255;
//            if(rgb[temp + 5] > 255) rgb[temp + 5] = 255;
        }
    }

    short Temp_Data[DataWidth*DataHeight*3] = {0};
//    // 3X3 高斯平滑滤波
//    for(int m = 0; m < 240; m++)
//    {
//        for(int n = 0; n < 320; n++)
//        {
//            uint Index = (m*320+n)*3;
//            if(m == 0 | n == 0 | m == 239 | n == 319)
//            {
//                short temp1 = rgb[Index];
//                Temp_Data[Index]   = temp1;
//                Temp_Data[Index+1] = temp1;
//                Temp_Data[Index+2] = temp1;
//            }
//            else
//            {
//                short temp =((rgb[((m-1)*320+(n-0))*3] + rgb[((m+1)*320+(n-0))*3] + rgb[((m-0)*320+(n-1))*3] + rgb[((m-0)*320+(n+1))*3]) * 2 \
//                           + (rgb[((m-1)*320+(n-1))*3] + rgb[((m-1)*320+(n+1))*3] + rgb[((m+1)*320+(n-1))*3] + rgb[((m+1)*320+(n+1))*3]) \
//                           + 4*rgb[Index]) / 16;
//                Temp_Data[Index]   = temp;
//                Temp_Data[Index+1] = temp;
//                Temp_Data[Index+2] = temp;
//            }
//        }
//    }
//    for(int m = 0; m < 240; m++)
//    {
//        for(int n = 0; n < 320; n++)
//        {
//            uint Index = (m*320+n)*3;
//            short temp = Temp_Data[Index];
//            rgb[Index]   = temp;
//            rgb[Index+1] = temp;
//            rgb[Index+2] = temp;
//         }
//    }

    // 边缘检测
#if Edge_Detection_Mode == 0
    // 拉普拉斯边缘检测
    for(int m = 0; m < DataHeight; m++)
    {
        for(int n = 0; n < DataWidth; n++)
        {
            uint Index = (m*DataWidth+n)*3;
            if(m == 0 || n == 0 || m == (DataWidth-1) || n == (DataHeight-1))
            {
                short temp1 = rgb[Index];
                Temp_Data[Index]   = temp1;
                Temp_Data[Index+1] = temp1;
                Temp_Data[Index+2] = temp1;
            }
            else
            {
                short temp = rgb[((m-1)*DataWidth+(n-0))*3] + rgb[((m+1)*DataWidth+(n-0))*3] + rgb[((m-0)*DataWidth+(n-1))*3] + rgb[((m-0)*DataWidth+(n+1))*3] \
                           + rgb[((m-1)*DataWidth+(n-1))*3] + rgb[((m-1)*DataWidth+(n+1))*3] + rgb[((m+1)*DataWidth+(n-1))*3] + rgb[((m+1)*DataWidth+(n+1))*3] \
                           - 8*rgb[Index];
                if(temp < 0) temp = 0-temp;
                Temp_Data[Index]   = temp;
                Temp_Data[Index+1] = temp;
                Temp_Data[Index+2] = temp;
            }
        }
    }
#elif Edge_Detection_Mode == 1
    // Robert边缘检测   Prewitt算子对边缘的定位不如Roberts算子
    for(int m = 0; m < DataHeight; m++)
    {
        for(int n = 0; n < DataWidth; n++)
        {
            uint Index = (m*DataWidth+n)*3;
            if(m == (DataHeight-1) || n == (DataWidth-1))
            {
                short temp1 = rgb[Index];
                Temp_Data[Index]   = temp1;
                Temp_Data[Index+1] = temp1;
                Temp_Data[Index+2] = temp1;
            }
            else
            {
                short temp = rgb[Index] - rgb[((m+1)*DataWidth+(n+1))*3] + rgb[((m+1)*DataWidth+(n-0))*3] - rgb[((m-0)*DataWidth+(n+1))*3];
                if(temp < 0) temp = 0-temp;
                Temp_Data[Index]   = temp;
                Temp_Data[Index+1] = temp;
                Temp_Data[Index+2] = temp;
            }
        }
    }
#elif Edge_Detection_Mode == 2
    // Sobel边缘检测
    for(int m = 0; m < DataHeight; m++)
    {
        for(int n = 0; n < DataWidth; n++)
        {
            uint Index = (m*DataWidth+n)*3;
            if(m == 0 || n == 0 || m == (DataHeight-1) || n == (DataWidth-1))
            {
                short temp1 = rgb[Index];
                Temp_Data[Index]   = temp1;
                Temp_Data[Index+1] = temp1;
                Temp_Data[Index+2] = temp1;
            }
            else
            {
                short temp = (rgb[((m-1)*DataWidth+(n+1))*3] + rgb[((m+1)*DataWidth+(n+1))*3] + 2*rgb[((m-0)*DataWidth+(n+1))*3]) \
                           - (rgb[((m-1)*DataWidth+(n-1))*3] + rgb[((m+1)*DataWidth+(n-1))*3] + 2*rgb[((m-0)*DataWidth+(n-1))*3]);
                short temp2= (rgb[((m-1)*DataWidth+(n-1))*3] + rgb[((m-1)*DataWidth+(n+1))*3] + 2*rgb[((m-1)*DataWidth+(n+0))*3]) \
                           - (rgb[((m+1)*DataWidth+(n-1))*3] + rgb[((m+1)*DataWidth+(n+1))*3] + 2*rgb[((m+1)*DataWidth+(n-0))*3]);
                temp = abs(temp) + abs(temp2);
                Temp_Data[Index]   = temp;
                Temp_Data[Index+1] = temp;
                Temp_Data[Index+2] = temp;
            }
        }
    }
#elif Edge_Detection_Mode == 3
    // Prewitt边缘检测
    for(int m = 0; m < DataHeight; m++)
    {
        for(int n = 0; n < DataWidth; n++)
        {
            uint Index = (m*DataWidth+n)*3;
            if(m == 0 || n == 0 || m == (DataHeight-1) || n == (DataWidth-1))
            {
                short temp1 = rgb[Index];
                Temp_Data[Index]   = temp1;
                Temp_Data[Index+1] = temp1;
                Temp_Data[Index+2] = temp1;
            }
            else
            {
                short temp = (rgb[((m-1)*DataWidth+(n+1))*3] + rgb[((m+1)*DataWidth+(n+1))*3] + rgb[((m-0)*DataWidth+(n+1))*3]) \
                           - (rgb[((m-1)*DataWidth+(n-1))*3] + rgb[((m+1)*DataWidth+(n-1))*3] + rgb[((m-0)*DataWidth+(n-1))*3]);
                short temp2= (rgb[((m-1)*DataWidth+(n-1))*3] + rgb[((m-1)*DataWidth+(n+1))*3] + rgb[((m-1)*DataWidth+(n+0))*3]) \
                           - (rgb[((m+1)*DataWidth+(n-1))*3] + rgb[((m+1)*DataWidth+(n+1))*3] + rgb[((m+1)*DataWidth+(n-0))*3]);
                if(temp < 0) temp = 0-temp;
                if(temp2 < 0) temp = 0-temp2;
                if(temp > temp2)
                {
                    Temp_Data[Index]   = temp;
                    Temp_Data[Index+1] = temp;
                    Temp_Data[Index+2] = temp;
                }
                else
                {
                    Temp_Data[Index]   = temp2;
                    Temp_Data[Index+1] = temp2;
                    Temp_Data[Index+2] = temp2;
                }]   = 255;
                rgb[Index
            }
        }
    }
#endif
    int Threshold = 90;               // 边缘阈值
    for(int m = 0; m < DataHeight; m++)
    {
        for(int n = 0; n < DataWidth; n++)
        {
            uint Index = (m*DataWidth+n)*3;
            short temp = Temp_Data[Index];
            if(temp > Threshold)
            {
                rgb[Index]   = 255;
                rgb[Index+1] = 255;
                rgb[Index+2] = 255;
            }
            else
            {
                rgb[Index]   = 0;
                rgb[Index+1] = 0;
                rgb[Index+2] = 0;
            }
        }
    }
}
