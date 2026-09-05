# Dùng ServerEngine.dll để viết server

Host của bạn là một chương trình game, web realtime hoặc công cụ riêng.
Host gọi C ABI trong `ServerEngine.dll`; DLL nhận/gửi qua mạng và đưa sự kiện
vào hàng đợi. Logic game, tài khoản và schema/query nằm trong host. Data module
trong DLL thực thi SQLite/Redis qua API riêng. Xem [Network + Data](network-and-data.md)
cho ví dụ HTTPS, truy vấn nền và WebRTC audio/video.

## Đọc năm nơi này trước

1. [C header](../include/ServerEngine/C/ServerEngine.h): toàn bộ API dùng từ ứng dụng.
2. [GameServer.cpp](../examples/GameServer/GameServer.cpp): cách gọi API thật.
3. [Lifecycle.cpp](../src/Abi/Lifecycle.cpp): create/start/stop đi vào engine.
4. [ServerHost.cpp](../src/Runtime/Host/ServerHost.cpp): ownership và hàng đợi sự kiện.
5. [TransportService.cpp](../src/Net/Async/TransportService.cpp): chọn TCP/UDP/WS.

Không cần include header Boost hoặc C++ engine để gọi DLL. File `.h` công khai
chỉ có số nguyên cố định, chuỗi UTF-8 đầu vào và buffer do host cấp.

## Vòng đời API

```text
se_get_abi_version() == SE_ABI_VERSION
    ↓
se_server_options_init → se_server_create
    ↓
se_listener_options_init → se_server_add_listener (một hoặc nhiều lần)
    ↓
se_server_start
    ↓
vòng lặp host:
    se_server_poll_event → logic của bạn → se_server_send / disconnect
    ↓
se_server_stop → se_server_destroy
```

| Hàm | Trách nhiệm |
| --- | --- |
| `se_server_create` | Tạo handle, giới hạn và hàng đợi; chưa mở port |
| `se_server_add_listener` | Sao chép cấu hình listener, trả listener ID |
| `se_server_start` | Mở tất cả listener; nếu lỗi bind/TLS, thu hồi những listener đã chuẩn bị |
| `se_server_poll_event` | Copy event và dữ liệu về buffer host; timeout=0 để poll trong game tick |
| `se_server_send` | Gửi một message tới session, kiểm tra giới hạn queue |
| `se_server_disconnect` | Ngắt TCP/WS hoặc xóa peer UDP logic |
| `se_server_stop` | Đóng tài nguyên và chờ I/O kết thúc; gọi lặp được |
| `se_server_destroy` | Vô hiệu hóa handle, stop nếu cần, giải phóng state DLL |

Stop là bước cuối của một handle. Muốn chạy lại, tạo handle mới. Những hàm
đang giữ handle qua một call có quyền sở hữu tạm để không dereference vùng
nhớ đã bị destroy bởi thread khác. Không tái sử dụng handle sau destroy/unload.

