# Học ServerEngine qua tám buổi đọc và trace

Mục tiêu là tự trả lời được: **yêu cầu đi qua đâu, ai giữ dữ liệu, chạy trên
thread nào, và phải sửa file nào khi đổi một hành vi**. Mỗi buổi khoảng
45–90 phút; đọc một đường đi nhỏ rồi tự giải thích lại trước khi mở rộng.

Bạn chưa cần đọc mã nguồn Boost/OpenSSL. Trước hết xem chúng nhận gì và trả gì
qua các lớp của repo. Bài tập bên dưới dành cho bạn thực hiện trên bản local;
đọc tài liệu không tự chạy server, build hoặc test.

## Buổi 1 — Phân biệt ứng dụng, DLL và client

Đọc [cách chạy ví dụ](run-examples.md), rồi mở:

- [GameServer/main.cpp](../examples/GameServer/main.cpp): `wmain()` đọc đường dẫn
  certificate/key, tạo `GameServer` và chờ Ctrl+C.
- [WebServer/main.cpp](../examples/WebServer/main.cpp): cùng cách khởi động host web.
- [GameServer.cpp](../examples/GameServer/GameServer.cpp): constructor mở listener;
  `run()` là vòng lặp lấy sự kiện và xử lý logic.
- [bản đồ Network + Data](network-and-data.md): hai module trong một DLL công khai.

```text
Browser/app client → mạng → GameServer.exe hoặc WebServer.exe
                                ↕ gọi hàm trong cùng process
                            ServerEngine.dll
```

DLL không phải chương trình chạy độc lập. Console chạy server; browser là một
client. Hai launcher `.cmd` truyền tham số cho host và giữ console hiện ra.

**Bài tập:** tìm ba port của GameServer và ba port của WebServer trong constructor.
Ghi mỗi port thuộc giao thức nào. **Tự kiểm tra:** đóng browser có dừng server
không? Vì sao nhấp đúp EXE thiếu tham số lại in `Usage` rồi thoát?

## Buổi 2 — Hiểu `.h`, `.lib`, `.dll` và C ABI

| Nơi đọc | Điều cần tìm |
| --- | --- |
| [ServerEngine.h](../include/ServerEngine/C/ServerEngine.h) | `SE_API`, `SE_CALL`, handle, struct, mã kết quả |
| [Lifecycle.cpp](../src/Abi/Lifecycle.cpp) | Thân hàm `se_server_create/start/stop/destroy` |
| [Boundary.h](../src/Abi/Boundary.h) | `protect()` chặn exception đi ra C ABI |
| [ServerEngine.def](../src/Abi/ServerEngine.def) | Tên hàm DLL công khai |
| [Abi/CMakeLists.txt](../src/Abi/CMakeLists.txt) | Target `SHARED`, tên output và gói SDK |
| [Cpp/Engine.h](../include/ServerEngine/Cpp/Engine.h) | Wrapper `Network`, `Sql`, `SqlResult` |

Header `.h` khai báo hợp đồng. `ServerEngine.lib` đi cùng DLL là **import library**
cho linker; DLL chứa phần triển khai được nạp lúc chạy. Các `.lib` nội bộ kiểu
`STATIC` được linker gộp vào sản phẩm cuối. Cùng đuôi `.lib`, hai vai trò khác nhau.

`Engine.h` được compile vào ứng dụng. Class C++ trong wrapper gọi C ABI;
`std::string`/`std::vector` của wrapper không được truyền nguyên object sang DLL.
Kiểm tra `se_get_abi_version()` trước initializer Network/HTTP/Redis và
`se_sql_get_abi_version()` trước initializer SQL. Host và DLL phải cùng kiến trúc.

**Bài tập:** đối chiếu khai báo `se_server_create` với phần triển khai và nơi
GameServer gọi nó. **Breakpoint:** `se_server_create`, `se_server_add_listener`,
`se_server_start`. Nếu debugger chưa vào DLL, kiểm tra DLL/PDB thực sự được nạp.
**Tự kiểm tra:** vì sao consumer không cần include `sqlite3.h` hoặc header Boost?

