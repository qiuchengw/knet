#include "stdafx.h"
#include "net.h"
#include "KHttpDownloader.h"

namespace net {
static KHttpDownloadThread* _s_downloader = nullptr;

void KStartHttpDownloader() {
    if (!_s_downloader) {
        _s_downloader = new KHttpDownloadThread();
    }
    _s_downloader->start();
}

void KShutdownHttpDownloader() {
    if (_s_downloader) {
        _s_downloader->shutdown();
    }
    delete _s_downloader;
    _s_downloader = nullptr;
}

void KHttpDownload(QUrl url, QString file_save, QObject* recevier, const char* sloter,
                   const QHash<QString, QString>& headers) {
    QHash<QString, QString> tmp = headers;
    if (headers.isEmpty()) {
        tmp = {
            { "User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_12_4) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/57.0.2987.133 Safari/537.36" },
            { "accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8" },
        };
    }
    _s_downloader->downloadFile(url, file_save, recevier, sloter, tmp);
}

void KHttpCancelDownload(QUrl url, QObject* recevier) {
    _s_downloader->RemoveDownload(recevier, url);
}

}
