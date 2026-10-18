#pragma once

#include <QJsonObject>
#include <QString>

#include <obs.h>

namespace obsmobile {

constexpr const char *kScenePrefix = "OBS Mobile · ";
constexpr const char *kCamerasScene = "OBS Mobile · Caméras";

/** Where the phone appears: "full" (centered, full height), "side" (right half), "corner". */
enum class Layout { Full, Side, Corner };

Layout layoutFromName(const QString &name);
QString layoutName(Layout layout);

/**
 * One accepted phone's camera in OBS: its source (obs_mobile_camera, listening on [port]) in the
 * "OBS Mobile · Caméras" scene, that scene inside the scene on air. The source is shown only
 * while pictures arrive and the phone wants to be on air; its mic follows. UI thread only.
 */
class PhoneCamera {
public:
	PhoneCamera(const QString &phoneName, int port);
	~PhoneCamera();

	int port() const { return srtPort; }

	/** Creates/updates scene and source, places them; the phone may then send to [port]. */
	void start(Layout layout);
	void stop();
	void setLayout(Layout layout);
	void addToCurrentScene();

	/** Applies visibility from what is received; returns the state to send to the phone. */
	QJsonObject tick();

private:
	obs_sceneitem_t *item() const;
	obs_source_t *source() const;
	void ensureSource();
	void place();
	void setVisible(bool visible);
	bool inCurrentScene() const;

	QString inputName;
	int srtPort;
	Layout layout = Layout::Corner;
	bool wantOnAir = false;
};

} // namespace obsmobile
