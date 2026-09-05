# ServerEngine

Thư viện server C++17 có **C ABI để xuất `ServerEngine.dll`**. Host game/web tự
giữ logic và gọi DLL để mở listener, poll sự kiện, gửi dữ liệu, ngắt session.
Source chia thành **Network** và **Data**, cùng liên kết vào một DLL công khai.
Muốn hiểu toàn bộ source theo từng luồng chạy, bắt đầu ở
**[lộ trình đọc code và thực hành](docs/learning-path.md)**.
Muốn mở ví dụ để thử ngay sau build, đọc [cách chạy từng bước](docs/run-examples.md).
Đọc [hướng dẫn Network + Data](docs/network-and-data.md) để bắt đầu từ ví dụ
HTTPS → SQL → JSON, hoặc [audio/video](docs/audio-video.md) để hiểu stream.

| Chức năng của DLL | Hiện trạng source |
| --- | --- |
| TCP binary | Frame độ dài 4 byte big-endian, hỗ trợ TLS 1.3 |
| UDP | Datagram, peer theo địa chỉ/port, plaintext phải chọn rõ |
| Datagram Transport (tùy chọn) | GNS UDP reliable/unreliable dùng chung cho client/server; mã hóa, mặc định loopback, chưa xác minh peer |
| WebSocket | Message binary, WS hoặc WSS/TLS 1.3, path cấu hình |
| HTTP/HTTPS | HTTP/1.1 request/response, keepalive, binary body có giới hạn; route ở host |
| ECC | ECDHE và chứng chỉ EC trong TLS; AEAD do OpenSSL thực hiện |
| Data / SQL | SQLite worker riêng, atomic batch, typed parameters/results, C ABI độc lập |
| Data / NoSQL | Redis GET/SET/DELETE + TTL khi bật `SE_WITH_REDIS`; plaintext opt-in, chưa TLS |
| Media mẫu | Browser WebRTC hai người, DLL chuyển WSS signaling; chưa SFU/codec/packager |
| MySQL/Kafka, DTLS/QUIC, HTTP/2/3, cluster | Chưa triển khai; không có API giả báo thành công |

Đọc **[DLL quickstart](docs/dll-quickstart.md)** trước nếu muốn xây ứng dụng mới.
Có [host game mẫu](examples/GameServer/main.cpp) chỉ dùng C ABI và
[client trình duyệt](examples/WebClient/index.html) dùng WSS.
Có thêm [host web/media](examples/WebServer/main.cpp) với C++ wrapper trên C ABI.
Để dùng **UDP tin cậy/không tin cậy và client native**, đọc
[Datagram Transport](docs/datagram-transport.md). Preset `vs2022-x64-datagram` bật module này;
source mới chưa được build/chạy kiểm chứng trong lượt triển khai.

Luồng chính mới:
`Host → C ABI → ServerHost → TransportService → TCP / UDP / WebSocket / HTTP`.

Với datagram reliable/unreliable, đường gọi riêng là
`Ứng dụng → se_datagram_* → Net/Transports/Gns → UDP`.
Logic game/web nằm trong ứng dụng; các phần network được chia theo cách truyền.
Data đi theo đường độc lập: `Host → C ABI → SQL/Redis service → driver`.
Boost.Asio/Beast và OpenSSL đảm nhiệm giao thức mạng/TLS đã có chuẩn; code riêng
giữ việc quản lý vòng đời, giới hạn tài nguyên và chuyển sự kiện đến host.

Phần C++ `runtime::Server` và ứng dụng `AUTH / WHO / echo` bên dưới được giữ
để tương thích và học IOCP/threaded. Chúng dùng TCP theo dòng, **khác wire format
của DLL mới**. Các preset cũ chỉ build phần này; preset có hậu tố `-dll` build SDK.

Mục tiêu của cấu trúc này: nhìn tên module biết nó làm gì, tìm được nơi cần sửa,
và biết đối tượng nào chịu trách nhiệm giải phóng tài nguyên.

## Đọc phần C++ tương thích cũ

