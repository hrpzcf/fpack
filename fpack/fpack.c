#include "fpack.h"

#include <stdlib.h>

// 扩充子文件信息表容量
static bool expand_bom(fpack_t *pf, size_t num) {
    info_t *finfo_tmp;
    size_t exp_size;
    if (!pf)
        return false;
    if (num > INT64_MAX)
        return false;
    if (num == 0ULL)
        num = 1ULL;
    if (!pf->subs) {
        finfo_tmp = malloc(num * sizeof(info_t));
        if (finfo_tmp) {
            pf->subn = (int64_t)num;
            pf->subs = finfo_tmp;
            return true;
        } else
            return false;
    }
    if (pf->subn - pf->head.count >= (int64_t)num) {
        return true;
    }
    exp_size = (num + pf->head.count) * sizeof(info_t);
    finfo_tmp = realloc(pf->subs, exp_size);
    if (!finfo_tmp)
        return false;
    pf->subs = finfo_tmp;
    pf->subn = (int64_t)num + pf->head.count;
    return true;
}

// 扩充文件读写缓冲区内存空间
static bool expand_buf(buf_t **pp_buf, int64_t size) {
    buf_t *buf_tmp;
    if (!pp_buf || !*pp_buf)
        return false;
    if ((*pp_buf)->size >= size)
        return true;
    buf_tmp = realloc(*pp_buf, sizeof(buf_t) + size);
    if (!buf_tmp)
        return false;
    buf_tmp->size = size, *pp_buf = buf_tmp;
    return true;
}

// 大文件复制，从子文件流复制到主文件流
static bool copier_sub2pack(FILE *sub_stm, FILE *pk_stm, buf_t **buf_frw) {
    size_t fr_size; // 每次fread的大小
    if (!expand_buf(buf_frw, U_BUF_SIZE))
        return false;
    while (!feof(sub_stm)) {
        fr_size = fread((*buf_frw)->fdata, 1, U_BUF_SIZE, sub_stm);
        if (fr_size == 0ULL) {
            return true;
        } else if (fr_size < 0ULL)
            return false;
        if (fwrite((*buf_frw)->fdata, fr_size, 1, pk_stm) != 1)
            return false;
    }
    return true;
}

// 大文件复制，从pk_stm复制到sub_stm
static bool copier_pack2sub(FILE *pk_stm, int64_t offset, int64_t fsize, FILE *sub_stm, buf_t **buf_frw) {
    if (!expand_buf(buf_frw, U_BUF_SIZE))
        return false;
    if (fseek_fpack(pk_stm, offset, SEEK_SET))
        return false;
    while (fsize >= U_BUF_SIZE) {
        if (fread((*buf_frw)->fdata, U_BUF_SIZE, 1, pk_stm) != 1)
            return false;
        if (fwrite((*buf_frw)->fdata, U_BUF_SIZE, 1, sub_stm) != 1)
            return false;
        if ((fsize -= U_BUF_SIZE) < U_BUF_SIZE) {
            if (fsize > 0LL) {
                if (fread((*buf_frw)->fdata, fsize, 1, pk_stm) != 1)
                    return false;
                if (fwrite((*buf_frw)->fdata, fsize, 1, sub_stm) != 1)
                    return false;
            }
            return true;
        }
    }
    return true;
}

// 获取JPEG文件的净大小
static int64_t realsizeofj(FILE *p_jpeg, int64_t total, buf_t **buf_8bit) {
    uint8_t *buf_u8;
    int64_t end = 0;
    if (total < 4)
        return JPEG_INVALID;
    if ((*buf_8bit)->size < total)
        if (!expand_buf(buf_8bit, total))
            return JPEG_ERRORS;
    rewind(p_jpeg);
    if (fread((*buf_8bit)->fdata, 1, total, p_jpeg) != total) {
        return JPEG_ERRORS;
    }
    buf_u8 = (uint8_t *)(*buf_8bit)->fdata;
    if (!(buf_u8[0] == JPEG_SIG && buf_u8[1] == JPEG_START))
        return JPEG_INVALID;
    for (end = 4; end <= total; ++end)
        if (buf_u8[end - 2] == JPEG_SIG && buf_u8[end - 1] == JPEG_END)
            return end;
    return JPEG_INVALID;
}

// 判断是否是伪装的JPEG文件
bool is_fake_jpeg(const char *fakej_path) {
    FILE *fhd_fake_jpeg;
    buf_t *buf_frw;
    int64_t fake_jpeg_size, jpeg_netsize;
    head_t head_tmp;
    static char buf_p[PATH_MSIZE];
    bool return_code = false;
    if (path_abspath(buf_p, PATH_MSIZE, fakej_path))
        return return_code;
    fhd_fake_jpeg = fopen_fpack(buf_p, "rb");
    if (!fhd_fake_jpeg)
        return return_code;
    if (fseek_fpack(fhd_fake_jpeg, 0LL, SEEK_END))
        return return_code;
    if ((fake_jpeg_size = ftell_fpack(fhd_fake_jpeg)) < 0LL)
        return return_code;
    if (buf_frw = malloc(fake_jpeg_size + sizeof(head_t) + sizeof(buf_t))) {
        buf_frw->size = fake_jpeg_size + sizeof(head_t);
    } else {
        return false;
    }
    jpeg_netsize = realsizeofj(fhd_fake_jpeg, fake_jpeg_size, &buf_frw);
    if (jpeg_netsize == JPEG_INVALID || jpeg_netsize == JPEG_ERRORS)
        goto free_return;
    if (fake_jpeg_size - jpeg_netsize < sizeof(head_t)) {
        goto free_return;
    }
    if (fseek_fpack(fhd_fake_jpeg, jpeg_netsize, SEEK_SET))
        goto free_return;
    if (fread(&head_tmp, sizeof(head_t), 1, fhd_fake_jpeg) != 1)
        goto free_return;
    if (memcmp(df_head.id, head_tmp.id, sizeof(df_head.id)))
        goto free_return;
    return_code = true;
free_return:
    free(buf_frw);
    fclose(fhd_fake_jpeg);
    return return_code;
}

