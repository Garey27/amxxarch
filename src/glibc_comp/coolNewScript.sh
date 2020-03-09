#!/bin/bash
maxver=2.19
headerf=${1:-GLIBC_.h}
set -e
for lib in libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 libresolv.so.2 libgcc_s.so.1 librt.so.1 ld-linux.so.2; do
objdump -T /lib32/$lib
done | awk -v maxver=${maxver} -vheaderf=${headerf} -vredeff=${headerf}.redef -f <(cat <<'EOF'
BEGIN {
split(maxver, ver, /\./)
limit_ver = ver[1] * 10000 + ver[2]*100 + ver[3]
}
/GLIBC_/ {
gsub(/\(|\)/, "",$(NF-1))
split($(NF-1), ver, /GLIBC_|\./)
vers = ver[2] * 10000 + ver[3]*100 + ver[4]
if (vers > 0) {
    if (symvertext[$(NF)] != $(NF-1))
        count[$(NF)]++
    if (vers <= limit_ver && vers > symvers[$(NF)]) {
        symvers[$(NF)] = vers
        symvertext[$(NF)] = $(NF-1)
    }
}
}
END {
for (s in symvers) {
    if (count[s] > 0) {
        printf("__asm__(\".symver %s,%s@%s\");\n", s, s, symvertext[s]) > headerf
        printf("%s %s@%s\n", s, s, symvertext[s]) > redeff
    }
}
}
EOF
)
sort ${headerf} -o ${headerf}
