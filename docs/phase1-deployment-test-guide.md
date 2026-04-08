# Phase-1 部署测试指南

目标：先把第一阶段最小闭环跑起来，验证 control-plane 与 runtime 的基础 wiring。

## 1. 目标仓库

- `edge-vision-contracts`
- `edge-vision-control-plane`
- `edge-vision-runtime`

## 2. 先决条件

### Control-plane
- Go 1.22+
- `/snap/bin/go build ./...` 可用

### Runtime
- C++17 工具链
- CMake 3.16+
- Ninja / Make
- `cmake --build build` 可用

## 3. 当前 phase-1 口径

- control-plane 负责创建 deployment
- runtime 负责表达 supervisor/source/worker 的最小 wiring
- 当前阶段优先跑通“语义闭环”，不追求真实推理、真实 IPC 或完整 proto 传输层

## 4. 启动顺序

### 4.1 启动 control-plane

```bash
cd projects/edge-vision-control-plane
/snap/bin/go build ./...
/snap/bin/go run ./cmd/api-server
```

默认检查：
- `GET /healthz`
- `GET /readyz`
- `POST /v1/deployments`

### 4.2 启动 runtime supervisor

```bash
cd projects/edge-vision-runtime
cmake -S . -B build
cmake --build build
./build/bin/runtime-supervisor apply --config configs/runtime-phase1-deployment.v1.example.yaml
```

### 4.3 启动 runtime worker

```bash
cd projects/edge-vision-runtime
./build/bin/runtime-worker --config configs/runtime-worker.v1.example.yaml
```

### 4.4 启动 runtime source

```bash
cd projects/edge-vision-runtime
./build/bin/runtime-source --config configs/runtime-source.v1.example.yaml
```

## 5. 验证点

### Control-plane
- 能返回 health 状态
- `POST /v1/deployments` 能接受 `node_id / runtime / revision_id`
- 返回的 deployment 记录状态正常流转

### Runtime
- `runtime-supervisor` 能读取 phase-1 配置
- deployment spec 能 normalize
- apply/status 命令能打印 wiring 结果
- `runtime-worker` / `runtime-source` 能各自完成最小启动

## 6. 当前已知限制

- 还不是完整 gRPC / IPC 闭环
- 还没有真实 contracts runtime/v1 的传输层接线
- 还没有真实 TensorRT / NVDEC 路径
- 状态主要仍是占位 / 内存态

## 7. 最小验收标准

满足以下条件即可认为 phase-1 部署测试通过：

1. control-plane 能成功编译并启动
2. runtime 三个二进制都能编译并启动
3. supervisor 的 phase-1 deployment 能正确 normalize
4. deployment 的关键字段（node/runtime/revision）不漂移
5. 没有明显的编译错误或配置解析错误
