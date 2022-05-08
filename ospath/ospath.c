#include "ospath.h"

#include <stdio.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#include <shlwapi.h>
#include <windows.h>
#pragma comment(lib, "shlwapi.lib")
#define OSP_AFS "*"
#else
#include <dirent.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#endif  // _WIN32
#endif  // _MSC_VER

#define EXCLUDE_RECS "$RECYCLE.BIN"
#define EXCLUDE_SVIS "System Volume Information"

// 分配内存时一次重新分配块的数量
// 不一定是内存大小，只是广义上的数量
#define MALLOC_NUM 128
#define RALLOC_NUM 128

// 最后一次函数执行状态码
static int OSP_LAST_STATE = STATUS_EXEC_SUCCESS;

// 设置函数的完成状态
static inline void ospath_stat(int state) { OSP_LAST_STATE = state; }

// 获取最后一次函数执行的错误代码
// 返回0代表没有发生错误
// 此函数返回值见同名头文件中名称以<STATUS_>开头的宏定义
int path_last_state(void) { return OSP_LAST_STATE; }

// 将字符串转换为全小写，_m表示改变原字符串
static char *lower_str(char *string) {
    char *tmp = string;
    for (; *tmp; ++tmp) *tmp = tolower(*tmp);
    return string;
}

// 验证路径是否存在
bool path_exists(const char *_path) {
    ospath_stat(STATUS_EXEC_SUCCESS);
#ifndef _MSC_VER
    return !access(_path, F_OK);
#else
    return PathFileExistsA(_path);
#endif  // _MSC_VER
}

// 验证路径是否是一个目录
bool path_isdir(const char *_path) {
    ospath_stat(STATUS_EXEC_SUCCESS);
#ifndef _MSC_VER
    struct stat buf;
    if (stat(_path, &buf)) {
        ospath_stat(STATUS_GET_ATTR_FAIL);
        return RESULT_FALSE;
    }
    return S_ISDIR(buf.st_mode);
#else
    DWORD fattr = GetFileAttributesA(_path);
    if (fattr == INVALID_FILE_ATTRIBUTES) return RESULT_FALSE;
    return (fattr & FILE_ATTRIBUTE_DIRECTORY);
#endif  // _MSC_VER
}

// 验证路径是否是一个文件
bool path_isfile(const char *_path) {
    ospath_stat(STATUS_EXEC_SUCCESS);
#ifndef _MSC_VER
    struct stat buf;
    if (stat(_path, &buf)) {
        ospath_stat(STATUS_GET_ATTR_FAIL);
        return RESULT_FALSE;
    }
    return !S_ISDIR(buf.st_mode);
#else
    DWORD fattr = GetFileAttributesA(_path);
    if (fattr == INVALID_FILE_ATTRIBUTES) return RESULT_FALSE;
    return !(fattr & FILE_ATTRIBUTE_DIRECTORY);
#endif  // _MSC_VER
}

