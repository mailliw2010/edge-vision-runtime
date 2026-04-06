# edge-vision-runtime

C++ Runtime 仓，当前先对齐**第一阶段最小闭环**：

- Jetson / TensorRT 优先
- proto 先冻结 `v1` 骨架
- 先把 supervisor / worker / source 的 app/session 边界搭出来
- 这版刻意不做重实现，不引入真实解码、IPC、TensorRT 细节

## 当前骨架

- `cmd/`：二进制入口
- `include/evr/runtime/`：公开接口与占位类型
- `src/`：最小实现
- `configs/`：v1 配置模板（当前只是结构草案，尚未接解析）
- `tests/`：smoke test

```text
.
├── cmd/
│   ├── runtime-source/
│   ├── runtime-supervisor/
│   └── runtime-worker/
├── configs/
├── include/evr/runtime/
│   ├── session/
│   ├── source/
│   ├── supervisor/
│   └── worker/
├── src/
│   ├── session/
│   ├── source/
│   ├── supervisor/
│   └── worker/
└── tests/
```

## 最小边界

这版只保留第一阶段真正需要的接口边界：

- `SupervisorApp` / `SupervisorSession`：后续承接调度、恢复、状态汇总
- `SourceApp` / `SourceSession`：后续承接拉流、解码、帧分发
- `WorkerApp` / `WorkerSession`：后续承接 TensorRT 推理、模型生命周期、本机 IPC

其中：

- `runtime-supervisor` 先只驱动 supervisor session
- `runtime-source` 先独立保留 source 占位入口
- `runtime-worker` 暂时串起 source + worker，作为第一阶段最小闭环

## 尚未接入

- proto v1 的真实消息定义与序列化
- Jetson NVDEC / TensorRT 具体适配
- supervisor <-> worker 的真实控制面链路
- source -> worker 的帧通路
- 配置文件解析与参数校验

## 构建

```bash
cmake -S . -B build
cmake --build build
(cd build && ctest --output-on-failure)
```

## 安装（可选）

```bash
cmake -S . -B build -DEVR_ENABLE_INSTALL=ON
cmake --build build --target install
```
