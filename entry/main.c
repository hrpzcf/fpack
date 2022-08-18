#include <stdlib.h>
#include <string.h>

#ifndef _MSC_VER
#include <getopt.h>
#else
#include "../msch/getopt.h"
#endif // _MSC_VER

#include "../fpack/fpack.h"
#include "../ospath/ospath.h"
#include "info.h"
#include "main.h"

int ParseCommands(int argc, char **argvs) {
    bool overwrite = false;
    bool add = false;
    bool subs = false;
    int sub_opt;
    FPACK_T *fpack; // 主文件信息结构体指针
    static char fpack_file_path[PATH_MSIZE];
    static char target_dir_path[PATH_MSIZE];
    static char jpeg_file_path[PATH_MSIZE];
    static char executable_name[PATH_MSIZE];
    static char extract_name[PATH_MSIZE];
    const char *p_extr_name = extract_name;
    // 主命令，必须是第一个命令行参数
    const char *MAINCMD_HELP = "help"; // 显示此程序的帮助信息
    const char *MAINCMD_VERS = "vers"; // 显示此程序的版本信息
    const char *MAINCMD_INFO = "info"; // 显示<.fp>文件信息及其子文件列表
    const char *MAINCMD_PACK = "pack"; // 将目录或文件打包为<.fp>文件
    const char *MAINCMD_FAKE = "fake"; // 打包目录或文件并将其伪装为JPEG文件
    const char *MAINCMD_EXTR = "extr"; // 从<.fp>文件中提取目录或文件

    const char *SUBCMD_INFO = "p:";        // 主命令[info]的子选项
    const char *SUBCMD_PACK = "p:t:osa";   // 主命令[pack]的子选项
    const char *SUBCMD_FAKE = "j:p:t:osa"; // 主命令[fake]的子选项
    const char *SUBCMD_EXTR = "p:t:n:o";   // 主命令[extr]的子选项

    if (argc < 2) {
        fprintf(stderr, MESSAGE_ERROR "命令行参数不足，请使用-h命令查看使用帮助。\n");
        return EXIT_CODE_FAILURE;
    }
    optind = 2; // 查找参数从第3个开始，否则查不到（getopt.h全局变量）
    OsPathSplitExt(executable_name, PATH_MSIZE, NULL, 0, OsPathBaseName(NULL, 0, argvs[0]), '.');
    if (!strcmp(argvs[1], MAINCMD_HELP)) {
        printf(COMMANDUSAGE, executable_name);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAINCMD_VERS)) {
        printf(AUTHOR_INFO "\n" BUILT_INFO "\n", executable_name);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAINCMD_PACK)) {
        while ((sub_opt = getopt(argc, argvs, SUBCMD_PACK)) != -1) {
            switch (sub_opt) {
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(fpack_file_path, optarg);
                break;
            case 't':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
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
                fprintf(stderr, MESSAGE_ERROR "没有此选项：-%c，请使用'%s -h'命令查看使用帮助。", optopt, executable_name);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*target_dir_path) {
            fprintf(stderr, MESSAGE_ERROR "未输入目标路径(应由[-t]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*fpack_file_path) {
            OsPathAbsolutePath(fpack_file_path, PATH_MSIZE, target_dir_path);
            OsPathDirName(fpack_file_path, PATH_MSIZE, fpack_file_path);
        }
        if (!*fpack_file_path) {
            fprintf(stderr, MESSAGE_ERROR "未输入<.fp>路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (OsPathExists(fpack_file_path))
            fpack = FilePackOpen(fpack_file_path);
        else
            fpack = FilePackMake(fpack_file_path, overwrite);
        FilePackPack(target_dir_path, subs, fpack, add);
        FilePackClose(fpack);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAINCMD_EXTR)) {
        while ((sub_opt = getopt(argc, argvs, SUBCMD_EXTR)) != -1) {
            switch (sub_opt) {
            case 'n':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "输入的文件名过长\n");
                    return EXIT_CODE_FAILURE;
                }
                strcpy(extract_name, optarg);
                break;
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(fpack_file_path, optarg);
                break;
            case 't':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(target_dir_path, optarg);
                break;
            case 'o':
                overwrite = true;
                break;
            default:
                fprintf(stderr, MESSAGE_ERROR "没有此选项：-%c，请使用'%s -h'命令查看使用帮助。", optopt, executable_name);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*fpack_file_path) {
            fprintf(stderr, MESSAGE_ERROR "未输入主文件路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*target_dir_path)
            strcpy(target_dir_path, PATH_CDIRS);
        if (!*extract_name)
            p_extr_name = NULL;
        if (FilePackIsFakeJPEG(fpack_file_path))
            fpack = FilePackOpenFakeJPEG(fpack_file_path);
        else
            fpack = FilePackOpen(fpack_file_path);
        FilePackExtract(p_extr_name, target_dir_path, overwrite, fpack);
        FilePackClose(fpack);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAINCMD_INFO)) {
        while ((sub_opt = getopt(argc, argvs, SUBCMD_INFO)) != -1) {
            switch (sub_opt) {
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(fpack_file_path, optarg);
                break;
            default:
                fprintf(stderr, MESSAGE_ERROR "没有此选项：-%c，请使用'%s -h'命令查看使用帮助。", optopt, executable_name);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*fpack_file_path) {
            fprintf(stderr, MESSAGE_ERROR "未输入主文件路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        fpack = FilePackInfo(fpack_file_path);
        FilePackClose(fpack);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAINCMD_FAKE)) {
        while ((sub_opt = getopt(argc, argvs, SUBCMD_FAKE)) != -1) {
            switch (sub_opt) {
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(fpack_file_path, optarg);
                break;
            case 't':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
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
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(jpeg_file_path, optarg);
                break;
            default:
                fprintf(stderr, MESSAGE_ERROR "没有此选项：-%c，请使用'%s -h'命令查看使用帮助。", optopt, executable_name);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*target_dir_path) {
            fprintf(stderr, MESSAGE_ERROR "未输入目标路径(应由[-t]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*fpack_file_path) {
            OsPathAbsolutePath(fpack_file_path, PATH_MSIZE, target_dir_path);
            OsPathDirName(fpack_file_path, PATH_MSIZE, fpack_file_path);
        }
        if (!*fpack_file_path) {
            fprintf(stderr, MESSAGE_ERROR "未输入<.fp>路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*jpeg_file_path) {
            fprintf(stderr, MESSAGE_ERROR "未输入JPEG路径(应由[-j]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (OsPathExists(fpack_file_path)) {
            printf(MESSAGE_WARN "已存在<.fp>文件，[-j]及[-o]选项将不生效。\n");
            fpack = FilePackOpenFakeJPEG(fpack_file_path);
        } else {
            fpack = FilePackMakeFakeJPEG(fpack_file_path, jpeg_file_path, overwrite);
        }
        FilePackPack(target_dir_path, subs, fpack, add);
        FilePackClose(fpack);
        return EXIT_CODE_SUCCESS;
    } else {
        fprintf(stderr, MESSAGE_ERROR "没有此命令：%s，请使用'%s -h'命令查看使用帮助。\n", argvs[1], executable_name);
        return EXIT_CODE_FAILURE;
    }
    return EXIT_CODE_SUCCESS;
}

int main(int argc, char *argvs[]) {
// DEBUG 宏 PACK_DEBUG 定义位置：
//      CMAKE 工程：定义在'./CMakeLists.txt'中
//      VS 工程：定义在'属性管理器->msbuild->Debug'中
#ifdef PACK_DEBUG
    printf(MESSAGE_WARN "调试：请更改'entry->main.c->main'函数的调试参数\n");
    argc = 6;
    char *cust[256];
    // ./fpack pack -p ./1.fp -t .
    cust[0] = "./fpack";
    cust[1] = "pack";
    cust[2] = "-p";
    cust[3] = "./1.fp";
    cust[4] = "-t";
    cust[5] = ".";
    // cust[6] = "-j";
    // cust[7] = "1.jpeg";
    return ParseCommands(argc, cust);
#else
    return ParseCommands(argc, argvs);
#endif
}