// 关闭fpack_t对象
void fpack_close(fpack_t *fpack) {
    if (fpack) {
        if (fpack->fpath)
            free(fpack->fpath);
        if (fpack->subs)
            free(fpack->subs);
        if (fpack->pfhd)
            fclose(fpack->pfhd);
        free(fpack);
    }
}

// 创建新PACK文件
fpack_t *fpack_make(const char *pf_path, bool overwrite) {
    FILE *pfhd_pack;           // 二进制文件流指针
    fpack_t *fpack;            // PACK文件信息结构体
    char buf_path[PATH_MSIZE]; // 绝对路径及父目录缓冲
    char *cp_pf_path;          // 拷贝路径用于结构体
    if (path_abspath(buf_path, PATH_MSIZE, pf_path)) {
        printf(PACK_ERROR "无法获取主文件绝对路径：%s\n", pf_path);
        exit(EXIT_CODE_FAILURE);
    }
    printf(PACK_INFO "创建主文件：%s\n", buf_path);
    cp_pf_path = malloc(strlen(buf_path) + 1ULL);
    if (!cp_pf_path) {
        PRINT_ERROR_AND_ABORT("为主文件路径数组分配内存失败");
    }
    strcpy(cp_pf_path, buf_path);
    if (path_exists(cp_pf_path)) {
        if (path_isdir(cp_pf_path)) {
            printf(PACK_ERROR "此位置已存在同名目录\n");
            exit(EXIT_CODE_FAILURE);
        } else if (path_last_state()) {
            printf(PACK_ERROR "获取路径属性失败\n");
            exit(EXIT_CODE_FAILURE);
        } else if (!overwrite) {
            printf(PACK_ERROR "已存在同名文件但未指定覆盖\n");
            exit(EXIT_CODE_FAILURE);
        }
    } else if (path_last_state()) {
        printf(PACK_ERROR "无法检查此路径是否存在\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!path_dirname(buf_path, PATH_MSIZE, buf_path)) {
        printf(PACK_ERROR "获取主文件的父目录路径失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!path_exists(buf_path)) {
        if (path_mkdirs(buf_path)) {
            printf(PACK_ERROR "为主文件创建目录失败\n");
            exit(EXIT_CODE_FAILURE);
        }
    } else if (!path_isdir(buf_path)) {
        printf(PACK_ERROR "父目录已被文件占用，无法创建主文件\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!(pfhd_pack = fopen_fpack(cp_pf_path, "wb"))) {
        printf(PACK_ERROR "主文件创建失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (fwrite(&df_head, sizeof(df_head), 1, pfhd_pack) != 1) {
        fclose(pfhd_pack);
        remove(pf_path);
        PRINT_ERROR_AND_ABORT("写入PACK文件头失败");
    }
    // 此时文件指针已经在文件末尾
    if (fpack = malloc(sizeof(fpack_t))) {
        fpack->head = df_head;
        fpack->start = 0LL;
        fpack->subs = NULL;
        fpack->subn = 0LL;
        fpack->fpath = cp_pf_path;
        fpack->pfhd = pfhd_pack;
        return fpack;
    } else {
        fclose(pfhd_pack);
        remove(pf_path);
        PRINT_ERROR_AND_ABORT("为文件信息结构体分配内存失败");
    }
}

// 打开已存在的PACK文件
fpack_t *fpack_open(const char *pf_path) {
    fpack_t *fpack;            // PACK文件信息结构体
    head_t tmp_head;           // 临时PACK文件头
    info_t *bom_pfile;         // 子文件信息表
    FILE *pfhd_pack;           // 主文件二进制流
    char buf_path[PATH_MSIZE]; // 绝对路径及父目录缓冲
    char *cp_pf_path;          // 拷贝路径用于结构体
    int64_t subn = 0LL;        // 子文件信息表容量
    if (path_abspath(buf_path, PATH_MSIZE, pf_path)) {
        printf(PACK_ERROR "无法获取主文件绝对路径：%s\n", pf_path);
        exit(EXIT_CODE_FAILURE);
    }
    printf(PACK_INFO "打开主文件：%s\n", buf_path);
    cp_pf_path = malloc(strlen(buf_path) + 1ULL);
    if (!cp_pf_path) {
        PRINT_ERROR_AND_ABORT("为主文件文件名分配内存失败");
    }
    strcpy(cp_pf_path, buf_path);
    if (!path_isfile(cp_pf_path)) {
        if (path_last_state()) {
            printf(PACK_ERROR "获取主文件路径属性失败\n");
            exit(EXIT_CODE_FAILURE);
        } else {
            printf(PACK_ERROR "此路径不是一个文件路径\n");
            exit(EXIT_CODE_FAILURE);
        }
    }
    if (!(pfhd_pack = fopen_fpack(cp_pf_path, "r+b"))) {
        printf(PACK_ERROR "主文件打开失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (fread(&tmp_head, sizeof(tmp_head), 1, pfhd_pack) != 1) {
        PRINT_ERROR_AND_ABORT("读取主文件头失败");
    }
    if (memcmp(df_head.id, tmp_head.id, sizeof(df_head.id))) {
        printf(PACK_ERROR "此文件不是一个PACK文件\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (tmp_head.count > 0)
        subn = tmp_head.count;
    else
        subn = 1LL;
    bom_pfile = malloc((size_t)subn * sizeof(info_t));
    if (!bom_pfile) {
        PRINT_ERROR_AND_ABORT("为子文件信息表分配内存失败");
    }
    if (fseek_fpack(pfhd_pack, DATA_O, SEEK_SET)) {
        PRINT_ERROR_AND_ABORT("移动文件指针到数据块起始位置失败");
    }
    for (int64_t i = 0; i < tmp_head.count; ++i) {
        if ((bom_pfile[i].offset = ftell_fpack(pfhd_pack)) < 0LL) {
            PRINT_ERROR_AND_ABORT("获取当前字段偏移量失败");
        }
        if (fread(&bom_pfile[i].fsize, FSNL_S, 1, pfhd_pack) != 1) {
            PRINT_ERROR_AND_ABORT("读取子文件属性失败");
        }
        if (bom_pfile[i].fnlen > PATH_MSIZE) {
            PRINT_ERROR_AND_ABORT("读取到的子文件名长度异常");
        }
        if (fread(bom_pfile[i].fname, bom_pfile[i].fnlen, 1, pfhd_pack) != 1) {
            PRINT_ERROR_AND_ABORT("从主文件读取子文件名失败");
        }
#ifdef _WIN32
        str_u2a(bom_pfile[i].fname, PM, bom_pfile[i].fname);
#endif // _WIN32
       // 遇到目录(大小是-1)或文件大小为0时没有数据块不需要移动文件指针
        if (bom_pfile[i].fsize <= 0)
            continue;
        if (fseek_fpack(pfhd_pack, bom_pfile[i].fsize, SEEK_CUR)) {
            PRINT_ERROR_AND_ABORT("移动文件指针至下一个位置失败");
        }
    }
    fseek_fpack(pfhd_pack, 0, SEEK_END); // 默认文件指针在末尾
    if (fpack = malloc(sizeof(fpack_t))) {
        fpack->head = tmp_head;
        fpack->start = 0LL;
        fpack->subs = bom_pfile;
        fpack->subn = subn;
        fpack->fpath = cp_pf_path;
        fpack->pfhd = pfhd_pack;
        return fpack;
    } else {
        free(bom_pfile), free(cp_pf_path);
        PRINT_ERROR_AND_ABORT("为pack文件信息结构体分配内存失败");
    }
}

// 将目标打包进已创建的空PACK文件
fpack_t *fpack_pack(const char *topack, bool sd, fpack_t *fpack, bool add) {
    // 如果packto是目录，则此变量用于存放其父目录
    char *parent_dir;
    // 存放绝对路径用于比较是否同一文件
    static char buf_absp1[PATH_MSIZE]; // 主文件
    static char buf_absp2[PATH_MSIZE]; // 子文件
#ifdef _WIN32
    static char buf_cased[PATH_MSIZE]; // WIN平台比较路径是否相同需要先转全小写
#endif
    // 收集路径的scanlist容器
    scanner_t *scanlist_paths;
    FILE *stream_subfile; // 打开子文件共用指针
    // 用于临时读写文件大小、文件名长度、文件名，也用于更新主文件结构体的子文件信息表
    info_t finfo;
    buf_t *buf_frw; // 文件读写缓冲区
    if (!topack) {
        PRINT_ERROR_AND_ABORT("打包目标路径是空指针");
    } else if (!*topack) {
        PRINT_ERROR_AND_ABORT("打包目标路径是空字符串");
    }
    if (!path_exists(topack)) {
        printf(PACK_ERROR "目标文件或目录不存在：%s\n", topack);
        exit(EXIT_CODE_FAILURE);
    }
    if (fpack->head.count > 0LL && !add) {
        printf(PACK_WARN "主文件已打包%" INT64_SPECIFIER "个文件，但此次未指定增量打包\n", fpack->head.count);
        exit(EXIT_CODE_FAILURE);
    }
    strcpy(buf_absp1, fpack->fpath);
    if (!path_normpath(path_normcase(buf_absp1), PATH_MSIZE)) {
        WHETHER_CLOSE_REMOVE(fpack);
        PRINT_ERROR_AND_ABORT("获取路径标准形式失败");
    }
    // 初始缓冲区大小L_BUF_SIZE字节
    if (buf_frw = malloc(sizeof(buf_t) + L_BUF_SIZE)) {
        buf_frw->size = L_BUF_SIZE;
    } else {
        WHETHER_CLOSE_REMOVE(fpack);
        PRINT_ERROR_AND_ABORT("为文件读写缓冲区分配初始内存失败");
    }
    if (path_isfile(topack)) {
        printf(PACK_INFO "写入：%s\n", topack);
        if (path_abspath(buf_absp2, PATH_MSIZE, topack)) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("获取子文件绝对路径失败");
        }
#ifndef _WIN32
        if (!strcmp(buf_absp1, buf_absp2))
#else
        strcpy(buf_cased, buf_absp2);
        if (!strcmp(buf_absp1, path_normcase(buf_cased)))
#endif // _WIN32
        {
            WHETHER_CLOSE_REMOVE(fpack);
            printf(PACK_ERROR "退出：此文件是主文件\n");
            exit(EXIT_CODE_SUCCESS);
        }
        if (!path_basename(finfo.fname, PATH_MSIZE, buf_absp2)) {
            WHETHER_CLOSE_REMOVE(fpack);
            printf(PACK_ERROR "获取子文件名失败：%s\n", buf_absp2);
            exit(EXIT_CODE_FAILURE);
        }
#ifdef _WIN32
        // WIN平台需要把文件名字符转为UTF8编码的字符串
        str_a2u(finfo.fname, PATH_MSIZE, finfo.fname);
#endif // _WIN32
        finfo.fnlen = (int16_t)(strlen(finfo.fname) + 1);
        if (!(stream_subfile = fopen_fpack(topack, "rb"))) {
            WHETHER_CLOSE_REMOVE(fpack);
            printf(PACK_ERROR "打开子文件失败：%s\n", topack);
            exit(EXIT_CODE_FAILURE);
        }
        fseek_fpack(stream_subfile, 0, SEEK_END);
        finfo.fsize = ftell_fpack(stream_subfile);
        rewind(stream_subfile); // 子文件读取大小后文件指针移回开头备用
        // 无需将主文件指针移至末尾，因为pack_open或pack_make函数已将其移至末尾
        if ((finfo.offset = ftell_fpack(fpack->pfhd)) < 0LL) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("获取当前字段偏移量失败");
        }
        if (fwrite(&finfo.fsize, FSNL_S + finfo.fnlen, 1, fpack->pfhd) != 1) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("写入子文件属性失败");
        }
        if (finfo.fsize > U_BUF_SIZE) {
            if (!copier_sub2pack(stream_subfile, fpack->pfhd, &buf_frw)) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("将子文件写入主文件失败\n");
            }
        } else if (finfo.fsize > 0) {
            if (finfo.fsize > buf_frw->size && !expand_buf(&buf_frw, finfo.fsize)) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("扩充文件读写缓冲区空间失败");
            }
            if (fread(buf_frw->fdata, finfo.fsize, 1, stream_subfile) != 1) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("读取子文件失败");
            }
            if (fwrite(buf_frw, finfo.fsize, 1, fpack->pfhd) != 1) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("写入子文件失败");
            }
        }
        fclose(stream_subfile);
        fseek_fpack(fpack->pfhd, fpack->start + FCNT_O, SEEK_SET);
        if (fwrite(&fpack->head.count, sizeof(int64_t), 1, fpack->pfhd) != 1) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("更新子文件数量失败");
        }
        fseek_fpack(fpack->pfhd, 0LL, SEEK_END);
        if (fpack->subn <= fpack->head.count) {
            if (!expand_bom(fpack, 1ULL)) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("扩充子文件信息表容量失败");
            }
        }
        fpack->subs[fpack->head.count++] = finfo;
    } else if (path_isdir(topack)) {
        if (path_abspath(buf_absp2, PATH_MSIZE, topack)) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("获取子文件目录绝对路径失败");
        }
        if (parent_dir = malloc(PATH_MSIZE)) {
            if (!path_dirname(parent_dir, PATH_MSIZE, path_normpath(buf_absp2, PATH_MSIZE))) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("获取子文件目录的父目录失败");
            }
        } else {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("为子文件父目录缓冲区分配内存失败");
        }
        printf(PACK_INFO "扫描目录...\n");
        if (!(scanlist_paths = path_mkscan(0))) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("创建路径扫描器失败");
        }
        if (path_scanpath(buf_absp2, FTYPE_BOTH, sd, &scanlist_paths)) {
            WHETHER_CLOSE_REMOVE(fpack);
            printf(PACK_ERROR "扫描目录失败：%s\n", buf_absp2);
            exit(EXIT_CODE_FAILURE);
        }
        if (!expand_bom(fpack, scanlist_paths->count)) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("扩充子文件信息表容量失败");
        }
        for (size_t i = 0; i < scanlist_paths->count; ++i) {
            printf(PACK_INFO "写入：%s\n", scanlist_paths->paths[i]);
            if (path_isdir(scanlist_paths->paths[i])) {
                finfo.fsize = DIR_SIZE; // 目录大小定义为DIR_SIZE
                if (path_relpath(finfo.fname, PATH_MSIZE, scanlist_paths->paths[i], parent_dir)) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    printf(PACK_WARN "跳过：获取子目录相对路径失败");
                    continue;
                }
#ifdef _WIN32
                // WIN平台要把字符串转为UTF8编码写入文件
                str_a2u(finfo.fname, PATH_MSIZE, finfo.fname);
#endif // _WIN32
                finfo.fnlen = (int16_t)(strlen(finfo.fname) + 1);
                if ((finfo.offset = ftell_fpack(fpack->pfhd)) < 0LL) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    printf(PACK_WARN "跳过：获取当前字段偏移量失败\n");
                    continue;
                }
                // 按fsize、fnlen类型长度及fnlen值将finfo_tmp的一部分写入主文件
                if (fwrite(&finfo.fsize, FSNL_S + finfo.fnlen, 1, fpack->pfhd) != 1) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    } else {
                        fseek_fpack(fpack->pfhd, finfo.offset, SEEK_SET);
                    }
                    printf(PACK_WARN "跳过：写入子文件属性失败\n");
                    continue;
                }
            } else {
#ifndef _WIN32
                if (!strcmp(buf_absp1, scanlist_paths->paths[i]))
#else
                strcpy(buf_cased, scanlist_paths->paths[i]);
                if (!strcmp(buf_absp1, path_normcase(buf_cased)))
#endif // _WIN32
                {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    printf(PACK_WARN "跳过：此文件是主文件\n");
                    continue;
                }
                if (path_relpath(finfo.fname, PATH_MSIZE, scanlist_paths->paths[i], parent_dir)) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    printf(PACK_WARN "跳过：获取子文件相对路径失败\n");
                    continue;
                }
                if (!(stream_subfile = fopen_fpack(scanlist_paths->paths[i], "rb"))) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    printf(PACK_WARN "跳过：子文件打开失败\n");
                    continue;
                }
                fseek_fpack(stream_subfile, 0, SEEK_END);
                finfo.fsize = (int64_t)ftell_fpack(stream_subfile);
                // 子文件读取大小后指针移回开头备用
                rewind(stream_subfile);
