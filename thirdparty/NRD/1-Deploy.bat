@echo off

git submodule update --init --recursive

mkdir "_Build"

cd "_Build"

cmake -DNRD_NRI=OFF .. %*
if %ERRORLEVEL% NEQ 0 goto END

:END
cd ..
exit /B %ERRORLEVEL%
