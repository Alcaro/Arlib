goto q
:h
pause
:q
cls
mingw32-make -j4 test TESTRUNNER=gdb
goto h