#ifdef _WIN32 // WIN平台需要将文件名编码转为UTF8保存
                str_a2u(finfo.fname, PATH_MSIZE, finfo.fname);
#endif // _WIN32
                finfo.fnlen = (int16_t)(strlen(finfo.fname) + 1);
                if ((finfo.offset = ftell_fpack(fpack->pfhd)) < 0LL) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    fclose(stream_subfile);
                    printf(PACK_WARN "跳过：获取主文件指针位置失败\n");
                    continue;
                }
                if (fwrite(&finfo.fsize, FSNL_S + finfo.fnlen, 1, fpack->pfhd) != 1) {
                    printf(PACK_WARN "跳过：写入子文件属性失败\n");
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    } else {
                        fseek_fpack(fpack->pfhd, finfo.offset, SEEK_SET);
                    }
                    fclose(stream_subfile);
                    continue;
                }
                if (finfo.fsize > U_BUF_SIZE) {
                    if (!copier_sub2pack(stream_subfile, fpack->pfhd, &buf_frw)) {
                        if (i >= scanlist_paths->count - 1) {
                            WHETHER_CLOSE_REMOVE(fpack);
                        } else {
                            fseek_fpack(fpack->pfhd, finfo.offset, SEEK_SET);
                        }
                        fclose(stream_subfile);
                        printf(PACK_WARN "跳过：将子文件写入主文件失败\n");
                        continue;
                    }
                } else if (finfo.fsize > 0) { // 大小等于0的文件无需读写
                    if (finfo.fsize > buf_frw->size && !expand_buf(&buf_frw, finfo.fsize)) {
                        WHETHER_CLOSE_REMOVE(fpack);
                        PRINT_ERROR_AND_ABORT("扩充文件读写缓冲区空间失败");
                    }
                    if (fread(buf_frw->fdata, finfo.fsize, 1, stream_subfile) != 1) {
                        if (i >= scanlist_paths->count - 1) {
                            WHETHER_CLOSE_REMOVE(fpack);
                        }
                        fclose(stream_subfile);
                        printf(PACK_WARN "跳过：读取子文件失败\n");
                        continue;
                    }
                    if (fwrite(buf_frw->fdata, finfo.fsize, 1, fpack->pfhd) != 1) {
                        if (i >= scanlist_paths->count - 1) {
                            WHETHER_CLOSE_REMOVE(fpack);
                        } else {
                            fseek_fpack(fpack->pfhd, finfo.offset, SEEK_SET);
                        }
                        fclose(stream_subfile);
                        printf(PACK_WARN "跳过：将子文件写入主文件失败\n");
                        continue;
                    }
                }
                fclose(stream_subfile);
            }
            fpack->subs[fpack->head.count++] = finfo;
        }
        path_delscan(scanlist_paths);
    } else {
        WHETHER_CLOSE_REMOVE(fpack);
        printf(PACK_ERROR "路径不是文件也不是目录：%s\n", topack);
        exit(EXIT_CODE_FAILURE);
    }
    fseek_fpack(fpack->pfhd, fpack->start + FCNT_O, SEEK_SET);
    if (fwrite(&fpack->head.count, sizeof(int64_t), 1, fpack->pfhd) != 1) {
        WHETHER_CLOSE_REMOVE(fpack);
        PRINT_ERROR_AND_ABORT("更新主文件中的子文件数量字段失败");
    }
    if (buf_frw)
        free(buf_frw);
    return fpack;
}

