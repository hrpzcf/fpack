#include "fpack.h"

#include <stdlib.h>

// 扩充子文件信息表容量
static bool ExpandBOM(FPACK_T *pFilePack, size_t Capacity) {
    INFO_T *finfo_tmp;
    size_t exp_size;
    if (!pFilePack)
        return false;
    if (Capacity > INT64_MAX)
        return false;
    if (Capacity == 0ULL)
        Capacity = 1ULL;
    if (!pFilePack->sheet) {
        finfo_tmp = malloc(Capacity * sizeof(INFO_T));
        if (finfo_tmp) {
            pFilePack->cells = (int64_t)Capacity;
            pFilePack->sheet = finfo_tmp;
            return true;
        } else
            return false;
    }
    if (pFilePack->cells - pFilePack->head.count >= (int64_t)Capacity) {
        return true;
    }
    exp_size = (Capacity + pFilePack->head.count) * sizeof(INFO_T);
    finfo_tmp = realloc(pFilePack->sheet, exp_size);
    if (!finfo_tmp)
        return false;
    pFilePack->sheet = finfo_tmp;
    pFilePack->cells = (int64_t)Capacity + pFilePack->head.count;
    return true;
}

// 扩充文件读写缓冲区内存空间
static bool ExpandBUF(BUFFER_T **pp_buf, int64_t size) {
    BUFFER_T *buf_tmp;
    if (!pp_buf || !*pp_buf)
        return false;
    if ((*pp_buf)->size >= size)
        return true;
    buf_tmp = realloc(*pp_buf, sizeof(BUFFER_T) + size);
    if (!buf_tmp)
        return false;
    buf_tmp->size = size, *pp_buf = buf_tmp;
    return true;
}

