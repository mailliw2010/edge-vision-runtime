# edge-vision-runtime

C++ Runtime 仓。

## 职责

- Runtime Supervisor
- Runtime Worker
- SourceSession
- 共享解码与本机 IPC
- 本地状态汇总与恢复

## 建议目录

- `cmd/`：supervisor / worker 入口
- `src/`：核心源码
- `include/`：头文件
- `configs/`：运行配置模板
- `tests/`：测试
- `scripts/`：构建与运行脚本
