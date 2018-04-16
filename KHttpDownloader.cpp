#include "stdafx.h"
#include "KHttpDownloader.h"
#include <QDir>

#include "net.h"

template<typename T>
QByteArray ptr2arr(T *t) {
    QString str =
#ifdef _WIN32
        QString::number((qint32)t);
#else
        QString::number((qint64)t);
#endif
    return str.toUtf8();
}

template<typename T>
T* arr2ptr(const QByteArray& a) {
#ifdef _WIN32
    qint32 p = a.toInt();
#else
    qint64 p = a.toLongLong();
#endif
    return (T*)p;
}

void DownloadSignal::AddReciver(QObject* obj, const char* sloter, QString file_save) {
    if ((nullptr != obj) && (nullptr != sloter)) {
        recivers_.insert(obj, file_save);

        auto cn = QObject::connect(this,
                                   SIGNAL(downloadSignal(int, QUrl, int, QNetworkReply::NetworkError)),
                                   obj, sloter, Qt::UniqueConnection);
        Q_ASSERT(cn);
    }
}

int DownloadSignal::RemoveReciver(QObject* obj) {
    auto idx = recivers_.find(obj);
    if (recivers_.end() != idx) {
        recivers_.erase(idx);
    }
    return recivers_.size();
}

void DownloadSignal::setUrl(const QUrl& url) {
    url_ = url;
}

//////////////////////////////////////////////////////////////////////////
KHttpDownloader::~KHttpDownloader() {
    for (auto p : reqs_) {
        p->abort(QNetworkReply::OperationCanceledError);
    }
    reqs_.clear();

    man_->clearAccessCache();
    man_.reset();
}

KHttpDownloader::KHttpDownloader(QObject *parent) : QObject(parent) {
    man_.reset(new QNetworkAccessManager(nullptr));
    connect(man_.get(), &QNetworkAccessManager::encrypted, this, &KHttpDownloader::onEncrypted);
}

bool KHttpDownloader::_ReqItem::start(QNetworkAccessManager* man, const QUrl& url,
                                      const QHash<QString, QString> &headers, int timeout /*= 10*/) {
    if (!file_.open()) {
        // 临时文件打开失败
        return false;
    }

    if (timeout >= 5) {
        timer_.setInterval(timeout * 1000);
        timer_.start();
    }

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    for (auto i = headers.begin(); i != headers.end(); ++i) {
        req.setRawHeader(i.key().toUtf8(), i.value().toUtf8());
    }
    rep_ = man->get(req);
    signal_.setUrl(url);
    return nullptr != rep_;
}

void KHttpDownloader::_ReqItem::abort(QNetworkReply::NetworkError err) {
    timer_.stop();
    if (rep_ && rep_->isRunning()) {
        signal_.emitDownloadSignal(net::HttpDownload_Event_Error, progs_, err);
        rep_->abort();
    }
}

void KHttpDownloader::_ReqItem::finshedError(QNetworkReply::NetworkError err) {
    signal_.emitDownloadSignal(net::HttpDownload_Event_Error, progs_, err);
}

void KHttpDownloader::_ReqItem::setProgress(int n) {
    if (n == progs_) {
        return;
    }

    progs_ = n;

    if (rep_ != nullptr) {
        signal_.emitDownloadSignal(net::HttpDownload_Event_Progress, progs_, rep_->error());

        if (progs_ >= 100) {
            file_.flush();
            // 保存文件
            for (const QString& s : signal_.FileSaves()) {
                // 先把原来的文件给删除掉
                QFile::remove(s);

                // 复制过去！
                file_.copy(s);
            }

            signal_.emitDownloadSignal(net::HttpDownload_Event_Finished, progs_, rep_->error());
        }
    }
}

