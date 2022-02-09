# 名称：fpack

#### 一个文件/目录打包解包小程序，兼顾打包并伪装成JPEG文件功能。

<br>

# 支持平台

#### 支持linux平台
- 测试环境：wsl2-Ubuntu 20.4.3 LTS；编译器：GCC 9.3.0。

#### 支持win32平台
- 测试环境：Windows 10 x64 19044；编译器：MinGW-GCC 11.2.0 / VS2022-msvc_v142/143；WinSDK：Windows 10 10.0.19041.0。

#### 其他环境未测试

<br>

# 源代码编译方法

源代码仓库：
- GITEE: https://gitee.com/hrpzcf/fpack
- GITHUB: https://github.com/hrpzcf/fpack

<br>

linux平台编译方法：
1. 安装CMake，安装GCC编译器；
2. 从源代码仓库下载/克隆fpack项目源代码至本地；
3. 在fpack项目目录打开命令窗口；
4. 输入命令：`cmake -DCMAKE_BUILD_TYPE:STRING=Release -B./build -G "Unix Makefiles"`;
5. 输入命令：`cmake --build ./build`；
6. 等待编译完成，生成的可执行文件在`fpack/execfile`目录下，名为`fpack`。

<br>

Windows平台编译方法：
- MinGW-GCC编译器：
  1. 安装CMake，安装GCC编译器(MinGW)；
  2. 从源代码仓库下载/克隆fpack项目源代码至本地；
  3. 在fpack项目目录打开命令窗口；
  4. 输入命令：`cmake -DCMAKE_BUILD_TYPE:STRING=Release -B./build -G "MinGW Makefiles`;
  5. 输入命令：`cmake --build ./build`；
  6. 等待编译完成，生成的可执行文件在`fpack/execfile`目录下，名为`fpack.exe`。

<br>

- Visual Studio：
    1. 安装宇宙最强IDE`Visual Studio`；
    2. 从源代码仓库下载/克隆fpack项目源代码至本地；
    3. 打开`fpack/msbuild`目录；
    4. 使用`Visual Studio`打开解决方案文件`pcbuild.sln`；
    5. 选择上方工具栏`解决方案配置`为`Release`，`x64`；
    6. 选择上方菜单栏`生成`->`生成解决方案`；
    7. 等待编译完成，生成的可执行文件在`fpack/execfile`目录下，名为`fpack.exe`。

<br>

# 使用方法

1. 将`fpack`或`fpakc.exe`所在目录路径加入系统环境变量PATH(可不加)；
2. 输入命令`fpack -h`查看帮助。
3. 如果未加入系统环境变量PATH中，则需在命令窗口中cd至`fpack`所在目录，运行命令`./fpack -h`。如果你使用的是Windows的CMD窗口则不用加`./`前缀，其他命令窗口需要`./`前缀。