1. [ServerEngine.cpp](ServerEngine.cpp): điểm vào chương trình, gọi `app::run()`.
2. [Application.cpp](apps/EchoServer/Application.cpp): đọc cấu hình, tạo logger,
   handler và server, chờ Ctrl+C, dừng server.
3. [EchoHandler.cpp](apps/EchoServer/EchoHandler.cpp): logic dễ thử nhất;
   nhận một message và trả về `AUTH OK`, `WHO...` hoặc `echo: ...`.
4. [Server.cpp](src/Runtime/Server.cpp): khởi động listener và gọi handler.
5. [TcpListener.cpp](src/Runtime/TcpListener.cpp): chuyển sự kiện TCP thành session.
6. [Hướng dẫn kiến trúc](docs/architecture.md): đi tiếp vào socket, thread và IOCP.

Nếu mới học networking, đọc backend `src/Net/Threaded/` trước IOCP.

## Bản đồ thư mục

| Nơi | Trách nhiệm |
| --- | --- |
| `apps/EchoServer/` | Cấu hình và logic của ứng dụng mẫu |
| `include/ServerEngine/` | API để dự án khác sử dụng engine |
| `src/Runtime/` | Vòng đời server, session, chuyển callback đến ứng dụng |
| `src/Net/` | Kết nối TCP, I/O, phân tách message |
| `src/Data/` | SQLite/Redis worker, request/result, driver và giới hạn tài nguyên |
| `src/Port/` | Khác biệt Windows/POSIX, socket system, clock, process |
| `src/Core/` | Buffer, logger, bộ đọc cấu hình |
| `src/Security/` | So sánh token và kiểm tra ECC provider |
| `tests/` | Test độc lập và client TCP có sẵn |
| `config/` | Cấu hình khi chạy |
| `cmake/` | Chính sách compiler dùng chung |

## Build và chạy thủ công

Mở terminal tại thư mục gốc repo. CMake sinh project Visual Studio vào `out/`.

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-debug
.\out\build\vs2022-x64\Debug\ServerEngine.exe
```

Chạy từ gốc repo để đường dẫn tương đối `config/serverengine.ini` và `logs/`
được giải quyết đúng. Debugger Visual Studio cũng được đặt working directory
về gốc repo. Các preset x86 và Release vẫn có trong `CMakePresets.json`.

Ở terminal thứ hai:

```powershell
.\out\build\vs2022-x64\Debug\ServerEngineTcpTest.exe
```

Chạy các test không cần server đang mở, sau khi bạn đã build:

```powershell
ctest --test-dir out/build/vs2022-x64 -C Debug --output-on-failure
```

Chi tiết ca kiểm tra: [testing.md](docs/testing.md).
DLL/hai host x64 Debug đã được chủ repo build ngày 05/09/2026 và đã thử các
luồng HTTPS/SQLite, TCP/TLS/WSS cơ bản. Chưa chạy toàn bộ CTest hoặc kiểm thử tải.

## Phạm vi của ứng dụng C++ tương thích cũ

Ứng dụng `ServerEngine.exe` cũ là nền tảng TCP để học. Token được gửi dưới dạng
văn bản thuần; `security.mode` không bật TLS. ECC handshake, UDP, WebSocket,
database và clustering chưa được triển khai. `ecc_p256_provider_check` chỉ
kiểm tra khả năng của hệ điều hành. Các hạn chế về thread, send queue và kiểm
thử tải được ghi rõ trong tài liệu kiến trúc.

## Học từ dự án lớn

Envoy mô tả listener, codec và xử lý request thành những trách nhiệm riêng.
Ở repo này, nguyên tắc đó được áp dụng thành transport → framer → runtime →
handler, với ít tầng hơn phù hợp phạm vi hiện tại.
[Nguồn: Envoy, Life of a Request](https://www.envoyproxy.io/docs/envoy/latest/intro/life_of_a_request.html).

Google C++ Style Guide nhấn mạnh header tự đủ dependency và chủ sở hữu tài
nguyên rõ ràng. Repo áp dụng những nguyên tắc này, đồng thời giữ C++17 và quy
ước tên có sẵn.
[Nguồn: Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html#Ownership_and_Smart_Pointers).
