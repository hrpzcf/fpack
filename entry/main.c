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
    bool Overwrite = false;
    bool Append = false;
    bool Recursion = false;
    int SubOption;
    FPACK_T *pFilePack; // 主文件信息结构体指针
    static char MainFIlePath[PATH_MSIZE];
    static char TargetDirPath[PATH_MSIZE];
    static char JPEGFilePath[PATH_MSIZE];
    static char ExecutableName[PATH_MSIZE];
    static char NameToExtract[PATH_MSIZE];
    const char *pNameToExtract = NameToExtract;
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
        fprintf(stderr, MESSAGE_ERROR "命令行参数不足，请使用 help 命令查看使用帮助。\n");
        return EXIT_CODE_FAILURE;
    }
    optind = 2; // 查找参数从第3个开始，否则查不到（此变量是 getopt.h 全局变量）
    OsPathSplitExt(ExecutableName, PATH_MSIZE, NULL, 0, OsPathBaseName(NULL, 0, argvs[0]), '.');
    if (!strcmp(argvs[1], MAINCMD_HELP)) {
        printf(COMMANDUSAGE, ExecutableName);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAINCMD_VERS)) {
        printf(AUTHOR_INFO "\n" BUILT_INFO "\n", ExecutableName);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAINCMD_PACK)) {
        while ((SubOption = getopt(argc, argvs, SUBCMD_PACK)) != -1) {
            switch (SubOption) {
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(MainFIlePath, optarg);
                break;
            case 't':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(TargetDirPath, optarg);
                break;
            case 'a':
                Append = true;
                break;
            case 's':
                Recursion = true;
                break;
            case 'o':
                Overwrite = true;
                break;
            default:
                fprintf(stderr, MESSAGE_ERROR "没有此选项：-%c，请使用'%s help'命令查看使用帮助。", optopt, ExecutableName);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*TargetDirPath) {
            fprintf(stderr, MESSAGE_ERROR "未输入目标路径(应由[-t]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*MainFIlePath) {
            OsPathAbsolutePath(MainFIlePath, PATH_MSIZE, TargetDirPath);
            OsPathDirName(MainFIlePath, PATH_MSIZE, MainFIlePath);
        }
        if (!*MainFIlePath) {
            fprintf(stderr, MESSAGE_ERROR "未输入<.fp>路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (OsPathExists(MainFIlePath))
            pFilePack = FilePackOpen(MainFIlePath);
        else
            pFilePack = FilePackMake(MainFIlePath, Overwrite);
        FilePackPack(TargetDirPath, Recursion, pFilePack, Append);
        FilePackClose(pFilePack);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAINCMD_EXTR)) {
        while ((SubOption = getopt(argc, argvs, SUBCMD_EXTR)) != -1) {
            switch (SubOption) {
            case 'n':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "输入的文件名过长\n");
                    return EXIT_CODE_FAILURE;
                }
                strcpy(NameToExtract, optarg);
                break;
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(MainFIlePath, optarg);
                break;
            case 't':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(TargetDirPath, optarg);
                break;
            case 'o':
                Overwrite = true;
                break;
            default:
                fprintf(stderr, MESSAGE_ERROR "没有此选项：-%c，请使用'%s help'命令查看使用帮助。", optopt, ExecutableName);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*MainFIlePath) {
            fprintf(stderr, MESSAGE_ERROR "未输入主文件路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*TargetDirPath)
            strcpy(TargetDirPath, PATH_CDIRS);
        if (!*NameToExtract)
            pNameToExtract = NULL;
        if (FilePackIsFakeJPEG(MainFIlePath))
            pFilePack = FilePackOpenFakeJPEG(MainFIlePath);
        else
            pFilePack = FilePackOpen(MainFIlePath);
        FilePackExtract(pNameToExtract, TargetDirPath, Overwrite, pFilePack);
        FilePackClose(pFilePack);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAINCMD_INFO)) {
        while ((SubOption = getopt(argc, argvs, SUBCMD_INFO)) != -1) {
            switch (SubOption) {
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(MainFIlePath, optarg);
                break;
            default:
                fprintf(stderr, MESSAGE_ERROR "没有此选项：-%c，请使用'%s help'命令查看使用帮助。", optopt, ExecutableName);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*MainFIlePath) {
            fprintf(stderr, MESSAGE_ERROR "未输入主文件路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        pFilePack = FilePackInfo(MainFIlePath);
        FilePackClose(pFilePack);
        return EXIT_CODE_SUCCESS;
    } else if (!strcmp(argvs[1], MAINCMD_FAKE)) {
        while ((SubOption = getopt(argc, argvs, SUBCMD_FAKE)) != -1) {
            switch (SubOption) {
            case 'p':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(MainFIlePath, optarg);
                break;
            case 't':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(TargetDirPath, optarg);
                break;
            case 'a':
                Append = true;
                break;
            case 's':
                Recursion = true;
                break;
            case 'o':
                Overwrite = true;
                break;
            case 'j':
                if (strlen(optarg) >= PATH_MSIZE) {
                    fprintf(stderr, MESSAGE_ERROR "路径太长：%s\n", optarg);
                    return EXIT_CODE_FAILURE;
                }
                strcpy(JPEGFilePath, optarg);
                break;
            default:
                fprintf(stderr, MESSAGE_ERROR "没有此选项：-%c，请使用'%s help'命令查看使用帮助。", optopt, ExecutableName);
                return EXIT_CODE_FAILURE;
            }
        }
        if (!*TargetDirPath) {
            fprintf(stderr, MESSAGE_ERROR "未输入目标路径(应由[-t]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*MainFIlePath) {
            OsPathAbsolutePath(MainFIlePath, PATH_MSIZE, TargetDirPath);
            OsPathDirName(MainFIlePath, PATH_MSIZE, MainFIlePath);
        }
        if (!*MainFIlePath) {
            fprintf(stderr, MESSAGE_ERROR "未输入<.fp>路径(应由[-p]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (!*JPEGFilePath) {
            fprintf(stderr, MESSAGE_ERROR "未输入JPEG路径(应由[-j]选项指定)\n");
            return EXIT_CODE_FAILURE;
        }
        if (OsPathExists(MainFIlePath)) {
            printf(MESSAGE_WARN "已存在<.fp>文件，[-j]及[-o]选项将不生效。\n");
            pFilePack = FilePackOpenFakeJPEG(MainFIlePath);
        } else {
            pFilePack = FilePackMakeFakeJPEG(MainFIlePath, JPEGFilePath, Overwrite);
        }
        FilePackPack(TargetDirPath, Recursion, pFilePack, Append);
        FilePackClose(pFilePack);
        return EXIT_CODE_SUCCESS;
    } else {
        fprintf(stderr, MESSAGE_ERROR "没有此命令：%s，请使用'%s help'命令查看使用帮助。\n", argvs[1], ExecutableName);
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
