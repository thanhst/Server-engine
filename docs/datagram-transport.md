# Datagram Transport: truyền message cho client và server

Đây là phần network dùng chung cho ứng dụng cần UDP với hai chế độ gửi.
Tên module mô tả cách truyền dữ liệu; không chứa nhân vật, phòng chơi, codec
audio/video hay logic nghiệp vụ. Game, ứng dụng đồng bộ trạng thái hoặc dịch vụ
nội bộ có thể dùng cùng API. Tất cả vẫn nằm trong `ServerEngine.dll`.

Tên công khai là `DatagramTransport`; backend hiện tại dùng GNS và nằm trong
`src/Net/Transports/Gns`. Các transport TCP/HTTP/WebSocket hiện có giữ API riêng;
đây chưa phải một endpoint chung có thể đổi tùy ý giữa mọi giao thức.

Module dùng **Valve GameNetworkingSockets 1.6.0**, được pin qua vcpkg baseline.
GNS lo xác nhận/gửi lại, phân mảnh/ghép message, điều tiết tốc độ và mã hóa.
Engine cung cấp C ABI, quản lý endpoint/peer, giới hạn hàng đợi và event cho host.

| Chế độ | Hợp đồng |
| --- | --- |
| `SE_DATAGRAM_RELIABLE_ORDERED` | Tin cậy và đúng thứ tự trong luồng reliable của connection còn hoạt động |
| `SE_DATAGRAM_UNRELIABLE` | Có thể mất/lặp/đảo thứ tự; snapshot nên mang tick/sequence riêng |

Reliable không có nghĩa giao dịch hoặc lưu SQL đã thành công. Ứng dụng vẫn
kiểm tra quyền, cập nhật trạng thái và xử lý gửi lại giao dịch sau reconnect.

## Bạn tự build

Developer PowerShell for VS 2022, tại gốc repo; `VCPKG_ROOT` trỏ tới vcpkg của
bạn như [hướng dẫn chạy examples](run-examples.md). Không dùng `cmake .`.

```powershell
# Vi du vcpkg di kem VS Community; doi neu may ban cai o noi khac.
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg'
cmake --preset vs2022-x64-datagram
cmake --build --preset vs2022-x64-datagram-debug
```

Preset này bật `SE_WITH_GNS=ON`. Tắt feature vẫn có export C ABI,
nhưng tạo endpoint trả `SE_NOT_SUPPORTED`. CMake luôn tạo target nội bộ và chỉ
thêm dependency GNS khi bật feature. ICE/P2P tắt; ví dụ dùng IP trực tiếp.

Mở hai terminal riêng, chạy server trước:

```powershell
.\out\build\vs2022-x64-datagram\Debug\ServerEngineDatagramTransportExample.exe --server 9010
```

```powershell
.\out\build\vs2022-x64-datagram\Debug\ServerEngineDatagramTransportExample.exe --client 9010
```

Kết quả mong đợi: client nhận `Reliable reply: PONG`, và có thể nhận echo các
snapshot. Client dừng sau bài thử; server chờ Ctrl+C. Đây là kết quả mong đợi,
chưa phải kết quả đã chạy xác nhận trong lượt triển khai này.

DLL nằm cạnh EXE. Preset static-md gộp dependency tĩnh; nếu tự chọn dependency
shared thì triển khai thêm các DLL tương ứng. Module có wire protocol riêng,
khác UDP thô 9001/TCP 9443. Browser hoặc client TCP cũ không tự dùng được nó.
Client native có thể gọi cùng DLL/SDK này; không cần lộ header GNS ra ứng dụng.

## Đọc source theo một đường

1. [Example](../examples/DatagramTransport/main.cpp): `server()` / `client()`.
2. [C++ wrapper](../include/ServerEngine/Cpp/DatagramTransport.h): `DatagramEndpoint::send/poll`.
3. [C ABI](../src/Abi/DatagramTransport.cpp): kiểm tra tham số và chuyển lời gọi.
4. [Runtime](../src/Net/Transports/Gns/Runtime.cpp): registry, mutex, pump callback, vòng đời GNS.
5. [Endpoint](../src/Net/Transports/Gns/Endpoint.cpp): listen/connect/send/disconnect.
6. [EndpointEvents](../src/Net/Transports/Gns/EndpointEvents.cpp): nhận message, queue, overflow.

```cpp
endpoint.send(peer, DatagramDelivery::ReliableOrdered, "PING");
endpoint.send(peer, DatagramDelivery::Unreliable, "SNAP 42");
```

Đây là lời gọi C++ trong example; bên dưới vẫn là C ABI `se_datagram_send`.
Các `se_server_*` cũ không đổi. App dùng API mới cần DLL mới có `se_datagram_*`.

- Cả server và client phải poll thường xuyên; chờ CONNECTED trước khi gửi.
- GNS có worker I/O riêng; Runtime serialize API/callback bằng một mutex.
  Không gọi logic ứng dụng trên worker. Poll chờ theo lát ngắn, nhả khóa khi chờ.