// 打印PACK文件中的文件列表即其他信息
fpack_t *fpack_info(const char *pk_path) {
    size_t a, b, c;        // 已打印的(大小、类型、路径)累计字符数
    size_t tmp_strlen;     // 每个fname长度的临时变量
    size_t max_strlen = 0; // 长度最大的fname的值
    int16_t *s;            // 指向head中的sp，写完显得太长
    int64_t index;         // 遍历主文件中文件总数head.count
    fpack_t *fpack;        // 主文件信息结构体
    char eqs1[EQS_MAX];    // 打印的子文件列表分隔符共用缓冲区
    char *eqs2, *eqs3;     // 用于将上面缓冲区分离为三个字符串
    if (is_fake_jpeg(pk_path))
        fpack = fpack_fakej_open(pk_path);
    else
        fpack = fpack_open(pk_path);
    s = fpack->head.sp;
    for (index = 0; index < fpack->head.count; ++index) {
        tmp_strlen = strlen(fpack->subs[index].fname);
        if (max_strlen < tmp_strlen)
            max_strlen = tmp_strlen;
    }
#ifdef _MSC_VER
    // 打开MSC编译器printf的%n占位符支持
    _set_printf_count_output(1);
#endif // _MSC_VER
    printf("\n格式版本：");
    printf("%hd.%hd.%hd.%hd\t", s[0], s[1], s[2], s[3]);
    printf("包含条目总数：");
    printf("%" INT64_SPECIFIER "\n\n", fpack->head.count);
    printf("%19s%zn\t%4s%zn\t%s%zn\n", "大小", &a, "类型", &b, "路径", &c);
    if (max_strlen < c - b - 1)
        max_strlen = c - b - 1;
    else if (max_strlen >= EQS_MAX)
        max_strlen = EQS_MAX - 1;
    memset(eqs1, '=', EQS_MAX);
    eqs1[a] = EMPTY_CHAR;
    eqs2 = eqs1 + a + 1;
    eqs1[b] = EMPTY_CHAR;
    eqs3 = eqs1 + b + 1;
    eqs3[max_strlen] = EMPTY_CHAR;
    printf("%s\t%s\t%s\n", eqs1, eqs2, eqs3);
    for (index = 0; index < fpack->head.count; ++index) {
        printf("%19" INT64_SPECIFIER "\t%s\t%s\n", fpack->subs[index].fsize, fpack->subs[index].fsize < 0 ? "目录" : "文件", fpack->subs[index].fname);
    }
    printf("\n格式版本：");
    printf("%hd.%hd.%hd.%hd\t", s[0], s[1], s[2], s[3]);
    printf("包含条目总数：");
    printf("%" INT64_SPECIFIER "\n\n", fpack->head.count);
    return fpack;
}

