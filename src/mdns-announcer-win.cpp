#include "mdns-announcer.hpp"
#include "protocol.hpp"

#include <plugin-support.h>
#include <obs.h>

// The DNS-SD API (DnsServiceRegister…) is only declared for Windows 10 targets.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windns.h>

#include <atomic>
#include <string>

namespace obsmobile {

namespace {

std::wstring widen(const std::string &s)
{
	if (s.empty())
		return {};
	const int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
	std::wstring out(size, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), size);
	return out;
}

// Windows 10 1809+ DNS-SD API (dnsapi): the OS answers mDNS queries for us.
class WindowsAnnouncer : public MdnsAnnouncer {
public:
	WindowsAnnouncer(const std::string &name, unsigned short port)
	{
		wchar_t host[MAX_COMPUTERNAME_LENGTH + 1] = {};
		DWORD hostSize = MAX_COMPUTERNAME_LENGTH + 1;
		GetComputerNameW(host, &hostSize);

		instanceName = widen(name) + L"." + widen(kServiceType) + L".local";
		hostName = std::wstring(host) + L".local";
		keyVersion = L"v";
		valueVersion = std::to_wstring(kProtocolVersion);
		keyObs = L"obs";
		valueObs = widen(obs_get_version_string());
		PCWSTR keys[] = {keyVersion.c_str(), keyObs.c_str()};
		PCWSTR values[] = {valueVersion.c_str(), valueObs.c_str()};

		instance = DnsServiceConstructInstance(instanceName.c_str(), hostName.c_str(), nullptr, nullptr, port, 0, 0,
						       2, keys, values);
		if (!instance) {
			obs_log(LOG_WARNING, "mDNS: cannot build the service instance");
			return;
		}
		request.Version = DNS_QUERY_REQUEST_VERSION1;
		request.InterfaceIndex = 0;
		request.pServiceInstance = instance;
		request.pRegisterCompletionCallback = &WindowsAnnouncer::onRegistered;
		request.pQueryContext = this;
		request.unicastEnabled = FALSE;
		const DWORD status = DnsServiceRegister(&request, &cancel);
		if (status != DNS_REQUEST_PENDING)
			obs_log(LOG_WARNING, "mDNS: DnsServiceRegister failed (%lu)", status);
	}

	~WindowsAnnouncer() override
	{
		if (instance) {
			if (registered.load())
				DnsServiceDeRegister(&request, nullptr);
			DnsServiceFreeInstance(instance);
		}
	}

	bool active() const override { return registered.load(); }

private:
	static VOID WINAPI onRegistered(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE result)
	{
		if (result)
			DnsServiceFreeInstance(result);
		auto *self = static_cast<WindowsAnnouncer *>(context);
		self->registered = status == ERROR_SUCCESS;
		if (status == ERROR_SUCCESS)
			obs_log(LOG_INFO, "mDNS: announced on the local network");
		else
			obs_log(LOG_WARNING, "mDNS: registration failed (%lu)", status);
	}

	std::wstring instanceName, hostName, keyVersion, valueVersion, keyObs, valueObs;
	PDNS_SERVICE_INSTANCE instance = nullptr;
	DNS_SERVICE_REGISTER_REQUEST request{};
	DNS_SERVICE_CANCEL cancel{};
	std::atomic<bool> registered{false};
};

} // namespace

std::unique_ptr<MdnsAnnouncer> MdnsAnnouncer::start(const std::string &instanceName, unsigned short port)
{
	return std::make_unique<WindowsAnnouncer>(instanceName, port);
}

} // namespace obsmobile
