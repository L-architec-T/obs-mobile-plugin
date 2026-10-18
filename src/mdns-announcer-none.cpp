#include "mdns-announcer.hpp"

namespace obsmobile {

namespace {
class NoAnnouncer : public MdnsAnnouncer {
public:
	bool active() const override { return false; }
};
} // namespace

std::unique_ptr<MdnsAnnouncer> MdnsAnnouncer::start(const std::string &, unsigned short)
{
	return std::make_unique<NoAnnouncer>();
}

} // namespace obsmobile
