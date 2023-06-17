#pragma once

#include <QObject>
#include <QStringList>

#include "QXmppMessage.h"

#include "Settings.h"

class QXmppClient;
class DownloadManager;
class Persistence;
class ChatMarkers;
class RosterController;
class LurchAdapter;
class XMPPMessageParserClient;

class MessageHandler : public QObject
{
    Q_OBJECT
public:
    MessageHandler(Persistence* persistence, Settings* settings, RosterController* rosterController, QObject* parent = 0);

    void setupWithClient(QXmppClient* client);
    void sendChatMessage(QString const &toJid, QString const &message, QString const &type);
    void sendMucMessage(QString const &toJid, QString const &message, QString const &type);
    void sendDisplayedForJid(const QString &jid);
    void downloadFile(const QString &str, const QString &msgId);

signals:
    void messageSent(QString msgId);
    void httpDownloadFinished(QString attachmentMsgId);

public slots:
    void slotAppGetsActive(bool active);
    void setAskBeforeDownloading(bool AskBeforeDownloading);
    void handleMessageReceived(const QXmppMessage &message);

private:
#ifdef DBUS
public:
#endif
    QXmppClient* client_;
    Persistence* persistence_;
    RosterController* rosterController_;
    Settings* settings_;

    DownloadManager* downloadManager_;
    ChatMarkers* chatMarkers_;

    bool appIsActive_;
    bool askBeforeDownloading_;
};
