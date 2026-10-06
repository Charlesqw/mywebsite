# ============================================================
#  个人网站云端部署镜像 (Render / Railway / Fly.io 等通用)
# ============================================================

# ---------- 构建阶段 ----------
FROM gcc:14-bookworm AS builder
WORKDIR /build
COPY server.cpp httplib.h ./
COPY www ./www
# Linux 下 cpp-httplib 仅需 pthread, 无需任何 Windows 库
RUN g++ -std=c++17 -O2 server.cpp -o server -pthread

# ---------- 运行阶段 ----------
FROM debian:bookworm-slim
WORKDIR /app
COPY --from=builder /build/server ./server
COPY --from=builder /build/www ./www
# 云平台通过 PORT 环境变量指定端口, 程序启动时自动读取
ENV PORT=8080
EXPOSE 8080
CMD ["./server"]