Luôn kiểm tra ABI version **trước initializer đầu tiên**. Layout v1 được đóng
băng; trường reserved phải bằng zero. Host và DLL phải cùng kiến trúc x64/x86.
Không truyền `std::string`, class, `FILE*` hoặc allocator qua ABI; không `free`
bộ nhớ DLL. Các hàm đều có mã trả về và `se_error` tùy chọn do host sở hữu.
[Lý do: Microsoft về ranh giới CRT/DLL](https://learn.microsoft.com/en-us/cpp/c-runtime-library/potential-errors-passing-crt-objects-across-dll-boundaries?view=msvc-170).

## Poll event đúng cách

Gọi `se_event_init` trước khi poll. Buffer payload có thể dùng lại sau khi xử lý
event. `SE_BUFFER_TOO_SMALL` trả kích thước trong `event.payload_size` và giữ
event trong queue; tăng buffer rồi poll lại. `SE_TIMEOUT` là chưa có event,
`SE_STOPPED` là đã dừng và hết sự kiện cần đọc.

`OPEN/MESSAGE/CLOSE` có `listener_id`, `session_id`, địa chỉ peer và `sequence`.
Payload của `MESSAGE` là byte, có thể chứa NUL; không dùng `strlen` để đo.
Payload của `ERROR` là UTF-8 không có NUL kết thúc. Đừng đọc event nếu status
là TIMEOUT, STOPPED hoặc lỗi âm. Một consumer poll cho mỗi handle dễ trace nhất.

Nếu queue đầy, engine từ chối event/send mới và phát một `OVERFLOW`, thay cho
lịch sử không còn đầy đủ. Khi poll lấy event đó, engine stop/join trước khi trả
về. Nếu host không poll nữa, tài nguyên vẫn được giữ có giới hạn tới khi poll,
stop/destroy hoặc peer hết timeout; không có thread gọi ngược logic host.
Ứng dụng phải bỏ state cũ và tạo server mới sau overflow để tránh player “ma”.

## Wire format

| Listener | Client cần gửi |
| --- | --- |
| TCP/TLS | 4 byte unsigned big-endian độ dài, tiếp theo chính xác N byte payload |
| WS/WSS | Một binary WebSocket message; Beast xử lý fragment, masking, control frame |
| UDP | Một datagram là một message; không thêm prefix TCP |
| HTTP/HTTPS | Request HTTP/1.1 chuẩn; dùng `Http.h` decode event và trả response |

`send` tự thêm prefix TCP. Đừng thêm prefix lần nữa từ host. Client TCP phải tự
đọc đủ 4 byte header rồi đủ payload; không giả định một recv = một message.
Ví dụ `PING` trên TCP có bytes `00 00 00 04 50 49 4E 47` trước khi TLS mã hóa.
Ví dụ browser dùng `socket.send(new TextEncoder().encode("PING"))`.

UDP không đảm bảo đến nơi, thứ tự hay chỉ một lần. Session UDP chỉ là ánh xạ
địa chỉ/port đã gửi datagram; không phải chứng nhận người dùng. `disconnect`
xóa ánh xạ, datagram sau có thể tạo session mới. Payload tối đa UDP là
`min(max_message_bytes, 65507)`; nên dùng packet nhỏ tránh IP fragmentation.
Giới hạn này không phải cam kết mọi đường mạng đều chuyển được datagram lớn.

## Build thủ công trên Windows

Cần VS2022 C++, CMake từ 3.21, Git và một checkout vcpkg có bootstrap. Đặt `VCPKG_ROOT`
đến checkout vcpkg của bạn. Manifest repo đã pin baseline; không tự tải/build
dependency trong phiên chỉnh sửa này.

```powershell
$env:VCPKG_ROOT = 'C:/dev/vcpkg' # đổi thành đường dẫn thực tế của bạn
cmake --preset vs2022-x64-dll
cmake --build --preset vs2022-x64-dll-debug
ctest --test-dir out/build/vs2022-x64-dll -C Debug --output-on-failure
```

Configure bằng preset DLL sẽ để vcpkg chuẩn bị Boost.Asio/Beast, OpenSSL và
SQLite theo manifest; bước này có thể cần thời gian. Không chạy `cmake .`.
Preset DLL dùng `x64-windows-static-md` (hoặc x86 tương ứng): dependency được
link static trong DLL, CRT của MSVC dùng `/MD`. Không trộn thư viện dependency
của triplet/cấu hình khác. Vẫn cần MSVC runtime phù hợp khi triển khai.

Output chính: `out/build/vs2022-x64-dll/Debug/ServerEngine.dll`, import library
trong `out/build/vs2022-x64-dll/lib/Debug/ServerEngine.lib`, và host mẫu
`out/build/vs2022-x64-dll/Debug/ServerEngineGameServer.exe`.

```powershell
cmake --install out/build/vs2022-x64-dll --config Debug --prefix out/sdk
```

SDK có `bin/ServerEngine.dll`, `lib/ServerEngine.lib` và các C header
`ServerEngine.h`, `Http.h`, `Sql.h`, `Redis.h` cùng wrapper C++ trong `include`.
Host dùng import library hoặc `LoadLibrary`/`GetProcAddress` với tên trong
[ServerEngine.def](../src/Abi/ServerEngine.def). Kiểm tra dependency thực tế bằng
`dumpbin /dependents` sau build. Nếu tự đổi sang triplet dynamic, cần triển khai
cả DLL OpenSSL/SQLite tương ứng; `.dll` không tự chứa dependency dynamic.

Các preset `vs2022-x64` / `vs2022-x86` cũ đặt `SE_BUILD_DLL=OFF`, nên vẫn dùng
được để đọc/build sample IOCP/threaded không cần bộ dependency mới.

## Chạy ví dụ

Tạo chứng chỉ dev theo [security.md](security.md), rồi chạy từ gốc repo:

```powershell
.\out\build\vs2022-x64-dll\Debug\ServerEngineGameServer.exe certs/server-cert.pem certs/server-key.pem
```

Host mẫu mở loopback: TCP/TLS 9443, UDP plaintext 9001, WSS 9444 path `/game`.
Client web là [examples/WebClient/index.html](../examples/WebClient/index.html).
Browser phải tin cậy certificate dev và hostname trước khi WSS kết nối.

`PING → PONG`, `NAME Thanh` đổi tên hiển thị trong RAM, `STATS` trả số message
do server đếm; `HISTORY` đọc lại số message từ SQLite qua query nền.
Data module ghi lịch sử connection TCP/WS bằng prepared statements. Các bảng
mới `engine_runs`/`engine_connections` giữ session ID riêng giữa các lần chạy,
không sửa/xóa bảng `connections` cũ. UDP chỉ echo/PING, không gọi database.
Đây là ví dụ tổ chức logic, chưa có xác thực account, game rules hoặc chống gian lận.

Chủ repo đã build DLL/hai host x64 Debug ngày 05/09/2026. Một lượt smoke check
HTTPS/SQLite và TCP/TLS/WSS đã chạy thành công; chưa chạy toàn bộ CTest hoặc
kiểm chứng browser/tải. Chi tiết ở [testing.md](testing.md).
