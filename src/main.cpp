/*
* (C) Copyright 2010 Ramуn 'Reymon' Berrutti <ramonberrutti@hotmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
* See LICENSE.TXT file for more information.
*
*/
//#include <tchar.h>
#define RARDLL
#include "amxxapi.h"
#include "archive.h"
#include "archive_entry.h"
#include <stdio.h>  /* defines FILENAME_MAX */
#include <string>
#include <algorithm>
#include <rar.hpp>
#define THREAD_IMPLEMENTATION
#include "thread.h"
#ifdef WIN32
#include <direct.h>
#define GetCurrentDir _getcwd
#else
#include <unistd.h>
#define GetCurrentDir getcwd
#endif

struct extract_context
{
    std::string input_path;
    std::string output_path;
    int calle_id;
    int callback_id;
};
extern AMX_NATIVE_INFO Natives[];
void OnAmxxAttach()
{
    MF_AddNatives(Natives);
}
static int
copy_data(struct archive* ar, struct archive* aw)
{

    int r;
    const void* buff;
    size_t size;
    la_int64_t offset;

    for (;;) {
        r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF)
            return (ARCHIVE_OK);
        if (r < ARCHIVE_OK)
            return (r);
        r = archive_write_data_block(aw, buff, size, offset);
        if (r < ARCHIVE_OK) {
            fprintf(stderr, "%s\n", archive_error_string(aw));
            return (r);
        }
    }
}

int g_callback_id;
int g_calle_id;
int g_error;
bool g_bFinished = false;
void finish(int error, int callback_id, int calle_id)
{
    MF_ExecuteForward(callback_id, calle_id, error);
    MF_UnregisterSPForward(callback_id);
};

void extract(std::string filename, std::string output_path, int callback_id, int calle_id)
{
    
    HANDLE rarFile;
    struct RARHeaderData fileInfo;
    struct RAROpenArchiveData archiveInfo;
    memset(&archiveInfo, 0, sizeof(archiveInfo));
    archiveInfo.CmtBuf = NULL;
    archiveInfo.OpenMode = RAR_OM_EXTRACT;
    archiveInfo.ArcName = (char*)filename.c_str();

    // Open file
    rarFile = RAROpenArchive(&archiveInfo);
    if (archiveInfo.OpenResult == ERAR_SUCCESS) {
        while (1)
        {
            if (RARReadHeader(rarFile, &fileInfo)) {
                //	MF_Log("RAR: FINISHED\n");
                break;
            }

            RARProcessFile(rarFile, RAR_EXTRACT, const_cast<char*>(output_path.c_str()), NULL);
        }

        RARCloseArchive(rarFile);
        finish(0, callback_id, calle_id);
        return;
    }
    RARCloseArchive(rarFile);

    struct archive* a;
    struct archive* ext;
    struct archive_entry* entry;
    int flags;
    int r;

    /* Select which attributes we want to restore. */
    flags = ARCHIVE_EXTRACT_TIME;
    flags |= ARCHIVE_EXTRACT_PERM;
    flags |= ARCHIVE_EXTRACT_ACL;
    flags |= ARCHIVE_EXTRACT_FFLAGS;

    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_compression_all(a);
    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);


    if ((r = archive_read_open_filename(a, filename.c_str(), 10240)))
        goto end;
    for (;;) {
        r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF)
        {
            r = ARCHIVE_OK;
            break;
        }
        if (r < ARCHIVE_OK)
            goto end;

        const char* currentFile = archive_entry_pathname(entry);
        std::string fullOutputPath = output_path;
        fullOutputPath.append(currentFile);
        archive_entry_set_pathname(entry, fullOutputPath.c_str());
        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK)
            fprintf(stderr, "%s\n", archive_error_string(ext));
        if (archive_entry_size(entry) > 0) {
            r = copy_data(a, ext);
            if (r < ARCHIVE_OK)
                goto end;
        }
        r = archive_write_finish_entry(ext);
        if (r < ARCHIVE_OK)
            goto end;
    }
end:
    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);
    finish(r, callback_id, calle_id);
}

int thread_proc(void* user_data)
{
    struct extract_context* context = (struct extract_context*) user_data;
	
    extract(context->input_path, context->output_path, context->callback_id, context->calle_id);
    delete context;
    return 0;
}

cell AMX_NATIVE_CALL AA_Unarchive(AMX* amx, cell* params)
{
    enum args_e { arg_count, arg_path, arg_output, arg_success_callback, arg_idcaller };
    char rootdir[FILENAME_MAX], moddir[FILENAME_MAX], path[FILENAME_MAX], outdir[FILENAME_MAX];

    GetCurrentDir(rootdir, sizeof(rootdir));

    GET_GAME_DIR(moddir);

    int len;
    char* file_path = MF_GetAmxString(amx, params[arg_path], 0, &len);
    char* out_path = MF_GetAmxString(amx, params[arg_output], 1, &len);

    sprintf(path, "%s/%s/%s", rootdir, moddir, file_path);
    sprintf(outdir, "%s/%s/%s/", rootdir, moddir, out_path);

    std::string i = path;
    std::string o = outdir;

    int idcaller = params[arg_idcaller];


    char* finish_callback = MF_GetAmxString(amx, params[arg_success_callback], 0, &len);
    int callback_id;

    if (g_fn_AmxFindPublic(amx, finish_callback, &callback_id) != AMX_ERR_NONE)
    {
        g_fn_LogErrorFunc(amx, AMX_ERR_NATIVE, "%s: public function \"%s\" not found.", __FUNCTION__, finish_callback);
        return 0;
    }

    g_bFinished = false;

    // cell idcaller, cell error_num
    callback_id = MF_RegisterSPForwardByName(amx, finish_callback, FP_CELL, FP_CELL, FP_DONE);


    std::replace(o.begin(), o.end(), '\\', '/');
    std::replace(i.begin(), i.end(), '\\', '/');

#ifdef __linux__
    std::replace(o.begin(), o.end(), '\\', '/');
    std::replace(i.begin(), i.end(), '\\', '/');
#else
    std::replace(o.begin(), o.end(), '/', '\\');
    std::replace(i.begin(), i.end(), '/', '\\');
#endif

    //std::thread(extract, i, o, callback_id, idcaller).detach();
    auto user_thread_context = new extract_context ;
    user_thread_context->callback_id = callback_id;
    user_thread_context->calle_id = idcaller;
    user_thread_context->input_path = i;
    user_thread_context->output_path = o;
    thread_ptr_t thread = thread_create(thread_proc, user_thread_context, "Example thread", THREAD_STACK_SIZE_DEFAULT);

	
    return 1;
}




AMX_NATIVE_INFO Natives[] =
{
    {"AA_Unarchive",		AA_Unarchive },
    {NULL,						NULL}
};