- `SE_OK` từ send là đã copy/nhận vào hàng đợi; `SE_BACKPRESSURE` là chưa nhận.
- `SE_BUFFER_TOO_SMALL` giữ event để gọi lại với buffer lớn hơn.
- Giới hạn có ở queue native từng peer và event queue từng endpoint. Đây không
  phải phép đo RAM toàn tiến trình; còn overhead của thư viện/OS.
- Overflow dừng endpoint để không giữ lịch sử thiếu OPEN/CLOSE; phải tạo lại.
- Destroy đánh thức poll đang chờ và hủy gửi đang chờ, không bảo đảm drain.
  Ứng dụng cần xác nhận nghiệp vụ quan trọng trước khi đóng.
- Engine quản lý GNS runtime; không đồng thời gọi trực tiếp init/kill GNS từ
  module khác trong cùng process, nhất là khi dùng dependency dạng shared.

## Giới hạn bảo mật và triển khai

**Mã hóa là bắt buộc, nhưng direct-IP hiện chưa xác minh danh tính peer/server
bằng chứng chỉ tin cậy hoặc pin khóa.** Không coi nó đã chống MITM chủ động.
Mặc định chỉ chấp nhận `127.0.0.1`/`::1`. Flag
`SE_DATAGRAM_ALLOW_REMOTE_UNAUTHENTICATED` cho phép IP khác và chỉ dành cho mạng
được bảo vệ/tin cậy. Không gửi bí mật qua Internet không tin cậy chỉ vì có mã hóa.
Đăng nhập, phân quyền và transport xác thực công khai còn cần hoàn thiện.

Module chưa có codec voice/WebRTC, TURN, matchmaking hoặc SFU. Các vấn đề
shutdown/SQL của GameServer mẫu cũ cũng không được sửa bởi module này.

## Kiểm thử bạn cần chạy

```powershell
ctest --test-dir out/build/vs2022-x64-datagram -C Debug -R DatagramTransport --output-on-failure
```

- `DatagramTransportCHeader`: header C, layout và liên kết export.
- `DatagramTransportContracts`: tham số, feature tắt, handle và RAII wrapper.
- `DatagramTransportLoopback`: connect, binary/copy buffer, thứ tự reliable,
  unreliable, disconnect và destroy đánh thức poll.
- `DatagramTransportLoss`: proxy UDP thật bỏ/lặp/đảo thứ tự gói; yêu cầu 128 message
  reliable 2 KiB đến đủ, đúng thứ tự, không trùng rồi nhận phản hồi hoàn tất.

Test mạng skip code 77 khi feature tắt. Phải kiểm tra không bị skip khi dùng
preset `-datagram`. Agent chỉ kiểm tra tĩnh, **không configure/build/chạy test**.
Chưa xác nhận module chạy được trên máy này, chưa có thử tải dài/WAN/security
audit; smoke checks cũ không chứng minh module mới sẵn sàng production.

## Tương thích với tên GameTransport trước đây

| Code mới nên dùng | Tên cũ còn hỗ trợ |
| --- | --- |
| `C/DatagramTransport.h`, `se_datagram_*` | `C/GameTransport.h`, `se_game_*` |
| `Cpp/DatagramTransport.h`, `DatagramEndpoint` | `Cpp/GameTransport.h`, `GameEndpoint` |
| `SE_WITH_GNS` | `SE_WITH_GAME_TRANSPORT` khi chưa đặt option mới |
| `vs2022-x64-datagram[-debug/-release]` | `vs2022-x64-game[-debug/-release]` |
| vcpkg feature `gns` | `game-transport` |

Tên cũ chỉ chuyển tiếp vào cùng implementation, registry và queue. Layout C v1,
giá trị hằng số và 10 export `se_game_*` được giữ; DLL bổ sung 10 export
`se_datagram_*`. Binary cũ vẫn có các symbol nó cần. Header C cũ còn nhận cả
`struct se_game_options`/`struct se_game_event`; C++ dùng alias tới lớp mới.

Option mới ưu tiên nếu đặt cả hai. Preset cũ giữ thư mục output theo tên cũ,
nhưng EXE mẫu nay tên `ServerEngineDatagramTransportExample.exe`; tên target
và test mới có tiền tố `DatagramTransport`. CMake target nội bộ là
`ServerEngineGns` (STATIC), được link vào DLL, không phải DLL triển khai riêng.

Source test C kiểm tra layout và tạo bằng API cũ/hủy bằng API mới rồi ngược lại;
test C++ kiểm tra các alias. Các kiểm tra này cần bạn build/chạy để xác nhận.

Nguồn: [Valve GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets),
[API v1.6.0](https://github.com/ValveSoftware/GameNetworkingSockets/blob/2cb93a06350bb065db53abdb0d87cf297e0bfd34/include/steam/isteamnetworkingsockets.h).
