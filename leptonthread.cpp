#include "leptonthread.h"
#include <QDebug>
#include <QSemaphore>
#include <QDateTime>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <limits.h>

static int  transfer(int fd);
static void save_Lepton_Data(void);

int Lepton_fd = 0;                  // Flir文件描述符
extern unsigned short Lepton_Data[];
extern int minValue;
extern int maxValue;

extern volatile bool Flir_Get_Over;      // 1:Flir获取数据完成

extern QSemaphore Dual_Get_Ready_sem;
extern QSemaphore Flir_Get_Over_sem;
extern QSemaphore UVC_Init_Over_sem;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define VOSPI_FRAME_SIZE (164)

static void pabort(const char *s)
{
    perror(s);
    abort();
}

static const char *Flir_Device = "/dev/spidev0.1";
uint8_t tx[VOSPI_FRAME_SIZE] = {0, };
uint8_t lepton_frame_packet[VOSPI_FRAME_SIZE];
static uint32_t speed = 16000000;
static uint16_t delay = 0;
static uint8_t  bits = 8;
static uint8_t  mode = SPI_MODE_3;   // (SPI_CPOL|SPI_CPHA)

struct spi_ioc_transfer tr = {
    tr.tx_buf = (unsigned long)tx,
    tr.rx_buf = (unsigned long)lepton_frame_packet,
    tr.len = VOSPI_FRAME_SIZE,
    tr.speed_hz = speed,
    tr.delay_usecs = delay,
    tr.bits_per_word = bits
};

static unsigned int lepton_image[60][80];

leptonthread::leptonthread()
{

}
leptonthread::~leptonthread()
{

}

void leptonthread::run()
{
    bool temp = false;
    while(1)
    {
        Lepton_fd = open(Flir_Device, O_RDWR); // 返回文件描述符（整型变量0~255）。由open 返回的文件描述符一定是该进程尚未使用的最小描述符。只要有一个权限被禁止则返回-1。
        if(Lepton_fd < 0)
        {
            qDebug() << "can't open device";
        }

        int ret = ioctl(Lepton_fd, SPI_IOC_WR_MODE, &mode);
        if (ret == -1)
        {
            qDebug() << "can't set spi mode" << errno;
        }
        ret = ioctl(Lepton_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
        if (ret == -1)
        {
            qDebug() << "can't set bits per word" << errno;
        }
        ret = ioctl(Lepton_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
        if (ret == -1)
        {
            qDebug() << "can't set max speed hz" << errno;
        }
        UVC_Init_Over_sem.acquire();          // 等待UVC初始化完成
        while(1)
        {
            Dual_Get_Ready_sem.acquire();          // 等待同步获取双摄像头数据

            // 开始获取Flir摄像头数据
            while(transfer(Lepton_fd) != 59);           // read SPI data(lepton)
            save_Lepton_Data();

            ShowSignal_Send();
            msleep(60);
        }
    }
}

static void save_Lepton_Data(void)
{
    int i, j;
    unsigned int maxval = 0;
    unsigned int minval = UINT_MAX;

    for(i = 0;i < 60; i++)
    {
        for(j = 0; j < 80; j++)
        {
            Lepton_Data[80*i+j] = lepton_image[i][j];     // 二维数组转一维数组
            if (lepton_image[i][j] > maxval)  maxval = lepton_image[i][j];
            if (lepton_image[i][j] < minval)  minval = lepton_image[i][j];
        }
    }
    minValue = minval;
    maxValue = maxval;
}

int transfer(int fd)
{
    int ret;
    int i;
    int frame_number = 0;

    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 1) qDebug() << "can't send spi message";

    if(((lepton_frame_packet[0] & 0x0f) != 0x0f))
    {
        frame_number = lepton_frame_packet[1];

        if(frame_number < 60 )
        {
            for(i = 0; i < 80; i++)
            {
                lepton_image[frame_number][i] = (lepton_frame_packet[2*i+4] << 8 | lepton_frame_packet[2*i+5]);
            }
        }
    }
    return frame_number;
}