void KHttpDownloader::downloadFile(UserRequest* rq) {
    if (!reqs_.contains(rq->url)) {
        std::shared_ptr<_ReqItem> p(new _ReqItem);
        p->start(man_.get(), rq->url, rq->headers);
        reqs_.insert(rq->url, p);

        // 信号连接
        if (auto *r = p->reply()) {
            connect(r, &QNetworkReply::finished, this, &KHttpDownloader::httpFinished);
            connect(r, &QNetworkReply::readyRead, this, &KHttpDownloader::httpReadyRead);
            connect(r, &QNetworkReply::downloadProgress,this, &KHttpDownloader::updateDataReadProgress);
            connect(r, &QNetworkReply::sslErrors, this, &KHttpDownloader::sslErrors);
            connect(r, SIGNAL(error(QNetworkReply::NetworkError)),
                    this, SLOT(errorMessage(QNetworkReply::NetworkError)));
        }
    }

    if (_ReqItem *p = Find(rq->url)) {
        // 监听者
        p->signal()->AddReciver(rq->obj, rq->sloter, rq->file_save);
    }
}

void KHttpDownloader::httpFinished() {
    QNetworkReply* r = qobject_cast<QNetworkReply*>(sender());
    if (nullptr != r) {
        auto i = Find(r);
        if (i != reqs_.end()) {
            if (auto p = i.value()) {
                int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                qDebug() << "code:" << status;
                if (200 == status) {   // ok
                    p->setProgress(100);
                    // 现在可以移除这个下载项目了
                    RemoveDownload(i.key());
                } else {
                    // 使用abort发射信号
                    p->finshedError(QNetworkReply::NoError);
                }
            }
        }
    }
}

void KHttpDownloader::httpReadyRead() {
    if (QNetworkReply* r = qobject_cast<QNetworkReply*>(sender())) {
        if (_ReqItem* p = FindReq(r)) {
            // 读数据，写入到临时文件里面
            p->httpRead();
        }
    }
}

void KHttpDownloader::updateDataReadProgress(qint64 bytesRead, qint64 totalBytes) {
    if (QNetworkReply* r = qobject_cast<QNetworkReply*>(sender())) {
        if (_ReqItem* p = FindReq(r)) {
            if (totalBytes <= 0 || bytesRead < 0)
                return;

            qint64 prog = (double)bytesRead / ((double)totalBytes / 100);
            p->setProgress(prog);
        }
    }
}

void KHttpDownloader::sslErrors(const QList<QSslError> &errors) {
    if (!errors.isEmpty()) {
        for (auto& t : errors) {
            qDebug() << t.error() << ":" << t.errorString()<<"\n";
        }
    }
}

void KHttpDownloader::onEncrypted(QNetworkReply *reply) {
    QSslConfiguration ssl;
    ssl.setProtocol(QSsl::AnyProtocol);
    reply->setSslConfiguration(ssl);
}

QByteArray KHttpDownloader::getDownloadData(QUrl url) {
    if (_ReqItem* r = Find(url)) {
        if (auto* p = r->reply()) {
            if ((r->progress() == 100) && p->isFinished()) {
                return p->readAll();
            }
        }
    }
    return "";
}

bool KHttpDownloader::saveToFile(QUrl url, QString path, bool overwrite) {
    if (_ReqItem* p = Find(url)) {
        if (QFile::exists(path) && !overwrite) {
            qWarning() << "File with name \"" << path << "\" already exists. Rewrite.";
            return false;
        }
        p->save2(path);
    }
    return false;
}

void KHttpDownloader::errorMessage(QNetworkReply::NetworkError er) {
    if (QNetworkReply* r = qobject_cast<QNetworkReply*>(sender())) {
        auto i  = Find(r);
        if (i != reqs_.end()) {
            auto p = i.value();
            p->signal()->emitDownloadSignal(net::HttpDownload_Event_Error, p->progress(), er);

            // 删除掉
            reqs_.erase(i);
        }
    }
}

