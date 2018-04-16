#ifndef NET_H
#define NET_H

#include <QUrl>
#include <QHash>

namespace net {
enum EnumHttpDownloadEvent {
    HttpDownload_Event_Start = 1,
    HttpDownload_Event_Finished,
    HttpDownload_Event_Error,
    HttpDownload_Event_Progress,
};

void KStartHttpDownloader();
void KShutdownHttpDownloader();

/*
 * sloter:
 *      signal_downloadSignal(int evt, QUrl, int progress, QNetworkReply::NetworkError);
 *
 *  file_save   文件下载好后保存到
 *
 */
void KHttpDownload(QUrl url, QString file_save,
                   QObject* recevier, const char* sloter,
                   const QHash<QString, QString>& headers = QHash<QString, QString>());

/*
 *	取消下载
 */
void KHttpCancelDownload(QUrl url, QObject* recevier);
}

#endif // NET_H
