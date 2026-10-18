#pragma once

// Shared with the Android app (OBS Mobile). One JSON object per line over TCP.
//
// Phone -> PC
//   {"type":"hello","protocol":1,"deviceId":"…","name":"A33 de Kevin","model":"SM-A336B","app":"0.0.2","token":"…"?}
//   {"type":"status","battery":82,"charging":false,"wifi":-55,"micDb":-23.5}
//   {"type":"preview","jpeg":"<base64>"}            (only while waiting for approval, ~360p)
// PC -> phone
//   {"type":"welcome","protocol":1,"pc":"PC-STREAM","obs":"32.2.2"}
//   {"type":"pairing","state":"pending"|"accepted"|"refused","token":"…"?}  (token when remembered)

namespace obsmobile {

constexpr int kProtocolVersion = 1;
constexpr unsigned short kControlPort = 4460;
constexpr const char *kServiceType = "_obsmobile._tcp";

// A line longer than this is a broken or hostile client: the connection is dropped.
constexpr int kMaxLineBytes = 2 * 1024 * 1024;
// Unapproved connections allowed at once: a LAN neighbour cannot flood the streamer with requests.
constexpr int kMaxPendingPhones = 4;

} // namespace obsmobile
