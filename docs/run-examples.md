# Chạy thử web và game trên máy của bạn

**Có hai cửa sổ khác nhau:** cửa sổ console chạy server và cửa sổ browser chạy
client. Giữ console mở trong lúc thử; browser gửi yêu cầu tới server đó.
Demo game hiện là gửi lệnh và nhận phản hồi, chưa có nhân vật hay đồ họa game.

Trong lần kiểm tra Windows x64 Debug ngày 05/09/2026, DLL và hai host đã được
build thành công. Đã chạy thử HTTPS/SQLite, binary echo, game TCP/TLS và WSS
bằng client Node.js xác minh chứng chỉ/hostname; chưa kiểm tra giao diện
browser, camera/mic hoặc chạy toàn bộ CTest. Binary, database thử và chứng chỉ
local không được đưa lên Git; bản clone mới cần làm bước 1.

**Nếu đã có binary và chứng chỉ, bắt đầu từ bước 2 hoặc 3 bên dưới.** Nhấp đúp `.exe`
trực tiếp sẽ thiếu tham số certificate/key: chương trình in `Usage` rồi thoát
với mã 2. Dùng hai file `.cmd` ở gốc repo để truyền đúng tham số và giữ cửa sổ.

## 1. Chuẩn bị một lần trên máy mới

Mở **Developer PowerShell for VS 2022**, chạy các lệnh sau để tự build:

```powershell
# Thay bang thu muc ban da clone repo.
Set-Location 'C:\dev\Server-engine'
# Vi du vcpkg di kem Visual Studio; doi neu may ban cai o noi khac.
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg'
cmake --preset vs2022-x64-dll
cmake --build --preset vs2022-x64-dll-debug
```

Đường dẫn vcpkg phía trên là cấu hình đã dùng khi kiểm tra. Đặt `VCPKG_ROOT`
đúng nơi có `scripts/buildsystems/vcpkg.cmake` trên máy của bạn; lệnh gán chỉ
sửa trong cửa sổ PowerShell đang mở.
Configure có thể tải và chuẩn bị dependency, nên lần đầu có thể lâu.
Đây là lệnh dành cho bạn chạy thủ công; launcher không tự build/configure.

Sau build, ba file cần thiết nằm cùng thư mục:

```text
out/build/vs2022-x64-dll/Debug/
  ServerEngine.dll
  ServerEngineWebServer.exe
  ServerEngineGameServer.exe
```

Tạo cặp chứng chỉ development bằng OpenSSL 3 theo
[hướng dẫn certificate](security.md#certificate-dev-để-bạn-tự-chạy), để có:

```text
certs/server-cert.pem
certs/server-key.pem
```

Nếu OpenSSL có trong Git for Windows nhưng chưa ở PATH, có thể dùng đường dẫn
đầy đủ như ví dụ sau; đổi đường dẫn nếu cần. Nếu chưa có hai file PEM,
chạy từ gốc repo bằng đường dẫn đầy đủ sau (không chạy lại để ghi đè key đang dùng):

```powershell
New-Item -ItemType Directory -Force certs
& 'C:\Program Files\Git\usr\bin\openssl.exe' req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -sha256 -noenc -days 30 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" -addext "basicConstraints=critical,CA:FALSE" -addext "keyUsage=critical,digitalSignature" -addext "extendedKeyUsage=serverAuth" -keyout certs/server-key.pem -out certs/server-cert.pem
```

Browser cần tin cậy chứng chỉ đó cho hostname `localhost` thì HTTPS/WSS mới
hoạt động. Có file PEM chưa đồng nghĩa browser đã tin cậy nó. Launcher không
cài chứng chỉ vào hệ điều hành và không bỏ qua xác minh TLS.

## 2. Thử web trước

1. Trong File Explorer, mở thư mục gốc repo đã clone (nơi có `CMakeLists.txt`).
2. Nhấp đúp [run-web-example.cmd](../run-web-example.cmd). Giữ cửa sổ console mở.
   Nếu thiếu EXE, DLL, chứng chỉ hoặc tài nguyên web, launcher liệt kê file thiếu
   và không chạy server.
3. Khi console báo server đã mở, tự mở browser tới
   **[https://localhost:9553/](https://localhost:9553/)**.
4. Bấm **Đọc profile mẫu**. Với database mới, kết quả mong đợi là:

   ```text
   HTTP 200
   {"id":1,"name":"Demo player","level":1}
   ```

   Kết quả này kiểm tra cả đường HTTPS → truy vấn SQLite nền → phản hồi.
5. Có thể mở [https://localhost:9553/health](https://localhost:9553/health)
   để kiểm tra riêng HTTP; phản hồi mong đợi: `{"network":"ready"}`.

Trang còn có kiểm tra tối đa 16 KiB dữ liệu từ file audio/video và demo gọi
camera/mic giữa hai tab. Hãy thử profile thành công trước. Demo camera cần
cho phép camera/mic, hai tab trên cùng máy và chứng chỉ HTTPS/WSS đáng tin cậy.

## 3. Thử game gửi lệnh

1. Nhấp đúp [run-game-example.cmd](../run-game-example.cmd) ở gốc repo.
   Giữ console mở tới khi thấy `GameServer ready`.
2. Mở [examples/WebClient/index.html](../examples/WebClient/index.html)
   bằng browser. Đây là file client; nó không tự khởi động server.
3. Giữ Endpoint là **`wss://localhost:9444/game`**, bấm **Kết nối**.
   Log cần hiện `OPEN`.
4. Nhập lần lượt các lệnh bên dưới và bấm **Gửi** mỗi lần:

   | Lệnh | Phản hồi mong đợi trên một kết nối mới |
   | --- | --- |
   | `PING` | `PONG` |
   | `NAME Thanh` | `OK display name=Thanh` |
   | `STATS` | `messages=3` |
   | `HISTORY` | `stored_messages=4` |

Mỗi lệnh đều tăng bộ đếm, kể cả `STATS` và `HISTORY`. Nếu gửi thêm lệnh thì
số trả về sẽ khác. `HISTORY` đọc số đã lưu từ SQLite; tên hiển thị chưa phải
tài khoản đăng nhập. Log browser thêm tiền tố `SEND`/`RECV` vào các dòng này.

## Dừng hoặc kiểm tra file trước khi chạy

Nhấn **Ctrl+C trong cửa sổ server** để dừng. Đóng browser chỉ ngắt client;
server vẫn chạy nếu console còn hoạt động.

Chỉ kiểm tra file cần thiết, không mở server:

```powershell
.\scripts\run-example.ps1 -Example Web -CheckOnly
.\scripts\run-example.ps1 -Example Game -CheckOnly
```

Hai launcher `.cmd` mặc định dùng bản **x64 DLL / Debug**. Nếu bạn đã build
Release, chạy `.\scripts\run-example.ps1 -Example Web -Configuration Release`
hoặc đổi `Web` thành `Game`.

Không dùng `ServerEngine.exe` hoặc `ServerEngineTcpTest.exe` để thử hai demo
này: đó là server/client mẫu cũ với giao thức TCP khác.
