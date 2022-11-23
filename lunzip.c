#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>

#include "zlib/contrib/minizip/unzip.h"

#include <string.h>


#define LIBNAME "unzip"

#define lunz_check(L, idx) ((lunz_State*)luaL_checkudata((L), (idx), LIBNAME))
#define lunz_test(L, idx)  ((lunz_State*)luaL_testudata((L), (idx), LIBNAME))

/* Lua 5.3 compatible layer */

#if LUA_VERSION_NUM >= 503
# define lua53_getuservalue lua_getuservalue
# define lua53_rawget       lua_rawget
# define lua53_rawgetp      lua_rawgetp
# define lua53_getfield     lua_getfield
#else
static int lua53_getuservalue(lua_State *L, int idx)
{ lua_getuservalue(L, idx); return lua_type(L, -1); }
static int lua53_rawget(lua_State *L, int idx)
{ lua_rawget(L, idx); return lua_type(L, -1); }
static int lua53_rawgetp(lua_State *L, int idx, const void *p)
{ lua_rawgetp(L, idx, p); return lua_type(L, -1); }
static int lua53_getfield(lua_State *L, int idx, const char *field)
{ lua_getfield(L, idx, field); return lua_type(L, -1); }
#endif


/* ioapi for lua callback */

static char udpool[] = "unzip objpool";

typedef struct lunz_State {
    unzFile* file;
    lua_State *L;
    ZPOS64_T files;
    unz64_file_pos *pos;
} lunz_State;

static int getbox(lua_State *L) {
    if (lua53_rawgetp(L, LUA_REGISTRYINDEX, (void*)&udpool) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_newtable(L);
        lua_pushstring(L, "v");
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);
        lua_pushvalue(L, -1);
        lua_rawsetp(L, LUA_REGISTRYINDEX, udpool);
        return 1;
    }
    return 0;
}

static void intern_state(lua_State *L, lunz_State *S) {
    getbox(L);
    lua_pushvalue(L, -2);
    lua_rawsetp(L, -2, (const void*)S);
    lua_pop(L, 1);
}

static int retrieve_state(lua_State *L, lunz_State *S) {
    getbox(L);
    if (lua53_rawgetp(L, -1, (const void*)S) == LUA_TNIL) {
        lua_pop(L, 2);
        return 0;
    }
    lua_remove(L, -2);
    return 1;
}

static int prepare_call(lua_State *L, lunz_State *S, const char *method) {
    if (!retrieve_state(L, S))
        return 0;
    if (lua53_getfield(L, -1, method) == LUA_TNIL) {
        lua_pop(L, 2);
        return 0;
    }
    lua_insert(L, -2);
    return 1;
}

static const char *getmode(int mode) {
    if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER)==ZLIB_FILEFUNC_MODE_READ)
        return "rb";
    else if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
        return "r+b";
    else if (mode & ZLIB_FILEFUNC_MODE_CREATE)
        return "wb";
    return "";
}

static const char *getorigin(int origin) {
    switch (origin) {
    case ZLIB_FILEFUNC_SEEK_CUR:
        return "cur";
    case ZLIB_FILEFUNC_SEEK_END:
        return "end";
    case ZLIB_FILEFUNC_SEEK_SET:
        return "set";
    }
    return "";
}

static voidpf ZCALLBACK lunz_open64_cb_func(voidpf opaque, const void* filename, int mode) {
    lunz_State *S = (lunz_State*)opaque;
    lua_State *L = S->L;
    voidpf stream = S;
    if (prepare_call(L, S, "open")) {
        lua_pushstring(L, (const char*)filename);
        lua_pushstring(L, getmode(mode));
        lua_call(L, 3, 1);
        if (lua_isnil(L, -1)) {
            stream = NULL;
            lua_pop(L, 1);
        }
        else {
            stream = (void*)lua_topointer(L, -1);
            if (stream == NULL) stream = S;
            retrieve_state(L, S);
            lua_getuservalue(L, -1);
            lua_pushvalue(L, -3);
            lua_setfield(L, -2, "stream");
            lua_pop(L, 3);
        }
    }
    return stream;
}

static voidpf ZCALLBACK lunz_opendisk64_cb_func(voidpf opaque, voidpf stream, int number_disk, int mode) {
    lunz_State *S = (lunz_State*)opaque;
    lua_State *L = S->L;
    if (prepare_call(L, S, "opendisk")) {
        lua_getfield(L, -1, "stream");
        lua_pushinteger(L, number_disk);
        lua_pushstring(L, getmode(mode));
        lua_call(L, 4, 1);
        if (lua_isnil(L, -1))
            lua_pop(L, 1);
        else {
            retrieve_state(L, S);
            lua_getuservalue(L, -1);
            lua_pushvalue(L, -3);
            lua_setfield(L, -2, "stream");
            lua_pop(L, 3);
        }
    }
    return stream;
}

