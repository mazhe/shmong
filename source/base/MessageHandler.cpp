#include "MessageHandler.h"
#include "Shmong.h"
#include "Persistence.h"
#include "ImageProcessing.h"
#include "DownloadManager.h"
#include "ChatMarkers.h"
#include "XmlProcessor.h"
#include "RosterController.h"

#include "QXmppClient.h"
#include "QXmppE2eeMetadata.h"
#include "QXmppMessage.h"
#include <QXmppMucManager.h>
#include "QXmppUtils.h"
#include "QXmppTask.h"


#include <QUrl>
#include <QDebug>
#include <QMimeDatabase>
#include <QFuture>
#include <QFutureWatcher>
#include <variant>


MessageHandler::MessageHandler(Persistence *persistence, Settings * settings, RosterController* rosterController, QObject *parent) : QObject(parent),
    client_(nullptr), persistence_(persistence), rosterController_(rosterController), settings_(settings), 
    downloadManager_(new DownloadManager(this)),
    chatMarkers_(nullptr),
    appIsActive_(true),
    askBeforeDownloading_(false)
{
    connect(settings_, SIGNAL(askBeforeDownloadingChanged(bool)), this, SLOT(setAskBeforeDownloading(bool)));
    connect(downloadManager_, SIGNAL(httpDownloadFinished(QString)), this, SIGNAL(httpDownloadFinished(QString)));
}

void MessageHandler::setupWithClient(QXmppClient* client)
{
    if (client != nullptr)
    {
        client_ = client;

        connect(client_, &QXmppClient::messageReceived, this, &MessageHandler::handleMessageReceived);

        chatMarkers_ = new ChatMarkers(persistence_, rosterController_);
        client_->addExtension(chatMarkers_);

        setAskBeforeDownloading(settings_->getAskBeforeDownloading());
    }
}

void MessageHandler::handleMessageReceived(const QXmppMessage &message)
{
    unsigned int security = 0;
    if(message.encryptionMethod() != QXmpp::NoEncryption)
    {
        security = 1;
    }

    if (message.type() == QXmppMessage::GroupChat && message.stamp().isValid())
    {
        // Ignore XEP-0045 history messages
        // Not well handled, they create duplicates in local history, should use MAM
        // Should also not be requested, but QXmpp don't have this option as of 1.5.4
        return;
    }

    // XEP 280
    bool sentCarbon = false;

    // If this is a carbon message, we need to retrieve the actual content
    if(client_->configuration().jidBare().compare(QXmppUtils::jidToBareJid(message.from()), Qt::CaseInsensitive) == 0)
    {
        sentCarbon = true;
    }

    if (! message.body().isEmpty())
    {
        QUrl oobUrl;
        QString type = "txt";
        QString messageId = message.id();

        if(message.body().startsWith("aesgcm://"))
        {
            oobUrl = message.body();
        }
        else if (! message.outOfBandUrl().isEmpty() )  // it's an url
        {
            oobUrl = message.outOfBandUrl();
        }

        if(oobUrl.isValid())
        {
            type = QMimeDatabase().mimeTypeForFile(oobUrl.fileName()).name();

            if(! askBeforeDownloading_)
                downloadManager_->doDownload(oobUrl, messageId); // keep the fragment in the sent message
        }

        // Group chats are not working for the moment
        //bool isGroupMessage = false;
        //if (message.type() == QXmppMessage::GroupChat)
        //{
        //    isGroupMessage = true;
        //}

        if (messageId.isEmpty() == true)
        {
            // No message id, try xep 0359
            messageId = message.stanzaId();

            // still empty?
            if (messageId.isEmpty() == true)
            {
                messageId = QString::number(QDateTime::currentMSecsSinceEpoch());
            }
        }

        qDebug() << "-----------------------------------------------------";
        qDebug() << "MESSAGE ADDED id:" << messageId << ", from: " << message.from() << ", to: " << message.to() << ", body: " << message.body();

        if (!sentCarbon)
        {
            persistence_->addMessage(messageId,
                                     QXmppUtils::jidToBareJid(message.from()),
                                     QXmppUtils::jidToResource(message.from()),
                                     message.body(), type, 1, security);
        } else
        {
            persistence_->addMessage(messageId,
                                     QXmppUtils::jidToBareJid(message.to()),
                                     QXmppUtils::jidToResource(message.to()),
                                     message.body(), type, 0, security);
        }

        // xep 0333
        QString currentChatPartner = persistence_->getCurrentChatPartner();
        //qDebug() << "fromJid: " << message.from() << "current: " << currentChatPartner << ", isGroup: " << isGroupMessage << ", appActive? " << appIsActive_;
        if ( (currentChatPartner.compare(QXmppUtils::jidToBareJid(message.from()), Qt::CaseInsensitive) == 0) &&     // immediatelly send read notification if sender is current chat partner
             appIsActive_ )                                                                                          // but only if app is active
        {
            this->sendDisplayedForJid(currentChatPartner);
        }
    }
}