KHttpDownloader::Reqs::iterator KHttpDownloader::Find(QNetworkReply* r) {
    auto ie = reqs_.end();
    for (auto i = reqs_.begin(); i != ie; ++i) {
        if (i.value()->reply() == r) {
            return i;
        }
    }
    return ie;
}

QUrl KHttpDownloader::UrlOfReqest(QNetworkReply* r) {
    auto i = Find(r);
    if (i != reqs_.end()) {
        return i.key();
    }
    return QUrl();
}

KHttpDownloader::_ReqItem* KHttpDownloader::FindReq(QNetworkReply* r) {
    auto i = Find(r);
    if (i != reqs_.end()) {
        return i.value().get();
    }
    return nullptr;
}

void KHttpDownloader::RemoveDownload(QObject* obj, QUrl url) {
    auto i = reqs_.find(url);
    if (i == reqs_.end()) {
        if (_ReqItem* p = i.value().get()) {
            if (0 == p->signal()->RemoveReciver(obj)) {
                // 没有人再关心这个下载了
                RemoveDownload(url);
            }
        }
    }
}

void KHttpDownloader::RemoveDownload(QUrl url) {
    auto i = reqs_.find(url);
    if (i != reqs_.end()) {
        if (_ReqItem* p = i.value().get()) {
            // 取消执行
            p->abort(QNetworkReply::OperationCanceledError);

            // 删除掉数据
            reqs_.erase(i);
        }
    }
}


//////////////////////////////////////////////////////////////////////////
enum EnumHttpAwakeReason {
    HttpAwakeReason_Download,
    HttpAwakeReason_CancelDownload,
};

void KHttpDownloadThread::run() {
    awaker_.reset(new HttpAWaker(this, SLOT(onAwake(int, QByteArray))));
    http_.reset(new KHttpDownloader(this));

    running_ = true;
    QThread::exec();

    http_.reset();
    awaker_.reset();

    running_ = false;
}

void KHttpDownloadThread::downloadFile(QUrl url, QString file_save, QObject* obj,
                                       const char* sloter, const QHash<QString, QString>& headers) {
    if (!awaker_) {
        Q_ASSERT(false);
        return;
    }

    UserRequest* rq = new UserRequest;
    rq->file_save = file_save;
    rq->url = url;
    rq->obj = obj;
    rq->sloter = sloter;
    rq->headers = headers;

    awaker_->AwakeMessage(HttpAwakeReason_Download, ptr2arr(rq));
}

void KHttpDownloadThread::RemoveDownload(QObject* obj, QUrl url) {
    if (!awaker_) {
        // Q_ASSERT(false);
        return;
    }

    UserRequest* rq = new UserRequest;
    rq->obj = obj;
    rq->url = url;

    awaker_->AwakeMessage(HttpAwakeReason_CancelDownload, ptr2arr(rq));
}

bool KHttpDownloadThread::start() {
    // 启动线程
    QThread::start();
    while (!running_) {
        QThread::msleep(10);
    }

    // 必须得move到自己的线程中去！
    //  the current thread must be same as the current thread affinity.
    //  In other words, this function can only "push" an object from the
    //  current thread to another thread, it cannot "pull" an object
    //  from any arbitrary thread to the current thread.
    this->moveToThread(this);

    return true;
}

void KHttpDownloadThread::onAwake(int reason, QByteArray data) {
    if (!http_) {
        Q_ASSERT(false);
        return;
    }

    UserRequest* rq = arr2ptr<UserRequest>(data);//  (UserRequest*)(data.constData());
    switch (reason) {
    case HttpAwakeReason_Download:
        http_->downloadFile(rq);
        break;

    case HttpAwakeReason_CancelDownload:
        http_->RemoveDownload(rq->obj, rq->url);
        break;

    default:
        break;
    }
}

bool KHttpDownloadThread::shutdown() {
    // 关闭线程
    quit();

    // 等待关闭
    while (running_) {
        QThread::msleep(10);
    }
    return running_;
}
