#ifndef __OSPATH_H__
#define __OSPATH_H__

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS  // 关闭MSC强制安全警告
#define _CRT_SECURE_NO_WARNINGS
#endif  // _CRT_SECURE_NO_WARNINGS
#endif  // _MSC_VER

#include <stdbool.h>
#include <stddef.h>

// 函数最后一次执行结束状态码
#define STATUS_EXEC_SUCCESS  0x00000000  // 函数执行成功
#define STATUS_COMMAND_FAIL  0x00000001  // 命令执行失败
#define STATUS_OTHER_ERRORS  0X00000002  // 其他出错原因
#define STATUS_PATH_TOO_LONG 0x00000004  // 路径太长
#define STATUS_MEMORY_ERROR  0x00000008  // 内存分配失败
#define STATUS_GET_ATTR_FAIL 0x00000010  // 获取属性出错
#define STATUS_EMPTY_POINTER 0x00000020  // 遇到空指针
#define STATUS_INVALID_PARAM 0x00000040  // 无效的参数值
#define STATUS_INSFC_BUFFER  0x00000080  // 缓冲区空间不足

// 函数enrich_scanlist的p_type参数可用值
#define PTYPE_FILE 0x00000001  // 搜索文件
#define PTYPE_DIR  0x00000002  // 搜索目录
#define PTYPE_BOTH 0x00000004  // 搜索文件与目录

#define RESULT_SUCCESS 0  // 成功
#define RESULT_FAILURE 1  // 失败

#define RESULT_TRUE  true   // 为真
#define RESULT_FALSE false  // 为假

#define PATH_CDIRS "."   // 代表当前目录的字符串
#define PATH_PDIRS ".."  // 代表上级目录的字符串
#define PATH_ESEP  '.'   // 代表扩展名分隔符的字符
#define PATH_ESEPS "."   // 代表扩展名分隔符的字符串

#define EMPTY_CHAR 0     // 空字符
#define PATH_MSIZE 4096  // 允许处理的最大路径字节数

#ifdef _WIN32
#define PATH_NSEP   '\\'    // 代表普通路径分隔符的字符
#define PATH_NSEPS  "\\"    // 代表普通路径分隔符的字符串
#define PATH_NSEPS2 "\\\\"  // 代表双普通路径分隔符的字符串
#define PATH_ASEP   '/'     // 代表变体路径分隔符的字符
#define PATH_ASEPS  "/"     // 代表变体路径分隔符的字符串
#define PATH_UNCDS  "\\\\.\\"
#define PATH_UNCQS  "\\\\?\\"
#define PATH_COLON  ':'  // 代表驱动器号与路径分隔符的字符
#define PATH_COLONS ":"  // 代表驱动器号与路径分隔符的字符串
#else
#define PATH_NSEP  '/'   // 代表普通路径分隔符的字符
#define PATH_NSEPS "/"   // 代表普通路径分隔符的字符串
// 以下在POSIX平台上无效，仅用于检查
#define PATH_ASEP  '\\'  // 代表变体路径分隔符的字符
#define PATH_ASEPS "\\"  // 代表变体路径分隔符的字符串
#endif

typedef struct {
    size_t blocks;  // 数组paths能容纳的指针数
    size_t count;   // 数组paths中已写入的字符指针数量
    char *paths[];  // 保存路径字符指针的指针数组
} scanlist_t;

int path_last_error(void);  //获取最后一次函数执行的错误状态
scanlist_t *make_scanlist(size_t block);
int enrich_scanlist(const char *dir_path, int p_type, int subdirs,
                    scanlist_t **const pp_scanlst);
void delete_scanlist(scanlist_t *scanlst);
bool is_exist(const char *_path);
bool is_dir(const char *_path);
bool is_file(const char *_path);
bool is_abs(const char *_path);
int make_dirs(const char *_path);
char *get_cwd(char *buf, size_t size);
char *normal_case(char path[]);
char *normal_path(char path[], size_t size);
int split_drv(char buf_d[], size_t bdsize, char buf_p[], size_t bpsize,
              const char *_path);
int join_path(char buf[], size_t bfsize, int n, const char *_path, ...);
int split_path(char buf_h[], size_t bhsize, char buf_t[], size_t btsize,
               const char *_path);
char *dir_name(char buf_dir[], size_t bfsize, const char *_path);
char *base_name(char buf_base[], size_t bfsize, const char *_path);
int rel_path(char buf[], size_t bfsize, const char *_path, const char *start);
int abs_path(char buf[], size_t bfsize, const char *_path);
int split_ext(char buf_h[], size_t bhsize, char buf_e[], size_t besize,
              const char *_path, int extsep);
int prune_path(char buf[], size_t bfsize, const char *_path);

#endif  // __OSPATH_H__
