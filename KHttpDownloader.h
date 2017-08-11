#ifndef HttpDownload_H
#define HttpDownload_H

#pragma once

#include <memory>

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QFileInfo>
#include <QUrl>
#include <QTimer>
#include <QTemporaryFile>
#include <QThread>
#include "http_awaker.h"

QT_BEGIN_NAMESPACE
class QNetworkReply;
class QFile;
QT_END_NAMESPACE

class DownloadSignal : public QObject
{
    Q_OBJECT

public:
    void setUrl(const QUrl& url);
    void AddReciver(QObject* obj, const char* sloter, QString file_save);

    // 返回剩余的监听者
    int RemoveReciver(QObject* obj);

    inline QUrl url()const
    {
        return url_;
    }

    inline QList<QString> FileSaves()const
    {
        return recivers_.values();
    }

    void emitDownloadSignal(int evt, int progress, QNetworkReply::NetworkError err){
        emit downloadSignal(evt, url_, progress, err);
    }

signals:
    void downloadSignal(int evt, QUrl, int progress, QNetworkReply::NetworkError);

private:
    QUrl    url_;

    // value : file save path
    QHash<QObject*, QString>     recivers_;
};

struct UserRequest
{
    QUrl url;
    QString file_save;
    QObject* obj;
    QByteArray sloter;
    QHash<QString, QString> headers;
};

class KHttpDownloadThread;
class KHttpDownloader : public QObject
{
    Q_OBJECT

    friend class KHttpDownloadThread;
    friend std::shared_ptr<KHttpDownloader> ;

    struct _ReqItem
    {
        _ReqItem(){
            file_.setAutoRemove(true);
            connect(&timer_, &QTimer::timeout, [=](){
                if (0 >= progs_){ // 如果还没有一点进度，那么就停止吧
                    abort(QNetworkReply::TimeoutError);
                }
            });
        }

        bool start(QNetworkAccessManager* man, const QUrl& url, 
            const QHash<QString, QString> &headers, int timeout = 10);

        void abort(QNetworkReply::NetworkError err);
        // 状态是已经完成，但是发生了错误
        void finshedError(QNetworkReply::NetworkError err);

        QNetworkReply* reply()const{
            return rep_;
        }

        DownloadSignal* signal(){
            return &signal_;
        }

        void setProgress(int n);
        int progress()const{
            return progs_;
        }

        void httpRead(){
            file_.write(rep_->readAll());
        }

        void save2(const QString& file){
            file_.copy(file);
        }
    private:
        // 请求
        QNetworkReply*  rep_ = nullptr;
        
        // 进度
        int progs_ = 0;

        // 连接超时检测
        QTimer  timer_;

        // 通知
        DownloadSignal signal_;

        // 临时文件
        QTemporaryFile      file_;
    };
    typedef QHash<QUrl, std::shared_ptr<_ReqItem>> Reqs;

public:
    ~KHttpDownloader();

private:
    KHttpDownloader(QObject *parent = 0);

    void downloadFile(UserRequest* rq);
    void RemoveDownload(QObject* obj, QUrl url);

    void RemoveDownload(QUrl url);

protected:
    Reqs::iterator Find(QNetworkReply* r);

    inline _ReqItem* Find(QUrl url)
    {
        return reqs_.value(url).get();
    }

    QUrl UrlOfReqest(QNetworkReply* r);

    _ReqItem* FindReq(QNetworkReply* r);

public slots:
    QByteArray getDownloadData(QUrl url);
    bool saveToFile(QUrl url, QString path, bool overwrite = true);
    void errorMessage(QNetworkReply::NetworkError);

    
private slots:
    void httpFinished();
    void httpReadyRead();
    void updateDataReadProgress(qint64 bytesRead, qint64 totalBytes);
    void sslErrors(const QList<QSslError> &errors);
    void onEncrypted(QNetworkReply *reply);

private:
    // 当前的请求
    Reqs reqs_;

    // 下载管理器
    std::shared_ptr<QNetworkAccessManager> man_ = nullptr;
};

class KHttpDownloadThread : public QThread
{
    Q_OBJECT

public:
    bool start();
    bool shutdown();

    void downloadFile(QUrl url, QString file_save, QObject* obj, const char* sloter, 
        const QHash<QString, QString>& header);
    void RemoveDownload(QObject* obj, QUrl url);

protected:
    virtual void run() override;

protected slots:
void onAwake(int reason, QByteArray data);

private:
    // 唤醒器
    std::shared_ptr<HttpAWaker> awaker_ = nullptr;

    // http 下载器
    std::shared_ptr<KHttpDownloader>  http_ = nullptr;

    // 是否已经在线程中执行
    bool running_ = false;
};

#endif