## Buổi 3 — Theo một lệnh game `PING → PONG`

Bắt đầu từ [WebClient/index.html](../examples/WebClient/index.html): nút Gửi dùng
`TextEncoder` rồi `WebSocket.send()` với bytes, tạo binary message.

Đường nhận WSS trong DLL:

```text
WebSocketConnection::read_message
  → ConnectionState::publish_message → WorkerContext::notify_message
  → ServerHost::callbacks (on_message) → EventQueue::push
```

Đọc [WebSocketConnection.h](../src/Net/Async/WebSocketConnection.h),
[ConnectionState.cpp](../src/Net/Async/ConnectionState.cpp),
[WorkerContext.cpp](../src/Net/Async/WorkerContext.cpp),
[ServerHost.cpp](../src/Runtime/Host/ServerHost.cpp) và
[EventQueue.cpp](../src/Runtime/Host/EventQueue.cpp) theo thứ tự này.

Đường ứng dụng lấy event rồi trả lời:

```text
GameServer::run → se_server_poll_event → EventQueue::poll
  → GameServer::handle_event → handle_message("PING") → reply("PONG")
  → se_server_send → ServerHost::send → TransportService::Impl::send
  → WebSocketConnection::send → write_next
```

Các export gửi/poll nằm ở [Messaging.cpp](../src/Abi/Messaging.cpp); việc chuyển
lệnh tới worker nằm ở [TransportService.cpp](../src/Net/Async/TransportService.cpp).
Callback I/O chỉ đưa dữ liệu vào queue; `handle_message()` chạy trong vòng lặp host.

**Breakpoint:** `GameServer::handle_message`, `GameServer::reply`,
`se_server_send`; sau đó thêm breakpoint tại callback của `read_message()`.
Quan sát `session_id`, `payload_size`, nội dung bytes và thread hiện tại.

**Bài tập:** thêm lệnh `HELLO → HELLO FROM GAME` chỉ trong `handle_message()`.
Trước khi sửa, dự đoán có cần đổi public ABI không. **Tự kiểm tra:** `PING` còn
gọi `save_message_count()`; nhận `PONG` có chứng minh SQL đã commit chưa? Chưa:
ghi database được submit nền, kết quả được xử lý ở `poll_database()`.

## Buổi 4 — Theo `HTTP → SQL → JSON`

Trong [WebMediaClient/app.js](../examples/WebMediaClient/app.js), nút profile gọi
`fetch('/api/profile')`. Theo tiếp các bước sau:

1. [HttpConnection.h](../src/Net/Async/HttpConnection.h): `read_request()` và
   `dispatch_request()` parse HTTP, tạo request ID và đưa request vào event queue.
2. [WebServer.cpp](../examples/WebServer/WebServer.cpp): `run()` → `handle()` →
   `route()`. `se_http_request_read()` lấy method/target/body qua offset.
3. [ProfileStore.cpp](../examples/WebServer/ProfileStore.cpp): `load_sample()`
   tạo tham số `id=1` và câu `SELECT ... WHERE id=?`.
4. Wrapper `Sql::query()` → `submit()` → export
   [se_sql_submit](../src/Abi/Sql.cpp), rồi
   [Service::submit](../src/Data/Sql/Service.cpp) copy request vào queue.
5. SQL worker `Service::run()` gọi
   [SqliteDriver::execute](../src/Data/Sql/SqliteDriver.cpp) và
   [execute_statement](../src/Data/Sql/SqliteStatement.cpp): bind, step, đọc kết quả,
   commit hoặc rollback.
6. `WebServer::poll_profiles()` lấy completion, kiểm tra `result.metadata.status`,
   đọc cột, tạo JSON rồi gọi `respond()` →
   [se_http_respond](../src/Abi/Http.cpp) → `HttpConnection::respond_http()`.

`session_id` nhận diện connection; `http_request` nhận diện lần yêu cầu HTTP;
`sql_request` nhận diện truy vấn. `pending_` nối ba ID đó. Không trả kết quả chỉ
bằng session ID: một connection keepalive có thể đã chuyển sang request khác.

