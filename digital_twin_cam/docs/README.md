# Tài liệu kiến trúc camera

Bàn giao cho bên tích hợp: [cài/systemd](handover.md) và [contract](integration-contract.md).

Để thực hành cài và reboot, dùng [hướng dẫn từ đầu](setup-from-scratch.md).

Đọc kiến trúc theo thứ tự:

1. [C4 System context](architecture/01-context.md): ai dùng hệ thống, phần nào ngoài project.
2. [C4 Containers](architecture/02-containers.md): tiến trình, giao thức, nơi xử lý video.
3. [C4 Components](architecture/03-components.md): module trong publisher Pi.
4. [Runtime](architecture/04-runtime.md): các bước publish, bắt tay, truyền video, đóng phiên.
5. [Deployment](deployment.md): máy thật, khóa, chạy tay, systemd, đổi IP.
6. [ADR-0001](adr/0001-hardware-h264.md): lý do chọn công nghệ và phần còn thiếu.
7. [Benchmark](benchmark.md) và [verification](verification.md): bằng chứng nghiệm thu.

C4 dừng ở mức Component vì mức Code xem trực tiếp source Python; không tạo thêm lớp
trừu tượng chỉ để đủ bốn hình. Các sơ đồ dùng Mermaid, container trong C4 nghĩa là
đơn vị triển khai/chạy, không bắt buộc Docker.