// 创建多级目录
// 成功返回0，失败返回1
int path_mkdirs(const char *dir_path) {
    static char buf[PATH_MSIZE + 12];
    char *basic_cmd = "mkdir";
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!dir_path || strlen(dir_path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
#ifdef _WIN32
    sprintf(buf, "%s \"%s\"", basic_cmd, dir_path);
#else
    sprintf(buf, "%s -p \"%s\"", basic_cmd, dir_path);
#endif
    if (system(buf)) {
        ospath_stat(STATUS_COMMAND_FAIL);
        return RESULT_FAILURE;
    } else
        return RESULT_SUCCESS;
}

// 获取当前工作目录
// 参数buf是接收路径的缓冲区
// 参数size是缓冲区大小
// 如果参数buf为NULL，则应注意，之前引用get_cwd(NULL, 0)返回值
// 地址的变量的值都有可能被此次运行改变
char *path_getcwd(char *buf, size_t size) {
    static char cwd[PATH_MSIZE];
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (NULL == buf) {
        buf = cwd;
        size = PATH_MSIZE;
    }
#ifndef _MSC_VER
#ifdef _WIN32
    return _getcwd(buf, size);
#else
    return getcwd(buf, size);
#endif  // _WIN32
#else
    return GetCurrentDirectoryA((DWORD)size, buf) ? buf : NULL;
#endif  // _MSC_VER
}

// 创建scanlist_t结构体并分配内存，返回结构体指针
// 如果参数blocks为0则默认以MALLOC_NUM代替,此处指定的blocks仅指定初始空间数量
// 后续enrich_scanlist收集路径时如果内存不足会自动扩充内存
scanner_t *path_mkscan(size_t blocks) {
    scanner_t *scanlst;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (blocks <= 0) blocks = MALLOC_NUM;
    scanlst = malloc(blocks * sizeof(scanner_t) + sizeof(char *));
    if (!scanlst) {
        ospath_stat(STATUS_MEMORY_ERROR);
        return NULL;
    }
    scanlst->count = 0, scanlst->blocks = blocks;
    return scanlst;
}

// 关闭scanlist_t对象，释放内存
int path_delscan(scanner_t *scanlst) {
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (scanlst) {
        for (size_t i = 0; i < scanlst->count; ++i) {
            free(scanlst->paths[i]);
        }
        free(scanlst), scanlst = NULL;
    }
    return RESULT_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
#ifdef _MSC_VER

// 功能：搜索给定路径中的文件或目录
// 参数pp_scanlst为结构体scanlist_t指针的指针
// 参数dir_path为目录路径
// 参数p_type为要收集的路径类型，可用值：PTYPE_DIR为目录，PTYPE_FILE为文件，PTYPE_BOTH则两者兼顾
// 参数subdirs控制是否搜索子目录
int path_scanpath(const char *dir_path, int ftype, int subdirs,
                  scanner_t **const pp_scanner) {
    size_t len_dpath, len_bytes_malloc;
    scanner_t *tmp_p_scanlst;  // 仅为了让Visual Studio不显示警告
    char *buf_full_path = NULL;
    char buffer_find_path[PATH_MSIZE];
    int return_code = RESULT_SUCCESS;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (NULL == pp_scanner || NULL == *pp_scanner) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    tmp_p_scanlst = *pp_scanner;
    if (!path_isdir(dir_path)) {
        if (!path_last_state()) ospath_stat(STATUS_INVALID_PARAM);
        return RESULT_FAILURE;
    }
    len_dpath = strlen(dir_path);
    if (len_dpath >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    strcpy(buffer_find_path, dir_path);
    if (path_joinpath(buffer_find_path, PATH_MSIZE, 2, buffer_find_path,
                      OSP_AFS))
        return RESULT_FAILURE;
    WIN32_FIND_DATAA find_paths;
    HANDLE hfind;
    if (INVALID_HANDLE_VALUE ==
        (hfind = FindFirstFileA(buffer_find_path, &find_paths))) {
        return_code = RESULT_FAILURE;
        ospath_stat(STATUS_COMMAND_FAIL);
        goto close_and_return;
    }
    // 直接FindNextFileA仍然可以获得FindFirstFileA的结果
    while (0 != FindNextFileA(hfind, &find_paths)) {
        if (!strcmp(find_paths.cFileName, PATH_CDIRS) ||
            !strcmp(find_paths.cFileName, PATH_PDIRS) ||
            !strcmp(find_paths.cFileName, EXCLUDE_RECS) ||
            !strcmp(find_paths.cFileName, EXCLUDE_SVIS))
            continue;
        if ((*pp_scanner)->count >= (*pp_scanner)->blocks) {
            // 用sizeof(*pp_scanner)得不到原对象已分配内存大小
            tmp_p_scanlst = realloc(
                *pp_scanner,
                sizeof(scanner_t) +
                    sizeof(char *) * ((*pp_scanner)->blocks + RALLOC_NUM));
            if (NULL == tmp_p_scanlst) {
                return_code = RESULT_FAILURE;
                ospath_stat(STATUS_MEMORY_ERROR);
                goto close_and_return;
            }
            *pp_scanner = tmp_p_scanlst;
            (*pp_scanner)->blocks += RALLOC_NUM;
        }
        // 为cFileName及dir_path、PATH_NSEPS、末尾0分配空间
        len_bytes_malloc = len_dpath + strlen(find_paths.cFileName) + 2;
        buf_full_path = malloc(len_bytes_malloc);
        if (NULL == buf_full_path) {
            return_code = RESULT_FAILURE;
            ospath_stat(STATUS_MEMORY_ERROR);
            goto close_and_return;
        }
        if (path_joinpath(buf_full_path, len_bytes_malloc, 2, dir_path,
                          find_paths.cFileName)) {
            free(buf_full_path);
            continue;
        }
        if (!path_normpath(buf_full_path, len_bytes_malloc)) {
            free(buf_full_path);
            continue;
        }
        if (find_paths.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (ftype & FTYPE_BOTH || ftype & FTYPE_DIR)
                (*pp_scanner)->paths[(*pp_scanner)->count++] = buf_full_path;
            if (subdirs)
                return_code =
                    path_scanpath(buf_full_path, ftype, subdirs, pp_scanner);
            if (!(ftype & FTYPE_BOTH) && !(ftype & FTYPE_DIR))
                if (NULL != buf_full_path)
                    free(buf_full_path), buf_full_path = NULL;
            if (return_code == RESULT_FAILURE) goto close_and_return;
        } else {
            if (ftype & FTYPE_BOTH || ftype & FTYPE_FILE)
                (*pp_scanner)->paths[(*pp_scanner)->count++] = buf_full_path;
            else if (NULL != buf_full_path)
                free(buf_full_path), buf_full_path = NULL;
        }
    }
close_and_return:
    FindClose(hfind);
    return return_code;
}

#else  // __GNUC__

// 功能：搜索给定路径中的文件或目录
// 参数pp_scanlst为结构体scanlist_t指针的指针
// 参数dir_path为目录路径
// 参数p_type为要收集的路径类型，可用值：PTYPE_DIR为目录，PTYPE_FILE为文件，PTYPE_BOTH则两者兼顾
// 参数subdirs控制是否搜索子目录
int path_scanpath(const char *dir_path, int ftype, int subdirs,
                  scanner_t **const pp_scanner) {
    size_t len_dpath, len_bytes_malloc;
    char *buf_full_path = NULL;
    int return_code = RESULT_SUCCESS;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (NULL == pp_scanner || NULL == *pp_scanner) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    len_dpath = strlen(dir_path);
    if (!path_isdir(dir_path)) {
        if (!path_last_state()) ospath_stat(STATUS_INVALID_PARAM);
        return RESULT_FAILURE;
    }
    struct dirent *p_dent;
    // MinGW的dirent没有d_type，统一stat
    struct stat buffer_d_type;
    DIR *dopened;
    if (!(dopened = opendir(dir_path))) {
        return_code = RESULT_FAILURE;
        ospath_stat(STATUS_COMMAND_FAIL);
        goto close_and_return;
    }
    while (NULL != (p_dent = readdir(dopened))) {
        if (!strcmp(p_dent->d_name, PATH_CDIRS) ||
            !strcmp(p_dent->d_name, PATH_PDIRS) ||
            !strcmp(p_dent->d_name, EXCLUDE_RECS) ||
            !strcmp(p_dent->d_name, EXCLUDE_SVIS))
            continue;
        if ((*pp_scanner)->count >= (*pp_scanner)->blocks) {
            // 用sizeof(*pp_scanner)得不到原对象已分配内存大小
            *pp_scanner = realloc(
                *pp_scanner,
                sizeof(scanner_t) +
                    sizeof(char *) * ((*pp_scanner)->blocks + RALLOC_NUM));
            if (NULL == *pp_scanner) {
                return_code = RESULT_FAILURE;
                ospath_stat(STATUS_MEMORY_ERROR);
                goto close_and_return;
            }
            (*pp_scanner)->blocks += RALLOC_NUM;
        }
        // 为dname和dir_path及PATH_NSEPS、末尾0分配空间
        len_bytes_malloc = len_dpath + strlen(p_dent->d_name) + 2;
        // 先拼接buf_full_path再用于stat
        buf_full_path = malloc(len_bytes_malloc);
        if (NULL == buf_full_path) {
            return_code = RESULT_FAILURE;
            ospath_stat(STATUS_MEMORY_ERROR);
            goto close_and_return;
        }
        if (path_joinpath(buf_full_path, len_bytes_malloc, 2, dir_path,
                          p_dent->d_name)) {
            free(buf_full_path);
            continue;
        }
        if (!path_normpath(buf_full_path, len_bytes_malloc)) {
            free(buf_full_path);
            continue;
        }
        if (stat(buf_full_path, &buffer_d_type)) continue;
        if (S_ISDIR(buffer_d_type.st_mode)) {
            if (ftype & FTYPE_BOTH || ftype & FTYPE_DIR)
                (*pp_scanner)->paths[(*pp_scanner)->count++] = buf_full_path;
            if (subdirs)
                return_code =
                    path_scanpath(buf_full_path, ftype, subdirs, pp_scanner);
            if (ftype != FTYPE_BOTH && ftype != FTYPE_DIR)
                if (buf_full_path) free(buf_full_path), buf_full_path = NULL;
            if (return_code == RESULT_FAILURE) goto close_and_return;
        } else {
            if (ftype & FTYPE_BOTH || ftype & FTYPE_FILE)
                (*pp_scanner)->paths[(*pp_scanner)->count++] = buf_full_path;
            else if (NULL != buf_full_path)
                free(buf_full_path), buf_full_path = NULL;
        }
    }
close_and_return:
    closedir(dopened);
    return return_code;
}

#endif  // _MSC_VER
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32  // 区分win32及posix平台

// 验证路径是否是绝对路径
// 是：返回1；否：返回0
// 当函数返回0时，最好获取path_last_error函数返回值，并验证返回值是否是STATUS_EXEC_SUCCESS
// 如果返回值不是STATUS_EXEC_SUCCESS，那么表示is_abs是因出错才返回0，结果将是不可靠的
bool path_isabs(const char *_path) {
    char tmp_path[PATH_MSIZE];
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FALSE;
    }
    if (strlen(_path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FALSE;
    }
    strcpy(tmp_path, _path);
    if (!path_normcase(tmp_path))
        return RESULT_FALSE;
    else if (!strncmp(tmp_path, PATH_UNCQS, 4)) {
        ospath_stat(STATUS_OTHER_ERRORS);
        return RESULT_TRUE;
    } else if (path_splitdrv(NULL, 0, tmp_path, PATH_MSIZE, tmp_path))
        return RESULT_FALSE;
    return (strlen(tmp_path) > 0 && *tmp_path == PATH_NSEP);
}

// 将路径中的斜杠转换为系统标准形式，大写转小写
// 此函数改变原路径，成功返回缓冲区指针，失败返回NULL
char *path_normcase(char path[]) {
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return NULL;
    }
    size_t p_len = strlen(path);
    if (p_len < PATH_MSIZE) {
        for (size_t i = 0; i < p_len; ++i) {
            if (path[i] == PATH_ASEP)
                path[i] = PATH_NSEP;
            else
                path[i] = tolower(path[i]);
        }
        return path;
    } else {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return NULL;
    }
}

// 1.将路径中的斜杠转换为系统标准形式
// 2.将路径中的可定位的相对路径字符'.'和'..'进行重定位
// 3.参数size为字符串数组path的空间大小(有可能比字符串path本身长的多)
// 4.改变path的时候，字符串长度可能会变，多数时候不变或变短，但空字符串会变为1字符
//    所以，就算path字符串长度为0，也要保证字符串数组path内存空间大于等于2
// 此函数改变原路径，成功返回缓冲区指针，失败返回NULL
char *path_normpath(char path[], size_t size) {
    char *result_s = path;
    char *p_path = path;
    char tmp_path[PATH_MSIZE];
    char **splited;
    char *token_spl, *prefix, *suffix;
    size_t p_len, index = 0, req_size = 0;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return NULL;
    }
    p_len = strlen(path);
    if (p_len >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return NULL;
    }
    if ((p_len >= 4) &&
        (!strncmp(path, PATH_UNCDS, 4) || !strncmp(path, PATH_UNCQS, 4)))
        return result_s;
    prefix = malloc(PATH_MSIZE);
    suffix = malloc(PATH_MSIZE);
    splited = malloc(PATH_MSIZE * sizeof(char *));
    if (!splited || !prefix || !suffix) {
        result_s = NULL;
        ospath_stat(STATUS_MEMORY_ERROR);
        goto clean_return;
    }
    while (*p_path) {
        if (*p_path == PATH_ASEP) *p_path = PATH_NSEP;
        ++p_path;
    }
    if (path_splitdrv(prefix, PATH_MSIZE, suffix, PATH_MSIZE, path)) {
        result_s = NULL;
        goto clean_return;
    }
    if (suffix[0] == PATH_NSEP) {
        strcat(prefix, PATH_NSEPS);
        memmove(suffix, suffix + 1, strlen(suffix));
    }
    token_spl = strtok(suffix, PATH_NSEPS);
    while (token_spl) {
        if (!strcmp(token_spl, PATH_CDIRS))
            goto next;
        else if (!strcmp(token_spl, PATH_PDIRS)) {
            if (index > 0 && strcmp(splited[index - 1], PATH_PDIRS))
                --index;
            else if (index == 0 && prefix[2] == PATH_NSEP)
                goto next;
            else
                splited[index++] = token_spl;
        } else
            splited[index++] = token_spl;
    next:
        token_spl = strtok(NULL, PATH_NSEPS);
    }
    if (!*prefix && index == 0) {
        if (size <= 2) {
            result_s = NULL;
            ospath_stat(STATUS_INSFC_BUFFER);
            goto clean_return;
        }
        strcpy(path, PATH_CDIRS);
        goto clean_return;
    } else {
        if (size <= strlen(prefix)) {
            result_s = NULL;
            ospath_stat(STATUS_INSFC_BUFFER);
            goto clean_return;
        }
        req_size += strlen(prefix);
        strcpy(tmp_path, prefix);
        for (size_t i = 0; i < index; ++i) {
            req_size += strlen(splited[i]);
            if (size <= req_size) {
                result_s = NULL;
                ospath_stat(STATUS_INSFC_BUFFER);
                goto clean_return;
            }
            strcat(tmp_path, splited[i]);
            if (i != index - 1) strcat(tmp_path, PATH_NSEPS);
        }
        strcpy(path, tmp_path);
    }
clean_return:
    if (NULL != prefix) free(prefix);
    if (NULL != suffix) free(suffix);
    if (NULL != splited) free(splited);
    return result_s;
}

// 将路径名拆分为(驱动器/共享点)和相对路径说明符。
//      得到的路径可能是空字符串。
//
//      如果路径包含驱动器号，则 buf_d将包含所有内容直到并包括冒号。
//      例如 path_splitdrv(buf_d, 10, buf_p, 10, "c:\\dir")，buf_d被设置为
//      "c:"， buf_p被设置为"\\dir"
//
//      如果路径包含共享点路径，则buf_d将包含主机名并共享最多但不包括
//      第四个目录分隔符。例如 path_splitdrv(buf_d, 10, buf_p, 10,
//      "//host/computer/dir")
//      buf_d被设置为"//host/computer"，buf_p被设置为"/dir"。
//
//      路径不能同时包含驱动器号和共享点路径。
// 成功返回0，失败返回1
int path_splitdrv(char buf_d[], size_t bdsize, char buf_p[], size_t bpsize,
                  const char *_path) {
    size_t p_len;
    char *sep3_index, *sep4_index;
    char tmp_path[PATH_MSIZE], cased[PATH_MSIZE];
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    p_len = strlen(_path);
    if (p_len >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    strcpy(tmp_path, _path);
    if (p_len >= 2) {
        strcpy(cased, tmp_path);
        if (!path_normcase(cased)) return RESULT_FAILURE;
        if (!strncmp(cased, PATH_NSEPS2, 2) && cased[2] != PATH_NSEP) {
            sep3_index = strchr(cased + 2, PATH_NSEP);
            if (NULL == sep3_index ||
                ((sep4_index = strchr(sep3_index + 1, PATH_NSEP)) ==
                 sep3_index + 1)) {
                if (NULL != buf_d) {
                    if (bdsize < 1) {
                        ospath_stat(STATUS_INSFC_BUFFER);
                        return RESULT_FAILURE;
                    }
                    buf_d[0] = EMPTY_CHAR;
                }
                if (NULL != buf_p) {
                    if (bpsize < p_len + 1) {
                        ospath_stat(STATUS_INSFC_BUFFER);
                        return RESULT_FAILURE;
                    }
                    strcpy(buf_p, tmp_path);
                }
                return RESULT_SUCCESS;
            }
            // 这一句判断语句不一定执行
            // 所以后面所有sep4_index - p_cased都不能简化为p_len
            if (NULL == sep4_index) sep4_index = cased + p_len;
            if (NULL != buf_d) {
                if (bdsize <= (size_t)(sep4_index - cased)) {
                    ospath_stat(STATUS_INSFC_BUFFER);
                    return RESULT_FAILURE;
                }
                strncpy(buf_d, tmp_path, sep4_index - cased);
                buf_d[sep4_index - cased] = EMPTY_CHAR;
            }
            if (NULL != buf_p) {
                if (bpsize <= strlen(sep4_index)) {
                    ospath_stat(STATUS_INSFC_BUFFER);
                    return RESULT_FAILURE;
                }
                strcpy(buf_p, tmp_path + (sep4_index - cased));
            }
            return RESULT_SUCCESS;
        }
        if (tmp_path[1] == PATH_COLON) {
            if (NULL != buf_d) {
                if (bdsize < 3) {
                    ospath_stat(STATUS_INSFC_BUFFER);
                    return RESULT_FAILURE;
                }
                strncpy(buf_d, tmp_path, 2);
                buf_d[2] = EMPTY_CHAR;
            }
            if (NULL != buf_p) {
                if (bpsize < p_len - 1) {
                    ospath_stat(STATUS_INSFC_BUFFER);
                    return RESULT_FAILURE;
                }
                strcpy(buf_p, tmp_path + 2);
            }
            return RESULT_SUCCESS;
        }
    }
    if (NULL != buf_d) {
        if (bdsize < 1) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return RESULT_FAILURE;
        }
        buf_d[0] = EMPTY_CHAR;
    }
    if (NULL != buf_p) {
        if (bpsize <= p_len) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return RESULT_FAILURE;
        }
        strcpy(buf_p, tmp_path);
    }
    return RESULT_SUCCESS;
}

