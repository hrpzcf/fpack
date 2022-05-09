#include <stdlib.h>
#include <string.h>

#ifndef _MSC_VER
#include <getopt.h>
#else
#include "../msch/getopt.h"
#endif // _MSC_VER

#include "../fpack/fpack.h"
#include "../ospath/ospath.h"

#define M2S1(x) #x
#define M2S2(x) M2S1(x)

#ifdef _MSC_VER
#define COMPILER "MSC " M2S2(_MSC_VER)
#elif defined(__GNUC__)
#define COMPILER "GCC "__VERSION__
#else
#define COMPILER "未知"
#endif

#define VERSION     "0.1.1"
#define AUTHOR_INFO "作者：hrpzcf，主页：https://github.com/hrpzcf"

#ifdef _WIN32
#ifndef _WIN64
#define PLATFORM "win32"
#else
#define PLATFORM "win_x64"
#endif
#elif defined(__linux__)
#ifndef __x86_64
#define PLATFORM "linux"
#else
#define PLATFORM "linux_x64"
#endif
#else
#define PLATFORM "posix"
#endif

#define BUILT_INFO \
    "名称：%s，版本：" VERSION ", 平台: " PLATFORM ", 编译器: " COMPILER \
    "，编译时间：" __DATE__ ", " __TIME__

