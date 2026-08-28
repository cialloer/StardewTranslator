StardewTranslator 是一个面向《星露谷物语》Mod 的 Windows 桌面翻译工具。它会读取
Mod 中的 JSON 文本，通过 DeepSeek、OpenAI、MicuAPI 或其他 OpenAI Chat Completions
兼容接口生成指定语言的译文。

当前版本：1.0

> [!IMPORTANT]
> 本工具使用 AI 服务翻译文本，译文可能不准确，也可能消耗 API 额度。使用前请备份
> Mod，并在发布或长期游玩前实际测试翻译结果。本项目是非官方工具，与 ConcernedApe、
> Stardew Valley、SMAPI 以及各 API 服务商没有隶属或授权关系。

 主要功能

- 识别标准 SMAPI i18n/default.json，生成 zh.json、en.json、ja.json 或 ko.json。
- 支持 DeepSeek、MicuAPI、OpenAI 和自定义 OpenAI 兼容接口。
- 可以通过接口的 /models 端点检测模型，也可以手动填写模型名称。
- 支持带 //、/* ... */ 注释或尾随逗号的常见 Mod JSON。
- 自动分批翻译，并检查译文数量和常见占位符是否保持不变。
- 网络错误、限流和服务端错误会自动重试，翻译过程不会阻塞窗口操作。
- Windows 下可使用 DPAPI 加密保存手动输入的 API Key。
- 没有标准i18n目录时，可选择实验性的“翻译全部 JSON”模式。

 下载与运行
 使用发布版
1. 打开本项目 GitHub 页面的 Releases。
2. 下载最新的 Windows ZIP 压缩包。
3. 将压缩包完整解压到一个可写目录，例如：
   D:\Tools\StardewTranslator
4. 运行：
   bin\StardewTranslator.exe
请勿直接在压缩包预览窗口中运行程序，也不要只复制 EXE。程序需要压缩包内附带的 Qt DLL
和 plugins 目录。如果提示缺少 Qt6Core.dll、qwindows.dll 或其他运行库，请重新完整
解压发布包。
发布版目前可能没有数字签名，因此 Windows SmartScreen 可能显示未知发布者。请只从本项目
的正式 GitHub Releases 下载，并在运行前确认文件来源。

 使用前准备
开始翻译前，需要准备：
1. 已经解压的 Stardew Valley Mod 文件夹，而不是 Mod 压缩包。
2. 一个受支持服务商的 API Key。
3. 对应服务商可用的模型名称和足够的账户额度。
4. 可访问所选 API 服务的网络环境。
5. 一份 Mod 备份。

典型的标准 SMAPI 翻译目录如下：
ExampleMod/
├─ manifest.json
└─ i18n/
   └─ default.json


在程序中应选择 `ExampleMod` 文件夹，而不是单独选择 `default.json` 或 `i18n` 文件夹。

 第一次翻译
1. 启动 StardewTranslator。
2. 将已解压的 Mod 文件夹拖入窗口，或点击“选择文件夹”。
3. 在“服务商”中选择你实际申请 API Key 的平台。
4. 输入 API Key。Key 必须和接口服务商一致。
5. 点击模型右侧的刷新按钮检测模型；如果服务商不支持模型列表，可直接手动输入模型名称。
6. 选择目标语言：简体中文、英语、日语或韩语。
7. 标准 Mod 不要勾选“无 i18n 时翻译全部 JSON（实验性）”。
8. 点击“开始翻译”。
9. 在右侧查看当前步骤、进度和运行日志。
10. 完成后检查生成的语言文件，并在 SMAPI 中实际运行 Mod。

标准模式的输出位置为：

| 目标语言 | 输出文件 |
| --- | --- |
| 简体中文 | i18n/zh.json |
| 英语 | i18n/en.json |
| 日语 | i18n/ja.json |
| 韩语 | i18n/ko.json |