// 大文件复制，从子文件流复制到主文件流
static bool SubCopyToMain(FILE *sub_stm, FILE *pk_stm, BUFFER_T **buf_frw) {
    size_t fr_size; // 每次fread的大小
    if (!ExpandBUF(buf_frw, U_BUF_SIZE))
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

// 大文件复制，从主文件流复制到提取子文件时新建的子文件流
static bool MainCopyToSub(FILE *pk_stm, int64_t offset, int64_t fsize, FILE *sub_stm, BUFFER_T **buf_frw) {
    if (!ExpandBUF(buf_frw, U_BUF_SIZE))
        return false;
    if (FilePackfseek(pk_stm, offset, SEEK_SET))
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
static int64_t RealSizeOfJPEG(FILE *p_jpeg, int64_t total, BUFFER_T **buf_8bit) {
    uint8_t *buf_u8;
    int64_t end = 0;
    if (total < 4)
        return JPEG_INVALID;
    if ((*buf_8bit)->size < total)
        if (!ExpandBUF(buf_8bit, total))
            return JPEG_ERROR;
    rewind(p_jpeg);
    if (fread((*buf_8bit)->fdata, 1, total, p_jpeg) != total) {
        return JPEG_ERROR;
    }
    buf_u8 = (uint8_t *)(*buf_8bit)->fdata;
    if (!(buf_u8[0] == JPEG_SIG && buf_u8[1] == JPEG_START))
        return JPEG_INVALID;
    for (end = 4; end <= total; ++end)
        if (buf_u8[end - 2] == JPEG_SIG && buf_u8[end - 1] == JPEG_END)
            return end;
    return JPEG_INVALID;
}

// 判断是否是伪装的 JPEG 文件
bool FilePackIsFakeJPEG(const char *fake_jpeg_path) {
    FILE *fhd_fake_jpeg;
    BUFFER_T *buf_frw;
    int64_t fake_jpeg_size, jpeg_netsize;
    HEAD_T head_tmp;
    static char buf_p[PATH_MSIZE];
    bool return_code = false;
    if (OsPathAbsolutePath(buf_p, PATH_MSIZE, fake_jpeg_path))
        return return_code;
    fhd_fake_jpeg = FilePackfopen(buf_p, "rb");
    if (!fhd_fake_jpeg)
        return return_code;
    if (FilePackfseek(fhd_fake_jpeg, 0LL, SEEK_END))
        return return_code;
    if ((fake_jpeg_size = FilePackftell(fhd_fake_jpeg)) < 0LL)
        return return_code;
    if (buf_frw = malloc(fake_jpeg_size + sizeof(HEAD_T) + sizeof(BUFFER_T))) {
        buf_frw->size = fake_jpeg_size + sizeof(HEAD_T);
    } else {
        return false;
    }
    jpeg_netsize = RealSizeOfJPEG(fhd_fake_jpeg, fake_jpeg_size, &buf_frw);
    if (jpeg_netsize == JPEG_INVALID || jpeg_netsize == JPEG_ERROR)
        goto free_return;
    if (fake_jpeg_size - jpeg_netsize < sizeof(HEAD_T)) {
        goto free_return;
    }
    if (FilePackfseek(fhd_fake_jpeg, jpeg_netsize, SEEK_SET))
        goto free_return;
    if (fread(&head_tmp, sizeof(HEAD_T), 1, fhd_fake_jpeg) != 1)
        goto free_return;
    if (memcmp(DEFAULT_HEAD.id, head_tmp.id, sizeof(DEFAULT_HEAD.id)))
        goto free_return;
    return_code = true;
free_return:
    free(buf_frw);
    fclose(fhd_fake_jpeg);
    return return_code;
}

// 关闭FPACK_T对象
void FilePackClose(FPACK_T *fpack) {
    if (fpack) {
        if (fpack->path)
            free(fpack->path);
        if (fpack->sheet)
            free(fpack->sheet);
        if (fpack->handle)
            fclose(fpack->handle);
        free(fpack);
    }
}

// 创建新<.fp>文件
FPACK_T *FilePackMake(const char *fp_path, bool overwrite) {
    FILE *pfhd_pack;           // 二进制文件流指针
    FPACK_T *fpack;            // <.fp>文件信息结构体
    char buf_path[PATH_MSIZE]; // 绝对路径及父目录缓冲
    char *cp_fp_path;          // 拷贝路径用于结构体
    if (OsPathAbsolutePath(buf_path, PATH_MSIZE, fp_path)) {
        printf(MESSAGE_ERROR "无法获取主文件绝对路径：%s\n", fp_path);
        exit(EXIT_CODE_FAILURE);
    }
    printf(MESSAGE_INFO "创建主文件：%s\n", buf_path);
    cp_fp_path = malloc(strlen(buf_path) + 1ULL);
    if (!cp_fp_path) {
        PRINT_ERROR_AND_ABORT("为主文件路径数组分配内存失败");
    }
    strcpy(cp_fp_path, buf_path);
    if (OsPathExists(cp_fp_path)) {
        if (OsPathIsDirectory(cp_fp_path)) {
            printf(MESSAGE_ERROR "此位置已存在同名目录\n");
            exit(EXIT_CODE_FAILURE);
        } else if (OsPathLastState()) {
            printf(MESSAGE_ERROR "获取路径属性失败\n");
            exit(EXIT_CODE_FAILURE);
        } else if (!overwrite) {
            printf(MESSAGE_ERROR "已存在同名文件但未指定覆盖\n");
            exit(EXIT_CODE_FAILURE);
        }
    } else if (OsPathLastState()) {
        printf(MESSAGE_ERROR "无法检查此路径是否存在\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!OsPathDirName(buf_path, PATH_MSIZE, buf_path)) {
        printf(MESSAGE_ERROR "获取主文件的父目录路径失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!OsPathExists(buf_path)) {
        if (OsPathMakeDIR(buf_path)) {
            printf(MESSAGE_ERROR "为主文件创建目录失败\n");
            exit(EXIT_CODE_FAILURE);
        }
    } else if (!OsPathIsDirectory(buf_path)) {
        printf(MESSAGE_ERROR "父目录已被文件占用，无法创建主文件\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!(pfhd_pack = FilePackfopen(cp_fp_path, "wb"))) {
        printf(MESSAGE_ERROR "主文件创建失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (fwrite(&DEFAULT_HEAD, sizeof(DEFAULT_HEAD), 1, pfhd_pack) != 1) {
        fclose(pfhd_pack);
        remove(fp_path);
        PRINT_ERROR_AND_ABORT("写入<.fp>文件头失败");
    }
    // 此时文件指针已经在文件末尾
    if (fpack = malloc(sizeof(FPACK_T))) {
        fpack->head = DEFAULT_HEAD;
        fpack->start = 0LL;
        fpack->sheet = NULL;
        fpack->cells = 0LL;
        fpack->path = cp_fp_path;
        fpack->handle = pfhd_pack;
        return fpack;
    } else {
        fclose(pfhd_pack);
        remove(fp_path);
        PRINT_ERROR_AND_ABORT("为文件信息结构体分配内存失败");
    }
}

// 打开已存在的<.fp>文件
FPACK_T *FilePackOpen(const char *fp_path) {
    FPACK_T *fpack;            // <.fp>文件信息结构体
    HEAD_T PackHeadTemp;   // 临时<.fp>文件头
    INFO_T *SheetOfSubFiles;   // 子文件信息表
    FILE *pFileHandle;         // 主文件二进制流
    char buf_path[PATH_MSIZE]; // 绝对路径及父目录缓冲
    char *pPathCopied;         // 拷贝路径用于结构体
    int64_t CellsCount = 0LL;  // 子文件信息表容量
    if (OsPathAbsolutePath(buf_path, PATH_MSIZE, fp_path)) {
        printf(MESSAGE_ERROR "无法获取主文件绝对路径：%s\n", fp_path);
        exit(EXIT_CODE_FAILURE);
    }
    printf(MESSAGE_INFO "打开主文件：%s\n", buf_path);
    pPathCopied = malloc(strlen(buf_path) + 1ULL);
    if (!pPathCopied) {
        PRINT_ERROR_AND_ABORT("为主文件文件名分配内存失败");
    }
    strcpy(pPathCopied, buf_path);
    if (!OsPathExists(pPathCopied)) {
        printf(MESSAGE_ERROR "指定的文件路径不存在\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!OsPathIsFile(pPathCopied)) {
        if (OsPathLastState()) {
            printf(MESSAGE_ERROR "获取主文件路径属性失败\n");
            exit(EXIT_CODE_FAILURE);
        } else {
            printf(MESSAGE_ERROR "此路径不是一个文件路径\n");
            exit(EXIT_CODE_FAILURE);
        }
    }
    if (!(pFileHandle = FilePackfopen(pPathCopied, "r+b"))) {
        printf(MESSAGE_ERROR "主文件打开失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (fread(&PackHeadTemp, sizeof(PackHeadTemp), 1, pFileHandle) != 1) {
        PRINT_ERROR_AND_ABORT("读取主文件头失败");
    }
    if (memcmp(DEFAULT_HEAD.id, PackHeadTemp.id, sizeof(DEFAULT_HEAD.id))) {
        printf(MESSAGE_ERROR "此文件不是一个<.fp>文件\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (PackHeadTemp.count > 0)
        CellsCount = PackHeadTemp.count;
    else
        CellsCount = 1LL;
    SheetOfSubFiles = malloc((size_t)CellsCount * sizeof(INFO_T));
    if (!SheetOfSubFiles) {
        PRINT_ERROR_AND_ABORT("为子文件信息表分配内存失败");
    }
    if (FilePackfseek(pFileHandle, DATA_O, SEEK_SET)) {
        PRINT_ERROR_AND_ABORT("移动文件指针到数据块起始位置失败");
    }
    for (int64_t i = 0; i < PackHeadTemp.count; ++i) {
        if ((SheetOfSubFiles[i].offset = FilePackftell(pFileHandle)) < 0LL) {
            PRINT_ERROR_AND_ABORT("获取当前字段偏移量失败");
        }
        if (fread(&SheetOfSubFiles[i].fsize, FSNL_S, 1, pFileHandle) != 1) {
            PRINT_ERROR_AND_ABORT("读取子文件属性失败");
        }
        if (SheetOfSubFiles[i].fnlen > PATH_MSIZE) {
            PRINT_ERROR_AND_ABORT("读取到的子文件名长度异常");
        }
        if (fread(SheetOfSubFiles[i].fname, SheetOfSubFiles[i].fnlen, 1, pFileHandle) != 1) {
            PRINT_ERROR_AND_ABORT("从主文件读取子文件名失败");
        }
#ifdef _WIN32
        StringUTF8ToANSI(SheetOfSubFiles[i].fname, PM, SheetOfSubFiles[i].fname);
#endif // _WIN32
       // 遇到目录(大小是-1)或文件大小为0时没有数据块不需要移动文件指针
        if (SheetOfSubFiles[i].fsize <= 0)
            continue;
        if (FilePackfseek(pFileHandle, SheetOfSubFiles[i].fsize, SEEK_CUR)) {
            PRINT_ERROR_AND_ABORT("移动文件指针至下一个位置失败");
        }
    }
    FilePackfseek(pFileHandle, 0, SEEK_END); // 默认文件指针在末尾
    if (fpack = malloc(sizeof(FPACK_T))) {
        fpack->head = PackHeadTemp;
        fpack->start = 0LL;
        fpack->sheet = SheetOfSubFiles;
        fpack->cells = CellsCount;
        fpack->path = pPathCopied;
        fpack->handle = pFileHandle;
        return fpack;
    } else {
        free(SheetOfSubFiles), free(pPathCopied);
        PRINT_ERROR_AND_ABORT("为<.fp>文件信息结构体分配内存失败");
    }
}

// 将目标打包进已创建的空<.fp>文件
FPACK_T *FilePackPack(const char *topack, bool sd, FPACK_T *fpack, bool add) {
    // 如果packto是目录，则此变量用于存放其父目录
    char *parent_dir;
    // 存放绝对路径用于比较是否同一文件
    static char buf_absp1[PATH_MSIZE]; // 主文件
    static char buf_absp2[PATH_MSIZE]; // 子文件
#ifdef _WIN32
    static char buf_cased[PATH_MSIZE]; // WIN平台比较路径是否相同需要先转全小写
#endif
    // 收集路径的scanlist容器
    SCANNER_T *scanlist_paths;
    FILE *stream_subfile; // 打开子文件共用指针
    // 用于临时读写文件大小、文件名长度、文件名，也用于更新主文件结构体的子文件信息表
    INFO_T finfo;
    BUFFER_T *buf_frw; // 文件读写缓冲区
    if (!topack) {
        PRINT_ERROR_AND_ABORT("打包目标路径是空指针");
    } else if (!*topack) {
        PRINT_ERROR_AND_ABORT("打包目标路径是空字符串");
    }
    if (!OsPathExists(topack)) {
        printf(MESSAGE_ERROR "目标文件或目录不存在：%s\n", topack);
        exit(EXIT_CODE_FAILURE);
    }
    if (fpack->head.count > 0LL && !add) {
        printf(MESSAGE_WARN "主文件已打包%" I64_SPECIFIER "个文件，但此次未指定增量打包\n", fpack->head.count);
        exit(EXIT_CODE_FAILURE);
    }
    strcpy(buf_absp1, fpack->path);
    if (!OsPathNormpath(OsPathNormcase(buf_absp1), PATH_MSIZE)) {
        WHETHER_CLOSE_REMOVE(fpack);
        PRINT_ERROR_AND_ABORT("获取路径标准形式失败");
    }
    // 初始缓冲区大小L_BUF_SIZE字节
    if (buf_frw = malloc(sizeof(BUFFER_T) + L_BUF_SIZE)) {
        buf_frw->size = L_BUF_SIZE;
    } else {
        WHETHER_CLOSE_REMOVE(fpack);
        PRINT_ERROR_AND_ABORT("为文件读写缓冲区分配初始内存失败");
    }
    if (OsPathIsFile(topack)) {
        printf(MESSAGE_INFO "写入：%s\n", topack);
        if (OsPathAbsolutePath(buf_absp2, PATH_MSIZE, topack)) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("获取子文件绝对路径失败");
        }
#ifndef _WIN32
        if (!strcmp(buf_absp1, buf_absp2))
#else
        strcpy(buf_cased, buf_absp2);
        if (!strcmp(buf_absp1, OsPathNormcase(buf_cased)))
#endif // _WIN32
        {
            WHETHER_CLOSE_REMOVE(fpack);
            printf(MESSAGE_ERROR "退出：此文件是主文件\n");
            exit(EXIT_CODE_SUCCESS);
        }
        if (!OsPathBaseName(finfo.fname, PATH_MSIZE, buf_absp2)) {
            WHETHER_CLOSE_REMOVE(fpack);
            printf(MESSAGE_ERROR "获取子文件名失败：%s\n", buf_absp2);
            exit(EXIT_CODE_FAILURE);
        }
#ifdef _WIN32
        // WIN平台需要把文件名字符转为UTF8编码的字符串
        StringANSIToUTF8(finfo.fname, PATH_MSIZE, finfo.fname);
#endif // _WIN32
        finfo.fnlen = (int16_t)(strlen(finfo.fname) + 1);
        if (!(stream_subfile = FilePackfopen(topack, "rb"))) {
            WHETHER_CLOSE_REMOVE(fpack);
            printf(MESSAGE_ERROR "打开子文件失败：%s\n", topack);
            exit(EXIT_CODE_FAILURE);
        }
        FilePackfseek(stream_subfile, 0, SEEK_END);
        finfo.fsize = FilePackftell(stream_subfile);
        rewind(stream_subfile); // 子文件读取大小后文件指针移回开头备用
        // 无需将主文件指针移至末尾，因为pack_open或pack_make函数已将其移至末尾
        if ((finfo.offset = FilePackftell(fpack->handle)) < 0LL) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("获取当前字段偏移量失败");
        }
        if (fwrite(&finfo.fsize, FSNL_S + finfo.fnlen, 1, fpack->handle) != 1) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("写入子文件属性失败");
        }
        if (finfo.fsize > U_BUF_SIZE) {
            if (!SubCopyToMain(stream_subfile, fpack->handle, &buf_frw)) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("将子文件写入主文件失败\n");
            }
        } else if (finfo.fsize > 0) {
            if (finfo.fsize > buf_frw->size && !ExpandBUF(&buf_frw, finfo.fsize)) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("扩充文件读写缓冲区空间失败");
            }
            if (fread(buf_frw->fdata, finfo.fsize, 1, stream_subfile) != 1) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("读取子文件失败");
            }
            if (fwrite(buf_frw, finfo.fsize, 1, fpack->handle) != 1) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("写入子文件失败");
            }
        }
        fclose(stream_subfile);
        FilePackfseek(fpack->handle, fpack->start + FCNT_O, SEEK_SET);
        if (fwrite(&fpack->head.count, sizeof(int64_t), 1, fpack->handle) != 1) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("更新子文件数量失败");
        }
        FilePackfseek(fpack->handle, 0LL, SEEK_END);
        if (fpack->cells <= fpack->head.count) {
            if (!ExpandBOM(fpack, 1ULL)) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("扩充子文件信息表容量失败");
            }
        }
        fpack->sheet[fpack->head.count++] = finfo;
    } else if (OsPathIsDirectory(topack)) {
        if (OsPathAbsolutePath(buf_absp2, PATH_MSIZE, topack)) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("获取子文件目录绝对路径失败");
        }
        if (parent_dir = malloc(PATH_MSIZE)) {
            if (!OsPathDirName(parent_dir, PATH_MSIZE, OsPathNormpath(buf_absp2, PATH_MSIZE))) {
                WHETHER_CLOSE_REMOVE(fpack);
                PRINT_ERROR_AND_ABORT("获取子文件目录的父目录失败");
            }
        } else {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("为子文件父目录缓冲区分配内存失败");
        }
        printf(MESSAGE_INFO "扫描目录...\n");
        if (!(scanlist_paths = OsPathMakeScanner(0))) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("创建路径扫描器失败");
        }
        if (OsPathScanPath(buf_absp2, OSPATH_BOTH, sd, &scanlist_paths)) {
            WHETHER_CLOSE_REMOVE(fpack);
            printf(MESSAGE_ERROR "扫描目录失败：%s\n", buf_absp2);
            exit(EXIT_CODE_FAILURE);
        }
        if (!ExpandBOM(fpack, scanlist_paths->count)) {
            WHETHER_CLOSE_REMOVE(fpack);
            PRINT_ERROR_AND_ABORT("扩充子文件信息表容量失败");
        }
        for (size_t i = 0; i < scanlist_paths->count; ++i) {
            printf(MESSAGE_INFO "写入：%s\n", scanlist_paths->paths[i]);
            if (OsPathIsDirectory(scanlist_paths->paths[i])) {
                finfo.fsize = DIR_SIZE; // 目录大小定义为DIR_SIZE
                if (OsPathRelativePath(finfo.fname, PATH_MSIZE, scanlist_paths->paths[i], parent_dir)) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    printf(MESSAGE_WARN "跳过：获取子目录相对路径失败");
                    continue;
                }
#ifdef _WIN32
                // WIN平台要把字符串转为UTF8编码写入文件
                StringANSIToUTF8(finfo.fname, PATH_MSIZE, finfo.fname);
#endif // _WIN32
                finfo.fnlen = (int16_t)(strlen(finfo.fname) + 1);
                if ((finfo.offset = FilePackftell(fpack->handle)) < 0LL) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    printf(MESSAGE_WARN "跳过：获取当前字段偏移量失败\n");
                    continue;
                }
                // 按fsize、fnlen类型长度及fnlen值将finfo_tmp的一部分写入主文件
                if (fwrite(&finfo.fsize, FSNL_S + finfo.fnlen, 1, fpack->handle) != 1) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    } else {
                        FilePackfseek(fpack->handle, finfo.offset, SEEK_SET);
                    }
                    printf(MESSAGE_WARN "跳过：写入子文件属性失败\n");
                    continue;
                }
            } else {
#ifndef _WIN32
                if (!strcmp(buf_absp1, scanlist_paths->paths[i]))
#else
                strcpy(buf_cased, scanlist_paths->paths[i]);
                if (!strcmp(buf_absp1, OsPathNormcase(buf_cased)))
#endif // _WIN32
                {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    printf(MESSAGE_WARN "跳过：此文件是主文件\n");
                    continue;
                }
                if (OsPathRelativePath(finfo.fname, PATH_MSIZE, scanlist_paths->paths[i], parent_dir)) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    printf(MESSAGE_WARN "跳过：获取子文件相对路径失败\n");
                    continue;
                }
                if (!(stream_subfile = FilePackfopen(scanlist_paths->paths[i], "rb"))) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    printf(MESSAGE_WARN "跳过：子文件打开失败\n");
                    continue;
                }
                FilePackfseek(stream_subfile, 0, SEEK_END);
                finfo.fsize = (int64_t)FilePackftell(stream_subfile);
                // 子文件读取大小后指针移回开头备用
                rewind(stream_subfile);
