# 额外工具以及技能

## 技能：缺失工具检测与安装助手

### 目标
当用户尝试执行代码或 Shell 命令，且因缺少某个工具导致报错时，自动分析错误原因，并根据当前操作系统提供具体的安装指令。

### 触发条件
1. 执行 Shell 命令返回非零退出码。
2. 标准错误输出包含以下关键词之一：
   - "command not found"
   - "not found"
   - "is not recognized as an internal or external command"
   - "No such file or directory" (针对可执行文件)
   - "ModuleNotFoundError" (针对 Python 库)
   - "npm ERR! missing" (针对 Node 包)

### 执行逻辑
1. **解析错误**：从错误信息中提取缺失的命令名称（例如 `jq`, `tree`, `ffmpeg`）。
2. **检测环境**：
   - 如果是 Linux (Debian/Ubuntu): 推荐使用 `sudo apt-get install <tool>`
   - 如果是 Linux (CentOS/RHEL): 推荐使用 `sudo yum install <tool>`
   - 如果是 macOS: 推荐使用 `brew install <tool>` (如果未安装 brew，提示去官网安装)
   - 如果是 Windows: 推荐使用 `choco install <tool>` 或 `winget install <tool>`
3. **用户交互**：
   - 不要直接尝试安装（除非用户有明确的安全配置）。
   - **礼貌地**告知用户缺少工具，并提供复制即可运行的安装命令。
   - 询问用户是否已安装，或者是否需要帮助运行安装命令。

### 响应示例
> 看起来你的系统中缺少 `jq` 工具，这是处理 JSON 数据所必需的。
>
> 检测到您正在使用 **macOS**，建议运行以下命令进行安装：
> ```bash
> brew install jq
> ```
>
> 如果您使用的是 **Ubuntu/Debian**，请运行：
> ```bash
> sudo apt-get update && sudo apt-get install -y jq
> ```
> 安装完成后，请告诉我，我会重试刚才的操作。

## 技能：智能 Git 版本控制

### 目标
在执行代码修改任务前后，利用 Git 确保代码库的安全，提供清晰的提交建议，并在出错时提供回退方案。

### 行为准则
1. **前置检查**：
   - 在修改任何文件前，必须运行 `git status`。
   - 如果工作区不干净（有未提交更改），必须警告用户，并建议先提交当前状态。
   
2. **提交建议**：
   - 完成任务后，不要自动执行 `git commit`。
   - 根据修改内容，生成一条语义化的 Commit Message（例如：`feat: add user authentication` 或 `fix: resolve null pointer in utils`）。
   - 以代码块形式提供给用户，方便复制。

3. **差异分析**：
   - 如果用户询问改了哪里，使用 `git diff` 展示变更，而不是重新读取文件。

4. **安全回退**：
   - 如果修改后的代码导致测试失败且无法修复，主动提出使用 `git restore <file>` 或 `git checkout .` 撤销更改。

### 响应示例
**场景：任务完成**
> 代码重构已完成。我修改了 `main.py` 和 `utils.py`。
>
> 建议提交以下更改：
> ```bash
> git add main.py utils.py
> git commit -m "refactor: optimize data processing logic in main"
> ```

**场景：环境不干净**
> 我注意到你的工作区有未提交的更改。
> 为了防止接下来的修改导致冲突，建议先处理这些更改。
> 你可以运行 `git stash` 暂存它们，或者让我帮你提交一个临时版本。