// 从PACK文件中提取子文件
fpack_t *fpack_extract(const char *name, const char *save_path, int overwrite, fpack_t *fpack) {
    int64_t index;  // 循环遍历子文件时的下标
    int64_t offset; // 子文件字段在主文件中的偏移
#ifdef _WIN32
    static char buf_cased1[PATH_MSIZE];
    static char buf_cased2[PATH_MSIZE];
#endif
    static char buf_sub_path[PATH_MSIZE];
    static char buf_sub_pard[PATH_MSIZE];
    buf_t *buf_frw;     // 从主文件提取到子文件时的文件读写缓冲区
    FILE *fhnd_subfile; // 创建子文件时每个子文件的二进制文件流句柄
    if (buf_frw = malloc(sizeof(buf_t) + L_BUF_SIZE)) {
        buf_frw->size = L_BUF_SIZE;
    } else {
        PRINT_ERROR_AND_ABORT("为文件读写缓冲区分配内存失败");
    }
    if (!save_path || !*save_path)
        save_path = PATH_CDIRS;
    else {
        if (!path_exists(save_path)) {
            if (path_last_state()) {
                fprintf(stderr, PACK_ERROR "获取路径属性失败：%s\n", save_path);
                exit(EXIT_CODE_FAILURE);
            }
            if (path_mkdirs(save_path)) {
                fprintf(stderr, PACK_ERROR "创建目录失败：%s\n", save_path);
                exit(EXIT_CODE_FAILURE);
            }
        } else if (!path_isdir(save_path)) {
            fprintf(stderr, PACK_ERROR "保存目录已被文件名占用：%s\n", save_path);
            exit(EXIT_CODE_FAILURE);
        }
    }
#ifdef _WIN32
    if (name) {
        strcpy(buf_cased1, name);
        path_normcase(buf_cased1);
    }
#endif
    for (index = 0; index < fpack->head.count; ++index) {
        if (name) {
#ifdef _WIN32
            strcpy(buf_cased2, fpack->subs[index].fname);
            if (strcmp(buf_cased1, path_normcase(buf_cased2)))
                continue;
#else
            if (strcmp(name, fpack->subs[index].fname))
                continue;
#endif
        }
        printf(PACK_INFO "提取：%s\n", fpack->subs[index].fname);
        if (path_joinpath(buf_sub_path, PATH_MSIZE, 2, save_path, fpack->subs[index].fname)) {
            printf(PACK_WARN "跳过：拼接子文件完整路径失败\n");
            continue;
        }
        if (fpack->subs[index].fsize < 0) {
            if (path_exists(buf_sub_path)) {
                if (path_isdir(fpack->subs[index].fname))
                    continue;
                printf(PACK_WARN "跳过：目录名称已被文件占用s\n");
                continue;
            }
            if (path_mkdirs(buf_sub_path)) {
                printf(PACK_WARN "跳过：无法在此位置创建目录\n");
                continue;
            }
        } else {
            if (path_exists(buf_sub_path)) {
                if (path_isdir(buf_sub_path)) {
                    printf(PACK_WARN "跳过：文件路径已被目录占用：%s\n", buf_sub_path);
                    continue;
                }
                if (!overwrite) {
                    printf(PACK_WARN "跳过：文件已存在且未指定覆盖：%s\n", buf_sub_path);
                    continue;
                }
            }
            if (!path_dirname(buf_sub_pard, PATH_MSIZE, buf_sub_path)) {
                printf(PACK_WARN "跳过：获取父级路径失败\n");
                continue;
            }
            if (path_exists(buf_sub_pard)) {
                if (path_isfile(buf_sub_pard)) {
                    printf(PACK_WARN "跳过：目录路径已被文件占用：%s\n", buf_sub_pard);
                    continue;
                }
            } else {
                if (path_mkdirs(buf_sub_pard)) {
                    printf(PACK_WARN "跳过：目录创建失败：%s\n", buf_sub_pard);
                }
            }
            if (!(fhnd_subfile = fopen_fpack(buf_sub_path, "wb"))) {
                printf(PACK_WARN "跳过：子文件创建失败：%s\n", buf_sub_path);
                continue;
            }
            offset = fpack->subs[index].offset + FSNL_S + fpack->subs[index].fnlen;
            if (fpack->subs[index].fsize > U_BUF_SIZE) {
                if (!copier_pack2sub(fpack->pfhd, offset, fpack->subs[index].fsize, fhnd_subfile, &buf_frw)) {
                    printf(PACK_WARN "跳过：写入子文件数据失败：%s\n", buf_sub_path);
                    continue;
                }
            } else if (fpack->subs[index].fsize > 0) {
                if (buf_frw->size < fpack->subs[index].fsize) {
                    if (!expand_buf(&buf_frw, fpack->subs[index].fsize)) {
                        PRINT_ERROR_AND_ABORT("扩充文件读写缓冲区失败");
                    }
                }
                if (fseek_fpack(fpack->pfhd, offset, SEEK_SET)) {
                    printf(PACK_WARN "跳过：移动主文件指针失败\n");
                    continue;
                }
                if (fread(buf_frw->fdata, fpack->subs[index].fsize, 1, fpack->pfhd) != 1) {
                    printf(PACK_WARN "跳过：读取子文件数据失败");
                    continue;
                }
                if (fwrite(buf_frw->fdata, fpack->subs[index].fsize, 1, fhnd_subfile) != 1) {
                    printf(PACK_WARN "跳过：写入子文件数据失败");
                    continue;
                }
            }
            fclose(fhnd_subfile);
        }
    }
    if (buf_frw)
        free(buf_frw);
    return fpack;
}

