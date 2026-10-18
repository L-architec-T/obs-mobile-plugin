#include "device-store.hpp"

#include <obs-module.h>
#include <util/bmem.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>

namespace obsmobile {

namespace {

QString newToken()
{
	QByteArray bytes(32, Qt::Uninitialized);
	QRandomGenerator::system()->generate(reinterpret_cast<quint32 *>(bytes.data()),
					     reinterpret_cast<quint32 *>(bytes.data() + bytes.size()));
	return QString::fromLatin1(bytes.toHex());
}

// Same length and content, without stopping at the first difference.
bool sameToken(const QString &a, const QString &b)
{
	if (a.size() != b.size() || a.isEmpty())
		return false;
	ushort diff = 0;
	for (qsizetype i = 0; i < a.size(); ++i)
		diff |= a[i].unicode() ^ b[i].unicode();
	return diff == 0;
}

} // namespace

DeviceStore::DeviceStore()
{
	load();
}

QString DeviceStore::path() const
{
	char *raw = obs_module_config_path("devices.json");
	QString result = QString::fromUtf8(raw);
	bfree(raw);
	return result;
}

bool DeviceStore::isTrusted(const QString &deviceId, const QString &token) const
{
	for (const auto &d : list)
		if (d.deviceId == deviceId)
			return sameToken(d.token, token);
	return false;
}

QString DeviceStore::remember(const QString &deviceId, const QString &name, const QString &model)
{
	forget(deviceId);
	RememberedDevice d{deviceId, name, model, newToken(), QDateTime::currentDateTimeUtc()};
	list.append(d);
	save();
	return d.token;
}

void DeviceStore::touch(const QString &deviceId)
{
	for (auto &d : list)
		if (d.deviceId == deviceId)
			d.lastSeen = QDateTime::currentDateTimeUtc();
	save();
}

void DeviceStore::forget(const QString &deviceId)
{
	const auto before = list.size();
	list.removeIf([&](const RememberedDevice &d) { return d.deviceId == deviceId; });
	if (list.size() != before)
		save();
}

void DeviceStore::load()
{
	QFile file(path());
	if (!file.open(QIODevice::ReadOnly))
		return;
	const auto array = QJsonDocument::fromJson(file.readAll()).array();
	for (const auto &value : array) {
		const auto o = value.toObject();
		RememberedDevice d{o["deviceId"].toString(), o["name"].toString(), o["model"].toString(),
				   o["token"].toString(),
				   QDateTime::fromString(o["lastSeen"].toString(), Qt::ISODate)};
		if (!d.deviceId.isEmpty() && !d.token.isEmpty())
			list.append(d);
	}
}

void DeviceStore::save() const
{
	QJsonArray array;
	for (const auto &d : list)
		array.append(QJsonObject{{"deviceId", d.deviceId},
					 {"name", d.name},
					 {"model", d.model},
					 {"token", d.token},
					 {"lastSeen", d.lastSeen.toString(Qt::ISODate)}});
	const QString file = path();
	QDir().mkpath(QFileInfo(file).absolutePath());
	QSaveFile out(file);
	if (out.open(QIODevice::WriteOnly)) {
		out.write(QJsonDocument(array).toJson());
		out.commit();
	}
}

} // namespace obsmobile
