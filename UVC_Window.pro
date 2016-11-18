#-------------------------------------------------
#
# Project created by QtCreator 2016-09-08T19:52:17
# UVC_X1+Flir融合  信号量及标志位规范
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = Dual_Camera_Sync
TEMPLATE = app

target.path = /home/pi/YJ_qt
INSTALLS += target

SOURCES += main.cpp\
        mainwindow.cpp \
    uvcthread.cpp \
    LEPTON_AGC.c \
    LEPTON_SDK.c \
    LEPTON_SYS.c \
    LEPTON_VID.c \
    LEPTON_I2C_Protocol.c \
    LEPTON_I2C_Service.c \
    bcm2835.c \
    crc16fast.c \
    raspi_I2C.c \
    leptonthread.cpp

HEADERS  += mainwindow.h \
    uvcthread.h \
    LEPTON_AGC.h \
    LEPTON_SDK.h \
    LEPTON_SYS.h \
    LEPTON_VID.h \
    LEPTON_ErrorCodes.h \
    LEPTON_I2C_Protocol.h \
    LEPTON_I2C_Reg.h \
    LEPTON_I2C_Service.h \
    LEPTON_Macros.h \
    LEPTON_SDKConfig.h \
    LEPTON_Types.h \
    crc16.h \
    raspi_I2C.h \
    bcm2835.h \
    leptonthread.h
