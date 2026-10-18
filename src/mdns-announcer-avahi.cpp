#include "mdns-announcer.hpp"
#include "protocol.hpp"

#include <plugin-support.h>
#include <obs.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/thread-watch.h>

#include <atomic>

namespace obsmobile {

namespace {

class AvahiAnnouncer : public MdnsAnnouncer {
public:
	AvahiAnnouncer(std::string name, unsigned short port) : name(std::move(name)), port(port)
	{
		poll = avahi_threaded_poll_new();
		if (!poll)
			return;
		int error = 0;
		client = avahi_client_new(avahi_threaded_poll_get(poll), AVAHI_CLIENT_NO_FAIL, &AvahiAnnouncer::onClient,
					  this, &error);
		if (!client) {
			obs_log(LOG_WARNING, "mDNS: Avahi unavailable (%s)", avahi_strerror(error));
			return;
		}
		avahi_threaded_poll_start(poll);
	}

	~AvahiAnnouncer() override
	{
		if (poll)
			avahi_threaded_poll_stop(poll);
		if (client)
			avahi_client_free(client); // also frees the entry group
		if (poll)
			avahi_threaded_poll_free(poll);
	}

	bool active() const override { return registered.load(); }

private:
	// Runs on Avahi's thread.
	static void onClient(AvahiClient *c, AvahiClientState state, void *userdata)
	{
		auto *self = static_cast<AvahiAnnouncer *>(userdata);
		if (state == AVAHI_CLIENT_S_RUNNING)
			self->publish(c);
		else if (state == AVAHI_CLIENT_FAILURE || state == AVAHI_CLIENT_S_COLLISION)
			self->registered = false;
	}

	static void onGroup(AvahiEntryGroup *, AvahiEntryGroupState state, void *userdata)
	{
		auto *self = static_cast<AvahiAnnouncer *>(userdata);
		self->registered = state == AVAHI_ENTRY_GROUP_ESTABLISHED;
		if (state == AVAHI_ENTRY_GROUP_ESTABLISHED)
			obs_log(LOG_INFO, "mDNS: announced as %s.%s", self->name.c_str(), kServiceType);
	}

	void publish(AvahiClient *c)
	{
		if (!group)
			group = avahi_entry_group_new(c, &AvahiAnnouncer::onGroup, this);
		if (!group || !avahi_entry_group_is_empty(group))
			return;
		const std::string version = "v=" + std::to_string(kProtocolVersion);
		const std::string obs = std::string("obs=") + obs_get_version_string();
		int ret = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, AvahiPublishFlags(0),
							name.c_str(), kServiceType, nullptr, nullptr, port,
							version.c_str(), obs.c_str(), nullptr);
		if (ret >= 0)
			ret = avahi_entry_group_commit(group);
		if (ret < 0)
			obs_log(LOG_WARNING, "mDNS: cannot announce (%s)", avahi_strerror(ret));
	}

	std::string name;
	unsigned short port;
	AvahiThreadedPoll *poll = nullptr;
	AvahiClient *client = nullptr;
	AvahiEntryGroup *group = nullptr;
	std::atomic<bool> registered{false};
};

} // namespace

std::unique_ptr<MdnsAnnouncer> MdnsAnnouncer::start(const std::string &instanceName, unsigned short port)
{
	return std::make_unique<AvahiAnnouncer>(instanceName, port);
}

} // namespace obsmobile
