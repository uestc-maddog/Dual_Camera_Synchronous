#ifndef LEPTONTHREAD_H
#define LEPTONTHREAD_H

#include <QThread>
#include <QObject>

class leptonthread : public QThread
{
    Q_OBJECT
public:
    leptonthread();
    ~leptonthread();

    void ShowSignal_Send()
    {
        emit show_signal();
    }

protected:
    void run();

signals:
    void show_signal();
};

#endif // LEPTONTHREAD_H
