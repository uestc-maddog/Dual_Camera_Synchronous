#ifndef UVCTHREAD_H
#define UVCTHREAD_H

#include <QThread>
#include <QObject>

class UVCThread : public QThread
{
    Q_OBJECT
public:
    UVCThread();
    void ShowUVCSignal_Send()
    {
        emit showuvc_signal();
    }

protected:
    void run();

signals:
    void showuvc_signal();

};

#endif // UVCTHREAD_H
