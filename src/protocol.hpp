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
//   {"type":"return","state":"start"|"stop"}          video return of what OBS has on air
//   {"type":"pong","id":n}                            answer to "ping"; "status" every 10 s too
// PC -> phone
//   {"type":"ping","id":n}                            every 3 s once accepted (latency in the panel)
//   {"type":"welcome","protocol":1,"pc":"PC-STREAM","obs":"32.2.2"}
//   {"type":"pairing","state":"pending"|"accepted"|"refused","token":"…"?}  (token when remembered)
//   {"type":"stream","port":9711,"latencyMs":80}     where to send SRT (MPEG-TS, H.264 + AAC)
//   {"type":"state","scene":"…","live":bool,"durationMs":n?,"receiving":bool,"onAir":bool,
//    "inScene":bool,"layout":"…","cameraError":"…"?}  on change, and every 2 s; cameraError
//                    says why OBS cannot receive the phone (French, shown as is)
//   {"type":"return","port":4461,"key":"…","width":960,"height":540}
//                    connect to port, send key + "\n", then read H.264 frames (program-return.hpp)

namespace obsmobile {

constexpr int kProtocolVersion = 1;
constexpr unsigned short kControlPort = 4460;
constexpr int kFirstCameraPort = 9711;
constexpr unsigned short kReturnPort = 4461;
constexpr const char *kServiceType = "_obsmobile._tcp";

// A line longer than this is a broken or hostile client: the connection is dropped.
constexpr int kMaxLineBytes = 2 * 1024 * 1024;
// Unapproved connections allowed at once: a LAN neighbour cannot flood the streamer with requests.
constexpr int kMaxPendingPhones = 4;

} // namespace obsmobile