#ifdef _WIN32 // WIN平台需要将文件名编码转为UTF8保存
                StringANSIToUTF8(finfo.fname, PATH_MSIZE, finfo.fname);
#endif // _WIN32
                finfo.fnlen = (int16_t)(strlen(finfo.fname) + 1);
                if ((finfo.offset = FilePackftell(fpack->handle)) < 0LL) {
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    }
                    fclose(stream_subfile);
                    printf(MESSAGE_WARN "跳过：获取主文件指针位置失败\n");
                    continue;
                }
                if (fwrite(&finfo.fsize, FSNL_S + finfo.fnlen, 1, fpack->handle) != 1) {
                    printf(MESSAGE_WARN "跳过：写入子文件属性失败\n");
                    if (i >= scanlist_paths->count - 1) {
                        WHETHER_CLOSE_REMOVE(fpack);
                    } else {
                        FilePackfseek(fpack->handle, finfo.offset, SEEK_SET);
                    }
                    fclose(stream_subfile);
                    continue;
                }
                if (finfo.fsize > U_BUF_SIZE) {
                    if (!SubCopyToMain(stream_subfile, fpack->handle, &buf_frw)) {
                        if (i >= scanlist_paths->count - 1) {
                            WHETHER_CLOSE_REMOVE(fpack);
                        } else {
                            FilePackfseek(fpack->handle, finfo.offset, SEEK_SET);
                        }
                        fclose(stream_subfile);
                        printf(MESSAGE_WARN "跳过：将子文件写入主文件失败\n");
                        continue;
                    }
                } else if (finfo.fsize > 0) { // 大小等于0的文件无需读写
                    if (finfo.fsize > buf_frw->size && !ExpandBUF(&buf_frw, finfo.fsize)) {
                        WHETHER_CLOSE_REMOVE(fpack);
                        PRINT_ERROR_AND_ABORT("扩充文件读写缓冲区空间失败");
                    }
                    if (fread(buf_frw->fdata, finfo.fsize, 1, stream_subfile) != 1) {
                        if (i >= scanlist_paths->count - 1) {
                            WHETHER_CLOSE_REMOVE(fpack);
                        }
                        fclose(stream_subfile);
                        printf(MESSAGE_WARN "跳过：读取子文件失败\n");
                        continue;
                    }
                    if (fwrite(buf_frw->fdata, finfo.fsize, 1, fpack->handle) != 1) {
                        if (i >= scanlist_paths->count - 1) {
                            WHETHER_CLOSE_REMOVE(fpack);
                        } else {
                            FilePackfseek(fpack->handle, finfo.offset, SEEK_SET);
                        }
                        fclose(stream_subfile);
                        printf(MESSAGE_WARN "跳过：将子文件写入主文件失败\n");
                        continue;
                    }
                }
                fclose(stream_subfile);
            }
            fpack->sheet[fpack->head.count++] = finfo;
        }
        OsPathDeleteScanner(scanlist_paths);
    } else {
        WHETHER_CLOSE_REMOVE(fpack);
        printf(MESSAGE_ERROR "路径不是文件也不是目录：%s\n", topack);
        exit(EXIT_CODE_FAILURE);
    }
    FilePackfseek(fpack->handle, fpack->start + FCNT_O, SEEK_SET);
    if (fwrite(&fpack->head.count, sizeof(int64_t), 1, fpack->handle) != 1) {
        WHETHER_CLOSE_REMOVE(fpack);
        PRINT_ERROR_AND_ABORT("更新主文件中的子文件数量字段失败");
    }
    if (buf_frw)
        free(buf_frw);
    return fpack;
}