static uLong ZCALLBACK lunz_read_cb_func(voidpf opaque, voidpf stream, void* buf, uLong size) {
    lunz_State *S = (lunz_State*)opaque;
    lua_State *L = S->L;
    uLong ret = 0;
    if (prepare_call(L, S, "read")) {
        size_t len;
        const char *s;
        lua_getfield(L, -1, "stream");
        lua_pushinteger(L, size);
        lua_call(L, 3, 1);
        s = lua_tolstring(L, -1, &len);
        if (s != NULL) {
            if (len > size)
                len = size;
            memcpy(buf, s, len);
            ret = len;
        }
        lua_pop(L, 1);
    }
    return ret;
}

static int ZCALLBACK lunz_close_cb_func(voidpf opaque, voidpf stream) {
    lunz_State *S = (lunz_State*)opaque;
    lua_State *L = S->L;
    int ret = 0;
    if (prepare_call(L, S, "close")) {
        lua_getfield(L, -1, "stream");
        lua_call(L, 2, 1);
        ret = lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    return ret;
}

static int ZCALLBACK lunz_testerror_cb_func(voidpf opaque, voidpf stream) {
    lunz_State *S = (lunz_State*)opaque;
    lua_State *L = S->L;
    int ret = 0;
    if (prepare_call(L, S, "testerror")) {
        lua_getfield(L, -1, "stream");
        lua_call(L, 2, 1);
        ret = lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    return ret;
}

static ZPOS64_T ZCALLBACK lunz_tell64_cb_func(voidpf opaque, voidpf stream) {
    lunz_State *S = (lunz_State*)opaque;
    lua_State *L = S->L;
    ZPOS64_T ret = 0;
    if (prepare_call(L, S, "tell")) {
        lua_getfield(L, -1, "stream");
        lua_call(L, 2, 1);
        ret = (ZPOS64_T)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    return ret;
}

static long ZCALLBACK lunz_seek64_cb_func(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) {
    lunz_State *S = (lunz_State*)opaque;
    lua_State *L = S->L;
    long ret = 0;
    if (prepare_call(L, S, "seek")) {
        lua_getfield(L, -1, "stream");
        lua_pushinteger(L, (lua_Integer)offset);
        lua_pushstring(L, getorigin(origin));
        lua_call(L, 4, 1);
        ret = lua_toboolean(L, -1) ? 0 : -1;
        lua_pop(L, 1);
    }
    return ret;
}

static void lunz_fill_cb_filefunc(zlib_filefunc64_def* pzlib_filefunc_def, lunz_State *S) {
    pzlib_filefunc_def->zopen64_file = lunz_open64_cb_func;
    /*pzlib_filefunc_def->zopendisk64_file = lunz_opendisk64_cb_func;*/
    pzlib_filefunc_def->zread_file = lunz_read_cb_func;
    pzlib_filefunc_def->zwrite_file = NULL;
    pzlib_filefunc_def->ztell64_file = lunz_tell64_cb_func;
    pzlib_filefunc_def->zseek64_file = lunz_seek64_cb_func;
    pzlib_filefunc_def->zclose_file = lunz_close_cb_func;
    pzlib_filefunc_def->zerror_file = lunz_testerror_cb_func;
    pzlib_filefunc_def->opaque = (voidpf)S;
}

/* ioapi for lua string */

typedef struct lunz_BufferCtx {
    size_t pos, len;
    const char *data;
    lua_State *L;
    int refdata, refctx;
} lunz_BufferCtx;

static voidpf ZCALLBACK lunz_open64_mem_func(voidpf opaque, const void* filename, int mode) {
    lunz_BufferCtx *ctx = (lunz_BufferCtx*)opaque;
    ctx->pos = 0;
    return (voidpf)ctx;
}

static voidpf ZCALLBACK lunz_opendisk64_mem_func(voidpf opaque, voidpf stream, int number_disk, int mode) {
    return NULL;
}

static uLong ZCALLBACK lunz_read_mem_func(voidpf opaque, voidpf stream, void* buf, uLong size) {
    lunz_BufferCtx *ctx = (lunz_BufferCtx*)opaque;
    if (ctx->pos + size > ctx->len)
        size = ctx->len - ctx->pos;
    memcpy(buf, ctx->data + ctx->pos, size);
    ctx->pos += size;
    return size;
}

static int ZCALLBACK lunz_close_mem_func(voidpf opaque, voidpf stream) {
    lunz_BufferCtx *ctx = (lunz_BufferCtx*)opaque;
    luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->refdata);
    luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->refctx);
    return 0;
}

static int ZCALLBACK lunz_testerror_mem_func(voidpf opaque, voidpf stream) {
    return 0;
}

static ZPOS64_T ZCALLBACK lunz_tell64_mem_func(voidpf opaque, voidpf stream) {
    lunz_BufferCtx *ctx = (lunz_BufferCtx*)opaque;
    return (ZPOS64_T)ctx->pos;
}

static long ZCALLBACK lunz_seek64_mem_func(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) {
    lunz_BufferCtx *ctx = (lunz_BufferCtx*)opaque;
    switch (origin) {
    case ZLIB_FILEFUNC_SEEK_SET:
        break;
    case ZLIB_FILEFUNC_SEEK_CUR:
        offset += ctx->pos;
        break;
    case ZLIB_FILEFUNC_SEEK_END:
        offset += ctx->len;
        break;
    }
    if (offset < 0 || offset > ctx->len)
        return -1;
    ctx->pos = (size_t)offset;
    return 0;
}

static void lunz_fill_mem_filefunc(zlib_filefunc64_def* pdef, lua_State *L, int idx) {
    lunz_BufferCtx *ctx;
    pdef->zopen64_file = lunz_open64_mem_func;
    /*pdef->zopendisk64_file = lunz_opendisk64_mem_func;*/
    pdef->zread_file = lunz_read_mem_func;
    pdef->zwrite_file = NULL;
    pdef->ztell64_file = lunz_tell64_mem_func;
    pdef->zseek64_file = lunz_seek64_mem_func;
    pdef->zclose_file = lunz_close_mem_func;
    pdef->zerror_file = lunz_testerror_mem_func;
    ctx = (lunz_BufferCtx*)lua_newuserdata(L, sizeof(*ctx));
    ctx->refctx = luaL_ref(L, LUA_REGISTRYINDEX);
    ctx->data = lua_tolstring(L, idx, &ctx->len);
    lua_pushvalue(L, idx);
    ctx->refdata = luaL_ref(L, LUA_REGISTRYINDEX);
    ctx->L = L;
    pdef->opaque = ctx;
}

/* metamethods */

static int lunz_pusherror(lua_State *L, int err) {
    lua_pushnil(L);
    switch (err) {
    case UNZ_ERRNO:
        lua_pushstring(L, strerror(errno));
        break;
    case UNZ_EOF:
        lua_pushliteral(L, "unexpected end of file");
        break;
    case UNZ_PARAMERROR:
        lua_pushliteral(L, "parameter error");
        break;
    case UNZ_BADZIPFILE:
        lua_pushliteral(L, "bad zip file");
        break;
    case UNZ_INTERNALERROR:
        lua_pushliteral(L, "internal error");
        break;
    case UNZ_CRCERROR:
        lua_pushliteral(L, "crc error");
        break;
    default:
        lua_pushfstring(L, "unknown error %d", err);
        break;
    }
    lua_pushinteger(L, err);
    return 3;
}

static int L__gc(lua_State *L) {
    lunz_State *S = lunz_test(L, 1);
    if (S != NULL && S->file != NULL) {
        unzClose(S->file);
        S->file = NULL;
    }
    return 0;
}

static int L__tostring(lua_State *L) {
    lunz_State *S = lunz_test(L, 1);
    if (S != NULL && S->file != NULL)
        lua_pushfstring(L, "unzip: %p", S->file);
    else if (S != NULL)
        lua_pushliteral(L, "unzip: (null)");
    else
        luaL_tolstring(L, 1, NULL);
    return 1;
}

static int L__len(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    if (S->file == NULL)
        return 0;
    lua_pushinteger(L, (lua_Integer)S->files);
    return 1;
}

static int push_filename(lua_State *L, unzFile *file) {
    char buff[LUAL_BUFFERSIZE], *fn = buff;
    unz_file_info64 info;
    int err;

    if ((err = unzGetCurrentFileInfo64(file, &info,
                    NULL, 0, NULL, 0, NULL, 0)) != UNZ_OK)
        goto error;

    if (info.size_filename > LUAL_BUFFERSIZE)
        fn = (char*)lua_newuserdata(L, info.size_filename);

    if ((err = unzGetCurrentFileInfo64(file, &info,
                    fn, info.size_filename, NULL, 0, NULL, 0)) != UNZ_OK)
        goto error;

    lua_pushlstring(L, fn, info.size_filename);
    return 1;

error:
    lunz_pusherror(L, err);
    return lua_error(L);
}

static int L__index(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    if (lua53_getuservalue(L, 1) != LUA_TNIL) {
        lua_pushvalue(L, 2);
        if (lua53_rawget(L, -2) != LUA_TNIL)
            return 1;
    }
    if (lua_type(L, 2) == LUA_TNUMBER) {
        int err;
        lua_Integer idx = lua_tointeger(L, 2);
        if (idx < 0) idx += S->files;
        if (idx <=0 || idx > S->files)
            luaL_argerror(L, 2, "index out of range");
        if ((err = unzGoToFilePos64(S->file, &S->pos[idx])) != UNZ_OK ||
                (err = unzOpenCurrentFile(S->file)) != UNZ_OK) {
            lunz_pusherror(L, err);
            return lua_error(L);
        }
        lua_settop(L, 2);
        return push_filename(L, S->file);
    }
    if (lua_getmetatable(L, 1)) {
        lua_pushvalue(L, 2);
        lua_rawget(L, -2);
        return 1;
    }
    return 0;
}

static int L__newindex(lua_State *L) {
    if (lua_type(L, 1) == LUA_TUSERDATA) {
        if (lua53_getuservalue(L, 1) == LUA_TNIL) {
            lua_pop(L, 1);
            lua_createtable(L, 0, 1);
            lua_pushvalue(L, -1);
            lua_setuservalue(L, 1);
        }
        lua_pushvalue(L, 3);
        lua_rawset(L, -2);
        return 0;
    }
    lua_settop(L, 3);
    lua_rawset(L, 1);
    return 0;
}

static int files_iter(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    lua_Integer idx = luaL_checkinteger(L, 2) + 1;
    int err;
    if (idx > 1) {
        err = unzGoToNextFile(S->file);
        if (err == UNZ_END_OF_LIST_OF_FILE)
            return 0;
        if (err != UNZ_OK)
            goto error;
    }
    else if ((err = unzGoToFirstFile(S->file)) != UNZ_OK)
        goto error;
    lua_pushinteger(L, idx);
    push_filename(L, S->file);
    return 2;

error:
    lunz_pusherror(L, err);
    return lua_error(L);
}

static int Lfiles(lua_State *L) {
    lua_pushcfunction(L, files_iter);
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 0);
    return 3;
}