**Breakpoint:** `WebServer::route`, `ProfileStore::load_sample`,
`Service::run` trong namespace SQL, `SqliteDriver::execute`,
`WebServer::poll_profiles`, `se_http_respond`.

**Bài tập:** vẽ lại ánh xạ ba ID từ log. Sau đó đề xuất một endpoint đọc riêng
`level`, chỉ rõ SQL nằm ở đâu và JSON nằm ở đâu. **Tự kiểm tra:** vì sao
`se_sql_poll()` trả `SE_OK` mà truy vấn vẫn có thể thất bại?

## Buổi 5 — Thread, quyền sở hữu và dừng an toàn

Đọc [ServerHost.h](../src/Runtime/Host/ServerHost.h), `stop_locked()` trong
TransportService và `stop()` trong SQL Service. Hiện mỗi Network handle có
một I/O worker; mỗi SQL service có một worker/connection riêng. Thread host
poll kết quả, giữ state game và thực thi luật ứng dụng.

| Dữ liệu/tài nguyên | Ai sở hữu? |
| --- | --- |
| Socket, TLS context, buffer I/O đang chạy | Connection/listener trong Network |
| Event chờ host lấy | `EventQueue` trong DLL |
| Buffer truyền vào `se_server_poll_event` | Host; DLL copy event vào đó |
| `string_view` nhận từ buffer poll | Chỉ là vùng nhìn; không sở hữu bytes |
| SQL statements/parameters đã submit thành công | SQL service giữ bản copy |
| SQL result đã poll | DLL giữ tới `release_result` hoặc destroy |
| `SqlResult` của wrapper | RAII: destructor gọi release; giữ service còn sống |

`shared_ptr` giữ object còn sống qua callback/call đang xử lý; không tự làm
mọi thao tác trên object an toàn giữa nhiều thread. `mutex` bảo vệ state dùng
chung. `std::move` chuyển tài nguyên; sau move phải xem lại object nào còn sở hữu.

Đọc [GameServer.h](../examples/GameServer/GameServer.h): `EngineHandle` tự destroy.
Đọc destructor GameServer: dừng Network, ghi trạng thái đóng, drain SQL rồi stop.
`se_server_stop` là terminal; muốn chạy lại tạo handle mới. Không unload DLL
khi còn worker, handle hoặc lời gọi đang chạy.

**Bài tập:** giải thích ai sở hữu buffer trong từng mũi tên của buổi 3.
Đánh dấu nơi copy dữ liệu trước khi tái sử dụng buffer poll. **Breakpoint:**
`ServerHost::stop`, `TransportService::Impl::stop_locked`, `Service::stop`,
`se_sql_release_result`. **Tự kiểm tra:** quên release SQL result có thể khiến
submit mới trả `SE_BACKPRESSURE` dù không còn query đang chạy như thế nào?

## Buổi 6 — Chọn giao thức, TLS và đường media

| Nhu cầu | Đọc implementation | Điều phải nhớ |
| --- | --- | --- |
| App/game binary TCP | [TcpConnection.h](../src/Net/Async/TcpConnection.h) | 4 byte big-endian độ dài trước payload |
| Datagram | [UdpListener.cpp](../src/Net/Async/UdpListener.cpp) | Có thể mất/lặp/đảo thứ tự; peer chưa xác thực |
| Browser realtime | WebSocketConnection | WS message binary; WSS có TLS |
| API/trang web | HttpConnection | HTTP/1.1, request/response có cap, routing ở host |
| TLS | [TlsContext.cpp](../src/Net/Async/TlsContext.cpp) | `make_tls_context()`, certificate và hostname/trust |

Mở `https://localhost:9553/` để browser tải trang. `wss://localhost:9444/game`
là endpoint cho JavaScript `new WebSocket(...)`, không phải URL một trang HTML.
WSS dùng HTTP upgrade lúc bắt tay rồi trao đổi WebSocket message.

ECC trong TLS thỏa thuận khóa/xác thực bằng EC; payload được mã hóa bằng AEAD.
TLS xác thực server không tự cấp tài khoản hoặc quyền truy cập cho người chơi.
Đọc [security.md](security.md), nhất là phần UDP và certificate development.