void MessageHandler::sendChatMessage(QString const &toJid, QString const &message, QString const &type)
{
    QXmppMessage msg("", toJid, message);
    unsigned int security = 0;

    msg.setType(QXmppMessage::GroupChat);
    msg.setReceiptRequested(true);
    msg.setMarkable(true);
    msg.setId(QXmppUtils::generateStanzaUuid());
	msg.setStanzaId(msg.id());

    // exchange body by omemo stuff if applicable
    if ((settings_->getSoftwareFeatureOmemoEnabled() == true)
        && (! settings_->getSendPlainText().contains(toJid))) // no force for plain text msg in settings
    {
        msg.setEncryptionMethodNs("eu.siacs.conversations.axolotl");
        security = 1;
    }
    else // xep-0066. Only add the stanza on plain-text messages, as described in the xep-0454
    {
        if(type.compare("txt", Qt::CaseInsensitive) != 0)   // XEP-0066
        {
            msg.setOutOfBandUrl(message);
        }
    }

    qDebug() << "sendMessage" << "to:" << msg.to() << "id:" << msg.stanzaId() << "security :" << security << " body:" << message << endl;

    persistence_->addMessage( msg.id(),
                              QXmppUtils::jidToBareJid(msg.to()),
                              QXmppUtils::jidToResource(msg.to()),
                              message,
                              type,
                              0,
                              security);
    emit messageSent(msg.id());

    if(security) {
        QXmppSendStanzaParams sendParams;
        sendParams.setAcceptedTrustLevels(ANY_TRUST_LEVEL);
        client_->sendSensitive(std::move(msg), sendParams);
    } else {
        client_->send(std::move(msg));
    }
}

void MessageHandler::sendMucMessage(QString const &toJid, QString const &message, QString const &type)
{
    QXmppMessage msg("", toJid, message);
    msg.setType(QXmppMessage::GroupChat);
    msg.setReceiptRequested(true);
    msg.setMarkable(true);
    msg.setId(QXmppUtils::generateStanzaUuid());

	int security = 0; // TODO: Omemo for MUC

    if (type.compare("txt", Qt::CaseInsensitive) != 0)
    {
		// xep-0066. Only add the stanza on plain-text messages, as described in the xep-0454
		msg.setOutOfBandUrl(message);
    }

    // Get the user nickname, not needed to send message, but to store it
	QList<QXmppMucRoom *> rooms = client_->findExtension<QXmppMucManager>()->rooms();
	QList<QXmppMucRoom *>::const_iterator room_it = std::find_if( rooms.cbegin(),
                                                                  rooms.cend(),
                                                                  [&toJid](const QXmppMucRoom *r) {return (toJid.compare(r->jid()) == 0);});
    if (room_it == rooms.cend())
    {
        return;
    }
	QString nickName = (*room_it)->nickName();

    persistence_->addMessage( msg.id(),
                              QXmppUtils::jidToBareJid(msg.to()),
                              nickName,
                              message,
                              type,
                              1,
                              security);
    emit messageSent(msg.id());

	client_->send(std::move(msg));
}

void MessageHandler::sendDisplayedForJid(const QString &jid)
{
    if(settings_->getSendReadNotifications())
    {
        chatMarkers_->sendDisplayedForJid(jid);
    }
}

void MessageHandler::downloadFile(const QString &str, const QString &msgId)
{
    downloadManager_->doDownload(QUrl(str), msgId);
}

void MessageHandler::slotAppGetsActive(bool active)
{
    appIsActive_ = active;
}

void MessageHandler::setAskBeforeDownloading(bool AskBeforeDownloading)
{
    askBeforeDownloading_ = AskBeforeDownloading;
}
