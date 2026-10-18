#pragma once

// Shared with the Android app (OBS Mobile). One JSON object per line over TCP.
//
// Phone -> PC
//   {"type":"hello","protocol":1,"deviceId":"…","name":"A33 de Kevin","model":"SM-A336B","app":"0.0.2","token":"…"?}
//   {"type":"status","battery":82,"charging":false,"wifi":-55,"micDb":-23.5}
//   {"type":"preview","jpeg":"<base64>"}            (only while waiting for approval, ~360p)
//   once accepted:
//   {"type":"camera","state":"start","layout":"full"|"side"|"corner"}  -> PC answers "stream"
//   {"type":"camera","state":"stop"}
//   {"type":"layout","layout":"…"}
//   {"type":"addToScene"}
// PC -> phone
//   {"type":"welcome","protocol":1,"pc":"PC-STREAM","obs":"32.2.2"}
//   {"type":"pairing","state":"pending"|"accepted"|"refused","token":"…"?}  (token when remembered)
//   {"type":"stream","port":9711,"latencyMs":80}     where to send SRT (MPEG-TS, H.264 + AAC)
//   {"type":"state","scene":"…","live":bool,"durationMs":n?,"receiving":bool,"onAir":bool,
//    "inScene":bool,"layout":"…"}                     on change, and every 2 s

namespace obsmobile {

constexpr int kProtocolVersion = 1;
constexpr unsigned short kControlPort = 4460;
constexpr int kFirstCameraPort = 9711;
constexpr const char *kServiceType = "_obsmobile._tcp";

// A line longer than this is a broken or hostile client: the connection is dropped.
constexpr int kMaxLineBytes = 2 * 1024 * 1024;
// Unapproved connections allowed at once: a LAN neighbour cannot flood the streamer with requests.
constexpr int kMaxPendingPhones = 4;

} // namespace obsmobile
