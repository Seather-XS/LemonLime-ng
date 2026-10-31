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
-   **数据**：把生成器 `gen.cpp`（签名固定为 `int main(int sub_id, int test_id)`，把这一组数据写到**标准输出**）与标程 `sol.cpp` 放进 `problem/<题>/generator/`，在选项卡里给参数（文件名前缀、样例文件名前缀（留空沿用文件名前缀）、是否按子任务配置、是否按子任务命名、测试点数目 / 子任务分布（如 `5,5,5,10`，用英文逗号分隔）、样例数目、输入输出后缀），点「生成数据」就会编译、逐个跑 `gen(sub_id, test_id)` 存成 `data/<文件名><后缀>`，再用标程生成答案，并自动配好测试点；样例用 `sub_id = -1` 生成，输入与答案放进 `down/`。编译出来的 `gen.exe` / `sol.exe` 放在系统临时目录的沙箱里（每次生成结束就清掉，题目目录里只留源码和数据）；每道题的生成参数（文件名前缀、样例文件名前缀、测试点数目、子任务分布、样例数目、后缀、写入测试点的方式）记在 `problem/<题>/generator/config.json`，切题时会自动带出来。生成结果写到测试点有三种方式：默认「覆盖原有测试点设置」（会先清掉 `data/` 与 `down/` 里的旧文件），也可以只「追加测试点」（保留旧文件，遇到同名文件直接报错停止，不会悄悄覆盖），两项都不勾选则不修改测试点、同样不允许覆盖已有文件。下方的表格按测试点列出每个输入 / 输出文件的生成状态（未开始 / 生成中 / 正常完成 / 状态异常），状态异常的行可以点「…」查看详情；这些状态记在 `problem/<题>/generator/status.json`，切题或重开程序后仍然保留。每个测试点（含样例）右侧还有一个「重新生成」按钮，只重跑这一个测试点（会重新编译 `gen.cpp` / `sol.cpp`，但不动其他数据和已有的测试点配置）。新建的 `generator/` 会自动放一份 `gen.cpp` 模板（含 `rand_int(l, r)` 随机数工具，主函数签名见模板注释）；前缀支持 `<name>`（题目文件名）、`<subid>`（子任务编号，需开启「按子任务命名」）、`<testid>`（测试点编号）；测试点分数在 100 分内平均分配，子任务之间逐级依赖。
-   **导出**：选择包类型与选项，导出到 `<比赛日>/dist/export/`；选手目录包里可以在「题面文件」下拉框里挑 `statement/` 下的哪个文件进包。
-   **统计**：查看整场统计，导出到 `<比赛日>/dist/reports/`。

比赛的导出物统一放在 `<比赛日>/dist/` 下；早期版本遗留在根目录的 `export/`、`reports/` 会在打开比赛日时自动清掉。

### 评测：按 CCF 规范（可在「通用设置」里改）

依据《关于NOI系列赛编程语言使用限制的规定》与《……补充说明》，评测环节按下面这些规则来（编程语言与编译选项的限定不在本系统的管辖范围）：