/* routines */

static int setup_filepos(lunz_State *S) {
    ZPOS64_T i;
    unz_global_info64 info;
    if (unzGetGlobalInfo64(S->file, &info) != UNZ_OK)
        goto err;
    S->files = info.number_entry;
    S->pos = (unz64_file_pos*)malloc(S->files*sizeof(unz64_file_pos));
    if (S->pos == NULL)
        goto err;
    if (unzGoToFirstFile(S->file) != UNZ_OK)
        goto err;
    for (i = 0; i < S->files; ++i) {
        if (unzGetFilePos64(S->file, &S->pos[i]) != UNZ_OK)
            goto err;
    }
    return 1;

err:
    if (S->pos) free(S->pos);
    if (S->file) unzClose(S->file);
    return 0;
}

static int Lload(lua_State *L) {
    lunz_State *S;
    const char *path = luaL_checkstring(L, 1);
    int hasinfo = lua_istable(L, 2);
    S = (lunz_State*)lua_newuserdata(L, sizeof(lunz_State));
    luaL_setmetatable(L, LIBNAME);
    intern_state(L, S);
    S->L = L;
    S->files = 0;
    S->pos = NULL;
    if (!hasinfo)
        S->file = unzOpen64(path);
    else {
        zlib_filefunc64_def def;
        lunz_fill_cb_filefunc(&def, S);
        lua_pushvalue(L, 2);
        lua_setuservalue(L, -2);
        S->file = NULL;
        S->file = unzOpen2_64(path, &def);
    }
    if (S->file == NULL || !setup_filepos(S))
        return 0;
    return 1;
}

