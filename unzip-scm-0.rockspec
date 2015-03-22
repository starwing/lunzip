package = "unzip"
version = "scm-0"
source = {
   url = "https://github.com/starwing/lunzip.git",
}
description = {
   summary = "A Lua module that use minizip to read zip file",
   homepage = "https://github.com/starwing/lunzip",
   license = "MIT"
}
dependencies = {
   "lua >= 5.1, < 5.4"
}
build = {
   type = "builtin",
   modules = {
      unzip = {
         incdirs = { "zlib" },
         sources = {
            "lunzip.c",
            "zlib/adler32.c",
            "zlib/contrib/minizip/ioapi.c",
            "zlib/contrib/minizip/unzip.c",
            "zlib/crc32.c",
            "zlib/inffast.c",
            "zlib/inflate.c",
            "zlib/inftrees.c",
            "zlib/zutil.c",
         },
      },
   },
}