// 用系统标准路径分隔符连接第4个及其后的所有参数值
// 参数n指示了它后面参数的个数
// 注意，要被拼接的路径不应以斜杠开头，否则会覆盖前面除驱动器号以外的路径
// 成功返回0，失败返回1
int path_joinpath(char buf[], size_t bfsize, int n, const char *_path, ...) {
    int return_code = RESULT_SUCCESS;
    size_t req_size = 0;
    char *p_drv, *p_pth, *res_drv, *res_pth, *arg_string;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    req_size += strlen(_path);
    if (req_size >= PATH_MSIZE) {
        ospath_stat(STATUS_INSFC_BUFFER);
        return RESULT_FAILURE;
    }
    p_drv = malloc(PATH_MSIZE);
    p_pth = malloc(PATH_MSIZE);
    res_drv = malloc(PATH_MSIZE);
    res_pth = malloc(PATH_MSIZE);
    if (!p_drv || !p_pth || !res_drv || !res_pth) {
        return_code = RESULT_FAILURE;
        ospath_stat(STATUS_MEMORY_ERROR);
        goto clean_return;
    }
    va_list arg_list;
    if (path_splitdrv(res_drv, PATH_MSIZE, res_pth, PATH_MSIZE, _path)) {
        return_code = RESULT_FAILURE;
        goto clean_return;
    }
    va_start(arg_list, _path);
    for (int i = 0; i < n - 1; ++i) {
        arg_string = va_arg(arg_list, char *);
        if (path_splitdrv(p_drv, PATH_MSIZE, p_pth, PATH_MSIZE, arg_string)) {
            return_code = RESULT_FAILURE;
            goto clean_return;
        }
        if (*p_pth && (p_pth[0] == PATH_NSEP || p_pth[0] == PATH_ASEP)) {
            if (*p_drv || !*res_drv) strcpy(res_drv, p_drv);
            req_size = strlen(p_pth);
            if (req_size >= PATH_MSIZE) {
                return_code = RESULT_FAILURE;
                ospath_stat(STATUS_INSFC_BUFFER);
                goto clean_return;
            }
            strcpy(res_pth, p_pth);
            continue;
        } else if (*p_drv && strcmp(p_drv, res_drv)) {
            if (strcmp(lower_str(p_drv), lower_str(res_drv))) {
                req_size = strlen(p_pth);
                if (req_size >= PATH_MSIZE) {
                    return_code = RESULT_FAILURE;
                    ospath_stat(STATUS_INSFC_BUFFER);
                    goto clean_return;
                }
                strcpy(res_drv, p_drv);
                strcpy(res_pth, p_pth);
                continue;
            }
            strcpy(res_drv, p_drv);
        }
        req_size += strlen(p_pth);
        if (req_size >= PATH_MSIZE) {
            return_code = RESULT_FAILURE;
            ospath_stat(STATUS_INSFC_BUFFER);
            goto clean_return;
        }
        char res_p_last = res_pth[strlen(res_pth) - 1];
        if (*res_pth && res_p_last != PATH_NSEP && res_p_last != PATH_ASEP) {
            ++req_size;
            if (req_size >= PATH_MSIZE) {
                return_code = RESULT_FAILURE;
                ospath_stat(STATUS_INSFC_BUFFER);
                goto clean_return;
            }
            strcat(res_pth, PATH_NSEPS);
        }
        strcat(res_pth, p_pth);
    }
    va_end(arg_list);
    if (bfsize <= strlen(res_drv) + strlen(res_pth)) {
        return_code = RESULT_FAILURE;
        ospath_stat(STATUS_INSFC_BUFFER);
        goto clean_return;
    }
    if (*res_pth && *res_pth != PATH_NSEP && *res_pth != PATH_ASEP &&
        *res_drv && res_drv[strlen(res_drv) - 1] != PATH_COLON) {
        sprintf(buf, "%s%s%s", res_drv, PATH_NSEPS, res_pth);
        goto clean_return;
    }
    sprintf(buf, "%s%s", res_drv, res_pth);
clean_return:
    if (p_drv) free(p_drv);
    if (p_pth) free(p_pth);
    if (res_drv) free(res_drv);
    if (res_pth) free(res_pth);
    return return_code;
}

