# Kiểm tra sau refactor

## DLL đa giao thức

Build bằng preset `vs2022-x64-dll` theo [quickstart](dll-quickstart.md), rồi:

```powershell
ctest --test-dir out/build/vs2022-x64-dll -C Debug --output-on-failure
```

| Test mới | Phạm vi |
| --- | --- |
| `AbiCHeader` | Compile public header bằng C, tạo/hủy handle qua DLL |
| `AbiContracts` | Version/size/reserved, handle cũ, lifecycle, từ chối TLS thiếu cert/UDP |
| `EventQueue` | Thăm dò buffer nhỏ không mất event, FIFO, count/byte cap, overflow, stop đánh thức poller |
| `TlsSecurity` | TLS1.3 thật bằng memory BIO, EC cert, CA/hostname verification, từ chối TLS1.2/key lỗi, Unicode PEM path |
| `TransportLoopback` | C ABI + client TCP/UDP/WS thật trên loopback, binary, framing chia/gộp, UDP rỗng, WS header/frame pipelined, rollback bind, stop |
| `Credentials` | So sánh credential nhị phân; regression chiều dài lệch 256/512 NUL bytes |
| `HttpLoopback` | HTTP/HTTPS, binary/unaligned envelope, keepalive/stale request, HEAD/204, chunked, lỗi headers/body/Expect, timeout |
| `SqlService` | Typed values, copy tham số, batch rollback, result cap/release, cancellation, handle sai, MySQL unsupported |
| `CppSdkOwnership` | Result giữ service sống qua vòng đời wrapper, move/reset không dereference null |
| `RedisContracts` | ABI, fail-closed TLS, feature-disabled, giới hạn và stale handle |
| `RedisLoopback` | Scripted RESP peer, binary+TTL, missing/empty, cap, oversized header, timeout/stop, ambiguous write |
| `SignalRoom` | Hai peer, chặn peer thứ ba, heartbeat, không chuyển signaling cũ sang pairing mới |

`RedisLoopback` báo CTest **Skipped** khi chưa bật `SE_WITH_REDIS`; đây không
phải bằng chứng connector chạy được. Test enabled dùng scripted peer, không
thay thế kiểm thử với Redis thật. WebRTC source cần kiểm tra hai browser, quyền
camera/mic, trust certificate và đường ICE/TURN; không có test browser tự động.

Loopback test tự mở listener, không cần server ngoài. Nó chọn port tạm rồi
nhả trước bind nên vẫn có khả năng tranh port với tiến trình khác; CTest đặt
RUN_SERIAL và timeout45s để test lỗi không treo vô hạn. Chạy cả x64/x86,
Debug/Release khi xác nhận ABI. Kiểm tra thêm `dumpbin /exports` và
`dumpbin /dependents` trên DLL đã build để xác nhận tên/hệ phụ thuộc thực tế.

Chưa có kết quả chạy toàn bộ các test DLL bằng CTest. TLS memory BIO không
thay thế test TCP/TLS/WSS trên mạng; mở browser WSS và chạy client tin cậy CA,
sai hostname, client đọc chậm, reconnect và stop khi còn I/O trước khi triển khai.

## C++ sample tương thích cũ

Sample Echo nằm trong [examples/EchoServer](../examples/EchoServer/main.cpp),
tự cung cấp `EchoHandler` cho runtime. Việc chuyển thư mục không đổi target
`ServerEngine`, file `ServerEngine.exe` hay các lệnh kiểm tra bên dưới.

Các test C++ sau được đăng ký với CTest, không dùng framework ngoài và không
cần server đang chạy. Chúng dùng điều kiện kiểm tra có exception thay vì
`assert`, nên vẫn kiểm tra trong cấu hình Release.

| Test | Hợp đồng được kiểm tra |
| --- | --- |
| `LineFramer` | TCP chia/gộp dữ liệu, CRLF, dòng rỗng, byte NUL, biên giới hạn, dòng quá dài |
| `EchoApp` | Cấu hình mặc định/fallback; AUTH sai/thiếu/đúng; BOM; chặn WHO/echo khi chưa đăng nhập; response cũ |
| `RuntimeSessions` | ID chung, cách ly xác thực, cấp lại slot, cap bằng 0, admission đồng thời |
| `IocpCallbacks` (Windows) | Hoãn đóng đến cuối callback; từ chối callback sau đóng; race đóng/kết thúc chỉ phát một thông báo |