int parse_cmds(int argc, char **argvs) {
    bool overwrite = false;
    bool add = false;
    bool subs = false;
    int sub_opt;
    fpack_t *fpack; // 主文件信息结构体指针
    static char fpack_file_path[PATH_MSIZE];
    static char target_dir_path[PATH_MSIZE];
    static char jpeg_file_path[PATH_MSIZE];
    static char exec_name[PATH_MSIZE];
    static char extract_name[PATH_MSIZE];
    const char *p_extr_name = extract_name;
    // 主命令，必须是第一个命令行参数
    const char *MAIN_HELP = "-h";   // 显示此程序的帮助信息
    const char *MAIN_VERS = "-v";   // 显示此程序的版本信息
    const char *MAIN_INFO = "info"; // 显示PACK文件信息及子文件列表
    const char *MAIN_PACK = "pack"; // 将目录及文件打包为PACK文件
    const char *MAIN_FAKE = "fake"; // 将目录及文件打包并伪装为JPEG图像
    const char *MAIN_EXTR = "extr"; // 从PACK文件中提取目录及文件

    const char *SUBOF_INFO = "p:";        // 主命令[info]的子选项
    const char *SUBOF_PACK = "p:t:osa";   // 主命令[pack]的子选项
    const char *SUBOF_FAKE = "j:p:t:osa"; // 主命令[fake]的子选项
    const char *SUBOF_EXTR = "p:t:n:o";   // 主命令[extr]的子选项
    // 使用帮助信息
    const char *PACK_USEAGE =
        "用法: %s [子命令] [选项[, 参数]]...\n\n"
        "可用的子命令:\n"
        "   [-v]\t\t显示程序版本信息及其他信息。\n"
        "   [-h]\t\t显示此帮助信息。\n"
        "   [info]\t显示PACK文件的子文件列表及其他信息。\n"
        "   [pack]\t将文件或目录打包为一个PACK文件。\n"
        "   [fake]\t将文件或目录打包并伪装为一个JPEG文件。\n"
        "   [extr]\t提取PACK文件中包含文件及目录。\n\n"

        "子命令的选项:\n"
        "   [info]命令选项:\n"
        "       [-p] "
        "文件路径\t要从中读取并显示子文件/目录列表及其他信息的PACK文件路径。\n"

        "   [pack]命令选项:\n"
        "       [-p] "
        "文件路径\t即将生成的PACK文件的路径，应包括文件名，文件名后缀名虽不影"
        "响打包，但建议以'.pfs'结尾。\n"
        "       [-t] "
        "路径\t即将被打包的目标，该目标将被打包到[-p]"
        "选项指定的PACK文件。此选项可以指定文件或目录路径。\n"
        "       "
        "[-s]\t\t如果[-t]选项指定的是一个目录，则搜索该目录的子目录，如果[-t]"
        "选项指定的是文件则不生效。忽略此选项则不搜索子目录。\n"
        "       "
        "[-a]\t\t如果[-p]"
        "选项指定的文件已存在，指定此选项将把目标追加到已存在的文件，忽略此选"
        "项则直接退出。[-p]选项指定的文件不存在则此选项不生效。\n"

        "   [fake]命令选项:\n"
        "       [-p] "
        "文件路径\t即将生成的伪装成JPEG图片的PACK文件的路径，应包括文件名，文"
        "件名后缀名虽不影响打包，但建议以'.jpeg'"
        "结尾，这样生成的文件表面看起来就是正常的JPEG文件。\n"
        "       [-j] "
        "文件路径\t有效的JPEG文件的路径，此路径的JPEG文件不会被修改，程序将使"
        "用其副本。此路径指定的文件需是真正的JPEG文件，通过更改其他格式文件的"
        "后缀名是无效的。\n"
        "       [-t] "
        "路径\t即将被打包的目标，该目标将被打包到[-p]"
        "选项指定的PACK文件。此选项可以指定文件或目录路径。\n"
        "       "
        "[-s]\t\t如果[-t]选项指定的是一个目录，则搜索该目录的子目录，如果[-t]"
        "选项指定的是文件则不生效。忽略此选项则不搜索子目录。\n"
        "       "
        "[-a]\t\t如果[-p]"
        "选项指定的文件已存在，指定此选项将把目标追加到已存在的文件，忽略此选"
        "项则直接退出。[-p]选项指定的文件不存在则此选项不生效。\n"

        "   [extr]命令选项:\n"
        "       [-p] "
        "文件路径\tPACK文件的路径，程序将从此路径指示的PACK文件提取子文件或目"
        "录。\n"
        "       [-t] "
        "目录路径\t此选项指定提取子文件时子文件的保存目录，忽略此选项则提取到"
        "当前目录。\n"
        "       [-n] "
        "文件名\t想要从[-p]"
        "选项指定的PACK文件中提取的子文件或目录的名称。注意，这里<文件名>"
        "指的是使用info命令列出的完整名称，忽略此选项则提取全部。\n"
        "       "
        "[-o]\t\t提取子文件时，如果[-t]"
        "选项指定的目录中已存在同名文件，此选项指示将其覆盖，忽略此选项则不提"
        "取该子文件。\n\n";

    if (argc < 2) {
        fprintf(stderr,
                PACK_ERROR "命令行参数不足，请使用-h命令查看使用帮助。\n");
        return EXIT_CODE_FAILURE;
    }
    optind = 2; // 查找参数从第3个开始，否则查不到（getopt.h全局变量）
    path_splitext(exec_name, PATH_MSIZE, NULL, 0,
                  path_basename(NULL, 0, argvs[0]), '.');
    if (!strcmp(argvs[1], MAIN_HELP)) {
        printf(PACK_USEAGE, exec_name);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAIN_VERS)) {
        printf(AUTHOR_INFO "\n" BUILT_INFO "\n", exec_name);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAIN_PACK)) {
        while ((sub_opt = getopt(argc, argvs, SUBOF_PACK)) != -1) {
            switch (sub_opt) {
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, PACK_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(fpack_file_path, optarg);
                break;
            case 't':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, PACK_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(target_dir_path, optarg);
                break;
            case 'a':
                add = true;
                break;
            case 's':
                subs = true;
                break;
            case 'o':
                overwrite = true;
                break;
            default:
                fprintf(stderr,
                        PACK_ERROR
                        "没有此选项：-%c，请使用'%s -h'命令查看使用帮助。",
                        optopt, exec_name);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*target_dir_path) {
            fprintf(stderr, PACK_ERROR "未输入目标路径(应由[-t]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*fpack_file_path) {
            path_abspath(fpack_file_path, PATH_MSIZE, target_dir_path);
            path_dirname(fpack_file_path, PATH_MSIZE, fpack_file_path);
        }
        if (!*fpack_file_path) {
            fprintf(stderr, PACK_ERROR "未输入PACK路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (path_exists(fpack_file_path))
            fpack = fpack_open(fpack_file_path);
        else
            fpack = fpack_make(fpack_file_path, overwrite);
        fpack_pack(target_dir_path, subs, fpack, add);
        fpack_close(fpack);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAIN_EXTR)) {
        while ((sub_opt = getopt(argc, argvs, SUBOF_EXTR)) != -1) {
            switch (sub_opt) {
            case 'n':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, PACK_ERROR "输入的文件名过长\n");
                    return EXIT_CODE_FAILURE;
                }
                strcpy(extract_name, optarg);
                break;
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, PACK_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(fpack_file_path, optarg);
                break;
            case 't':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, PACK_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(target_dir_path, optarg);
                break;
            case 'o':
                overwrite = true;
                break;
            default:
                fprintf(stderr,
                        PACK_ERROR
                        "没有此选项：-%c，请使用'%s -h'命令查看使用帮助。",
                        optopt, exec_name);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*fpack_file_path) {
            fprintf(stderr, PACK_ERROR "未输入主文件路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*target_dir_path)
            strcpy(target_dir_path, PATH_CDIRS);
        if (!*extract_name)
            p_extr_name = NULL;
        if (is_fake_jpeg(fpack_file_path))
            fpack = fpack_fakej_open(fpack_file_path);
        else
            fpack = fpack_open(fpack_file_path);
        fpack_extract(p_extr_name, target_dir_path, overwrite, fpack);
        fpack_close(fpack);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAIN_INFO)) {
        while ((sub_opt = getopt(argc, argvs, SUBOF_INFO)) != -1) {
            switch (sub_opt) {
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, PACK_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(fpack_file_path, optarg);
                break;
            default:
                fprintf(stderr,
                        PACK_ERROR
                        "没有此选项：-%c，请使用'%s -h'命令查看使用帮助。",
                        optopt, exec_name);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*fpack_file_path) {
            fprintf(stderr, PACK_ERROR "未输入主文件路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        fpack = fpack_info(fpack_file_path);
        fpack_close(fpack);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAIN_FAKE)) {
        while ((sub_opt = getopt(argc, argvs, SUBOF_FAKE)) != -1) {
            switch (sub_opt) {
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, PACK_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(fpack_file_path, optarg);
                break;
            case 't':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, PACK_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(target_dir_path, optarg);
                break;
            case 'a':
                add = true;
                break;
            case 's':
                subs = true;
                break;
            case 'o':
                overwrite = true;
                break;
            case 'j':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, PACK_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(jpeg_file_path, optarg);
                break;
            default:
                fprintf(stderr,
                        PACK_ERROR
                        "没有此选项：-%c，请使用'%s -h'命令查看使用帮助。",
                        optopt, exec_name);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*target_dir_path) {
            fprintf(stderr, PACK_ERROR "未输入目标路径(应由[-t]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*fpack_file_path) {
            path_abspath(fpack_file_path, PATH_MSIZE, target_dir_path);
            path_dirname(fpack_file_path, PATH_MSIZE, fpack_file_path);
        }
        if (!*fpack_file_path) {
            fprintf(stderr, PACK_ERROR "未输入PACK路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*jpeg_file_path) {
            fprintf(stderr, PACK_ERROR "未输入JPEG路径(应由[-j]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (path_exists(fpack_file_path)) {
            printf(PACK_WARN "已存在PACK文件，[-j]及[-o]选项将不生效。\n");
            fpack = fpack_fakej_open(fpack_file_path);
        } else {
            fpack =
                fpack_fakej_make(fpack_file_path, jpeg_file_path, overwrite);
        }
        fpack_pack(target_dir_path, subs, fpack, add);
        fpack_close(fpack);
        return EXIT_CODE_SUCCESS;
    } else {
        fprintf(stderr,
                PACK_ERROR "没有此命令：%s，请使用'%s -h'命令查看使用帮助。\n",
                argvs[1], exec_name);
        return EXIT_CODE_FAILURE;
    }
    return EXIT_CODE_SUCCESS;
}

int main(int argc, char *argvs[]) {
// DEBUG 宏 PACK_DEBUG 定义位置：
// CMAKE工程：定义在'./CMakeLists.txt'中
// VS工程：定义在'属性管理器->msbuild->Debug'中
#ifdef PACK_DEBUG
    printf(PACK_WARN "调试：请更改'entry->main.c->main'函数的调试参数\n");
    argc = 6;
    char *cust[256];
    // ./fpack pack -p ./p.pfs -t ./
    cust[0] = "./fpack";
    cust[1] = "pack";
    cust[2] = "-p";
    cust[3] = "./p.pfs";
    cust[4] = "-t";
    cust[5] = "./";
    // cust[6] = "-j";
    // cust[7] = "1.jpeg";
    return parse_cmds(argc, cust);
#else
    return parse_cmds(argc, argvs);
#endif
}
