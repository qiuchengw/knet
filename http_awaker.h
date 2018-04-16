#ifndef http_awaker_h__
#define http_awaker_h__

#include <memory>

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QThread>

class QThreadAWakerEmitor : public QObject {
    Q_OBJECT;

public:
    QThreadAWakerEmitor();

signals:
    // 这个信号用于触发signal_Awake信号
    void signal_AwakeSignal(int reason, QByteArray msg);
};

class HttpAWaker {
    Q_DISABLE_COPY(HttpAWaker);

    friend class KAsyncReqThreadPrivate;
    friend class KHttpDownloadThread;

public:
    ~HttpAWaker() {
        QMutexLocker locker(&event_locker_);

        for (auto *p : awake_emitors_.values()) {
            delete p;
        }
    }

private:
    HttpAWaker(QObject* p, const char* awake_sloter)
        :sloter_obj_(p), awake_sloter_(awake_sloter) {
        Q_ASSERT(nullptr != awake_sloter_);
    }

    /**
    *	向被唤醒者发送消息
    *
    *	@return
    *		true    发送唤醒成功。
    *	@param
    *		-[in]
    *      msg     唤醒消息
    **/
    bool AwakeMessage(int reason, const QByteArray& msg) {
        event_locker_.lock();
        QThreadAWakerEmitor* emitor = socket(QThread::currentThreadId());
        event_locker_.unlock();

        Q_ASSERT(nullptr != emitor);
        if (nullptr != emitor) {
            // 阻塞方式
            emit emitor->signal_AwakeSignal(reason, msg);

            return true;
        }
        return false;
    }

private:
    // 如果当前没有则创建
    QThreadAWakerEmitor* socket(Qt::HANDLE thread_id) {
        auto * p = awake_emitors_.value(thread_id);
        if (nullptr == p) {
            p = new QThreadAWakerEmitor();

            // 保存
            awake_emitors_.insert(thread_id, p);

            // 连接上去
            QByteArray slot = awake_sloter_.toUtf8();
            QObject::connect(p, SIGNAL(signal_AwakeSignal(int, QByteArray)),
                             sloter_obj_, slot.constData());
        }
        return p;
    }

private:
    // 每个线程有且仅有一个唤醒socket
    QHash<Qt::HANDLE, QThreadAWakerEmitor*> awake_emitors_;

    // 需要被唤醒的地址
    QString     awake_sloter_;

    // 事件锁
    QMutex      event_locker_;

    // sloter_obj_
    QObject*    sloter_obj_ = nullptr;
};

#endif // awaker_h__