static int Lloadstring(lua_State *L) {
    lunz_State *S;
    zlib_filefunc64_def def;
    luaL_checkstring(L, 1);
    lunz_fill_mem_filefunc(&def, L, 1);
    S = (lunz_State*)lua_newuserdata(L, sizeof(lunz_State));
    luaL_setmetatable(L, LIBNAME);
    intern_state(L, S);
    S->L = L;
    S->files = 0;
    S->pos = NULL;
    S->file = unzOpen2_64(NULL, &def);
    if (S->file == NULL || !setup_filepos(S))
        return 0;
    return 1;
}

static int Lglobalcomment(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    unz_global_info64 info;
    luaL_Buffer b;
    if (S->file == NULL)
        return 0;
    int err;
    if ((err = unzGetGlobalInfo64(S->file, &info)) != UNZ_OK)
        return lunz_pusherror(L, err);
    char *buff = luaL_buffinitsize(L, &b, (size_t)info.size_comment);
    if ((err = unzGetGlobalComment(S->file, buff, info.size_comment)) != UNZ_OK)
        return lunz_pusherror(L, err);
    luaL_addsize(&b, (size_t)info.size_comment);
    luaL_pushresult(&b);
    return 1;
}

static int Llocatefile(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    const char *file = luaL_checkstring(L, 2);
    int ignorecase = lua_toboolean(L, 3);
    int err;
    if (S->file == NULL) return 0;
    if ((err = unzLocateFile(S->file, file, ignorecase)) != UNZ_OK)
        return lunz_pusherror(L, err);
    lua_settop(L, 1);
    return 1;
}