// 打印<.fp>文件中的文件列表即其他信息
FPACK_T *FilePackInfo(const char *fp_path) {
    int a, b, c;           // 已打印的(大小、类型、路径)累计字符数
    size_t tmp_strlen;     // 每个fname长度的临时变量
    size_t max_strlen = 0; // 长度最大的fname的值
    int16_t *s;            // 指向head中的sp，写完显得太长
    int64_t index;         // 遍历主文件中文件总数head.count
    FPACK_T *fpack;        // 主文件信息结构体
    char eqs1[EQS_MAX];    // 打印的子文件列表分隔符共用缓冲区
    char *eqs2, *eqs3;     // 用于将上面缓冲区分离为三个字符串
    if (FilePackIsFakeJPEG(fp_path))
        fpack = FilePackOpenFakeJPEG(fp_path);
    else
        fpack = FilePackOpen(fp_path);
    s = fpack->head.sp;
    for (index = 0; index < fpack->head.count; ++index) {
        tmp_strlen = strlen(fpack->sheet[index].fname);
        if (max_strlen < tmp_strlen)
            max_strlen = tmp_strlen;
    }
#ifdef _MSC_VER
    // 打开MSVC编译器printf的%n占位符支持
    _set_printf_count_output(1);
#endif // _MSC_VER
    printf("\n格式版本：");
    printf("%hd.%hd.%hd.%hd\t", s[0], s[1], s[2], s[3]);
    printf("包含条目总数：");
    printf("%" I64_SPECIFIER "\n\n", fpack->head.count);
    printf("%19s%n\t%4s%n\t%s%n\n", "大小", &a, "类型", &b, "路径和文件名", &c);
    if (max_strlen < (size_t)c - b - 1)
        max_strlen = (size_t)c - b - 1;
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
        printf("%19" I64_SPECIFIER "\t%s\t%s\n", fpack->sheet[index].fsize, fpack->sheet[index].fsize < 0 ? "目录" : "文件", fpack->sheet[index].fname);
    }
    printf("\n格式版本：");
    printf("%hd.%hd.%hd.%hd\t", s[0], s[1], s[2], s[3]);
    printf("包含条目总数：");
    printf("%" I64_SPECIFIER "\n\n", fpack->head.count);
    return fpack;
}