// 将路径按最后一个路径分隔符(斜杠)分割成两部分
// 成功返回0，失败返回1
int path_splitpath(char buf_h[], size_t bhsize, char buf_t[], size_t btsize,
                   const char *_path) {
    int index, return_code = RESULT_SUCCESS;
    size_t p_len, last_sep_plus1;
    char *ah_rs = NULL;  // 右边第一个路径分隔符以左的字符串
    char p_drv[PATH_MSIZE], p_pth[PATH_MSIZE];
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    if (strlen(_path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    if (path_splitdrv(p_drv, PATH_MSIZE, p_pth, PATH_MSIZE, _path))
        return RESULT_FAILURE;
    p_len = last_sep_plus1 = strlen(p_pth);
    // last_sep_plus1:去除驱动器号后的路径的最后一个斜杠下标+1位置
    while (last_sep_plus1 && p_pth[last_sep_plus1 - 1] != PATH_ASEP &&
           p_pth[last_sep_plus1 - 1] != PATH_NSEP)
        --last_sep_plus1;
    if (NULL != buf_t) {
        if (btsize <= p_len - last_sep_plus1) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return RESULT_FAILURE;
        }
        strcpy(buf_t, p_pth + last_sep_plus1);
    }
    if (last_sep_plus1 == 0)
        ah_rs = calloc(1, 1);
    else
        ah_rs = calloc(last_sep_plus1 + 1, 1);
    if (!ah_rs) {
        ospath_stat(STATUS_MEMORY_ERROR);
        return RESULT_FAILURE;
    }
    if (last_sep_plus1 > 0) {
        strncpy(ah_rs, p_pth, last_sep_plus1);
    }
    ah_rs[last_sep_plus1] = EMPTY_CHAR;
    for (index = (int)(last_sep_plus1 - 1); index >= 0; --index) {
        if (ah_rs[index] == PATH_ASEP || ah_rs[index] == PATH_NSEP)
            continue;
        else {
            ah_rs[index + 1] = EMPTY_CHAR;  // 去除尾随斜杠
            break;
        }
    }
    if (NULL != buf_h) {
        if (last_sep_plus1 == 0) {
            if (bhsize <= strlen(p_drv)) {
                return_code = RESULT_FAILURE;
                ospath_stat(STATUS_INSFC_BUFFER);
                goto clean_return;
            }
            strcpy(buf_h, p_drv);
            goto clean_return;
        } else {
            if (bhsize <= strlen(p_drv) + last_sep_plus1) {
                return_code = RESULT_FAILURE;
                ospath_stat(STATUS_INSFC_BUFFER);
                goto clean_return;
            }
            strcpy(buf_h, p_drv);
            strcat(buf_h, ah_rs);
            goto clean_return;
        }
    }
clean_return:
    if (NULL != ah_rs) free(ah_rs);
    return return_code;
}

// 获取路径的上一级路径
// 成功返回字符指针，失败返回NULL
char *path_dirname(char buf_dir[], size_t bfsize, const char *_path) {
    static char dir_path[PATH_MSIZE];
    if (!buf_dir) {
        bfsize = PATH_MSIZE;
        buf_dir = dir_path;
    }
    return path_splitpath(buf_dir, bfsize, NULL, 0, _path) ? NULL : buf_dir;
}

// 获取路径中的文件名
// 成功返回字符指针，失败返回NULL
char *path_basename(char buf_base[], size_t bfsize, const char *_path) {
    static char base_path[PATH_MSIZE];
    if (!buf_base) {
        bfsize = PATH_MSIZE;
        buf_base = base_path;
    }
    return path_splitpath(NULL, 0, buf_base, bfsize, _path) ? NULL : buf_base;
}

// 功能：生成相对路径
// 此相对路径是_path相对于start的路径
// 成功返回0，失败返回1
int path_relpath(char buf[], size_t size, const char *_path,
                 const char *start) {
    int ret_status = RESULT_FAILURE;
    int req_size = 0;  // 结果字符数，size要大于此数才能装下结果
    // cnt_p及cnt_s：以斜杠分割后的字符串数量；cnt_min：两者中的较小值
    int cnt_min, cnt_p = 0, cnt_s = 0;
    char **p_lst = NULL, **s_lst = NULL;
    int cnt_same = 0;  // 相同目录数量
    char *p_abs, *s_abs, *buf_p, *buf_s, *stk;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    if (strlen(_path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    p_abs = malloc(PATH_MSIZE);
    s_abs = malloc(PATH_MSIZE);
    buf_p = malloc(PATH_MSIZE);
    buf_s = malloc(PATH_MSIZE);
    if (NULL == p_abs || NULL == s_abs || NULL == buf_p || NULL == buf_s) {
        ospath_stat(STATUS_MEMORY_ERROR);
        goto clean_return;
    }
    if (!start) start = PATH_CDIRS;
    if (path_abspath(p_abs, PATH_MSIZE, _path)) goto clean_return;
    if (path_abspath(s_abs, PATH_MSIZE, start)) goto clean_return;
    if (path_splitdrv(buf_p, PATH_MSIZE, p_abs, PATH_MSIZE, p_abs))
        goto clean_return;
    if (path_splitdrv(buf_s, PATH_MSIZE, s_abs, PATH_MSIZE, s_abs))
        goto clean_return;
    if (strcmp(lower_str(buf_s), lower_str(buf_p))) {
        ospath_stat(STATUS_OTHER_ERRORS);
        goto clean_return;
    }
    p_lst = malloc(sizeof(char *) * strlen(p_abs));
    s_lst = malloc(sizeof(char *) * strlen(s_abs));
    if (NULL == p_lst || NULL == s_lst) {
        ospath_stat(STATUS_MEMORY_ERROR);
        goto clean_return;
    }
    stk = strtok(p_abs, PATH_NSEPS);
    while (stk) {
        p_lst[cnt_p++] = stk;
        stk = strtok(NULL, PATH_NSEPS);
    }
    stk = strtok(s_abs, PATH_NSEPS);
    while (stk) {
        s_lst[cnt_s++] = stk;
        stk = strtok(NULL, PATH_NSEPS);
    }
    cnt_min = cnt_p < cnt_s ? cnt_p : cnt_s;
    for (; cnt_same < cnt_min; ++cnt_same) {
        strcpy(buf_p, p_lst[cnt_same]);
        strcpy(buf_s, s_lst[cnt_same]);
        if (strcmp(lower_str(buf_p), lower_str(buf_s))) break;
    }
    // 将buf_p或buf_s重置为空字符以复用其内存空间
    buf_s[0] = EMPTY_CHAR;
    for (int i = 0; i < (cnt_s - cnt_same); ++i) {
        req_size += 3;
        if (req_size >= PATH_MSIZE) {
            ospath_stat(STATUS_PATH_TOO_LONG);
            goto clean_return;
        }
        strcat(buf_s, PATH_PDIRS);
        strcat(buf_s, PATH_NSEPS);
    }
    for (int i = cnt_same; i < cnt_p; ++i) {
        req_size += (int)strlen(p_lst[i]);
        if (req_size >= PATH_MSIZE) {
            ospath_stat(STATUS_PATH_TOO_LONG);
            goto clean_return;
        }
        strcat(buf_s, p_lst[i]);
        if (i != cnt_p - 1) {
            ++req_size;
            if (req_size >= PATH_MSIZE) {
                ospath_stat(STATUS_PATH_TOO_LONG);
                goto clean_return;
            }
            strcat(buf_s, PATH_NSEPS);
        }
    }
    if (!strlen(buf_s)) {
        if (size <= 1) {
            ospath_stat(STATUS_INSFC_BUFFER);
            goto clean_return;
        }
        strcat(buf, PATH_CDIRS);
        ret_status = RESULT_SUCCESS;
        goto clean_return;
    }
    if (size <= strlen(buf_s)) {
        ospath_stat(STATUS_INSFC_BUFFER);
        goto clean_return;
    }
    strcpy(buf, buf_s);
    ret_status = RESULT_SUCCESS;
    goto clean_return;
clean_return:
    if (NULL != p_abs) free(p_abs);
    if (NULL != s_abs) free(s_abs);
    if (NULL != buf_p) free(buf_p);
    if (NULL != buf_s) free(buf_s);
    if (NULL != p_lst) free(p_lst);
    if (NULL != s_lst) free(s_lst);
    return ret_status;
}

// 生成给定路径的绝对路径
// 成功返回0，失败返回1
int path_abspath(char buf[], size_t size, const char *_path) {
    size_t length;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    if (strlen(_path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    length = GetFullPathNameA(_path, (DWORD)size, buf, NULL);
    if (length <= 0 || length > size) {
        ospath_stat(STATUS_INSFC_BUFFER);
        return RESULT_FAILURE;
    }
    return (path_normpath(buf, size)) ? RESULT_SUCCESS : RESULT_FAILURE;
}

#else  // _WIN32 else POSIX

// 验证路径是否是绝对路径
// 是：返回1，否：返回0
bool path_isabs(const char *_path) {
    ospath_stat(STATUS_EXEC_SUCCESS);
    return *_path == PATH_NSEP;
}

// 在posix上没有效果
// 返回path指针
char *path_normcase(char path[]) {
    ospath_stat(STATUS_EXEC_SUCCESS);
    return path;
}

// 规范化路径，消除双斜线等，例如 A//B、A/./B 和 A/foo/../B 都变成 A/B
// 应该理解，如果路径包含符号链接，这可能会改变路径的含义
// 改变path的时候，字符串长度可能会变，多数时候不变或变短，但空字符串会变为1字符
//    所以，如果path字符串长度为 0，也要保证字符串数组path空间大于等于2
// 此函数改变原路径，成功返回缓冲区指针，失败返回NULL
char *path_normpath(char path[], size_t size) {
    char *result_s = path;
    char initial_slashes[3] = {0};
    char tmp_path[PATH_MSIZE];
    char *token_spl, *splited[PATH_MSIZE];
    size_t index = 0, req_size = 0;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return NULL;
    }
    if (strlen(path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return NULL;
    }
    if (!*path) {
        if (size < 2) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return NULL;
        }
        strcpy(path, PATH_CDIRS);
        return result_s;
    }
    if (path[0] == PATH_NSEP) initial_slashes[0] = PATH_NSEP;
    // posix允许一个或两个初始斜杠，但将三个或更多视为单斜杠
    if (path[0] == PATH_NSEP && path[1] == PATH_NSEP && !path[2] == PATH_NSEP)
        initial_slashes[1] = PATH_NSEP;
    strcpy(tmp_path, path);
    token_spl = strtok(tmp_path, PATH_NSEPS);
    while (token_spl) {
        if (!strcmp(token_spl, PATH_CDIRS)) goto next;
        if (strcmp(token_spl, PATH_PDIRS) ||
            (!*initial_slashes && index == 0) ||
            (index > 0 && !strcmp(splited[index - 1], PATH_PDIRS)))
            splited[index++] = token_spl;
        else if (index > 0)
            --index;
    next:
        token_spl = strtok(NULL, PATH_NSEPS);
    }
    if (size <= strlen(initial_slashes)) {
        ospath_stat(STATUS_INSFC_BUFFER);
        return NULL;
    }
    req_size += strlen(initial_slashes);
    strcpy(tmp_path, initial_slashes);
    for (int i = 0; i < index; ++i) {
        req_size += strlen(splited[i]);
        if (size <= req_size) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return NULL;
        }
        strcat(tmp_path, splited[i]);
        if (i != index - 1) strcat(tmp_path, PATH_NSEPS);
    }
    if (*tmp_path) {
        strcpy(path, tmp_path);
    } else {
        if (size > 1) {
            strcpy(path, PATH_CDIRS);
        } else {
            ospath_stat(STATUS_INSFC_BUFFER);
            return NULL;
        }
    }
    return result_s;
}

// 将路径分割为驱动器号和路径
// 在posix平台上，驱动器号总是空字符串
// 成功返回0，失败返回1
int path_splitdrv(char buf_d[], size_t bdsize, char buf_p[], size_t bpsize,
                  const char *_path) {
    char tmp_path[PATH_MSIZE];
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    if (strlen(_path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    strcpy(tmp_path, _path);
    if (NULL != buf_d) {
        if (bdsize < 1) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return RESULT_FAILURE;
        }
        buf_d[0] = EMPTY_CHAR;
    }
    if (NULL != buf_p) {
        if (bpsize <= strlen(tmp_path)) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return RESULT_FAILURE;
        }
        strcpy(buf_p, tmp_path);
    }
    return RESULT_SUCCESS;
}

// 用系统标准路径分隔符连接第4个及其后的所有参数值
// 注意，要被拼接的路径不应以斜杠开头，否则会覆盖前面除驱动器号以外的路径
// 参数n指示了它后面的参数个数
// 成功返回0，失败返回1
int path_joinpath(char buf[], size_t bfsize, int n, const char *_path, ...) {
    size_t req_size = 0;
    char *arg_string, res_pth[PATH_MSIZE];
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    req_size += strlen(_path);
    if (req_size >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    strcpy(res_pth, _path);
    va_list arg_list;
    va_start(arg_list, _path);
    for (int i = 0; i < n - 1; ++i) {
        arg_string = va_arg(arg_list, char *);
        if (arg_string[0] == PATH_NSEP) {
            req_size = strlen(arg_string);
            if (req_size >= PATH_MSIZE) {
                ospath_stat(STATUS_PATH_TOO_LONG);
                return RESULT_FAILURE;
            }
            strcpy(res_pth, arg_string);
        } else if (!*res_pth || res_pth[strlen(res_pth) - 1] == PATH_NSEP) {
            req_size += strlen(arg_string);
            if (req_size >= PATH_MSIZE) {
                ospath_stat(STATUS_PATH_TOO_LONG);
                return RESULT_FAILURE;
            }
            strcat(res_pth, arg_string);
        } else {
            req_size += 1 + strlen(arg_string);
            if (req_size >= PATH_MSIZE) {
                ospath_stat(STATUS_PATH_TOO_LONG);
                return RESULT_FAILURE;
            }
            strcat(res_pth, PATH_NSEPS);
            strcat(res_pth, arg_string);
        }
    }
    va_end(arg_list);
    if (bfsize <= req_size) {
        ospath_stat(STATUS_INSFC_BUFFER);
        return RESULT_FAILURE;
    }
    strcpy(buf, res_pth);
    return RESULT_SUCCESS;
}

// 将路径按最后一个路径分隔符(斜杠)分割为两部分
// 成功返回0，失败返回1
int path_splitpath(char buf_h[], size_t bhsize, char buf_t[], size_t btsize,
                   const char *_path) {
    size_t p_len, last_sep_plus1;
    char head[PATH_MSIZE];
    char tmp_path[PATH_MSIZE];
    char stk_tmp[PATH_MSIZE];
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    if (strlen(_path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    strcpy(tmp_path, _path);
    p_len = last_sep_plus1 = strlen(tmp_path);
    while (last_sep_plus1 && tmp_path[last_sep_plus1 - 1] != PATH_NSEP)
        --last_sep_plus1;
    if (last_sep_plus1 > 0) strncpy(head, tmp_path, last_sep_plus1);
    head[last_sep_plus1] = EMPTY_CHAR;
    strcpy(stk_tmp, head);
    if (*head && strtok(stk_tmp, PATH_NSEPS)) {
        for (int i = strlen(head) - 1; i >= 0; --i) {
            if (head[i] == PATH_NSEP)
                continue;
            else {
                head[i + 1] = EMPTY_CHAR;
                break;
            }
        }
    }
    if (NULL != buf_h) {
        if (bhsize <= strlen(head)) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return RESULT_FAILURE;
        }
        strcpy(buf_h, head);
    }
    if (NULL != buf_t) {
        if (btsize <= p_len - last_sep_plus1) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return RESULT_FAILURE;
        }
        strcpy(buf_t, tmp_path + last_sep_plus1);
    }
    return RESULT_SUCCESS;
}

// 获取路径的上一级路径
// 成功返回字符指针，失败返回NULL
char *path_dirname(char buf_dir[], size_t bfsize, const char *_path) {
    static char dir_path[PATH_MSIZE];
    if (!buf_dir) {
        bfsize = PATH_MSIZE;
        buf_dir = dir_path;
    }
    return path_splitpath(buf_dir, bfsize, NULL, 0, _path) ? NULL : buf_dir;
}

// 获取路径中的文件名
// 成功返回字符指针，失败返回NULL
char *path_basename(char buf_base[], size_t bfsize, const char *_path) {
    static char base_path[PATH_MSIZE];
    if (!buf_base) {
        bfsize = PATH_MSIZE;
        buf_base = base_path;
    }
    return path_splitpath(NULL, 0, buf_base, bfsize, _path) ? NULL : buf_base;
}

// 功能：生成相对路径
// 生成的相对路径是_path相对于start的路径
// 成功返回0，失败返回1
int path_relpath(char buf[], size_t bfsize, const char *_path,
                 const char *start) {
    int return_code = RESULT_SUCCESS;
    int req_size = 0;  // 结果字符数，size要大于此数才能装下结果
    // cnt_p及cnt_s：以斜杠分割后的字符串数量；cnt_min：两者中的较小值
    int cnt_min, cnt_p = 0, cnt_s = 0;
    char **p_lst = NULL, **s_lst = NULL;
    int cnt_same = 0;  // 相同目录数量
    char *p_abs, *s_abs, *stk, *tmp_path;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    if (strlen(_path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    p_abs = malloc(PATH_MSIZE);
    s_abs = malloc(PATH_MSIZE);
    tmp_path = malloc(PATH_MSIZE);
    if (!p_abs || !s_abs || !tmp_path) {
        return_code = RESULT_FAILURE;
        ospath_stat(STATUS_MEMORY_ERROR);
        goto clean_return;
    }
    if (!start) start = PATH_CDIRS;
    if (path_abspath(p_abs, PATH_MSIZE, _path)) {
        return_code = RESULT_FAILURE;
        goto clean_return;
    }
    if (path_abspath(s_abs, PATH_MSIZE, start)) {
        return_code = RESULT_FAILURE;
        goto clean_return;
    }
    if (strlen(p_abs) <= 0)
        p_lst = malloc(sizeof(char *));
    else
        p_lst = malloc(sizeof(char *) * strlen(p_abs));
    if (strlen(s_abs) <= 0)
        s_lst = malloc(sizeof(char *));
    else
        s_lst = malloc(sizeof(char *) * strlen(s_abs));
    if (!p_lst || !s_lst) {
        return_code = RESULT_FAILURE;
        ospath_stat(STATUS_MEMORY_ERROR);
        goto clean_return;
    }
    stk = strtok(p_abs, PATH_NSEPS);
    while (stk) {
        p_lst[cnt_p++] = stk;
        stk = strtok(NULL, PATH_NSEPS);
    }
    stk = strtok(s_abs, PATH_NSEPS);
    while (stk) {
        s_lst[cnt_s++] = stk;
        stk = strtok(NULL, PATH_NSEPS);
    }
    cnt_min = cnt_p < cnt_s ? cnt_p : cnt_s;
    for (; cnt_same < cnt_min; ++cnt_same) {
        if (strcmp(p_lst[cnt_same], s_lst[cnt_same])) break;
    }
    // 先将p_tmp重置为空字符串
    tmp_path[0] = EMPTY_CHAR;
    for (int i = 0; i < (cnt_s - cnt_same); ++i) {
        req_size += 3;
        if (req_size >= PATH_MSIZE) {
            return_code = RESULT_FAILURE;
            ospath_stat(STATUS_PATH_TOO_LONG);
            goto clean_return;
        }
        strcat(tmp_path, PATH_PDIRS);
        strcat(tmp_path, PATH_NSEPS);
    }
    for (int i = cnt_same; i < cnt_p; ++i) {
        req_size += strlen(p_lst[i]);
        if (req_size >= PATH_MSIZE) {
            return_code = RESULT_FAILURE;
            ospath_stat(STATUS_PATH_TOO_LONG);
            goto clean_return;
        }
        strcat(tmp_path, p_lst[i]);
        if (i != cnt_p - 1) {
            ++req_size;
            if (req_size >= PATH_MSIZE) {
                return_code = RESULT_FAILURE;
                ospath_stat(STATUS_PATH_TOO_LONG);
                goto clean_return;
            }
            strcat(tmp_path, PATH_NSEPS);
        }
    }
    if (!strlen(tmp_path)) {
        if (bfsize <= 1) {
            return_code = RESULT_FAILURE;
            ospath_stat(STATUS_INSFC_BUFFER);
            goto clean_return;
        }
        strcat(buf, PATH_CDIRS);
        goto clean_return;
    }
    if (bfsize <= strlen(tmp_path)) {
        return_code = RESULT_FAILURE;
        ospath_stat(STATUS_INSFC_BUFFER);
        goto clean_return;
    }
    strcpy(buf, tmp_path);
clean_return:
    if (NULL != p_abs) free(p_abs);
    if (NULL != s_abs) free(s_abs);
    if (NULL != tmp_path) free(tmp_path);
    if (NULL != p_lst) free(p_lst);
    if (NULL != s_lst) free(s_lst);
    return return_code;
}

// 功能：生成给定路径的绝对路径
// 成功返回0，失败返回1
int path_abspath(char buf[], size_t bfsize, const char *_path) {
    char tmp_path[PATH_MSIZE] = {0};
    char *end_ch = tmp_path;
    if (!path_isabs(_path)) {
        if (path_last_state()) return RESULT_FAILURE;
        if (!path_getcwd(tmp_path, PATH_MSIZE)) return RESULT_FAILURE;
    }
    if (strlen(tmp_path) + strlen(_path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    while (*end_ch) ++end_ch;
    if (end_ch != tmp_path) --end_ch;
    if (*end_ch != PATH_NSEP && *_path != PATH_NSEP) {
        if (strlen(tmp_path) + 1 >= PATH_MSIZE) {
            ospath_stat(STATUS_PATH_TOO_LONG);
            return RESULT_FAILURE;
        }
        strcat(tmp_path, PATH_NSEPS);
    }
    strcat(tmp_path, _path);
    if (!path_normpath(tmp_path, PATH_MSIZE)) return RESULT_FAILURE;
    if (bfsize <= strlen(tmp_path)) {
        ospath_stat(STATUS_INSFC_BUFFER);
        return RESULT_FAILURE;
    }
    strcpy(buf, tmp_path);
    return RESULT_SUCCESS;
}

#endif  // _WIN32
////////////////////////////////////////////////////////////////////////////////

// 功能：将路径分割为[路径，扩展名]，扩展名包括'.'号
// 成功返回0，失败返回1
int path_splitext(char buf_h[], size_t bhsize, char buf_e[], size_t besize,
                  const char *_path, int extsep) {
    size_t dot_index_n;
    size_t p_len;
    char tmp_path[PATH_MSIZE];
    char *nsep_index, *asep_index, *dot_index;
    const char *name_index;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    p_len = strlen(_path);
    if (p_len >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    strcpy(tmp_path, _path);
    nsep_index = strrchr(tmp_path, PATH_NSEP);
#ifdef _WIN32
    asep_index = strrchr(tmp_path, PATH_ASEP);
    if (asep_index > nsep_index) nsep_index = asep_index;
#endif
    if (!extsep) extsep = PATH_ESEP;
    dot_index = strrchr(tmp_path, extsep);
    if (dot_index > nsep_index) {
        if (NULL != nsep_index)
            name_index = nsep_index + 1;
        else
            name_index = tmp_path;
        while (name_index < dot_index) {
            if (*name_index != extsep) {
                dot_index_n = dot_index - tmp_path;
                if (NULL != buf_h) {
                    if (bhsize <= dot_index_n) {
                        ospath_stat(STATUS_INSFC_BUFFER);
                        return RESULT_FAILURE;
                    }
                    strncpy(buf_h, tmp_path, dot_index_n);
                    buf_h[dot_index_n] = EMPTY_CHAR;
                }
                if (NULL != buf_e) {
                    if (besize <= p_len - dot_index_n) {
                        ospath_stat(STATUS_INSFC_BUFFER);
                        return RESULT_FAILURE;
                    }
                    strcpy(buf_e, dot_index);
                }
                return RESULT_SUCCESS;
            }
            ++name_index;
        }
    }
    if (NULL != buf_h) {
        if (bhsize <= p_len) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return RESULT_FAILURE;
        }
        strcpy(buf_h, tmp_path);
    }
    if (NULL != buf_e) {
        if (besize < 1) {
            ospath_stat(STATUS_INSFC_BUFFER);
            return RESULT_FAILURE;
        }
        buf_e[0] = EMPTY_CHAR;
    }
    return RESULT_SUCCESS;
}

// 功能：重定位路径中的'.'和'..'并把不能重定位的'..'修剪掉
// 成功返回0，失败返回1
int path_prunepath(char buf[], size_t bfsize, const char *_path) {
    size_t total_size = 0;
    int count = 0;
    int return_code = RESULT_SUCCESS;
    char **splited;
    char *stk, *tmp_path, *p_tmp2;
    ospath_stat(STATUS_EXEC_SUCCESS);
    if (!_path) {
        ospath_stat(STATUS_EMPTY_POINTER);
        return RESULT_FAILURE;
    }
    if (strlen(_path) >= PATH_MSIZE) {
        ospath_stat(STATUS_PATH_TOO_LONG);
        return RESULT_FAILURE;
    }
    tmp_path = malloc(PATH_MSIZE * sizeof(char));
    p_tmp2 = malloc(PATH_MSIZE * sizeof(char));
    splited = malloc(PATH_MSIZE * sizeof(char *));
    if (!tmp_path || !p_tmp2 || !splited) {
        return_code = RESULT_FAILURE;
        ospath_stat(STATUS_MEMORY_ERROR);
        goto clean_return;
    }
    strcpy(tmp_path, _path);
    if (!path_normpath(tmp_path, PATH_MSIZE)) {
        return_code = RESULT_FAILURE;
        goto clean_return;
    }
    stk = strtok(tmp_path, PATH_NSEPS);
    while (stk) {
        if (count > PATH_MSIZE) {
            return_code = RESULT_FAILURE;
            ospath_stat(STATUS_PATH_TOO_LONG);
            goto clean_return;
        }
        if (strcmp(stk, PATH_PDIRS)) {
            splited[count++] = stk;
        }
        stk = strtok(NULL, PATH_NSEPS);
    }
    if (NULL != buf) {
        p_tmp2[0] = EMPTY_CHAR;
        for (int i = 0; i < count; ++i) {
            total_size += strlen(splited[i]) + 1;
            if (total_size >= bfsize) {
                return_code = RESULT_FAILURE;
                ospath_stat(STATUS_INSFC_BUFFER);
                goto clean_return;
            }
            strcat(p_tmp2, splited[i]);
            if (i != count - 1) strcat(p_tmp2, PATH_NSEPS);
        }
        strcpy(buf, p_tmp2);
    }
clean_return:
    if (NULL != tmp_path) free(tmp_path);
    if (NULL != p_tmp2) free(p_tmp2);
    if (NULL != splited) free(splited);
    return return_code;
}
