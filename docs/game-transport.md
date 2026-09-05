# UDP cho game: server và client cùng dùng DLL

Module dùng **Valve GameNetworkingSockets 1.6.0**, được pin qua vcpkg baseline.
GNS lo xác nhận/gửi lại, phân mảnh/ghép message, điều tiết tốc độ và mã hóa.
Engine cung cấp C ABI, quản lý endpoint/peer, giới hạn hàng đợi và event cho host.

| Chế độ | Hợp đồng |
| --- | --- |
| `SE_GAME_RELIABLE_ORDERED` | Tin cậy và đúng thứ tự trong luồng reliable của connection còn hoạt động |
| `SE_GAME_UNRELIABLE` | Có thể mất/lặp/đảo thứ tự; snapshot nên mang tick/sequence riêng |

Reliable không có nghĩa đã mua đồ hoặc lưu SQL thành công. Game vẫn kiểm tra
quyền, cập nhật trạng thái và xử lý gửi lại giao dịch sau reconnect.

## Bạn tự build

Developer PowerShell for VS 2022, tại gốc repo; `VCPKG_ROOT` trỏ tới vcpkg của
bạn như [hướng dẫn chạy examples](run-examples.md). Không dùng `cmake .`.

```powershell
# Vi du vcpkg di kem VS Community; doi neu may ban cai o noi khac.
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg'
cmake --preset vs2022-x64-game
cmake --build --preset vs2022-x64-game-debug
```

Preset này bật `SE_WITH_GAME_TRANSPORT=ON`. Tắt feature vẫn có export C ABI,
nhưng tạo endpoint trả `SE_NOT_SUPPORTED`. CMake luôn tạo target nội bộ và chỉ
thêm dependency GNS khi bật feature. ICE/P2P tắt; ví dụ dùng IP trực tiếp.

Mở hai terminal riêng, chạy server trước:

```powershell
.\out\build\vs2022-x64-game\Debug\ServerEngineGameTransportExample.exe --server 9010
```

```powershell
.\out\build\vs2022-x64-game\Debug\ServerEngineGameTransportExample.exe --client 9010
```

Kết quả mong đợi: client nhận `Reliable reply: PONG`, và có thể nhận echo các
snapshot. Client dừng sau bài thử; server chờ Ctrl+C. Đây là kết quả mong đợi,
chưa phải kết quả đã chạy xác nhận trong lượt triển khai này.

DLL nằm cạnh EXE. Preset static-md gộp dependency tĩnh; nếu tự chọn dependency
shared thì triển khai thêm các DLL tương ứng. Module có wire protocol riêng,
khác UDP thô 9001/TCP 9443. Browser hoặc client TCP cũ không tự dùng được nó.
Client native có thể gọi cùng DLL/SDK này; không cần lộ header GNS ra ứng dụng.

## Đọc source theo một đường

1. [Example](../examples/GameTransport/main.cpp): `server()` / `client()`.
2. [C++ wrapper](../include/ServerEngine/Cpp/GameTransport.h): `GameEndpoint::send/poll`.
3. [C ABI](../src/Abi/GameTransport.cpp): kiểm tra tham số và chuyển lời gọi.
4. [Runtime](../src/Net/Game/Runtime.cpp): registry, mutex, pump callback, vòng đời GNS.
5. [Endpoint](../src/Net/Game/Endpoint.cpp): listen/connect/send/disconnect.
6. [EndpointEvents](../src/Net/Game/EndpointEvents.cpp): nhận message, queue, overflow.

```cpp
endpoint.send(peer, GameDelivery::ReliableOrdered, "PING");
endpoint.send(peer, GameDelivery::Unreliable, "SNAP 42");
```

Đây là lời gọi C++ trong example; bên dưới vẫn là C ABI `se_game_send`.
Các `se_server_*` cũ không đổi. App dùng API mới cần DLL mới có `se_game_*`.

- Cả server và client phải poll thường xuyên; chờ CONNECTED trước khi gửi.
- GNS có worker I/O riêng; Runtime serialize API/callback bằng một mutex.
  Không gọi logic game trên worker. Poll chờ theo lát ngắn, nhả khóa khi chờ.
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
`SE_GAME_ALLOW_REMOTE_UNAUTHENTICATED` cho phép IP khác và chỉ dành cho mạng
được bảo vệ/tin cậy. Không gửi bí mật qua Internet không tin cậy chỉ vì có mã hóa.
Đăng nhập, quyền vào phòng và transport xác thực công khai còn cần hoàn thiện.

Module chưa có codec voice/WebRTC, TURN, matchmaking hoặc SFU. Các vấn đề
shutdown/SQL của GameServer mẫu cũ cũng không được sửa bởi module này.

## Kiểm thử bạn cần chạy

```powershell
ctest --test-dir out/build/vs2022-x64-game -C Debug -R GameTransport --output-on-failure
```

- `GameTransportCHeader`: header C, layout và liên kết export.
- `GameTransportContracts`: tham số, feature tắt, handle và RAII wrapper.
- `GameTransportLoopback`: connect, binary/copy buffer, thứ tự reliable,
  unreliable, disconnect và destroy đánh thức poll.
- `GameTransportLoss`: proxy UDP thật bỏ/lặp/đảo thứ tự gói; yêu cầu 128 message
  reliable 2 KiB đến đủ, đúng thứ tự, không trùng rồi nhận phản hồi hoàn tất.

Test mạng skip code 77 khi feature tắt. Phải kiểm tra không bị skip khi dùng
preset `-game`. Agent chỉ kiểm tra tĩnh, **không configure/build/chạy test**.
Chưa xác nhận module chạy được trên máy này, chưa có thử tải dài/WAN/security
audit; smoke checks cũ không chứng minh module mới sẵn sàng production.

Nguồn: [Valve GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets),
[API v1.6.0](https://github.com/ValveSoftware/GameNetworkingSockets/blob/2cb93a06350bb065db53abdb0d87cf297e0bfd34/include/steam/isteamnetworkingsockets.h).
