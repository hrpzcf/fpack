#ifndef __PACKFILE_H
#define __PACKFILE_H
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../ospath/ospath.h"

#define PACK_VERSION "0.1.1"

#define FILE_MAX   LLONG_MAX  // 文件最大字节数
#define PM         PATH_MSIZE // 路径最大字节数
#define PACK_INFO  "\33[32m[信息]\33[0m "
#define PACK_WARN  "\33[33m[警告]\33[0m "
#define PACK_ERROR "\33[31m[错误]\33[0m "

#ifdef _WIN32
#define fseek_fpack _fseeki64
#define ftell_fpack _ftelli64
#define inline _inline
int str_a2u(char buf[], int bfsize, char *ansiSTR);
int str_u2a(char buf[], int bfsize, char *utf8STR);
#else // else posix
#define fseek_fpack fseek
#define ftell_fpack ftell
#endif // end of _WIN32
#define fopen_fpack fopen

// 区分不同平台上printf中的int64_t格式化占位符
#ifndef _WIN32
#ifdef __x86_64
// 64位linux平台的int64类型是(int long)
#define INT64_SPECIFIER "ld"
#else
// 32位linux平台的int64类型是(int long long)
#define INT64_SPECIFIER "lld"
#endif // __x86_64
#else
// 32和64位windows平台上int64_t都是(int long long)
#define INT64_SPECIFIER "lld"
#endif // _WIN32

#define EXIT_CODE_SUCCESS 0 // 成功退出状态码
#define EXIT_CODE_FAILURE 1 // 失败退出状态码

#define ID_N 16  // head的id字段数组大小
#define SP_N 4   // head的sp字段数组大小
#define EM_N 256 // head的emt字段数组大小

// 文件读写缓冲区
typedef struct {
    int64_t size;
    char fdata[];
} buf_t;

#define L_BUF_SIZE 8388608LL   // 文件读写缓冲区大小下限
#define U_BUF_SIZE 134217728LL // 文件读写缓冲区大小上限
#define DIR_SIZE   -1          // 定义目录本身大小为-1
#define EQS_MAX    512         // 显示子文件信息表时分隔符缓冲区大小

#define JPEG_SIG     0xFF // JPEG字段标记码
#define JPEG_START   0xD8 // JPEG图像起始
#define JPEG_END     0xD9 // JPEG图像结束
#define JPEG_INVALID 0    // 无效的JPEG图像
#define JPEG_ERRORS  -1   // 检查JPEG图像过程中出现了错误

// 文件头信息集合
// 注意结构体成员的内存对齐
// 因为要把结构体直接写入到文件或从文件直接读取
#pragma pack(16)
typedef struct {
    char id[ID_N];    // 文件标识符
    char emt[EM_N];   // 预留空字节
    int16_t sp[SP_N]; // 文件规范版本
    int64_t count;    // 包含文件总数
} head_t;             // 文件头信息结构体
#pragma pack()

// 子文件信息，包括文件大小,文件名长度,文件名
// 注意结构体成员的内存对齐
// 因为要把结构体直接写入到文件或从文件直接读取
#pragma pack(2)
typedef struct {
    int64_t offset; // 数据偏移量
    int64_t fsize;  // 数据块大小
    int16_t fnlen;  // 文件名长度
    char fname[PM]; // 文件名字符串
} info_t;
#pragma pack()

// 文件基本信息结构体
typedef struct {
    head_t head;   // 文件的头信息
    int64_t start; // 标识符起始位置
    info_t *subs;  // 子文件信息表
    int64_t subn;  // subs的容量
    FILE *pfhd;    // 打开的二进制流
    char *fpath;   // 文件的绝对路径
} fpack_t;

// 默认文件头信息结构体
static const head_t df_head = {
    // 格式标识：\377pack file\0等16字节，余下字节皆为零
    .id = {0xFF, 0x70, 0x61, 0x63, 0x6B, 0x20, 0x66, 0x69, 0x6C, 0x65},
    // 分别为：2位年份，主版本，次版本，修订版本
    .sp = {22, 1, 0, 6},
    // 预留的空字节用于可能增加的信息字段
    .emt = {0},
    // 主文件中包含的子文件总数，初始总数总是设置为零
    .count = 0LL,
};

// 出错时打印调试信息及退出程序
#define PRINT_ERROR_AND_ABORT(STR) \
    fprintf(stderr, PACK_ERROR STR ": 源码 %s 第 %d 行，[ %s ]\n", path_basename(NULL, 0ULL, __FILE__), __LINE__, PACK_VERSION); \
    exit(EXIT_CODE_FAILURE)

// 出错时判断是否关闭文件流并删除文件
#define WHETHER_CLOSE_REMOVE(PACK) \
    if (PACK->head.count <= 0) { \
        fclose(PACK->pfhd), remove(PACK->fpath); \
    }

#define FS_S (sizeof(int64_t))        // bom的fsize字段大小
#define NL_S (sizeof(int16_t))        // bom的fnlen字段大小
#define ID_S (ID_N * sizeof(char))    // head的id字段大小
#define SP_S (SP_N * sizeof(int16_t)) // head的sp字段大小
#define EM_S (EM_N * sizeof(char))    // head的emt字段大小
#define FC_S (sizeof(int64_t))        // head的count字段大小

#define FCNT_O (ID_S + SP_S + EM_S)        // fcount字段在PACK文件中的偏移量
#define DATA_O (ID_S + SP_S + EM_S + FC_S) // 数据块起始偏移量
#define FSNL_S (FS_S + NL_S)               // 结构体finfo_t中fsize和fnlen的类型大小之和

bool is_fake_jpeg(const char *fakej_path);
fpack_t *fpack_make(const char *file_path, bool overwrite);
fpack_t *fpack_open(const char *file_path);
void fpack_close(fpack_t *st_pfile);
fpack_t *fpack_pack(const char *topack, bool sd, fpack_t *fpack, bool add);
fpack_t *fpack_extract(const char *name, const char *save_path, int overwrite, fpack_t *fpack);
fpack_t *fpack_info(const char *pk_path);
fpack_t *fpack_fakej_make(const char *pf_path, const char *jpeg_path, bool overwrite);
fpack_t *fpack_fakej_open(const char *fake_jpeg_path);

#endif //__PACKFILE_H