// 从<.fp>文件中提取子文件
FPACK_T *FilePackExtract(const char *name, const char *save_path, int overwrite, FPACK_T *fpack) {
    int64_t index;  // 循环遍历子文件时的下标
    int64_t offset; // 子文件字段在主文件中的偏移
#ifdef _WIN32
    static char buf_cased1[PATH_MSIZE];
    static char buf_cased2[PATH_MSIZE];
#endif
    static char buf_sub_path[PATH_MSIZE];
    static char buf_sub_pard[PATH_MSIZE];
    BUFFER_T *buf_frw;  // 从主文件提取到子文件时的文件读写缓冲区
    FILE *fhnd_subfile; // 创建子文件时每个子文件的二进制文件流句柄
    if (buf_frw = malloc(sizeof(BUFFER_T) + L_BUF_SIZE)) {
        buf_frw->size = L_BUF_SIZE;
    } else {
        PRINT_ERROR_AND_ABORT("为文件读写缓冲区分配内存失败");
    }
    if (!save_path || !*save_path)
        save_path = PATH_CDIRS;
    else {
        if (!OsPathExists(save_path)) {
            if (OsPathLastState()) {
                fprintf(stderr, MESSAGE_ERROR "获取路径属性失败：%s\n", save_path);
                exit(EXIT_CODE_FAILURE);
            }
            if (OsPathMakeDIR(save_path)) {
                fprintf(stderr, MESSAGE_ERROR "创建目录失败：%s\n", save_path);
                exit(EXIT_CODE_FAILURE);
            }
        } else if (!OsPathIsDirectory(save_path)) {
            fprintf(stderr, MESSAGE_ERROR "保存目录已被文件名占用：%s\n", save_path);
            exit(EXIT_CODE_FAILURE);
        }
    }
#ifdef _WIN32
    if (name) {
        strcpy(buf_cased1, name);
        OsPathNormcase(buf_cased1);
    }
#endif
    for (index = 0; index < fpack->head.count; ++index) {
        if (name) {
#ifdef _WIN32
            strcpy(buf_cased2, fpack->sheet[index].fname);
            if (strcmp(buf_cased1, OsPathNormcase(buf_cased2)))
                continue;
#else
            if (strcmp(name, fpack->sheet[index].fname))
                continue;
#endif
        }
        printf(MESSAGE_INFO "提取：%s\n", fpack->sheet[index].fname);
        if (OsPathJoinPath(buf_sub_path, PATH_MSIZE, 2, save_path, fpack->sheet[index].fname)) {
            printf(MESSAGE_WARN "跳过：拼接子文件完整路径失败\n");
            continue;
        }
        if (fpack->sheet[index].fsize < 0) {
            if (OsPathExists(buf_sub_path)) {
                if (OsPathIsDirectory(fpack->sheet[index].fname))
                    continue;
                printf(MESSAGE_WARN "跳过：目录名称已被文件占用s\n");
                continue;
            }
            if (OsPathMakeDIR(buf_sub_path)) {
                printf(MESSAGE_WARN "跳过：无法在此位置创建目录\n");
                continue;
            }
        } else {
            if (OsPathExists(buf_sub_path)) {
                if (OsPathIsDirectory(buf_sub_path)) {
                    printf(MESSAGE_WARN "跳过：文件路径已被目录占用：%s\n", buf_sub_path);
                    continue;
                }
                if (!overwrite) {
                    printf(MESSAGE_WARN "跳过：文件已存在且未指定覆盖：%s\n", buf_sub_path);
                    continue;
                }
            }
            if (!OsPathDirName(buf_sub_pard, PATH_MSIZE, buf_sub_path)) {
                printf(MESSAGE_WARN "跳过：获取父级路径失败\n");
                continue;
            }
            if (OsPathExists(buf_sub_pard)) {
                if (OsPathIsFile(buf_sub_pard)) {
                    printf(MESSAGE_WARN "跳过：目录路径已被文件占用：%s\n", buf_sub_pard);
                    continue;
                }
            } else {
                if (OsPathMakeDIR(buf_sub_pard)) {
                    printf(MESSAGE_WARN "跳过：目录创建失败：%s\n", buf_sub_pard);
                }
            }
            if (!(fhnd_subfile = FilePackfopen(buf_sub_path, "wb"))) {
                printf(MESSAGE_WARN "跳过：子文件创建失败：%s\n", buf_sub_path);
                continue;
            }
            offset = fpack->sheet[index].offset + FSNL_S + fpack->sheet[index].fnlen;
            if (fpack->sheet[index].fsize > U_BUF_SIZE) {
                if (!MainCopyToSub(fpack->handle, offset, fpack->sheet[index].fsize, fhnd_subfile, &buf_frw)) {
                    printf(MESSAGE_WARN "跳过：写入子文件数据失败：%s\n", buf_sub_path);
                    continue;
                }
            } else if (fpack->sheet[index].fsize > 0) {
                if (buf_frw->size < fpack->sheet[index].fsize) {
                    if (!ExpandBUF(&buf_frw, fpack->sheet[index].fsize)) {
                        PRINT_ERROR_AND_ABORT("扩充文件读写缓冲区失败");
                    }
                }
                if (FilePackfseek(fpack->handle, offset, SEEK_SET)) {
                    printf(MESSAGE_WARN "跳过：移动主文件指针失败\n");
                    continue;
                }
                if (fread(buf_frw->fdata, fpack->sheet[index].fsize, 1, fpack->handle) != 1) {
                    printf(MESSAGE_WARN "跳过：读取子文件数据失败");
                    continue;
                }
                if (fwrite(buf_frw->fdata, fpack->sheet[index].fsize, 1, fhnd_subfile) != 1) {
                    printf(MESSAGE_WARN "跳过：写入子文件数据失败");
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
FPACK_T *FilePackMakeFakeJPEG(const char *fp_path, const char *jpeg_path, bool overwrite) {
    FILE *pfhd_pack;           // 主文件二进制文件流
    FILE *fhd_jpeg;            // JPEG文件二进制文件流
    FPACK_T *fpack;            // 主文件信息结构体
    int64_t jpeg_netsize;      // JPEG文件净大小
    int64_t fake_jpeg_size;    // JPEG文件的总大小
    BUFFER_T *buf_frw;         // 文件读写缓冲区
    char *cp_fp_path;          // 复制的文件路径
    char buf_path[PATH_MSIZE]; // 绝对路径及父目录缓冲
    if (OsPathAbsolutePath(buf_path, PATH_MSIZE, fp_path)) {
        printf(MESSAGE_ERROR "无法获取主文件绝对路径：%s\n", fp_path);
        exit(EXIT_CODE_FAILURE);
    }
    printf(MESSAGE_INFO "创建主文件：%s\n", buf_path);
    cp_fp_path = malloc(strlen(buf_path) + 1ULL);
    if (!cp_fp_path) {
        PRINT_ERROR_AND_ABORT("为主文件路径数组分配内存失败");
    }
    strcpy(cp_fp_path, buf_path);
    if (OsPathExists(cp_fp_path)) {
        if (OsPathIsDirectory(cp_fp_path)) {
            printf(MESSAGE_ERROR "此位置已存在同名目录\n");
            exit(EXIT_CODE_FAILURE);
        } else if (OsPathLastState()) {
            printf(MESSAGE_ERROR "获取路径属性失败\n");
            exit(EXIT_CODE_FAILURE);
        } else if (!overwrite) {
            printf(MESSAGE_ERROR "已存在同名文件但未指定覆盖\n");
            exit(EXIT_CODE_FAILURE);
        }
    } else if (OsPathLastState()) {
        printf(MESSAGE_ERROR "无法检查此路径是否存在\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!OsPathDirName(buf_path, PATH_MSIZE, buf_path)) {
        printf(MESSAGE_ERROR "获取主文件的父目录路径失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (!OsPathExists(buf_path)) {
        if (OsPathMakeDIR(buf_path)) {
            printf(MESSAGE_ERROR "为主文件创建目录失败\n");
            exit(EXIT_CODE_FAILURE);
        }
    } else if (!OsPathIsDirectory(buf_path)) {
        printf(MESSAGE_ERROR "父目录名已被文件名占用：%s\n", buf_path);
        exit(EXIT_CODE_FAILURE);
    }
    if (!OsPathIsFile(jpeg_path)) {
        printf(MESSAGE_ERROR "指定的图像路径不是一个文件或不存在：%s\n", jpeg_path);
        exit(EXIT_CODE_FAILURE);
    }
    if (!(pfhd_pack = FilePackfopen(cp_fp_path, "wb"))) {
        PRINT_ERROR_AND_ABORT("主文件创建失败");
    }
    if (!(fhd_jpeg = FilePackfopen(jpeg_path, "rb"))) {
        fclose(pfhd_pack), remove(cp_fp_path);
        PRINT_ERROR_AND_ABORT("打开JPEG图像文件失败");
    }
    if (FilePackfseek(fhd_jpeg, 0LL, SEEK_END)) {
        fclose(fhd_jpeg);
        fclose(pfhd_pack), remove(cp_fp_path);
        PRINT_ERROR_AND_ABORT("移动JPEG文件指针失败");
    }
    if ((fake_jpeg_size = FilePackftell(fhd_jpeg)) < 0LL) {
        fclose(fhd_jpeg);
        fclose(pfhd_pack), remove(cp_fp_path);
        PRINT_ERROR_AND_ABORT("读取JPEG文件大小失败");
    }
    if (buf_frw = malloc(sizeof(BUFFER_T) + L_BUF_SIZE)) {
        buf_frw->size = L_BUF_SIZE;
    } else {
        fclose(pfhd_pack), remove(cp_fp_path);
        PRINT_ERROR_AND_ABORT("为文件读写缓冲区分配内存失败");
    }
    jpeg_netsize = RealSizeOfJPEG(fhd_jpeg, fake_jpeg_size, &buf_frw);
    rewind(fhd_jpeg);
    if (jpeg_netsize == JPEG_INVALID) {
        printf(MESSAGE_WARN "无效的JPEG图像：%s\n", jpeg_path);
        exit(EXIT_CODE_FAILURE);
    } else if (jpeg_netsize == JPEG_ERROR) {
        PRINT_ERROR_AND_ABORT("验证JPEG图像过程中发生错误");
    }
    if (fake_jpeg_size > U_BUF_SIZE) {
        if (!SubCopyToMain(fhd_jpeg, pfhd_pack, &buf_frw)) {
            fclose(fhd_jpeg);
            fclose(pfhd_pack), remove(cp_fp_path);
            PRINT_ERROR_AND_ABORT("复制JPEG文件失败");
        }
    } else {
        if (fake_jpeg_size > buf_frw->size) {
            if (!ExpandBUF(&buf_frw, fake_jpeg_size)) {
                fclose(fhd_jpeg);
                fclose(pfhd_pack), remove(cp_fp_path);
                PRINT_ERROR_AND_ABORT("为文件读写缓冲区扩充内存失败");
            }
        }
        if (fread(buf_frw->fdata, fake_jpeg_size, 1, fhd_jpeg) != 1) {
            fclose(fhd_jpeg);
            fclose(pfhd_pack), remove(cp_fp_path);
            PRINT_ERROR_AND_ABORT("读取JPEG文件失败");
        }
        if (fwrite(buf_frw->fdata, fake_jpeg_size, 1, pfhd_pack) != 1) {
            fclose(fhd_jpeg);
            fclose(pfhd_pack), remove(cp_fp_path);
            PRINT_ERROR_AND_ABORT("复制JPEG至主文件失败");
        }
    }
    fclose(fhd_jpeg);
    if (FilePackfseek(pfhd_pack, jpeg_netsize, SEEK_SET)) {
        PRINT_ERROR_AND_ABORT("移动伪JPEG文件指针失败");
    }
    if (fwrite(&DEFAULT_HEAD, sizeof(HEAD_T), 1, pfhd_pack) != 1) {
        fclose(fhd_jpeg);
        fclose(pfhd_pack), remove(cp_fp_path);
        PRINT_ERROR_AND_ABORT("写入<.fp>文件头信息失败");
    }
    // 最后一次写入后主文件的文件指针已移至末尾不需再移
    if (fpack = malloc(sizeof(FPACK_T))) {
        fpack->head = DEFAULT_HEAD;
        fpack->start = jpeg_netsize;
        fpack->sheet = NULL;
        fpack->cells = 0LL;
        fpack->path = cp_fp_path;
        fpack->handle = pfhd_pack;
        if (buf_frw)
            free(buf_frw);
        return fpack;
    } else {
        fclose(pfhd_pack), remove(cp_fp_path);
        PRINT_ERROR_AND_ABORT("为主文件信息结构体分配内存失败");
    }
}

// 打开已存在的伪装的JPEG文件
FPACK_T *FilePackOpenFakeJPEG(const char *fake_jpeg_path) {
    FPACK_T *fpack;            // <.fp>文件信息结构体
    HEAD_T tmp_head;           // 临时<.fp>文件头
    INFO_T *bom_pfile;         // 子文件信息表
    FILE *pfhd_pack;           // 主文件二进制流
    char buf_path[PATH_MSIZE]; // 绝对路径及父目录缓冲
    char *cp_fp_path;          // 拷贝路径用于结构体
    int64_t cells = 0LL;       // 子文件信息表容量
    BUFFER_T *buf_frw;         // 文件读写缓冲区
    int64_t fake_jpeg_size;    // 伪JPEG文件的总大小
    int64_t jpeg_netsize;      // 伪JPEG文件净大小
    if (OsPathAbsolutePath(buf_path, PATH_MSIZE, fake_jpeg_path)) {
        printf(MESSAGE_ERROR "无法获取文件绝对路径：%s\n", fake_jpeg_path);
        exit(EXIT_CODE_FAILURE);
    }
    printf(MESSAGE_INFO "打开主文件：%s\n", buf_path);
    cp_fp_path = malloc(strlen(buf_path) + 1ULL);
    if (!cp_fp_path) {
        PRINT_ERROR_AND_ABORT("为伪图文件文件名分配内存失败");
    }
    strcpy(cp_fp_path, buf_path);
    if (!OsPathIsFile(cp_fp_path)) {
        if (OsPathLastState()) {
            printf(MESSAGE_ERROR "获取主文件路径属性失败\n");
            exit(EXIT_CODE_FAILURE);
        } else {
            printf(MESSAGE_ERROR "此路径不是一个文件路径\n");
            exit(EXIT_CODE_FAILURE);
        }
    }
    if (buf_frw = malloc(sizeof(BUFFER_T) + L_BUF_SIZE)) {
        buf_frw->size = L_BUF_SIZE;
    } else {
        PRINT_ERROR_AND_ABORT("为文件读写缓冲区分配内存失败");
    }
    if (!(pfhd_pack = FilePackfopen(cp_fp_path, "r+b"))) {
        printf(MESSAGE_ERROR "主文件打开失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (FilePackfseek(pfhd_pack, 0LL, SEEK_END)) {
        PRINT_ERROR_AND_ABORT("无法移动文件指针至末尾");
    }
    if ((fake_jpeg_size = FilePackftell(pfhd_pack)) < 0) {
        PRINT_ERROR_AND_ABORT("获取伪图文件大小失败");
    }
    jpeg_netsize = RealSizeOfJPEG(pfhd_pack, fake_jpeg_size, &buf_frw);
    if (jpeg_netsize == JPEG_INVALID) {
        printf(MESSAGE_WARN "无效的伪JPEG文件\n");
        exit(EXIT_CODE_FAILURE);
    } else if (jpeg_netsize == JPEG_ERROR) {
        printf(MESSAGE_ERROR "查找伪JPEG文件结束点时出错\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (FilePackfseek(pfhd_pack, jpeg_netsize, SEEK_SET)) {
        printf(MESSAGE_ERROR "移动文件指针失败\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (fread(&tmp_head, sizeof(tmp_head), 1, pfhd_pack) != 1) {
        PRINT_ERROR_AND_ABORT("读取主文件头失败");
    }
    if (memcmp(DEFAULT_HEAD.id, tmp_head.id, sizeof(DEFAULT_HEAD.id))) {
        printf(MESSAGE_ERROR "此伪JPEG文件不包含<.fp>文件\n");
        exit(EXIT_CODE_FAILURE);
    }
    if (tmp_head.count > 0)
        cells = tmp_head.count;
    else
        cells = 1LL;
    bom_pfile = malloc((size_t)cells * sizeof(INFO_T));
    if (!bom_pfile) {
        PRINT_ERROR_AND_ABORT("为子文件信息表分配内存失败");
    }
    for (int64_t i = 0; i < tmp_head.count; ++i) {
        if ((bom_pfile[i].offset = FilePackftell(pfhd_pack)) < 0LL) {
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
        StringUTF8ToANSI(bom_pfile[i].fname, PM, bom_pfile[i].fname);
#endif // _WIN32
       // 遇到目录(大小是-1)或文件大小为0时没有数据块不需要移动文件指针
        if (bom_pfile[i].fsize <= 0)
            continue;
        if (FilePackfseek(pfhd_pack, bom_pfile[i].fsize, SEEK_CUR)) {
            PRINT_ERROR_AND_ABORT("移动文件指针至下一个位置失败");
        }
    }
    // 默认将文件指针置于末尾
    FilePackfseek(pfhd_pack, 0, SEEK_END);
    if (fpack = malloc(sizeof(FPACK_T))) {
        fpack->head = tmp_head;
        fpack->start = jpeg_netsize;
        fpack->sheet = bom_pfile;
        fpack->cells = cells;
        fpack->path = cp_fp_path;
        fpack->handle = pfhd_pack;
        return fpack;
    } else {
        free(bom_pfile), free(cp_fp_path);
        PRINT_ERROR_AND_ABORT("为<.fp>文件信息结构体分配内存失败");
    }
}