// 创建空的伪装的JPEG文件
fpack_t *fpack_fakej_make(const char *pf_path, const char *jpeg_path, bool overwrite) {
    FILE *pfhd_pack;           // 主文件二进制文件流
    FILE *fhd_jpeg;            // JPEG文件二进制文件流
    fpack_t *fpack;            // 主文件信息结构体
    int64_t jpeg_netsize;      // JPEG文件净大小
    int64_t fake_jpeg_size;    // JPEG文件的总大小
    buf_t *buf_frw;            // 文件读写缓冲区
    char *cp_pf_path;          // 复制的文件路径
    char buf_path[PATH_MSIZE]; // 绝对路径及父目录缓冲
    if (path_abspath(buf_path, PATH_MSIZE, pf_path)) {
        printf(PACK_ERROR "无法获取主文件绝对路径：%s\n", pf_path);
        exit(EXIT_CODE_FAILURE);
    }
    printf(PACK_INFO "创建主文件：%s\n", buf_path);
    cp_pf_path = malloc(strlen(buf_path) + 1ULL);
    if (!cp_pf_path) {
        PRINT_ERROR_AND_ABORT("为主文件路径数组分配内存失败");
    }
    strcpy(cp_pf_path, buf_path);
    if (path_exists(cp_pf_path)) {
        if (path_isdir(cp_pf_path)) {
            printf(PACK_ERROR "此位置已存在同名目录\n");
            exit(EXIT_CODE_FAILURE);
        } else if (path_last_state()) {
            printf(PACK_ERROR "获取路径属性失败\n");
            exit(EXIT_CODE_FAILURE);
        } else if (!overwrite) {
            printf(PACK_ERROR "已存在同名文件但未指定覆盖\n");
            exit(EXIT_CODE_FAILURE);
        }
    } else if (path_last_state()) {
        printf(PACK_ERROR "无法检查此路径是否存在\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!path_dirname(buf_path, PATH_MSIZE, buf_path)) {
        printf(PACK_ERROR "获取主文件的父目录路径失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!path_exists(buf_path)) {
        if (path_mkdirs(buf_path)) {
            printf(PACK_ERROR "为主文件创建目录失败\n");
            exit(EXIT_CODE_FAILURE);
        }
    } else if (!path_isdir(buf_path)) {
        printf(PACK_ERROR "父目录名已被文件名占用：%s\n", buf_path);
        exit(EXIT_CODE_FAILURE);
    }
    if (!path_isfile(jpeg_path)) {
        printf(PACK_ERROR "指定的图像路径不是一个文件或不存在：%s\n", jpeg_path);
        exit(EXIT_CODE_FAILURE);
    }
    if (!(pfhd_pack = fopen_fpack(cp_pf_path, "wb"))) {
        PRINT_ERROR_AND_ABORT("主文件创建失败");
    }
    if (!(fhd_jpeg = fopen_fpack(jpeg_path, "rb"))) {
        fclose(pfhd_pack), remove(cp_pf_path);
        PRINT_ERROR_AND_ABORT("打开JPEG图像文件失败");
    }
    if (fseek_fpack(fhd_jpeg, 0LL, SEEK_END)) {
        fclose(fhd_jpeg);
        fclose(pfhd_pack), remove(cp_pf_path);
        PRINT_ERROR_AND_ABORT("移动JPEG文件指针失败");
    }
    if ((fake_jpeg_size = ftell_fpack(fhd_jpeg)) < 0LL) {
        fclose(fhd_jpeg);
        fclose(pfhd_pack), remove(cp_pf_path);
        PRINT_ERROR_AND_ABORT("读取JPEG文件大小失败");
    }
    if (buf_frw = malloc(sizeof(buf_t) + L_BUF_SIZE)) {
        buf_frw->size = L_BUF_SIZE;
    } else {
        fclose(pfhd_pack), remove(cp_pf_path);
        PRINT_ERROR_AND_ABORT("为文件读写缓冲区分配内存失败");
    }
    jpeg_netsize = realsizeofj(fhd_jpeg, fake_jpeg_size, &buf_frw);
    rewind(fhd_jpeg);
    if (jpeg_netsize == JPEG_INVALID) {
        printf(PACK_WARN "无效的JPEG图像：%s\n", jpeg_path);
        exit(EXIT_CODE_FAILURE);
    } else if (jpeg_netsize == JPEG_ERRORS) {
        PRINT_ERROR_AND_ABORT("验证JPEG图像过程中发生错误");
    }
    if (fake_jpeg_size > U_BUF_SIZE) {
        if (!copier_sub2pack(fhd_jpeg, pfhd_pack, &buf_frw)) {
            fclose(fhd_jpeg);
            fclose(pfhd_pack), remove(cp_pf_path);
            PRINT_ERROR_AND_ABORT("复制JPEG文件失败");
        }
    } else {
        if (fake_jpeg_size > buf_frw->size) {
            if (!expand_buf(&buf_frw, fake_jpeg_size)) {
                fclose(fhd_jpeg);
                fclose(pfhd_pack), remove(cp_pf_path);
                PRINT_ERROR_AND_ABORT("为文件读写缓冲区扩充内存失败");
            }
        }
        if (fread(buf_frw->fdata, fake_jpeg_size, 1, fhd_jpeg) != 1) {
            fclose(fhd_jpeg);
            fclose(pfhd_pack), remove(cp_pf_path);
            PRINT_ERROR_AND_ABORT("读取JPEG文件失败");
        }
        if (fwrite(buf_frw->fdata, fake_jpeg_size, 1, pfhd_pack) != 1) {
            fclose(fhd_jpeg);
            fclose(pfhd_pack), remove(cp_pf_path);
            PRINT_ERROR_AND_ABORT("复制JPEG至主文件失败");
        }
    }
    fclose(fhd_jpeg);
    if (fseek_fpack(pfhd_pack, jpeg_netsize, SEEK_SET)) {
        PRINT_ERROR_AND_ABORT("移动伪JPEG文件指针失败");
    }
    if (fwrite(&df_head, sizeof(head_t), 1, pfhd_pack) != 1) {
        fclose(fhd_jpeg);
        fclose(pfhd_pack), remove(cp_pf_path);
        PRINT_ERROR_AND_ABORT("写入PACK文件头信息失败");
    }
    // 最后一次写入后主文件的文件指针已移至末尾不需再移
    if (fpack = malloc(sizeof(fpack_t))) {
        fpack->head = df_head;
        fpack->start = jpeg_netsize;
        fpack->subs = NULL;
        fpack->subn = 0LL;
        fpack->fpath = cp_pf_path;
        fpack->pfhd = pfhd_pack;
        if (buf_frw)
            free(buf_frw);
        return fpack;
    } else {
        fclose(pfhd_pack), remove(cp_pf_path);
        PRINT_ERROR_AND_ABORT("为主文件信息结构体分配内存失败");
    }
}

// 打开已存在的伪装的JPEG文件
fpack_t *fpack_fakej_open(const char *fake_jpeg_path) {
    fpack_t *fpack;            // PACK文件信息结构体
    head_t tmp_head;           // 临时PACK文件头
    info_t *bom_pfile;         // 子文件信息表
    FILE *pfhd_pack;           // 主文件二进制流
    char buf_path[PATH_MSIZE]; // 绝对路径及父目录缓冲
    char *cp_pf_path;          // 拷贝路径用于结构体
    int64_t subn = 0LL;        // 子文件信息表容量
    buf_t *buf_frw;            // 文件读写缓冲区
    int64_t fake_jpeg_size;    // 伪JPEG文件的总大小
    int64_t jpeg_netsize;      // 伪JPEG文件净大小
    if (path_abspath(buf_path, PATH_MSIZE, fake_jpeg_path)) {
        printf(PACK_ERROR "无法获取文件绝对路径：%s\n", fake_jpeg_path);
        exit(EXIT_CODE_FAILURE);
    }
    printf(PACK_INFO "打开主文件：%s\n", buf_path);
    cp_pf_path = malloc(strlen(buf_path) + 1ULL);
    if (!cp_pf_path) {
        PRINT_ERROR_AND_ABORT("为伪图文件文件名分配内存失败");
    }
    strcpy(cp_pf_path, buf_path);
    if (!path_isfile(cp_pf_path)) {
        if (path_last_state()) {
            printf(PACK_ERROR "获取主文件路径属性失败\n");
            exit(EXIT_CODE_FAILURE);
        } else {
            printf(PACK_ERROR "此路径不是一个文件路径\n");
            exit(EXIT_CODE_FAILURE);
        }
    }
    if (buf_frw = malloc(sizeof(buf_t) + L_BUF_SIZE)) {
        buf_frw->size = L_BUF_SIZE;
    } else {
        PRINT_ERROR_AND_ABORT("为文件读写缓冲区分配内存失败");
    }
    if (!(pfhd_pack = fopen_fpack(cp_pf_path, "r+b"))) {
        printf(PACK_ERROR "主文件打开失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (fseek_fpack(pfhd_pack, 0LL, SEEK_END)) {
        PRINT_ERROR_AND_ABORT("无法移动文件指针至末尾");
    }
    if ((fake_jpeg_size = ftell_fpack(pfhd_pack)) < 0) {
        PRINT_ERROR_AND_ABORT("获取伪图文件大小失败");
    }
    jpeg_netsize = realsizeofj(pfhd_pack, fake_jpeg_size, &buf_frw);
    if (jpeg_netsize == JPEG_INVALID) {
        printf(PACK_WARN "无效的伪JPEG文件\n");
        exit(EXIT_CODE_FAILURE);
    } else if (jpeg_netsize == JPEG_ERRORS) {
        printf(PACK_ERROR "查找伪JPEG文件结束点时出错\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (fseek_fpack(pfhd_pack, jpeg_netsize, SEEK_SET)) {
        printf(PACK_ERROR "移动文件指针失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (fread(&tmp_head, sizeof(tmp_head), 1, pfhd_pack) != 1) {
        PRINT_ERROR_AND_ABORT("读取主文件头失败");
    }
    if (memcmp(df_head.id, tmp_head.id, sizeof(df_head.id))) {
        printf(PACK_ERROR "此伪JPEG文件不包含PACK文件\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (tmp_head.count > 0)
        subn = tmp_head.count;
    else
        subn = 1LL;
    bom_pfile = malloc((size_t)subn * sizeof(info_t));
    if (!bom_pfile) {
        PRINT_ERROR_AND_ABORT("为子文件信息表分配内存失败");
    }
    for (int64_t i = 0; i < tmp_head.count; ++i) {
        if ((bom_pfile[i].offset = ftell_fpack(pfhd_pack)) < 0LL) {
            PRINT_ERROR_AND_ABORT("获取当前字段偏移量失败");
        }
        if (fread(&bom_pfile[i].fsize, FSNL_S, 1, pfhd_pack) != 1) {
            PRINT_ERROR_AND_ABORT("读取子文件属性失败");
        }
        if (bom_pfile[i].fnlen > PATH_MSIZE) {
            PRINT_ERROR_AND_ABORT("读取到的子文件名长度异常");
        }
        if (fread(bom_pfile[i].fname, bom_pfile[i].fnlen, 1, pfhd_pack) != 1) {
            PRINT_ERROR_AND_ABORT("从主文件读取子文件名失败");
        }
#ifdef _WIN32
        str_u2a(bom_pfile[i].fname, PM, bom_pfile[i].fname);
#endif // _WIN32
       // 遇到目录(大小是-1)或文件大小为0时没有数据块不需要移动文件指针
        if (bom_pfile[i].fsize <= 0)
            continue;
        if (fseek_fpack(pfhd_pack, bom_pfile[i].fsize, SEEK_CUR)) {
            PRINT_ERROR_AND_ABORT("移动文件指针至下一个位置失败");
        }
    }
    // 默认将文件指针置于末尾
    fseek_fpack(pfhd_pack, 0, SEEK_END);
    if (fpack = malloc(sizeof(fpack_t))) {
        fpack->head = tmp_head;
        fpack->start = jpeg_netsize;
        fpack->subs = bom_pfile;
        fpack->subn = subn;
        fpack->fpath = cp_pf_path;
        fpack->pfhd = pfhd_pack;
        return fpack;
    } else {
        free(bom_pfile), free(cp_pf_path);
        PRINT_ERROR_AND_ABORT("为PACK文件信息结构体分配内存失败");
    }
}