如果目标文件已经存在，程序会替换该文件。标准模式不会自动为旧的目标语言文件创建备份，
因此重新翻译前应自行备份已有人工译文。

API 服务配置
内置服务商
| 服务商 | 默认接口 | 默认或常用模型 | 环境变量 |
| --- | --- | --- | --- |
| DeepSeek | `https://api.deepseek.com/chat/completions` | `deepseek-chat`、`deepseek-reasoner` | `DEEPSEEK_API_KEY` |
| OpenAI | `https://api.openai.com/v1/chat/completions` | `gpt-4o-mini`，也可检测或手动填写 | `OPENAI_API_KEY` |
| MicuAPI | `https://www.micuapi.ai/v1/chat/completions` | 以服务商实际支持为准 | `MICUAPI_API_KEY` |

模型名称、可用区域、价格和额度由服务商决定，可能随时变化。程序中显示的名称只是预设，
不代表账户一定有权使用。请以服务商控制台和接口文档为准。

API Key 通常需要在服务商自己的开发者平台申请。StardewTranslator 不提供 API Key，
也不会替服务商充值。翻译会产生 Token 用量，较大的 Mod 可能产生明显费用。

自定义兼容接口
自定义服务必须兼容 OpenAI Chat Completions 请求和响应格式。接口地址可以填写：
https://gateway.example/v1
程序会补全为：
https://gateway.example/v1/chat/completions
也可以直接填写完整端点：
https://gateway.example/v1/chat/completions
不要把普通网站首页当作 API 地址。为避免向错误网站发送 Key，未知网站的根地址会被拒绝。
Azure 等带部署路径或查询参数的兼容接口，应直接填写完整的 Chat Completions 端点。

模型检测说明
刷新模型时，程序会按照 OpenAI 兼容约定访问接口的 /models 地址。模型检测失败并不一定
表示翻译接口不可用：部分兼容服务没有模型列表接口，或者当前 Key 无权枚举模型。这种情况
下可以根据服务商文档手动输入模型名称，再开始翻译。

 API Key 的保存与读取
程序按以下优先级使用 API Key：
1. 界面中手动输入的 Key。
2. 当前服务商对应的环境变量。
3. 通用环境变量 STARDEW_TRANSLATOR_API_KEY。

Windows 下，手动输入的 Key 会使用当前 Windows 用户的 DPAPI 凭据加密后保存在本机设置
中。密文只能由保存它的 Windows 用户账户解开。不同服务商的 Key 分开保存；切换服务商
时不会混用。修改自定义接口的域名时，界面也会清除当前 Key，避免发送到新域名。

要删除程序保存的 Key，请清空 API Key 输入框后正常关闭程序。其他平台如果没有安全存储
支持，程序不会持久化手动输入的 Key。

也可以将 .env.example复制为 .env，只填写实际使用的变量：
dotenv
DEEPSEEK_API_KEY=your-key-here
MICUAPI_API_KEY=
OPENAI_API_KEY=
STARDEW_TRANSLATOR_API_KEY=

