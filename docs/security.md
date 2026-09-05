# Bảo mật của DLL

## ECC được dùng thế nào?

TLS 1.3 dùng ECDHE để thỏa thuận khóa phiên và chứng chỉ EC để xác thực server.
Dữ liệu được mã hóa/xác thực bằng AES-GCM hoặc ChaCha20-Poly1305 trong OpenSSL.
Không mã hóa từng packet bằng thuật toán ECC tự viết. TLS factory giới hạn
ECDHE P-256/P-384, certificate/private key EC từ 256 bit, tắt early data/0-RTT.
[OpenSSL: groups](https://docs.openssl.org/3.0/man3/SSL_CTX_set1_curves/),
[TLS versions](https://docs.openssl.org/3.0/man3/SSL_CTX_set_min_proto_version/).

TCP/TLS, WSS và HTTPS dùng cùng policy. Mặc định listener C ABI là TLS; thiếu chứng
chỉ hoặc private key sẽ trả lỗi, không hạ xuống plaintext. Factory kiểm tra
key khớp certificate. Private key PEM mã hóa bằng password hiện bị từ chối;
không có hộp nhập password hoặc prompt ngầm trong service.

Certificate/private-key path là UTF-8, được đọc bằng filesystem path Unicode
trên Windows. Nội dung PEM không ghi vào error/log. Buffer chứa private PEM
được xóa khi giải phóng; SSL context vẫn phải giữ key trong bộ nhớ khi phục vụ.
Giới hạn mỗi file PEM 1MiB. Phân quyền file key cho tài khoản chạy service.

Client phải kiểm tra CA/trust và hostname/SAN. Ví dụ test có cả trường hợp
không tin certificate, sai hostname và client chỉ hỗ trợ TLS 1.2 để phát hiện
việc bỏ qua xác thực. Chứng chỉ TLS xác thực server; tài khoản người chơi vẫn
cần application authentication. Phiên bản này chưa có mTLS.

## UDP

UDP hiện chỉ hỗ trợ `SE_SECURITY_NONE`, phải chọn rõ. Source address/port có
thể bị giả mạo và datagram có thể mất/lặp/đảo thứ tự. Không dùng UDP mẫu để gửi
token, mật khẩu hoặc thao tác tài khoản. Trong host mẫu UDP chỉ echo/PING trên
loopback. Chuyển thao tác nhạy cảm sang TCP/TLS hoặc WSS.

DTLS hoặc QUIC là hướng chuẩn để bổ sung kênh datagram bảo mật; chưa triển khai
ở đây. TLS được cấu hình trên UDP sẽ bị từ chối. Không dùng chung khóa AES tự
đặt hoặc chỉ thêm chữ ký ECC vào packet để gọi đó là transport bảo mật hoàn chỉnh.

## Certificate dev để bạn tự chạy

Lệnh sau dành cho thử trên máy local với OpenSSL 3. Cặp file này đã được tạo
trên máy thử ngày 05/09/2026; không chạy lại để ghi đè private key đang dùng.
Trên máy mới, tạo thư mục `certs` nếu chưa có và chạy từ gốc repo:

```powershell
New-Item -ItemType Directory -Force certs
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -sha256 -noenc -days 30 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" -addext "basicConstraints=critical,CA:FALSE" -addext "keyUsage=critical,digitalSignature" -addext "extendedKeyUsage=serverAuth" -keyout certs/server-key.pem -out certs/server-cert.pem
```

Client dev cần tin certificate này một cách rõ ràng và dùng đúng hostname.
Browser kiểm tra trust bằng kho chứng chỉ của nó/hệ điều hành; không sửa client
để bỏ xác minh. Production dùng certificate ECDSA được CA phù hợp cấp, kèm
intermediate chain. Không commit key. `certs/` đã nằm trong `.gitignore`.
[OpenSSL req options](https://docs.openssl.org/3.0/man1/openssl-req/).

## Phạm vi đã viết và cần chứng minh

Data Redis hiện không có TLS, yêu cầu plaintext phải chọn rõ, không tự fallback.
TLS ở listener Network không mã hóa kết nối database/cache. Ví dụ WebRTC dùng
WSS cho signaling và bảo mật media riêng của browser; phòng demo không xác
thực. Xem [Data](data-services.md) và [audio/video](audio-video.md).

Source có TLS factory và test handshake TLS thật qua memory BIO; không mở
network trong test TLS đó. Loopback tests kiểm tra TCP/UDP/WS plaintext qua
C ABI. HTTP test source có HTTPS handshake/hostname verification qua socket.
WSS/WebRTC với browser, certificate chain production, stress, reconnect,
shutdown nhiều connection và lỗi hệ điều hành vẫn cần kiểm tra runtime.

Đây không phải một security audit hay chứng nhận production. Các thư viện
bảo mật cần được cập nhật có kiểm soát cùng baseline dependency và chạy lại
test. Tính năng ECC provider trong sample cũ chỉ là capability probe; nó
khác với TLS implementation của DLL mới.
