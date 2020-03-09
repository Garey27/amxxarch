#!/bin/bash
maxver=4.2.0
headerf=${1:-GCC_libstdc++.h}
set -e
for lib in libstdc++.so.6; do
objdump -T /usr/libx32/$lib
done | awk -v maxver=${maxver} -vheaderf=${headerf} -vredeff=${headerf}.redef -f <(cat <<'EOF'
BEGIN {
split(maxver, ver, /\./)
limit_ver = ver[1] * 10000 + ver[2]*100 + ver[3]
}
/GCC_/ {
gsub(/\(|\)/, "",$(NF-1))
split($(NF-1), ver, /GCC_|\./)
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
