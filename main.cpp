#include "mainwindow.h"
#include <QApplication>
#include <QDebug>
#include "LEPTON_SDK.h"
#include "LEPTON_SYS.h"
#include "LEPTON_AGC.h"
#include "LEPTON_VID.h"

int lepton_connect(void);
int enable_lepton_agc(void);
bool _connected;
LEP_CAMERA_PORT_DESC_T _port;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    lepton_connect();                           // Flir_AGC ON
    if(enable_lepton_agc() != 0) return 1;

    MainWindow w;
    w.show();

    return app.exec();
}

int lepton_connect(void)
{
    LEP_OpenPort(1, LEP_CCI_TWI, 400, &_port);
    qDebug() << "lepton_connect start...\n";
    _connected = true;
    return 0;
}

int enable_lepton_agc(void)
{
  LEP_RESULT result;
  LEP_AGC_ENABLE_E enabled;

  qDebug() << "Open AGC Start...";
  result = LEP_GetAgcEnableState(&_port, &enabled);
  if (result != LEP_OK)
  {
    qDebug() << "Could not query AGC value:" << result;
    return -1;
  }
  qDebug() << "Open AGC Finished!";
  if(result == LEP_OK) qDebug() << "result = LEP_OK";
  result = LEP_SetAgcCalcEnableState(&_port, LEP_AGC_ENABLE);
  if (result != LEP_OK)
  {
    qDebug() << "Could not enable AGC calc:" << result;
    return -1;
  }

  LEP_SetAgcPolicy(&_port, LEP_AGC_HEQ);
  if (result != LEP_OK)
  {
    qDebug() << "Could not set AGC policy:" << result;
    return -1;
  }

  result = LEP_SetAgcEnableState(&_port, LEP_AGC_ENABLE);
  if (result != LEP_OK)
  {
    qDebug() << "Could-------------- not enable AGC:" << result;
    return -1;
  }

  result = LEP_GetAgcEnableState(&_port, &enabled);
  if (result != LEP_OK)
  {
    qDebug() << "Could not query AGC value:" << result;
    return -1;
  }
  qDebug() << "Current AGC value:" << enabled;
  return 0;
}

