#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace obsmobile {

/** A phone accepted once with "Se souvenir de cet appareil": it reconnects without asking. */
struct RememberedDevice {
	QString deviceId;
	QString name;
	QString model;
	QString token;
	QDateTime lastSeen;
};

/** Remembered phones, kept in the plugin's config folder (devices.json). UI thread only. */
class DeviceStore {
public:
	DeviceStore();

	QList<RememberedDevice> devices() const { return list; }

	/** True when [deviceId] was remembered and [token] is the one it was given. */
	bool isTrusted(const QString &deviceId, const QString &token) const;

	/** Remembers the device and returns the fresh token to hand to the phone. */
	QString remember(const QString &deviceId, const QString &name, const QString &model);

	void touch(const QString &deviceId);
	void forget(const QString &deviceId);

private:
	void load();
	void save() const;
	QString path() const;

	QList<RememberedDevice> list;
};

} // namespace obsmobile