static int Lfilepos(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    unz64_file_pos pos; 
    int err, isint;
    pos.pos_in_zip_directory = lua_tointegerx(L, 2, &isint);
    if (S->file == NULL) return 0;
    if (isint) {
        pos.num_of_file = lua_tointegerx(L, 3, NULL);
        if ((err = unzGoToFilePos64(S->file, &pos)) != UNZ_OK)
            return lunz_pusherror(L, err);
        lua_settop(L, 1);
        return 1;
    }
    if ((err = unzGetFilePos64(S->file, &pos)) != UNZ_OK)
        return lunz_pusherror(L, err);
    lua_pushinteger(L, (lua_Integer)pos.pos_in_zip_directory);
    lua_pushinteger(L, (lua_Integer)pos.num_of_file);
    return 2;
}

static int Lfileinfo(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    unz_file_info64 info;
    int err;
    if (S->file == NULL) return 0;
    if ((err = unzGetCurrentFileInfo64(S->file, &info,
                    NULL,0, NULL,0, NULL,0)) != UNZ_OK)
        return lunz_pusherror(L, err);
    lua_settop(L, 2);
    if (!lua_istable(L, 2)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
#define FIELD(field) \
    (lua_pushinteger(L,(lua_Integer)info.field),\
     lua_setfield(L,2,#field))
    FIELD(version);
    FIELD(version_needed);
    FIELD(flag);
    FIELD(compression_method);
    FIELD(dosDate);
    FIELD(crc);
    FIELD(compressed_size);
    FIELD(uncompressed_size);
    FIELD(disk_num_start);
    FIELD(internal_fa);
    FIELD(external_fa);
#undef FIELD
    /* read string */ {
        char buff[LUAL_BUFFERSIZE];
        char *filename, *file_extra, *file_comment;
        size_t total = info.size_filename +
            info.size_file_extra +
            info.size_file_comment;
        if (total <= LUAL_BUFFERSIZE)
            filename = buff;
        else
            filename = (char*)lua_newuserdata(L, total);
        file_extra = filename + info.size_filename;
        file_comment = file_extra + info.size_file_extra;
        if ((err = unzGetCurrentFileInfo64(S->file, NULL,
                        filename, info.size_filename,
                        file_extra, info.size_file_extra,
                        file_comment, info.size_file_comment)) != UNZ_OK)
            return lunz_pusherror(L, err);
        lua_pushlstring(L, filename, info.size_filename);
        lua_setfield(L, 2, "filename");
        lua_pushlstring(L, file_extra, info.size_file_extra);
        lua_setfield(L, 2, "file_extra");
        lua_pushlstring(L, file_comment, info.size_file_comment);
        lua_setfield(L, 2, "file_comment");
        lua_settop(L, 2);
    }
    return 1;
}

static int Lopenfile(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    const char *password = luaL_optstring(L, 2, NULL);
    int raw = lua_toboolean(L, 3);
    int err, method, level;
    unz_file_info64 info;
    if (S->file == NULL) return 0;
    if ((err = unzGetCurrentFileInfo64(S->file, &info,
                    NULL,0, NULL,0, NULL,0)) != UNZ_OK)
        return lunz_pusherror(L, err);
    if ((err = unzOpenCurrentFile3(S->file,
                    &method, &level, raw, password)) != UNZ_OK)
        return lunz_pusherror(L, err);
    lua_settop(L, 1);
    lua_pushinteger(L, info.uncompressed_size);
    return 2;
}

static int Lclosefile(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    int err;
    if (S->file == NULL) return 0;
    if ((err = unzCloseCurrentFile(S->file)) != UNZ_OK)
        return lunz_pusherror(L, err);
    lua_settop(L, 1);
    return 1;
}

static int Lreadfile(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    char buff[LUAL_BUFFERSIZE], *p = buff;
    lua_Integer bytes = 0;
    int err;
    if (S->file == NULL) return 0;
    switch (lua_type(L, 2)) {
    case LUA_TSTRING: {
        const char *s = lua_tostring(L, 2);
        int fmt = s[*s == '*'];
        if (fmt == 'a') { /* read all */
            unz_file_info64 info;

    case LUA_TNONE:
            if ((err = unzGetCurrentFileInfo64(S->file, &info,
                            NULL,0,NULL,0,NULL,0)) != UNZ_OK)
                return lunz_pusherror(L, err);
            bytes = info.uncompressed_size;
        }
        break;
    }
    case LUA_TNUMBER: {
        bytes = (size_t)lua_tointeger(L, 2);
        break;
    }
    default:
        lua_pushfstring(L, "number/string expected, got %s",
                luaL_typename(L, 2));
        return luaL_argerror(L, 2, lua_tostring(L, -1));
    }
    if (bytes > LUAL_BUFFERSIZE)
        p = (char*)lua_newuserdata(L, bytes);
    if ((err = unzReadCurrentFile(S->file, p, bytes)) < 0)
        return lunz_pusherror(L, err);
    lua_pushlstring(L, p, err);
    lua_pushinteger(L, err);
    return 2;
}

static int Ltell(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    if (S->file == NULL) return 0;
    lua_pushinteger(L, (lua_Integer)unztell64(S->file));
    return 1;
}

static int Leof(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    if (S->file == NULL) return 0;
    lua_pushboolean(L, unzeof(S->file));
    return 1;
}

static int Lextrafield(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    luaL_Buffer b;
    int bytes;
    if (S->file == NULL) return 0;
    bytes = unzGetLocalExtrafield(S->file, NULL, 0);
    if (bytes < 0) return lunz_pusherror(L, bytes);
    if (bytes == 0) {
        lua_pushliteral(L, "");
        return 1;
    }
    bytes = unzGetLocalExtrafield(S->file,
            luaL_buffinitsize(L, &b, bytes),
            bytes);
    if (bytes < 0) return lunz_pusherror(L, bytes);
    luaL_addsize(&b, bytes);
    luaL_pushresult(&b);
    return 1;
}

static int Loffset(lua_State *L) {
    lunz_State *S = lunz_check(L, 1);
    int isint;
    lua_Integer off = lua_tointegerx(L, 2, &isint);
    if (S->file == NULL) return 0;
    if (isint) {
        int err;
        if ((err = unzSetOffset64(S->file, off)) != UNZ_OK)
            return lunz_pusherror(L, err);
        lua_settop(L, 1);
        return 1;
    }
    lua_pushinteger(L, (lua_Integer)unzGetOffset64(S->file));
    return 1;
}

/* entry */

LUALIB_API int luaopen_unzip(lua_State *L) {
    luaL_Reg libs[] = {
#define Lclose L__gc
#define ENTRY(name) { #name, L##name }
        ENTRY(load),
        ENTRY(loadstring),
        ENTRY(close),
        ENTRY(globalcomment),
        ENTRY(files),
        ENTRY(locatefile),
        ENTRY(filepos),
        ENTRY(fileinfo),
        ENTRY(openfile),
        ENTRY(closefile),
        ENTRY(readfile),
        ENTRY(tell),
        ENTRY(eof),
        ENTRY(extrafield),
        ENTRY(offset),
        ENTRY(__gc),
        ENTRY(__tostring),
        ENTRY(__len),
        ENTRY(__newindex),
        ENTRY(__index),
#undef  ENTRY
        { NULL, NULL }
    };

    if (luaL_newmetatable(L, LIBNAME))
        luaL_setfuncs(L, libs, 0);

    return 1;
}

/* cc: flags+='-s -O2 -mdll -DLUA_BUILD_AS_DLL' output='unzip.dll'
 * cc: libs+='-llua53 -L. -lminiz -lz' */
