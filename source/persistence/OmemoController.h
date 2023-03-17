#ifndef OMEMOCONTROLLER_H
#define OMEMOCONTROLLER_H

#include "QXmppOmemoStorage.h"
#include "Database.h"

#include <QSqlTableModel>

class OmemoController : public QXmppOmemoStorage
{
public:
    OmemoController(Database *db, QObject *parent);
    ~OmemoController() override;

    void setup();

    QFuture<OmemoData> allData() override;

    QFuture<void> setOwnDevice(const std::optional<OwnDevice> &device) override;

    QFuture<void> addSignedPreKeyPair(uint32_t keyId, const SignedPreKeyPair &keyPair) override;
    QFuture<void> removeSignedPreKeyPair(uint32_t keyId) override;

    QFuture<void> addPreKeyPairs(const QHash<uint32_t, QByteArray> &keyPairs) override;
    QFuture<void> removePreKeyPair(uint32_t keyId) override;

    QFuture<void> addDevice(const QString &jid, uint32_t deviceId, const Device &device) override;
    QFuture<void> removeDevice(const QString &jid, uint32_t deviceId) override;
    QFuture<void> removeDevices(const QString &jid) override;

    QFuture<void> resetAll() override;

private:
    bool isDevicePresentInDatabase(const QString &jid, uint32_t deviceId);
    void printSqlError(QSqlTableModel *table);

    Database *database_;
    QSqlTableModel *identityTable_;
    QSqlTableModel *devicesTable_;
    QSqlTableModel *signedPreKeysTable_;
    QSqlTableModel *preKeysTable_;
};


#endif  // OMEMOCONTROLLER_H