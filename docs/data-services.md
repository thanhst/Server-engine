# Data module: SQL và NoSQL có hợp đồng riêng

Data không sở hữu network listener. Bạn có thể tạo SQL service trong một công
cụ console không mở port nào. Hiện SQL và Redis có implementation riêng; tên
Data là nhóm module để tránh gọi Redis hoặc Kafka là một SQL database.

## SQL v1

Public API ở [Sql.h](../include/ServerEngine/C/Sql.h). Kiểm tra
`se_sql_get_abi_version()` trước initializer. Layout v1 cố định, không đưa
`sqlite3*`, STL hoặc allocator ra ngoài DLL.

```text
options_init → create
    submit(statements + typed parameters) → request_id
    poll → result metadata
    get_cell / column_name → copy vào buffer ứng dụng
    release_result
stop → drain completions → destroy
```

`create` mở SQLite file đồng bộ để báo lỗi đường dẫn trước khi chạy server.
Mọi query sau đó chạy trên một worker sở hữu connection. Đây chưa phải pool
nhiều connection. Có thể tạo nhiều SQL service độc lập, nhưng số service không
làm mất các giới hạn locking của cùng database file.

Mỗi submit nhận 1..64 statement, chạy thành một transaction atomic trên cùng
connection. Chỉ statement cuối được trả bảng kết quả. Truyền từng câu SQL
riêng trong array, không ghép nhiều câu bằng dấu chấm phẩy trong một statement.
Tham số có kiểu NULL, int64, double, UTF-8 text, binary blob và được sao chép
trước khi submit trả về. Empty blob/text được phân biệt với NULL.

Engine quản lý BEGIN/COMMIT/ROLLBACK. Vì thế public SQL không cho tự chạy các
lệnh transaction, ATTACH/DETACH, PRAGMA, virtual table hoặc load extension.
Các migrations của ví dụ chỉ dùng CREATE/INSERT/UPDATE được hỗ trợ. Đây là
API thực thi ứng dụng có giới hạn, không phải SQLite administration console.

## Hai mã trạng thái khác nhau

- `se_sql_submit == SE_OK`: đã nhận job, chưa phải đã ghi database.
- `se_sql_poll == SE_OK`: có kết quả để đọc; phải kiểm tra `result.status`.
- `result.status == SE_OK`: transaction đã commit thành công.
- `SE_BACKPRESSURE` khi submit: không nhận job, không thực thi câu SQL nào.
- `SE_TIMEOUT` bên trong result: hết thời hạn thực thi, có rollback trừ trường
  hợp lỗi rollback được báo `SE_OUTCOME_UNKNOWN`.

Mọi kết quả, kể cả lỗi, phải `release_result`. Job được nhận giữ slot và budget
từ queued → running → complete → delivered cho đến release. Điều này tránh
mất kết quả của một lệnh ghi vì hàng đợi completion đầy sau khi ghi thành công.
Getters báo kích thước khi buffer nhỏ và không hủy kết quả.

Giới hạn bytes/rows/columns chặn input và kết quả do engine giữ. SQLite parser,
cache, temporary work, overhead allocator và dữ liệu host đã copy có bộ nhớ
riêng; `memory_budget_bytes` không phải giới hạn RAM cứng của toàn tiến trình.
`max_columns` cũng được áp cho giới hạn SQLite column, nên cần tính cả schema.

Stop từ chối job mới, hủy job chưa chạy, interrupt query đang chạy và join
worker. Kết quả đã commit vẫn báo thành công khi stop diễn ra ngay sau đó.
Kết quả được giữ để poll; destroy sẽ giải phóng phần chưa đọc. Không unload
DLL khi còn service, result hoặc thread của ứng dụng đang gọi API.

`query_timeout_ms` tính từ lúc worker bắt đầu xử lý, không gồm thời gian chờ
queue. Deadline và interrupt là cơ chế hợp tác của SQLite; một lời gọi disk
đang bị chặn có thể làm query hoặc stop kéo dài hơn deadline. Trong GameServer,
5 giây chỉ là thời gian chờ hoàn tất nhẹ nhàng trước khi yêu cầu hủy, không
phải giới hạn cứng cho toàn bộ quá trình shutdown.

SQLite mở connection ở chế độ serialized; worker sở hữu thực thi, thread
stop chỉ dùng interrupt. [Tài liệu SQLite về threading](https://sqlite.org/threadsafe.html).

## Redis v1

[Redis.h](../include/ServerEngine/C/Redis.h) chỉ cung cấp GET, SET (TTL tùy
chọn), DELETE. Một service có một worker/connection, dùng database 0. Không
cho command tùy ý thay đổi trạng thái như SELECT, MULTI, AUTH hay SUBSCRIBE.

Hiredis format command và parse RESP; Asio quản lý socket, deadline toàn thao
tác và cancellation. GET trả kiểu missing/empty/value khác nhau. Input binary
được copy, `poll` dùng buffer caller; quá nhỏ thì giữ completion cho retry.
Không cần release riêng sau một Redis poll thành công.

`open` tạo worker; kết nối/auth xảy ra ở job đầu tiên. Chỉ khi completion báo
thành công mới biết thao tác thực sự thành công. Nếu timeout/disconnect sau
khi có thể đã gửi SET/DELETE, kết quả là `SE_OUTCOME_UNKNOWN`. Engine không tự
gửi lại lệnh ghi; ứng dụng phải đối chiếu trạng thái trước khi retry.

TLS của Network listener không bảo vệ kết nối Redis. Redis TLS chưa được hỗ
trợ; default TLS được từ chối để buộc ứng dụng chọn NONE có chủ ý. Không dùng
connector plaintext để gửi credential/data trên đường truyền không tin cậy.

## Mở rộng tiếp theo

Driver MySQL cần thật sự triển khai kết nối, TLS, binding, kiểu kết quả,
transaction và lỗi commit không xác định; hiện provider MySQL trả
`SE_NOT_SUPPORTED`. Cấu hình một URL không tự cung cấp driver đó.

Kafka cần API producer/consumer riêng, delivery report, offset và shutdown.
Nó chưa có trong source. Các module có thể chia sẻ cách trace/lifecycle mà
vẫn giữ hợp đồng của từng dịch vụ. Lộ trình không thay thế implementation.
