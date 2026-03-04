# CodeMap 全量扫描 Obfuscation 项目

**日期**: 2026-02-25
**标签**: codemap, 项目结构, 代码图谱

## Summary

对 `llvm/lib/Obfuscation/` 目录执行了 CodeMap 全量扫描，生成结构化代码图谱。扫描识别出 373 个 C++ 源文件、9,394 个函数、2,259 个类，分布在 3 个模块中（_root, Eigen, SVM）。图谱已持久化到 `.codemap/` 目录。

## Changes Made

- `.codemap/` 目录（新建）- CodeMap 生成的代码图谱数据
  - `slices/_overview.json` - 项目概览（模块、函数、类统计）
  - `slices/` 下各模块切片文件

## Decisions & Rationale

### 扫描范围选择
- **决策**: 以 `llvm/lib/Obfuscation/` 为扫描根目录
- **理由**: 这是当前 feature 分支的工作目录，包含所有混淆 pass 源码

## Technical Details

- CodeMap 工具版本: codegraph 0.2.1 (codegraph-x86_64-windows.exe)
- 扫描结果:
  - **_root 模块**: 58 文件, 308 导出符号 — 混淆 pass 主体代码
  - **Eigen 模块**: 307 文件, 6,055 导出符号 — MBA 混淆依赖的矩阵库
  - **SVM 模块**: 8 文件, 141 导出符号 — SmallVmp VM 保护模块
- 当前分支 `feature/merge-obfuscation-passes` 有 8 个未提交的修改文件（P0-P3 合并重构进行中）

## Open Items / Follow-ups

- [ ] 使用 `/codemap:load` 加载图谱到会话上下文
- [ ] 对 _root 模块进行详细分析，辅助后续重构工作
- [ ] `.codemap/` 目录需添加到 `.gitignore`

## Learnings

- CodeMap 首次运行会自动从 GitHub Releases 下载 codegraph 二进制，缓存到 `~/.codemap/bin/`
- `_overview.json` 文件可能很大（本项目约 95K tokens），需要用脚本解析而非直接读取
- Eigen 库占据了绝大多数文件（307/373），实际混淆 pass 代码集中在 _root 的 58 个文件中
