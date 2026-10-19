/*
OBS Mobile — turns phones into cameras and remote controls for OBS.
Copyright (C) 2026 L-architec-T

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "blur-backdrop.hpp"
#include "control-server.hpp"
#include "device-store.hpp"
#include "mdns-announcer.hpp"
#include "mobile-camera-source.hpp"
#include "phones-dock.hpp"
#include "program-return.hpp"
#include "protocol.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QHostInfo>
#include <QMainWindow>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

using namespace obsmobile;

// Everything lives on the UI thread, created once OBS's window exists and destroyed on exit.
struct Plugin {
	DeviceStore store;
	ProgramReturn programReturn;
	ControlServer server{store, programReturn};
	std::unique_ptr<MdnsAnnouncer> announcer;
};

std::unique_ptr<Plugin> plugin;

void start()
{
	plugin = std::make_unique<Plugin>();
	plugin->programReturn.start();
	if (plugin->server.start())
		plugin->announcer = MdnsAnnouncer::start(QHostInfo::localHostName().toStdString(), kControlPort);

	auto *dock = new PhonesDock(plugin->server, plugin->store, [] {
		return plugin && plugin->announcer && plugin->announcer->active();
	});
	QObject::connect(&plugin->server, &ControlServer::approvalRequested, dock, &PhonesDock::showPairing);
	obs_frontend_add_dock_by_id("obs-mobile-phones", "Téléphones", dock);
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		start();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		// Phones get a clean close, and the mDNS record is withdrawn, before Qt goes away.
		if (plugin)
			plugin->server.disconnectAll();
		plugin.reset();
	}
}

} // namespace

bool obs_module_load(void)
{
	registerCameraSource();
	registerReturnOutput();
	registerBlurBackdrop();
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	obs_log(LOG_INFO, "plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	plugin.reset();
	obs_log(LOG_INFO, "plugin unloaded");
}
