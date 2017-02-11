#!/bin/bash
[ -e Makefile ] && exit

program=$1
[[ -z $program ]] && program=$(basename $(pwd))

echo '#include "arlib/arlib.h"' > arlib.h

mkdir obj

echo "PROGRAM = $program" > Makefile
echo "include arlib/Makefile" >> Makefile

cat > z0_run.bat <<-EOF
	goto q
	:h
	pause
	:q
	cls
	del $program.exe
	if exist $program.exe goto h
	mingw32-make -j4
	$program.exe
	goto h
EOF

cat > z1_job1.bat <<-EOF
	goto q
	:h
	pause
	:q
	cls
	del $program.exe
	if exist $program.exe goto h
	mingw32-make -j1
	$program.exe
	goto h
EOF

cat > z6_run64.bat <<-EOF
	goto q
	:h
	pause
	:q
	cls
	del $program64.exe
	if exist $program64.exe goto h
	mingw32-make CC=gcc64 CXX=g++64 LD=g++64 RCFLAGS="-Fpe-x86-64" OBJSUFFIX="-64" -j4 OUTNAME=$program64.exe
	$program64.exe
	goto h
EOF

cat > zc_clean.bat <<-EOF
	del /q obj\*
	del $program*.exe
EOF

cat > zd_debug.bat <<-EOF
	goto q
	:h
	pause
	:q
	cls
	del $program.exe
	if exist $program.exe goto h
	mingw32-make -j4
	if not exist $program.exe goto h
	gdb $program.exe
	goto h
EOF

cat > zk_keepgoing.bat <<-EOF
	goto q
	:h
	pause
	:q
	cls
	del $program.exe
	if exist $program.exe goto h
	mingw32-make -j4 -k
	$program.exe
	goto h
EOF

cat > zm_makerelease.bat <<-EOF
	rem del /q obj\*
	del $program.exe
	mingw32-make OBJSUFFIX="-opt" OPT=1 -j4 OUTNAME=$program.exe
	
	rem del $program64.exe
	rem mingw32-make CC=gcc64 CXX=g++64 LD=g++64 RCFLAGS="-Fpe-x86-64" OBJSUFFIX="-opt64" OPT=1 -j4 OUTNAME=$program64.exe
	pause
EOF

cat > zt_testsuite.bat <<-EOF
	goto q
	:h
	pause
	:q
	cls
	mingw32-make -j4 test
	pause
	goto h
EOF
