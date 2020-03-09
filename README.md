# AMX Mod X Arch
[![Build status](https://ci.appveyor.com/api/projects/status/kne29se779e14ebn?svg=true)](https://ci.appveyor.com/project/Garey/amxxarch)

This is a simple module that extract multiple type of archives to specific folder.

# Dependencies

- libarchive:
    -ZLIB
    -BZip2
    -LZMA
    -LIBXML2
    -lz4
    -OpenSSL
    
And since libarchive can't handle rarvm archives there also:
- libunrar