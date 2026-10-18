#pragma once

#include <memory>
#include <string>

namespace obsmobile {

/**
 * Announces OBS on the local network as "<PC name>._obsmobile._tcp.local" so phones find it with
 * no address to type. Platform implementations: Windows DNS-SD API, Avahi on Linux.
 */
class MdnsAnnouncer {
public:
	virtual ~MdnsAnnouncer() = default;

	/** True while the service is registered on the network. */
	virtual bool active() const = 0;

	static std::unique_ptr<MdnsAnnouncer> start(const std::string &instanceName, unsigned short port);
};

} // namespace obsmobile