`IocpCallbacks` kiểm tra state nội bộ với socket không hợp lệ; không mở socket
hay completion port. Test này không thay cho kiểm tra I/O Windows thực tế.

## Bạn chạy thủ công

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-debug
ctest --test-dir out/build/vs2022-x64 -C Debug --output-on-failure
```

Không dùng `cmake .`; repo chặn build ngay trong thư mục source.

## Kiểm tra TCP thật

Trong `config/serverengine.ini`, lần lượt chọn `tcp_backend=threaded` và
`tcp_backend=iocp` (IOCP trên Windows). Mỗi lần đổi cần dừng server rồi chạy lại.

Tại gốc repo, terminal thứ nhất:

```powershell
.\out\build\vs2022-x64\Debug\ServerEngine.exe
```

Terminal thứ hai:

```powershell
.\out\build\vs2022-x64\Debug\ServerEngineTcpTest.exe 127.0.0.1 8080 "HELLO"
```

Client nhận greeting, đăng nhập, kiểm tra echo và lấy WHO. Client này vẫn dùng
user/token mặc định; có thể truyền thêm user rồi token theo thứ tự đối số.
Nó cần server chạy riêng nên không được thêm vào CTest tự động.

## Những ca cần kiểm tra thêm với backend thật

- Hai client đồng thời; một client đăng nhập không làm client kia được xác thực.
- Hai listener khác port qua `ServerOptions`, mỗi listener nhận client đầu tiên:
  hub phải có hai ID khác nhau.
- Gửi một dòng qua nhiều lần send; gửi nhiều dòng trong một lần send.
- Dòng đúng giới hạn được nhận; dòng vượt giới hạn bị ngắt.
- Client ngừng đọc trong khi server gửi nhiều dữ liệu; ghi nhận queue/memory.
- Ctrl+C khi có client đang chờ recv hoặc đang gửi; tiến trình phải dừng và
  giải phóng port; chạy lại server để xác nhận.
- Lặp connect/disconnect và quan sát thread/handle count; test hiện tại chưa
  đo rò rỉ, stress, thứ tự completion và các nhánh send từng phần của IOCP.

## Module Datagram Transport mới

Xem [Datagram Transport](datagram-transport.md) để build/test riêng server/client UDP
tin cậy/không tin cậy. Các test có tiền tố `DatagramTransport`, gồm proxy gây mất,
lặp và đảo thứ tự datagram. Đây là source test mới, chưa chạy xác nhận. Phần
smoke check dưới đây được thực hiện trước module này và không kiểm chứng nó.

## Trạng thái kiểm chứng

Ngày 05/09/2026, chủ repo đã build DLL, GameServer và WebServer x64 Debug.
Agent không configure/build. Sau đó agent đã chạy hai host với database thử
riêng, dùng Node.js xác minh chứng chỉ dev và hostname để kiểm tra:

- HTTPS `/health` trả 200, `/api/profile` trả profile từ SQLite.
- HTTPS `/api/echo` trả đúng 16 KiB binary đã gửi.
- Game TCP/TLS 1.3: `PING` trả `PONG`, `HISTORY` trả `stored_messages=2`.
- Game WSS `/game`: `PING` trả `PONG`.
- Web WSS `/signal`: client đầu tiên nhận `WAIT`.

Kết quả được lưu tại `out/startup-smoke-9c0bc767/startup-results.json` trên máy
thử. Client chỉ tin chứng chỉ dev qua cấu hình riêng; không thay đổi kho trust
của Windows/browser. Hai tiến trình thử đã được dừng sau kiểm tra.

Đây là smoke check khởi động và trao đổi dữ liệu. Chưa chạy toàn bộ CTest,
browser/camera/mic, UDP trong lượt này, stress/soak hoặc xác nhận shutdown
graceful bằng exit code; chưa phải bằng chứng sẵn sàng production.