Đọc [SignalRoom.cpp](../examples/WebServer/SignalRoom.cpp) và `Call.start/receive`
trong app.js: DLL chuyển SDP/ICE qua WSS; audio/video đi bằng WebRTC của browser.
Network không tự biến bytes thành hình/âm thanh. HTTP hiện buffer body, chưa
có streaming file/Range/HLS packager. Xem [audio-video.md](audio-video.md).

**Bài tập:** vẽ hai đường signaling và media. **Tự kiểm tra:** vì sao gửi một
file MP4 bằng `send()` chưa tạo thành hệ thống livestream? Với UDP, điều gì
xảy ra nếu packet bị mất? Không dùng plaintext UDP/Redis để gửi bí mật.

## Buổi 7 — Đọc đường C++ cũ và IOCP sau cùng

Repo giữ hai đường chạy. Chọn đúng đường trước khi đặt breakpoint:

| Đường | Entry point và transport |
| --- | --- |
| DLL đang dùng bởi Game/Web | `examples/*/main.cpp` → C ABI → `src/Net/Async/` |
| Echo C++ tương thích cũ | [ServerEngine.cpp](../ServerEngine.cpp) → [app::run](../apps/EchoServer/Application.cpp) → [runtime::Server](../src/Runtime/Server.cpp) |

Echo cũ dùng `AUTH/WHO/echo` theo dòng; không dùng framing 4 byte của DLL.
Đọc [EchoHandler::on_message](../apps/EchoServer/EchoHandler.cpp),
[runtime::detail::TcpListener::on_message](../src/Runtime/TcpListener.cpp), rồi
[threaded::ClientConnection::receive_messages](../src/Net/Threaded/ClientConnection.cpp).
Sau đó đọc [IOCP Server::accept_loop](../src/Net/Iocp/ServerAccept.cpp) và
[Server::worker_loop/deliver_received](../src/Net/Iocp/ServerIo.cpp).

**Bài tập:** so sánh thread chờ `recv` với thread nhận completion IOCP.
**Tự kiểm tra:** đổi backend trong cấu hình Echo cũ có đổi backend DLL mới
không? Không; đây là hai đường triển khai riêng. Xem [architecture.md](architecture.md).

## Buổi 8 — Tự sửa một hành vi và chứng minh hiểu nó

Chọn một bài nhỏ: lệnh game mới, endpoint HTTP mới hoặc query có tham số mới.
Ghi trước input, output, nơi sửa, trường hợp lỗi và dữ liệu cần giữ qua callback.
Chỉ đổi public ABI khi hợp đồng dùng chung cần đổi; logic game thường không cần.

Đọc [tests/CMakeLists.txt](../tests/CMakeLists.txt) và [testing.md](testing.md).
Tìm test gần thay đổi: `HttpLoopback`, `TransportLoopback`, `SqlService`,
`EventQueue`, `AbiContracts`. Đọc điều kiện kiểm tra trước khi đọc implementation.
Build và chạy kiểm chứng do bạn thực hiện theo [quickstart](dll-quickstart.md).

Khi cần hiểu lớp phụ trợ, tra [Core/Buffer.cpp](../src/Core/Buffer.cpp),
[Port/Socket.cpp](../src/Port/Socket.cpp), [Data guide](data-services.md) và
[CMakePresets.json](../CMakePresets.json). Không cần đọc toàn bộ dependency.

**Checkpoint cuối:** tự trace được PING và HTTP→SQL; giải thích buffer/handle
lifetime; tìm đúng nhánh lỗi; phân biệt gửi thành công với remote nhận/SQL commit;
biết dừng server và ghi lại kết quả kiểm tra thay vì chỉ nhìn thấy file EXE.

Trạng thái đã ghi nhận trên Windows x64 Debug: build DLL/hai host và smoke check
HTTPS/SQLite, TCP/TLS/WSS trên loopback. Chưa có bằng chứng toàn bộ CTest, tải dài,
giao diện browser/camera hoặc production. Linux/macOS chưa được build/chạy xác
nhận; có mã đa nền tảng không đồng nghĩa mọi target đã hoạt động trên các OS đó.
