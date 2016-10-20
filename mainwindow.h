#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QLabel>
#include <QGridLayout>
#include <QVBoxLayout>
#include "uvcthread.h"
#include "leptonthread.h"
#include <QPushButton>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = 0);
    ~MainWindow();
private:
    enum{
        FrameWidth = 80,
        FrameHeight = 60,
        RowPacketWords = FrameWidth + 2,
        RowPacketBytes = 2*RowPacketWords,
        FrameWords = FrameWidth*FrameHeight
    };
    enum{ImageWidth = 320, ImageHeight = 240};

    QPushButton *RunButton;
    QPushButton *ShutButton;
    QPushButton *ColorButton;
    QPushButton *CloseButton;
    QLabel *imageLabel;
    //QLabel *FlirLabel;
    QLabel *tempLabel;
    QGridLayout *Mainlayout;
    QVBoxLayout *Buttonlayout;

    unsigned short rawMin, rawMax;
    QVector<unsigned short> rawData;
    QImage rgbImage;
    QImage UVCImage;

    UVCThread *threaduvc;
    leptonthread *threadlepton;

public slots:
    void updateUVCImage(void);
    void updateLeptonImage(void);
    void UVC_Start(void);
    void UVC_Shut(void);
};

#endif // MAINWINDOW_H