开发模式会读取当前工作目录中的 .env；发布版也会读取 EXE 所在目录中的 .env。
.env` 是明文文件，请勿分享、截图、提交到 GitHub 或放入公开压缩包。项目的 .gitignore
已经排除真实 .env，但网页手动上传文件时仍需自行确认。

程序会清理粘贴 Key 中多余的引号、空白和Bearer前缀。它不会把某个平台的 Key 转换
成另一个平台可用的 Key。

标准 i18n 模式
当所选 Mod 中存在 i18n/default.json 时，程序只读取该文件，并在同一目录生成目标语言
文件。default.json 本身不会被修改。

处理过程包括：
1. 解析 JSON。
2. 提取非空字符串值。
3. 每批最多发送 30 条文本到所选 API。
4. 根据数字 ID 对齐原文和译文。
5. 检查返回数量和常见占位符。
6. 使用临时文件完成原子写入。

程序支持读取包含注释和尾随逗号的 Mod JSON，但生成的语言文件会是标准格式 JSON，原有
注释和自定义排版不会出现在输出文件中。缺少引号、括号不配对等结构错误不会被自动修复。

程序会检查 `{{token}}`、`%s`、`%d`、`{0}` 等常见占位符是否被模型改变。检查可以降低
损坏风险，但无法保证所有 Stardew Valley 或第三方 Mod 的特殊标记都被识别。

“翻译全部 JSON”实验模式
只有在找不到 `i18n/default.json` 时，程序才会使用这个选项。启用后，它会递归查找 Mod
目录下的 JSON 文件，但会排除：

- manifest.json
- config.json
- config-schema.json
- 所有i18n目录中的文件

每个准备修改的文件都会先创建：
原文件名.json.stardewtranslator.bak
以后再次翻译时，程序会继续从第一次保存的 .stardewtranslator.bak读取原文，避免把已经
翻译过的文本反复翻译。如果原始 Mod 文件后来升级或被手动修改，请先妥善处理旧备份，
否则程序仍会使用旧备份内容。

此模式会原地替换 JSON 文件。任意 JSON 中都可能同时包含玩家可见文本、内部 ID、文件名、
条件表达式和其他机器字段，AI 无法可靠判断哪些字符串不能翻译。即使程序排除了常见配置
文件，也仍可能破坏 Mod 逻辑。请只对确认结构安全的 Mod 使用，并保留整个 Mod 的独立备份。

 取消、重试与写入行为

- 点击“取消”后，程序会等待当前网络请求结束，再停止后续批次。
- 单次网络请求的超时时间为 60 秒，因此取消操作可能不会立即完成。
- 网络错误、HTTP 408、409、429 和服务端 5xx 错误最多自动尝试 3 次。
- 服务端返回 `Retry-After` 时，程序会等待该时间，最长等待 10 秒。
- 不支持 JSON Object 模式的接口会自动改用普通兼容请求再试一次。
- 当前文件只有在翻译和验证全部完成后才会写入。
- 实验模式中，取消任务不会撤销已经完成并写入的前序文件。
- 写入使用同目录临时文件原子替换，写入失败时不会提交不完整的目标文件。

 隐私与安全

翻译时，Mod 中提取出的文本会发送到你选择的 API 接口。请在使用前确认该服务商的隐私
政策，不要翻译包含私人信息、未公开内容或无权上传的材料。

程序没有用于接收翻译内容的开发者服务器，也没有内置遥测上传；网络请求会发送到界面中
显示的 API 地址。API Key 作为 `Authorization: Bearer ...` 请求头发送给该接口。

日志会隐藏常见形式的 Bearer Token 和 `sk-` Key，但服务商返回的错误信息仍可能包含账户、
接口或请求细节。提交 Issue 前应人工检查日志并删除敏感内容。任何时候都不要把 API Key
放入截图、Issue、README、源码或翻译文件。

 翻译质量与限制

- AI 译文可能存在错译、漏译、语气不一致和专有名词不统一。
- 每条字符串的上下文有限，角色口吻和剧情前后关系可能无法正确判断。
- 程序不会替代人工校对，也不会自动验证 Mod 的游戏逻辑。
- 模型返回结构不正确、数量不一致或改变占位符时，程序会拒绝写入当前文件。
- 非标准兼容接口可能在请求字段、模型名称、响应格式或限流规则上存在差异。
- JSON 输出会标准化缩进，不能保留源文件的注释和原始格式。

建议完成后重点检查：

- 人名、地名和物品名是否统一。
- {{...}}、%s、%d、{0} 等占位符是否完整。
- 对话换行、颜色标记和控制字符是否正常。
- Content Patcher 条件、内部 ID 和文件路径是否被误译。
- SMAPI 控制台是否出现红色错误。

 常见问题
 程序提示缺少 DLL 或无法启动
请完整解压发布包，并保持 bin、plugins和其他目录的相对位置。不要只下载或复制 EXE。
找不到 i18n/default.json
确认选择的是解压后的 Mod 根目录，并检查文件是否确实位于：
所选文件夹\i18n\default.json
如果该 Mod 没有标准 i18n 结构，只能在理解风险后尝试“翻译全部 JSON”。
 HTTP 401 或 403
Key 无效、过期、没有模型权限，或者 Key 与接口服务商不匹配。确认服务商、接口域名、账户
和 Key 来自同一平台。例如 DeepSeek Key 不能用于 OpenAI 接口。
HTTP 404、返回网页 HTML 或接口地址无效
通常是填写了服务商网站首页，而不是 API 地址。优先使用程序内置服务商预设；自定义服务
请填写以 /v1 结尾的 API Base URL，或完整的 /chat/completions 地址。
HTTP 429
请求被限流或账户额度不足。等待一段时间，检查服务商余额、速率限制和并发限制后重试。
程序会自动重试临时限流，但不能绕过服务商的账户限制。
模型检测失败
部分接口不提供 /models，或不允许当前 Key 查询模型。根据服务商文档手动填写模型名称。
如果翻译也失败，再检查接口地址、Key 和模型权限。

 JSON 解析失败
日志会显示文件路径以及大致行列。程序可以兼容注释和尾随逗号，但无法安全修复缺引号、
缺括号或编码损坏。请先使用文本编辑器或 JSON 工具修复源文件。

译文改变占位符，程序拒绝写入
模型修改或删除了程序识别的占位符。可以重新尝试、选择更可靠的模型，或将该文件交给人工
翻译。不要通过删除占位符来绕过错误，否则 Mod 运行时可能出现异常。

无法创建或写入文件
检查 Mod 目录是否只读、文件是否被其他程序占用，以及当前 Windows 用户是否拥有写权限。
必要时把 Mod 复制到普通用户目录中完成翻译，再放回游戏目录。

点击取消后仍在等待
这是正常行为。程序不会强行中断正在传输的网络请求，而是在当前请求结束或达到 60 秒超时
后停止。

翻译成功但游戏中没有显示
确认目标语言代码与游戏当前语言匹配，并检查 Mod 是否真的使用 SMAPI i18n。查看 SMAPI
控制台日志，确认没有 JSON 语法错误、缺少键或 Mod 自身的加载错误。

从源码构建
普通用户建议直接下载 Releases。需要自行构建时，请准备：

- CMake 3.16 或更高版本
- 支持 C++17 的编译器
- Qt 6：Widgets、Network、Concurrent
- 运行单元测试时还需要 Qt Test
- Windows 发布构建还会链接系统 crypt32

以下是 Qt Online Installer + MinGW + Ninja 的 PowerShell 示例。请把 Qt 版本和安装路径改成
本机实际值：

```powershell
$env:Path = "C:\Qt\Tools\mingw1310_64\bin;$env:Path"

C:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64 `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe `
  -DBUILD_TESTING=ON

C:\Qt\Tools\CMake_64\bin\cmake.exe --build build
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build --output-on-failure
C:\Qt\Tools\CMake_64\bin\cmake.exe --install build --prefix dist
```

安装完成后，可运行程序位于：
dist\bin\StardewTranslator.exe
cmake --install 会收集 Qt 插件、Qt DLL 和 MinGW 运行库。对外发布时必须分发完整的
dist内容，并同时遵守 Qt、MinGW 和其他第三方组件的许可证要求。

 反馈问题
提交 GitHub Issue 时，请提供：
- StardewTranslator 版本。
- Windows 版本。
- 服务商和模型名称，但不要提供 API Key。
- 已删除敏感信息的完整运行日志。
- 能复现问题的最小 JSON 示例。
- 问题发生在标准 i18n 模式还是实验模式。

请不要上传完整的付费 Mod、未授权内容、个人账户信息或任何 API Key。