-   **程序必须正常结束且 `main()` 返回 0**（编程通则 3）：非 0 退出码直接判**运行时错误**。开关：「通用设置 → Require Return 0」（默认开）。解释型语言看的是**解释器的退出码**：Python 脚本正常跑完就是 0，抛了未捕获的异常（Traceback）或 `sys.exit(1)` 就是非 0 → 判运行时错误；Java 同理（未捕获异常时 JVM 退出码非 0）。也就是说 Python 不需要写 `return 0`，只要别异常退出即可；如果你的脚本习惯用非 0 退出码表示别的东西，把这个开关取消勾选，就回到「只看输出」的判定。
-   **源程序不得大于 100 KB**（编程通则 2）：超限的文件不会被编译，编译消息里会写清楚是哪个文件、超过了多少 —— 默认值是 CCF 的 100 KB，可在「通用设置 → Source File Size Limit」改。
-   **同时提交多个源程序时的选取顺序**（编程通则 1）由**编译器列表的顺序**决定：评测按编译器列表逐个试，第一个能匹配到源文件并编译成功的编译器说了算。所以把 `gcc`（后缀 `c`）排在 `g++`（后缀 `cpp;cc;cxx`）前面，就得到 CCF 要求的 `.c → .cpp → .pas`；同一个语言声明多个后缀（`cpp;cc;cxx`、`pas;pp;inc`）时，也按声明的先后顺序取第一个存在的文件。要调整顺序就在「选项 → 编译器」里拖动 / 增删编译器。
-   **静态与动态内存总和默认 128 MB**（编程通则 5）：「通用设置 → Default Memory Limit」的默认值已对齐 CCF，逐题还可以在试题设置里再改。
-   **严禁的操作**（编程通则 4：访问网络、`fork` / `exec` / `system` 或其它进程 / 线程生成函数、运行其它程序……）：限制清单按**编译器（语言）**保存，在「选项 → 编译器 → 高级」里逐条增删；比赛日的「违规检测」只勾选启用哪些语言的限制。默认清单由程序按**编译器名 / 源文件后缀**从内置的语言命令表里读取（新建编译器、读取旧设置、点「恢复默认」都走这条路径），目前内置五份：C/C++（CCF 清单：`system`、`popen`、`fork`、`exec` / `execl` / `execv` / `execve`、`CreateProcess`、`WinExec`、`ShellExecute`、`CreateThread`、`_beginthread`、`pthread_create`、`socket`、`WSAStartup`、`gethostbyname`、`#pragma`、`__asm__`/`asm`，以及 `windows.h` / `unistd.h` / `sys/socket.h` / `winsock2.h`）、Free Pascal（`ExecuteProcess`、`fpSystem`、`fpFork`、`BeginThread` 及 `process` / `BaseUnix` / `Sockets` 单元）、FreeBASIC（`Exec`、`Chain`、`Open Pipe`、`windows.bi`）、Java（`exec`、`ProcessBuilder`、`Runtime.getRuntime`、`java.net`、`java.lang.Thread`）、Python（`os.system` / `subprocess` / `socket` / `threading` 等）；认不出语言的自定义编译器回退到 C/C++ 清单。命中即整位选手判 0 分。检测时会先屏蔽注释与字符串，所以「注释里写了 system(」不算违规。老比赛日文件里那份全局清单仍按原样生效，在比赛日设置里保存一次后改为按语言勾选。
-   源程序文件名与后缀要求全小写（编程通则 1）没有强制：国内很多比赛日的题目就叫 `T1` / `A+B`，强制小写会误伤；需要的话在对应编译器的限制里加一条「字符串」规则。

### 准考证：细节

> **当前状态**：准考证功能暂时停用 —— 源码与模板仍保留在仓库里（[src/admission/](src/admission/)、`assets/templates/admission/`），但不编进 `lemon.exe`，界面上没有这个选项卡，发布包里也不带模板。下面保留的是这套功能的设计说明，重新启用时按此对照；`lemon.exe --build-admission` 命令行入口同样已不可用。

**产物与目录**

-   名单里**一行就是一份 PDF**（不是一份大 PDF），输出到 `<比赛日>/dist/admission/`；是否按赛区 / 按考场分层、是否按赛区分包（`admission-<赛区>.zip`）由选项卡上的两个下拉框决定。
-   数据都在 `<比赛日>/admission/` 下：`config.json`（标题、**测试时间**、准考证号规则 `idRule`、排座位策略与输出文件名 / 目录 / 打包策略 `generatePolicy`）、`contest-notes.md`（比赛注意）；每个赛区一个 `regions/<赛区>/`，里面有 `notes.md`（注意事项）、`contestants.csv`（名单）、`columns.json`（列生成规则）、`rooms.json`（考点 / 考场与容量）。**未启用赛区时 `regions/` 自己就是唯一一组**。
-   名单的内置列是 **姓名 / 准考证号 / 考点 / 考场 / 座位号 / 照片**（固定在前、不可删改名），全部是**选手级**的：考点、考场、座位号逐人填（右键整列填充或直接在 `contestants.csv` 里写都行），照片写图片路径（相对赛区目录、或绝对路径都行）。留空就是空（考场不再有赛区级默认值）。
-   锁定行之外新加的**自定义列会自动印成额外行**（「列名 | 该行单元格」，单元格里同样可以用 `<row>` `<name>` `<room>` 等占位符），所以「考场位置」「学号」这类内容直接加一列写进名单就行。
-   生成时每人跑一次 xelatex；编译失败的那些会把 `.tex` / `.log` 留在 `dist/admission/_failed/` 里。
-   生成日志**每次重新开始记**（不会和上一次混在一起）：先是这次的分区 / 打包选项与要生成的赛区，然后每个赛区「人数 → 输出目录」、每个选手「姓名（准考证号）→ 文件名」、赛区小结（成功 / 失败 / 用时）、最后的总体小结与输出目录；谁编译不过，那一行会带上 xelatex 的报错摘要。

**版面**

-   版式是程序内置、**不开放编辑**的 `templates/admission/main.tex`：表格前 6 行锁定，标题、注意事项、比赛注意与额外行都有固定位置；表格右侧那一列是**照片框**（写路径的那一列有图就放进去，`keepaspectratio` 等比缩放，没填就是空框）；自带思源宋体（`templates/admission/fonts/`），字体缺失时回退系统字体。
-   注意事项、比赛注意与额外行单元格都是**纯文本**，只额外认 Markdown 的**超链接**语法 `[显示文字](链接)`（会变成可点的链接）；`**加粗**`、`# 标题`、`- 列表` 这些一律按原样印出来，不需要装 pandoc。
**考点与考场（每个赛区一套）**

-   打开「**考点 / 考场**」编辑区有四个入口：选项卡第二行的按钮、名单编辑器右下角的按钮、赛区表右键、排座位对话框里的「编辑考点 / 考场…」。编辑区是树形：**一个赛区可以有多个考点，每个考点下面挂多个考场**；考点行只写名字（加粗，不缩进容量），**容量只写在考场那一行（≥1）**。方案存在 `regions/<赛区>/rooms.json`。
-   **排座位就以此为唯一根据**（不再从名单里的考场列猜），一次会把名单的**考点 / 考场 / 座位号**三列都写好：
    -   摊法两种：「尽量先填满靠前的考点 / 考场」，或「各考场平均分配」；顺序可以按名单行号或随机（座位号的位数由人数最多的考场决定）。
    -   座位号每个考场都从 1 开始，**位数由全赛区人数最多的那个考场决定**（比如各考场 15 / 8 / 14 人 → 两位，`01`…`15`）；这个位数在同赛区的所有考点 / 考场间是统一的。
    -   总容量不够会直接报错（`15 contestants but the rooms hold only 4.`），不会默默塞满。

**名单编辑器**

-   右键列头：整列填固定值 / 序列 / 行号 / 清空、加列 / 改名 / 删列、「配置本列」、列统计；右键单元格：批量填充、**在末尾增加行 / 列**、**删除选定的行 / 列**（内置列不会真被删掉；删列会连它的生成规则一起清）；照片列的右键菜单多一项「选择图片…」（选中的行会一起填上，能放进赛区目录就用相对路径）；拖选区右下角的**填充柄**往下 / 往右填（数字按递增，其余按块循环复制）；`Ctrl+Z` / `Ctrl+Y` 撤销重做（一步一张表）。表格里**只有名单里真实的行**：最后一行双击还能顺手再添一行，空行不会写进 `contestants.csv`。
-   「排座位」以考点 / 考场方案为准（见上）：选摊法、顺序，对话框里直接预览每个考点 / 考场分到多少人（xx / 容量）、座位号位数与逐人结果，旁边就有「编辑考点 / 考场…」；「生成准考证号」会先弹出**策略对话框**：只填模板（`<section> <number> <char> <row> <seat> <room> <name> <id>` 与名单里的任意列）—— **数字一律取自名单行号**（`<number>` 的个数就是位数，`<number><number><number>` → 001、002…，不再让人挑来源），模板里写了 `<char>` 才会多出一行「字母取自」；下面实时预览全部号码。两个对话框的预览都是**只读、可滚动的文本框**（人数 / 考场再多也塞得下）。生成的号码**一律覆盖已有的**（想保留旧号就别点这个按钮）；确定后这套策略写进 `config.json` 的 `idRule`，下次打开还是它。默认模板是 `<section>-S<number>×5`（即 `HN-S00001`），不想带赛区前缀把 `<section>-` 删掉即可。
-   导入 `contestants.csv` 时自动识别 UTF-8 与 GBK / GB18030，国内系统导出的名单可以直接沿用。

**列生成规则（`columns.json`）**

-   每列可以派生：**模板**用占位符拼（`<section> <number> <char> <row> <col> <name> <id> <seat> <room>`，以及名单里的其它列名；连续同类的 `<number>` / `<char>` 算一组，位数就是位数，数字零填充、字母 `A`…`ZZ`），**随机**则设长度与字符集（可排除易混字符），勾上「保持值不变」后按行号 / 准考证号 / 姓名做种子，重新生成也是同一个值。
-   模板列按列序求值，只能引用左边已经算好的列；**生成准考证时模板列会重算，随机列只填空值**（规则保存在 `columns.json`，不用手动点按钮）。

题面 Markdown 的写法、各选项的含义等细节，见用户手册与本节说明。

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
