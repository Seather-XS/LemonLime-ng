<a href="https://project-lemonlime.github.io/Project_LemonLime/"><img src="assets/icons/lemon-lime.png" align=right /></a>

# gengen-tuack

[Project LemonLime](https://github.com/Project-LemonLime/Project_LemonLime) 的一个分支，面向 gengen 风格的比赛：
在原有评测能力之上，补齐了**赛区**、**题面**与**一键打包**这几件事。

需要 Qt 6.8 或更高版本。主要在 Windows + MinGW 上开发与验证，其它平台沿用上游的构建方式。

## 与原版的区别

评测本身与上游一致，这个分支主要在比赛的组织方式上做了补齐：

-   **比赛日布局**：试题数据放在 `problem/<题>/{data,down,graders,generator}`，选手代码放在 `answers/<赛区>/<选手>/`；打开旧布局的比赛日时会自动迁移，早期版本的 `gen/`（数据生成器）也会改名为 `generator/`。
-   **目录显示顺序**：打开比赛日时会在目录里写一个隐藏的 `desktop.ini`（并给该目录加上 ReadOnly 标记），让资源管理器按 `problem` → `import` → `answers` → `statement` → `dist` 排列。它只改「显示名」（前缀是不可见的零宽空格），真实路径一个字符都不变；换机器、换系统或删掉 `desktop.ini` 时只是回到原来的字母序。
-   **从 problem/ 配置试题**：把数据按 `problem/<题>/data/` 放好后，用「从 problem 自动配置试题」一键登记还没加入的试题与测试点（只看 `data/` 这一层，不读子目录；`graders/<题>.h` + `<题>_grader.cpp` 会自动识别成交互题）。
-   **赛区**：可以按赛区组织选手，成绩表、统计和导出都会带上赛区，也能按赛区拆开。
-   **题面**：内置题面编辑器，写 Markdown，用 pandoc + LaTeX 编译成 PDF，自带 ccpc / noi / noi new 模板，右侧即时预览；导出的 PDF 名字可以自己写模板。
-   **导出**：新增「导出」选项卡，把比赛日一键打成 zip —— 选手目录、测试数据、选手代码三类包；选手目录里可以勾选 `statement/` 下的题面文件（0 个到任意多个），还能额外套一层目录、压缩包内再套压缩包、密码加密（固定 WinZip AES-256 / AE-2）。
-   **准考证（暂未编进程序）**：这套功能的源码（`src/admission/`）与 LaTeX 模板（`assets/templates/admission/`）仍留在仓库里，但当前不参与构建，`lemon.exe` 里没有「准考证」选项卡，也不会读写 `admission/`、`dist/admission/`。下面「准考证：细节」保留的是重新启用后的设计说明。
-   **成绩与统计**：固定导出到 `<比赛日>/dist/reports/`；启用赛区时每个赛区各出一份。

## 安装

Windows 上可以直接用仓库里的 `build.cmd` 构建（需要 Qt 6、MinGW、CMake、Ninja）：

```powershell
.\build.cmd            # 构建到 build\，产物是 build\lemon.exe
.\build.cmd -NoLto     # 关掉 LTO，链接快很多
.\build.cmd -Package   # 顺便打包成带 Qt 运行库的 zip（放到 dist\）
```

`build.cmd` / `package.cmd` 只是把 `build.ps1` / `package.ps1` 换成
`powershell -ExecutionPolicy Bypass -File ...` 调用：Windows 客户端默认的执行策略是 `Restricted`，
直接 `.\build.ps1` 会报「因为在此系统上禁止运行脚本」。
不想用包装脚本的话，二选一：

```powershell
# A. 只对当前窗口生效
powershell -NoProfile -ExecutionPolicy Bypass -File .\package.ps1

# B. 只对本用户放开（不需要管理员）
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

其它平台的依赖与构建方式见 [BUILD.md](BUILD.md)。

### 打包成能直接运行的完整软件

`package.ps1` 会在构建之后把 Qt 运行库铺到 `lemon.exe` 旁边，并删掉用不到的文件，
让 `build\` 拷到别的机器上也能直接跑：

```powershell
.\package.cmd                 # 构建 + 就地部署到 build\（含题面模板），最后自检
.\package.cmd -NoBuild        # 不重新构建，只部署 / 精简
.\package.cmd -OutDir .\out   # 生成一个干干净净的发布目录（不含任何构建中间产物）
.\package.cmd -PruneBuildJunk # 就地部署时顺便清掉 *.obj / CMakeFiles\ 等（下次构建会完整重编）
```

（用 `package.ps1` 时要先解决执行策略的问题，两种办法见上一节。）

它会删掉这些用不到的东西：`opengl32sw.dll`、`D3Dcompiler_47.dll`、Qt 自带的几十种语言翻译
（只留 `zh_CN` / `zh_TW`，界面文字用的是程序内嵌的翻译）、多余的图片格式插件（只留 SVG）、
调试符号，以及 `tls` / `networkinformation` / `generic` 这些插件目录。

每次部署完都会跑一遍 `lemon.exe --self-test`（把 PATH 里的 Qt / MinGW 全部去掉），
确认平台插件、SVG 图标、题面模板、内嵌翻译和主窗口都正常；要发布前能放心。

## 快速使用

新建比赛 → 新建比赛日 → 添加试题 → 评测，与上游相同。这个分支多出来的部分集中在四个选项卡：

-   **题面**：编辑 Markdown，点「导出并打开 PDF」编译题面（编译完直接打开），PDF 放在 `<比赛日>/statement/` 下。文件名可以在「PDF 文件名」里自定义，支持占位符：`<day>` 比赛日文件名、`<title-day>` 比赛日标题、`<title>` 比赛标题（默认 `statement.pdf`）。右侧的 PDF 预览**按预览窗宽度、按屏幕像素比渲染**（缩小窗口 / 拖分隔条、把程序拖到另一块缩放比例不同的屏幕都会自动重渲染），所以文字是清晰的；预览不提供缩放控件，页面宽度就是预览窗宽度。选 `ccpc` 模板时，试题标题下方还有「输入文件 / 输出文件 / 时间限制 / 内存限制」四栏（对应模板里的 `\proinfo`），新建试题默认填入 `Standard Input / Standard Output / 1000 ms / 512 MiB`，可以按题修改；留空的词条不会输出。
-   **数据**：把生成器 `gen.cpp`（签名固定为 `int main(int sub_id, int test_id)`，把这一组数据写到**标准输出**）与标程 `sol.cpp` 放进 `problem/<题>/generator/`，在选项卡里给参数（文件名前缀、样例文件名前缀（留空沿用文件名前缀）、是否按子任务配置、是否按子任务命名、测试点数目 / 子任务分布（如 `5,5,5,10`，用英文逗号分隔）、样例数目、输入输出后缀），点「生成数据」就会编译、逐个跑 `gen(sub_id, test_id)` 存成 `data/<文件名><后缀>`，再用标程生成答案，并自动配好测试点；样例用 `sub_id = -1` 生成，输入与答案放进 `down/`。编译出来的 `gen.exe` / `sol.exe` 放在系统临时目录的沙箱里（每次生成结束就清掉，题目目录里只留源码和数据）；每道题的生成参数（文件名前缀、样例文件名前缀、测试点数目、子任务分布、样例数目、后缀、写入测试点的方式）记在 `problem/<题>/generator/config.json`，切题时会自动带出来。生成结果写到测试点有三种方式：默认「覆盖原有测试点设置」（会先清掉 `data/` 与 `down/` 里的旧文件），也可以只「追加测试点」（保留旧文件，遇到同名文件直接报错停止，不会悄悄覆盖），两项都不勾选则不修改测试点、同样不允许覆盖已有文件。下方的表格分左右两组（编号 / 文件 / 状态 各三列，右侧再重复一组），编号按竖列从上往下排：先排满左列，再接着排右列（1、2、3 在左列，4、5 在右列）。每个测试点的「文件」格里并排列出它的输入 / 输出两个文件名，「状态」格里则是这两个文件各自的生成状态（未开始 / 生成中 / 正常完成 / 状态异常）；样例同样按竖列排列，编号只写序号，测试点与样例之间有一行「样例数据」作分隔。状态记在 `problem/<题>/generator/status.json`，切题或重开程序后仍然保留。表格按「组」选择：一组就是一个测试点的三列，拖动会选中拖出的矩形范围内的组（只在左列下拉就只选左列，横向拖到右列则这段行里的两列一起选中），Ctrl 点击可以加选、Shift 点击可以连选。选中后点「重新生成选中项」，或在选中的组上右键选「重新生成选中的数据」，就只重跑这些测试点（开始时先把这些测试点的状态重置为「未开始」，会重新编译 `gen.cpp` / `sol.cpp`，但不动其他数据和已有的测试点配置）；看不到日志时，把鼠标停在状态上会显示这一行的具体信息。新建的 `generator/` 会自动放一份 `gen.cpp` 模板（含 `rand_int(l, r)` 随机数工具，主函数签名见模板注释）；前缀支持 `<name>`（题目文件名）、`<subid>`（子任务编号，需开启「按子任务命名」）、`<testid>`（测试点编号）；测试点分数在 100 分内平均分配，子任务之间逐级依赖。
-   **导出**：选择包类型与选项，导出到 `<比赛日>/dist/export/`；选手目录包里可以在「题面文件」下拉框里挑 `statement/` 下的哪个文件进包。
-   **统计**：查看整场统计，导出到 `<比赛日>/dist/reports/`。

比赛的导出物统一放在 `<比赛日>/dist/` 下；早期版本遗留在根目录的 `export/`、`reports/` 会在打开比赛日时自动清掉。

打开比赛日时还会自动检查并修复配置：测试点指向的文件找不到时，会在该题目的目录里（优先 `data/`，支持子任务子目录）按文件名找到真实文件，把路径改写成 `problem/<题>/<子目录>/<文件>`；为 0 的默认时限 / 内存会按已有测试点或通用设置补齐（测试点自身为 0 的限值同样补齐）；空白的答案文件后缀会补成通用设置里的值；特殊评测 / 交互题的可执行文件路径若只在 `graders/` 下存在，也会自动改到正确位置。修好的配置会立即写回比赛日文件，修复条目数量显示在状态栏，每条明细写在日志里。批量修复也可以直接用命令行：`lemon.exe --repair-day <比赛日文件>`（原地写回，可重复执行）。

### 评测：按 CCF 规范（可在「通用设置」里改）

依据《关于NOI系列赛编程语言使用限制的规定》与《……补充说明》，评测环节按下面这些规则来（编程语言与编译选项的限定不在本系统的管辖范围）：

-   **程序必须正常结束且 `main()` 返回 0**（编程通则 3）：非 0 退出码直接判**运行时错误**。开关：「通用设置 → Require Return 0」（默认开）。解释型语言看的是**解释器的退出码**：Python 脚本正常跑完就是 0，抛了未捕获的异常（Traceback）或 `sys.exit(1)` 就是非 0 → 判运行时错误；Java 同理（未捕获异常时 JVM 退出码非 0）。也就是说 Python 不需要写 `return 0`，只要别异常退出即可；如果你的脚本习惯用非 0 退出码表示别的东西，把这个开关取消勾选，就回到「只看输出」的判定。
-   **源程序不得大于 100 KB**（编程通则 2）：超限的文件不会被编译，编译消息里会写清楚是哪个文件、超过了多少 —— 默认值是 CCF 的 100 KB，可在「通用设置 → Source File Size Limit」改。
-   **同时提交多个源程序时的选取顺序**（编程通则 1）由**编译器列表的顺序**决定：评测按编译器列表逐个试，第一个能匹配到源文件并编译成功的编译器说了算。所以把 `gcc`（后缀 `c`）排在 `g++`（后缀 `cpp;cc;cxx`）前面，就得到 CCF 要求的 `.c → .cpp → .pas`；同一个语言声明多个后缀（`cpp;cc;cxx`、`pas;pp;inc`）时，也按声明的先后顺序取第一个存在的文件。要调整顺序就在「选项 → 编译器」里拖动 / 增删编译器。
-   **静态与动态内存总和默认 128 MB**（编程通则 5）：「通用设置 → Default Memory Limit」的默认值已对齐 CCF，逐题还可以在试题设置里再改。
-   **严禁的操作**（编程通则 4：访问网络、`fork` / `exec` / `system` 或其它进程 / 线程生成函数、运行其它程序……）：限制清单按**编译器（语言）**保存，在「选项 → 编译器 → 高级」里逐条增删；比赛日的「违规检测」只勾选启用哪些语言的限制。默认清单由程序按**编译器名 / 源文件后缀**从内置的语言命令表里读取（新建编译器、读取旧设置、点「恢复默认」都走这条路径），目前内置五份：C/C++（CCF 清单：`system`、`popen`、`fork`、`exec` / `execl` / `execv` / `execve`、`CreateProcess`、`WinExec`、`ShellExecute`、`CreateThread`、`_beginthread`、`pthread_create`、`socket`、`WSAStartup`、`gethostbyname`、`#pragma`、`__asm__`/`asm`，以及 `windows.h` / `unistd.h` / `sys/socket.h` / `winsock2.h`）、Free Pascal（`ExecuteProcess`、`fpSystem`、`fpFork`、`BeginThread` 及 `process` / `BaseUnix` / `Sockets` 单元）、FreeBASIC（`Exec`、`Chain`、`Open Pipe`、`windows.bi`）、Java（`exec`、`ProcessBuilder`、`Runtime.getRuntime`、`java.net`、`java.lang.Thread`）、Python（`os.system` / `subprocess` / `socket` / `threading` 等）；认不出语言的自定义编译器回退到 C/C++ 清单。命中即整位选手判 0 分。检测时会先屏蔽注释与字符串，所以「注释里写了 system(」不算违规。老比赛日文件里那份全局清单仍按原样生效，在比赛日设置里保存一次后改为按语言勾选。
-   源程序文件名与后缀要求全小写（编程通则 1）没有强制：国内很多比赛日的题目就叫 `T1` / `A+B`，强制小写会误伤；需要的话在对应编译器的限制里加一条「字符串」规则。

## 用户手册

[在线用户手册](https://project-lemonlime.github.io/Project_LemonLime/)（上游版本；本分支新增的功能以上一节的说明与界面提示为准）。

## Credit

本项目按 GPL-3.0-or-later 授权，见 [LICENSE](LICENSE)。

```
Copyright (c) 2019-2022 Project LemonLime.

Libraries and other files that have been used in LemonLime are listed below:

Copyright (c) 2020 Itay Grudev (@itay-grudev): SingleApplication (MIT)

Copyright (c) 2020 Qv2ray Development Group (@Qv2ray): Design of Translator/Log, Project Structure and CI files (GPLv3)
```
