@echo off
setlocal

set "SCRIPT_DIRECTORY=%~dp0"
for %%I in ("%SCRIPT_DIRECTORY%..") do set "SOURCE_DIRECTORY=%%~fI"

set "BUILD_DIRECTORY=%~1"
if not defined BUILD_DIRECTORY set "BUILD_DIRECTORY=%SOURCE_DIRECTORY%\build-tests"

set "BUILD_CONFIGURATION=%~2"
if not defined BUILD_CONFIGURATION set "BUILD_CONFIGURATION=Debug"

call :ConfigureTests || exit /b 1
call :BuildTests || exit /b 1
call :RunTests || exit /b 1

exit /b 0

:ConfigureTests
echo Configuring tests in "%BUILD_DIRECTORY%"...
cmake -S "%SOURCE_DIRECTORY%" -B "%BUILD_DIRECTORY%" -DARDASHIR_BUILD_TESTS=ON
exit /b %errorlevel%

:BuildTests
echo Building tests using configuration "%BUILD_CONFIGURATION%"...
cmake --build "%BUILD_DIRECTORY%" --config "%BUILD_CONFIGURATION%" --parallel
exit /b %errorlevel%

:RunTests
echo Running all project tests...
ctest --test-dir "%BUILD_DIRECTORY%" -C "%BUILD_CONFIGURATION%" --output-on-failure
exit /b %errorlevel%
